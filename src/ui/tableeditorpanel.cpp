#include "ui/tableeditorpanel.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <utility>

#include "3rdparty/nlohmann/json.hpp"
#include "3rdparty/portable-file-dialogs.h"
#include "imgui.h"

namespace ui {

  TableEditorPanel::TableEditorPanel(std::string title, std::string panelTypeIdValue,
    std::string displayName, core::Table2D initialTable, int xDecimalPlaces, int yDecimalPlaces)
    : PlotPanel(std::move(title))
    , panelTypeIdValue_(std::move(panelTypeIdValue))
    , displayName_(std::move(displayName))
    , table_(std::move(initialTable))
    , xDecimalPlaces_(xDecimalPlaces)
    , yDecimalPlaces_(yDecimalPlaces)
  {}

  std::string TableEditorPanel::formatValue(double value, int decimalPlaces)
  {
    if (decimalPlaces < 0) decimalPlaces = 0;
    if (decimalPlaces == 0) {
      return std::to_string(static_cast<int>(value));
    }
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimalPlaces, value);
    return std::string(buffer);
  }

  void TableEditorPanel::copyToClipboard(bool includeHeaders) const
  {
    const auto &xBp = table_.xBreakpoints(); 
      const auto &yBp = table_.yBreakpoints(); 
      std::string result;

    if (includeHeaders) {
      result += "\t";
      for (size_t c = 0; c < xBp.size(); ++c) {
        result += formatValue(xBp[c], xDecimalPlaces_); 
          if (c + 1 < xBp.size()) result += "\t";
      }
      result += "\n";
    }

    // Top-to-bottom matching TunerStudio layout (highest Y breakpoint first)
    for (size_t r = yBp.size(); r-- > 0; ) {
      if (includeHeaders) {
        result += formatValue(yBp[r], yDecimalPlaces_) + "\t"; 
      }
      for (size_t c = 0; c < xBp.size(); ++c) {
        result += formatValue(table_.value(r, c), 2); 
          if (c + 1 < xBp.size()) result += "\t";
      }
      result += "\n";
    }

    ImGui::SetClipboardText(result.c_str());
  }

  void TableEditorPanel::pasteFromClipboard()
  {
    const char *text = ImGui::GetClipboardText();
    if (!text || *text == '\0') return;

    std::string clip(text);
    std::stringstream ss(clip);
    std::string line;

    std::vector<std::vector<std::string>> grid;
    while (std::getline(ss, line)) {
      if (line.empty() || line == "\r") continue;
      std::stringstream lineStream(line);
      std::string cell;
      std::vector<std::string> row;
      while (std::getline(lineStream, cell, '\t')) {
        cell.erase(std::remove(cell.begin(), cell.end(), '\r'), cell.end());
        row.push_back(cell);
      }
      if (!row.empty()) grid.push_back(row);
    }

    if (grid.empty()) return;

    // Header detection: row 0 starts with empty token or tab
    bool hasHeaders = grid[0][0].empty();

    if (hasHeaders && grid.size() > 1) {
      std::vector<double> newXBp;
      for (size_t c = 1; c < grid[0].size(); ++c) {
        try { newXBp.push_back(std::stod(grid[0][c])); } catch (...) {}
      }

      std::vector<double> newYBp;
      std::vector<std::vector<double>> newValues;

      for (size_t r = 1; r < grid.size(); ++r) {
        try {
          newYBp.push_back(std::stod(grid[r][0]));
          std::vector<double> valRow;
          for (size_t c = 1; c < grid[r].size(); ++c) {
            valRow.push_back(std::stod(grid[r][c]));
          }
          newValues.push_back(valRow);
        } catch (...) {}
      }

      std::reverse(newYBp.begin(), newYBp.end());
      std::reverse(newValues.begin(), newValues.end());

      table_.setXBreakpoints(newXBp); 
        table_.setYBreakpoints(newYBp); 
        for (size_t r = 0; r < newValues.size() && r < table_.rowCount(); ++r) {
          
          for (size_t c = 0; c < newValues[r].size() && c < table_.columnCount(); ++c) {
            
            table_.setValue(r, c, newValues[r][c]); 
          }
        }
    } else {
      // Plain cell grid paste (preserving existing axes)
      for (size_t r = 0; r < grid.size() && r < table_.rowCount(); ++r) {
        
        size_t targetRow = table_.rowCount() - 1 - r;
        for (size_t c = 0; c < grid[r].size() && c < table_.columnCount(); ++c) {
          
          try { table_.setValue(targetRow, c, std::stod(grid[r][c])); } catch (...) {}
        }
      }
    }
  }

  bool TableEditorPanel::importTunerStudioXml(const std::string &xmlContent)
  {
    auto extractVector = [](const std::string &src, const std::string &tag) -> std::vector<double> {
      std::vector<double> result;
      size_t startTag = src.find("<" + tag);
      if (startTag == std::string::npos) return result;
      size_t contentStart = src.find('>', startTag);
      size_t endTag = src.find("</" + tag + ">", contentStart);
      if (contentStart == std::string::npos || endTag == std::string::npos) return result;

      std::string inner = src.substr(contentStart + 1, endTag - contentStart - 1);
      std::stringstream ss(inner);
      double val;
      while (ss >> val) {
        result.push_back(val);
      }
      return result;
      };

    std::vector<double> xBp = extractVector(xmlContent, "xAxis");
      std::vector<double> yBp = extractVector(xmlContent, "yAxis");
      std::vector<double> zVals = extractVector(xmlContent, "zValues");

      if (xBp.empty() || yBp.empty() || zVals.empty()) {
        return false;
      }

    table_.setXBreakpoints(xBp); 
      table_.setYBreakpoints(yBp); 

      size_t rows = yBp.size();
    size_t cols = xBp.size();

    if (zVals.size() < rows * cols) return false;

    for (size_t r = 0; r < rows; ++r) {
      for (size_t c = 0; c < cols; ++c) {
        table_.setValue(r, c, zVals[r * cols + c]); 
      }
    }

    return true;
  }

  void TableEditorPanel::renderToast()
  {
    if (copyToastTimer_ > 0.0f) {
      copyToastTimer_ -= ImGui::GetIO().DeltaTime;
      float alpha = (copyToastTimer_ < 0.5f) ? (copyToastTimer_ / 0.5f) : 1.0f;
      if (alpha < 0.0f) alpha = 0.0f;

      ImVec2 winPos = ImGui::GetWindowPos();
      ImVec2 winSize = ImGui::GetWindowSize();

      ImVec2 toastSize(160.0f, 30.0f);
      // Anchor to top-right of the current panel
      ImVec2 pMin(winPos.x + winSize.x - toastSize.x - 20.0f, winPos.y + 35.0f);
      ImVec2 pMax(pMin.x + toastSize.x, pMin.y + toastSize.y);

      ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.1f, 0.3f, 0.1f, 0.9f * alpha));
      ImU32 textCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.4f, 1.0f, 0.4f, alpha));
      ImU32 borderCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.2f, 0.6f, 0.2f, 0.9f * alpha));

      // Paint directly to the screen overlay (immune to ImGui window state bugs)
      ImDrawList *drawList = ImGui::GetForegroundDrawList();
      drawList->AddRectFilled(pMin, pMax, bgCol, 4.0f);
      drawList->AddRect(pMin, pMax, borderCol, 4.0f);

      const char *text = "Copied to clipboard!";
      ImVec2 textSize = ImGui::CalcTextSize(text);
      ImVec2 textPos(pMin.x + (toastSize.x - textSize.x) * 0.5f, pMin.y + (toastSize.y - textSize.y) * 0.5f);
      drawList->AddText(textPos, textCol, text);
    }
  }

  bool TableEditorPanel::renderAxisEditorPopup(const char *title)
  {
    ImGui::Text("Number of bins");

    int remapBinCount = static_cast<int>(axisEditorValues_.size());
    int oldCount = remapBinCount;

    ImGui::InputInt("##bin_count", &remapBinCount, 1, 10);

    if (remapBinCount < 0) remapBinCount = 0;

    if (remapBinCount != oldCount) {
      axisEditorValues_.resize(remapBinCount, 0.0);
    }

    ImGui::Separator();

    ImGui::BeginChild("BinValues", ImVec2(250, 300), true);

    int decimalPlaces = (editingAxis_ == AxisEditing::X) ? xDecimalPlaces_ : yDecimalPlaces_;
    std::string format = "%." + std::to_string(decimalPlaces) + "f";

    for (int i = 0; i < static_cast<int>(axisEditorValues_.size()); ++i) {
      ImGui::PushID(i);
      std::string label = "Bin " + std::to_string(i + 1);
      ImGui::InputDouble(label.c_str(), &axisEditorValues_[i], 0.0, 0.0, format.c_str());
      ImGui::PopID();
    }

    ImGui::EndChild();

    if (ImGui::Button("Space evenly")) {
      if (axisEditorValues_.size() >= 2) {
        double first = axisEditorValues_.front();
        double last = axisEditorValues_.back();
        double step = (last - first) / (axisEditorValues_.size() - 1);
        for (size_t i = 1; i + 1 < axisEditorValues_.size(); ++i)
          axisEditorValues_[i] = first + step * i;
      }
    }

    ImGui::Separator();

    bool accepted = false;
    if (ImGui::Button("OK")) {
      ImGui::CloseCurrentPopup();
      accepted = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
      ImGui::CloseCurrentPopup();
      if (editingAxis_ == AxisEditing::X)
        axisEditorValues_ = table_.xBreakpoints(); 
      else if (editingAxis_ == AxisEditing::Y)
        axisEditorValues_ = table_.yBreakpoints(); 
    }
    return accepted;
  }

  void TableEditorPanel::renderValueGrid()
  {
    const auto &xBreakpoints = table_.xBreakpoints(); 
      const auto &yBreakpoints = table_.yBreakpoints(); 
      const size_t cols = xBreakpoints.size();
    const size_t rows = yBreakpoints.size();

    if (cols == 0 || rows == 0) {
      ImGui::TextDisabled("No breakpoints defined yet.");
      return;
    }

    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
      ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
      ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings;

    if (ImGui::BeginTable("ValueGrid", static_cast<int>(cols + 1), flags, ImVec2(0.0f, 400.0f))) {
      ImGui::TableSetupScrollFreeze(1, 1);
      ImGui::TableSetupColumn("Load \\ RPM", ImGuiTableColumnFlags_WidthFixed, 90.0f);

      for (size_t c = 0; c < cols; ++c) {
        std::string header = formatValue(xBreakpoints[c], xDecimalPlaces_); 
          ImGui::TableSetupColumn(header.c_str(), ImGuiTableColumnFlags_WidthFixed, 60.0f);
      }
      ImGui::TableHeadersRow();

      for (size_t r = rows; r-- > 0; ) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%s", formatValue(yBreakpoints[r], yDecimalPlaces_).c_str()); 

          for (size_t c = 0; c < cols; ++c) {
            ImGui::TableSetColumnIndex(static_cast<int>(c + 1));
            ImGui::PushID(static_cast<int>(r * cols + c));

            double v = table_.value(r, c); 

              if (editingRow_ == static_cast<int>(r) && editingCol_ == static_cast<int>(c)) {
                ImGui::SetNextItemWidth(55.0f);
                // Auto-focus the input widget on the frame it opens
                ImGui::SetKeyboardFocusHere();

                bool committed = ImGui::InputDouble("##cell_edit", &v, 0.0, 0.0, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue);

                // 1. Commit on Enter
                if (committed) {
                  table_.setValue(r, c, v);
                  editingRow_ = -1;
                  editingCol_ = -1;
                }
                // 2. Cancel on Escape key or when clicking away (deactivated without committing)
                else if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsItemDeactivated()) {
                  editingRow_ = -1;
                  editingCol_ = -1;
                }
              } else {
                char cellText[32];
                std::snprintf(cellText, sizeof(cellText), "%.2f", v);
                bool isSelected = (selectedRow_ == static_cast<int>(r) && selectedCol_ == static_cast<int>(c));
                if (ImGui::Selectable(cellText, isSelected, ImGuiSelectableFlags_AllowDoubleClick)) {
                  selectedRow_ = static_cast<int>(r);
                  selectedCol_ = static_cast<int>(c);
                  if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    editingRow_ = static_cast<int>(r);
                    editingCol_ = static_cast<int>(c);
                  }
                }
              }

            ImGui::PopID();
          }
      }
      ImGui::EndTable();
    }
  }

  void TableEditorPanel::render(PlotCursor & /*cursor*/)
  {
    if (ImGui::Button("Edit vertical axis")) {
      axisEditorValues_ = table_.yBreakpoints(); 
      axisEditorBinCount_ = static_cast<int>(axisEditorValues_.size());
      editingAxis_ = AxisEditing::Y;
      ImGui::OpenPopup("Axis Editor");
    }

    ImGui::SameLine();

    if (ImGui::Button("Edit horizontal axis")) {
      axisEditorValues_ = table_.xBreakpoints(); 
      axisEditorBinCount_ = static_cast<int>(axisEditorValues_.size());
      editingAxis_ = AxisEditing::X;
      ImGui::OpenPopup("Axis Editor");
    }

    ImGui::SameLine();

    if (ImGui::Button("Import / Export...")) {
      ImGui::OpenPopup("TableImportExportMenu");
    }

    if (ImGui::BeginPopup("TableImportExportMenu")) {
      if (ImGui::MenuItem("Copy Table")) {
        copyToClipboard(false);
        showCopyToast();
      }
      if (ImGui::MenuItem("Copy Table + Headers (Clipboard)")) {
        copyToClipboard(true);
        showCopyToast();
      }
      if (ImGui::MenuItem("Paste Grid (Clipboard)")) {
        pasteFromClipboard();
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Import TunerStudio (.table)...")) {
        auto openDlg = std::make_shared<pfd::open_file>(
          "Import TunerStudio Table", ".",
          std::vector<std::string>{"TunerStudio Table (*.table)", "*.table", "All Files", "*"});
        auto result = openDlg->result();
        if (!result.empty()) {
          std::ifstream f(result.front());
          if (f.is_open()) {
            std::string content((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            importTunerStudioXml(content);
          }
        }
      }
      ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal("Axis Editor")) {
      if (renderAxisEditorPopup("Axis Editor")) {
        if (editingAxis_ == AxisEditing::X)
          table_.setXBreakpoints(axisEditorValues_); 
        else
          table_.setYBreakpoints(axisEditorValues_); 

          editingAxis_ = AxisEditing::None;
      }
      ImGui::EndPopup();
    }

    ImGui::Separator();

    renderValueGrid();

    renderToast();
  }

  nlohmann::json TableEditorPanel::saveState() const
  {
    nlohmann::json valuesJson = PlotPanel::saveState();
    for (size_t r = 0; r < table_.rowCount(); ++r) {
      nlohmann::json rowJson = nlohmann::json::array();
      for (size_t c = 0; c < table_.columnCount(); ++c) {
        rowJson.push_back(table_.value(r, c)); 
      }
      valuesJson.push_back(rowJson);
    }
    return nlohmann::json{ {"xBreakpoints", table_.xBreakpoints()},
                           {"yBreakpoints", table_.yBreakpoints()},
                           {"values", valuesJson} };
  }

  void TableEditorPanel::loadState(const nlohmann::json &state)
  {
    PlotPanel::loadState(state);
    std::vector<double> xBp = table_.xBreakpoints(); 
      std::vector<double> yBp = table_.yBreakpoints(); 
      if (state.contains("xBreakpoints") && state["xBreakpoints"].is_array()) {
        xBp = state["xBreakpoints"].get<std::vector<double>>();
      }
    if (state.contains("yBreakpoints") && state["yBreakpoints"].is_array()) {
      yBp = state["yBreakpoints"].get<std::vector<double>>();
    }
    table_.setXBreakpoints(xBp); 
      table_.setYBreakpoints(yBp); 

      if (state.contains("values") && state["values"].is_array()) {
        const auto &valuesJson = state["values"];
        for (size_t r = 0; r < valuesJson.size() && r < table_.rowCount(); ++r) {
          const auto &rowJson = valuesJson[r];
          if (!rowJson.is_array()) continue;
          for (size_t c = 0; c < rowJson.size() && c < table_.columnCount(); ++c) {
            if (rowJson[c].is_number()) {
              table_.setValue(r, c, rowJson[c].get<double>()); 
            }
          }
        }
      }
  }

} // namespace ui