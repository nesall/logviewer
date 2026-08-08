#include "ui/tableeditorpanel.h"
#include "ui/ui_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <utility>
#include <random>

#include "3rdparty/nlohmann/json.hpp"
#include "3rdparty/portable-file-dialogs.h"
#include "imgui.h"
#include "imgui_internal.h"

namespace {
  std::string generateUniqueId(size_t length = 8) {
    static const char charset[] =
      "0123456789"
      "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
      "abcdefghijklmnopqrstuvwxyz";
    static std::mt19937 rng(std::random_device{}());
    static std::uniform_int_distribution<> dist(0, sizeof(charset) - 2);
    std::string id;
    id.reserve(length);
    for (size_t i = 0; i < length; ++i) {
      id += charset[dist(rng)];
    }
    return id;
  }
}

namespace ui {

  TableEditorPanel::TableEditorPanel(std::string title, std::string panelTypeIdValue,
    std::string displayName, core::Table2D initialTable, int xDecimalPlaces, int yDecimalPlaces)
    : PlotPanel(std::move(title))
    , tableUniqueId_(generateUniqueId())
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

  void TableEditorPanel::clearSelection()
  {
    selectedCells_.clear();
  }

  bool TableEditorPanel::isCellSelected(int r, int c) const
  {
    return selectedCells_.count({ r, c }) > 0;
  }

  void TableEditorPanel::selectRectangularRegion(int r1, int c1, int r2, int c2, bool keepExisting)
  {
    if (!keepExisting) {
      clearSelection();
    }
    int minR = (std::min)(r1, r2);
    int maxR = (std::max)(r1, r2);
    int minC = (std::min)(c1, c2);
    int maxC = (std::max)(c1, c2);

    for (int r = minR; r <= maxR; ++r) {
      for (int c = minC; c <= maxC; ++c) {
        selectedCells_.insert({ r, c });
      }
    }
  }

  void TableEditorPanel::getSelectionBounds(int &minR, int &maxR, int &minC, int &maxC) const
  {
    if (selectedCells_.empty()) {
      minR = maxR = minC = maxC = -1;
      return;
    }
    minR = 1e9; maxR = -1;
    minC = 1e9; maxC = -1;
    for (const auto &[r, c] : selectedCells_) {
      if (r < minR) minR = r;
      if (r > maxR) maxR = r;
      if (c < minC) minC = c;
      if (c > maxC) maxC = c;
    }
  }

  void TableEditorPanel::setSelection(const std::set<std::pair<int, int>> &cells)
  {
    selectedCells_ = cells;
  }

  void TableEditorPanel::applyBatchMultiply(double factor)
  {
    for (const auto &[r, c] : selectedCells_) {
      double v = table_.value(r, c);
      table_.setValue(r, c, v * factor);
    }
    notifyDataChanged();
  }

  void TableEditorPanel::applyBatchOffset(double delta)
  {
    for (const auto &[r, c] : selectedCells_) {
      double v = table_.value(r, c);
      table_.setValue(r, c, v + delta);
    }
    notifyDataChanged();
  }

  void TableEditorPanel::applyBatchSetValue(double value)
  {
    for (const auto &[r, c] : selectedCells_) {
      table_.setValue(r, c, value);
    }
    notifyDataChanged();
  }

  void TableEditorPanel::interpolateSelectedRegion()
  {
    int minR, maxR, minC, maxC;
    getSelectionBounds(minR, maxR, minC, maxC);
    if (minR < 0 || (minR == maxR && minC == maxC)) return;

    double vTopLeft = table_.value(maxR, minC);
    double vTopRight = table_.value(maxR, maxC);
    double vBottomLeft = table_.value(minR, minC);
    double vBottomRight = table_.value(minR, maxC);

    int numRows = maxR - minR;
    int numCols = maxC - minC;

    for (int r = minR; r <= maxR; ++r) {
      double tY = (numRows > 0) ? static_cast<double>(r - minR) / numRows : 0.0;
      for (int c = minC; c <= maxC; ++c) {
        double tX = (numCols > 0) ? static_cast<double>(c - minC) / numCols : 0.0;

        double top = vBottomLeft + (vBottomRight - vBottomLeft) * tX;
        double bottom = vTopLeft + (vTopRight - vTopLeft) * tX;
        double interpolated = top + (bottom - top) * tY;

        table_.setValue(r, c, interpolated);
      }
    }
    notifyDataChanged();
  }

  void TableEditorPanel::renderBatchToolbar()
  {
    bool hasSelection = !selectedCells_.empty();

    if (batchToolbarVisible_) {
      ImGui::BeginDisabled(!hasSelection);

      ImGui::SetNextItemWidth(80.0f);
      ImGui::InputDouble("##batchVal", &batchValue_, 0.0, 0.0, "%.2f");

      ImGui::SameLine();
      if (ui::UI::Button("* Scale", {}, {}, "Multiply selected cells by factor")) {
        applyBatchMultiply(batchValue_);
      }

      ImGui::SameLine();
      if (ui::UI::Button("+ Offset", {}, {}, "Add offset to selected cells")) {
        applyBatchOffset(batchValue_);
      }

      ImGui::SameLine();
      if (ui::UI::Button("= Set", {}, {}, "Set all selected cells to value")) {
        applyBatchSetValue(batchValue_);
      }

      ImGui::SameLine();
      if (ui::UI::Button("Interpolate", {}, {}, "Bilinear interpolation between bounding corners")) {
        interpolateSelectedRegion();
      }

      ImGui::SameLine();
      if (ui::UI::Button("Extrapolate VE", {}, {}, "Extrapolate values for selected cells")) {
        showExtrapolateModal_ = true;
        wasExtrapolateModalOpen_ = false;
        ImGui::OpenPopup(ui::popups::ExtrapolateVe);
      }
      renderExtrapolateModal();

      ImGui::EndDisabled();

      if (hasSelection) {
        ImGui::SameLine();
        ImGui::TextDisabled("(%zu cells selected)", selectedCells_.size());
      }
    }

    if (customToolbarCallback_) {
      if (batchToolbarVisible_) {
        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
      }
      ImGui::SameLine();
      customToolbarCallback_();
    }
  }

  void TableEditorPanel::renderExtrapolateModal()
  {
    // Pass &showExtrapolateModal_ so ImGui can toggle it if 'X' or Escape is pressed
    bool isOpen = ImGui::BeginPopupModal(ui::popups::ExtrapolateVe, &showExtrapolateModal_, ImGuiWindowFlags_AlwaysAutoResize);

    if (isOpen) {

      if (!wasExtrapolateModalOpen_) {
        tableBackup_ = table_;
        wasExtrapolateModalOpen_ = true;
        showPreview_ = true;
      }

      ImGui::InputFloat("Tolerance (%)", &guessTolerance_, 1.0f, 5.0f, "%.1f");

      ImGui::SeparatorText("Slope Calculation");
      ImGui::RadioButton("Auto (Nearest Neighbor)", &slopeMode_, 0);
      ImGui::SameLine();
      ImGui::RadioButton("Manual Slope", &slopeMode_, 1);

      ImGui::BeginDisabled(slopeMode_ == 0);
      ImGui::InputFloat("Manual Delta", &manualSlope_, 0.5f, 1.0f, "%.2f");
      ImGui::EndDisabled();

      ImGui::Separator();

      // Dynamically compute preview while modal is active
      applyExtrapolationPreview();

      if (ui::UI::Button("Apply", {}, ImVec2(100, 0)), "Commit the extrapolated values") {
        showPreview_ = false;
        showExtrapolateModal_ = false;
        wasExtrapolateModalOpen_ = false;
        ImGui::CloseCurrentPopup();
        notifyDataChanged();
      }
      ImGui::SameLine();
      if (ui::UI::Button("Cancel", {}, {}, "Discard changes and close popup")) {
        if (showPreview_) {
          table_ = tableBackup_;      // Revert changes
          showPreview_ = false;
        }
        showExtrapolateModal_ = false;
        wasExtrapolateModalOpen_ = false;
        ImGui::CloseCurrentPopup();
      }

      ImGui::EndPopup();
    } else if (wasExtrapolateModalOpen_) {
      // Handle close via 'X' button or Escape key
      if (showPreview_) {
        table_ = tableBackup_;
        showPreview_ = false;
      }
      wasExtrapolateModalOpen_ = false;
    }
  }

  void TableEditorPanel::applyExtrapolationPreview()
  {
    table_ = tableBackup_;
    showPreview_ = true;

    float tolMultiplier = guessTolerance_ / 100.0f;

    for (const auto &[r, c] : selectedCells_) {
      double oldVe = table_.value(r, c);
      double stepDelta = 0.0;

      if (slopeMode_ == 0) {
        // Auto: Use slope of previous two cells (assuming horizontal/RPM extrapolation)
        if (c >= 2) {
          stepDelta = table_.value(r, c - 1) - table_.value(r, c - 2);
        }
      } else {
        // Manual: Use user's exact input
        stepDelta = manualSlope_;
      }

      // Base guess
      double guessVe = (c >= 1 ? table_.value(r, c - 1) : oldVe) + stepDelta;

      // Clamp to tolerance limits
      double minVe = oldVe * (1.0 - tolMultiplier);
      double maxVe = oldVe * (1.0 + tolMultiplier);
      double finalVe = std::clamp(guessVe, minVe, maxVe);

      table_.setValue(r, c, finalVe);
    }
  }

  void TableEditorPanel::copyToClipboard(bool includeHeaders) const
  {
    const auto &xBp = table_.xBreakpoints();
    const auto &yBp = table_.yBreakpoints();
    std::string result;

    int minR, maxR, minC, maxC;
    getSelectionBounds(minR, maxR, minC, maxC);

    bool filterSelection = (minR >= 0);
    if (!filterSelection) {
      minR = 0; maxR = static_cast<int>(yBp.size()) - 1;
      minC = 0; maxC = static_cast<int>(xBp.size()) - 1;
    }

    if (includeHeaders) {
      result += "\t";
      for (int c = minC; c <= maxC; ++c) {
        result += formatValue(xBp[c], xDecimalPlaces_);
        if (c < maxC) result += "\t";
      }
      result += "\n";
    }

    for (int r = maxR; r >= minR; --r) {
      if (includeHeaders) {
        result += formatValue(yBp[r], yDecimalPlaces_) + "\t";
      }
      for (int c = minC; c <= maxC; ++c) {
        result += formatValue(table_.value(r, c), 2);
        if (c < maxC) result += "\t";
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
      tableUniqueId_ = generateUniqueId();
    } else {
      int startR = static_cast<int>(table_.rowCount()) - 1;
      int startC = 0;
      if (!selectedCells_.empty()) {
        int minR, maxR, minC, maxC;
        getSelectionBounds(minR, maxR, minC, maxC);
        startR = maxR;
        startC = minC;
      }

      for (size_t r = 0; r < grid.size(); ++r) {
        int targetRow = startR - static_cast<int>(r);
        if (targetRow < 0 || targetRow >= static_cast<int>(table_.rowCount())) continue;

        for (size_t c = 0; c < grid[r].size(); ++c) {
          int targetCol = startC + static_cast<int>(c);
          if (targetCol >= static_cast<int>(table_.columnCount())) break;

          try { table_.setValue(targetRow, targetCol, std::stod(grid[r][c])); } catch (...) {}
        }
      }
    }
    notifyDataChanged();
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
    tableUniqueId_ = generateUniqueId();
    notifyDataChanged();
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
      ImVec2 pMin(winPos.x + winSize.x - toastSize.x - 20.0f, winPos.y + 35.0f);
      ImVec2 pMax(pMin.x + toastSize.x, pMin.y + toastSize.y);

      ImU32 bgCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.1f, 0.3f, 0.1f, 0.9f * alpha));
      ImU32 textCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.4f, 1.0f, 0.4f, alpha));
      ImU32 borderCol = ImGui::ColorConvertFloat4ToU32(ImVec4(0.2f, 0.6f, 0.2f, 0.9f * alpha));

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

    if (ui::UI::Button("Space evenly", {}, {}, "Distribute bins evenly")) {
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
    if (ui::UI::Button("OK")) {
      ImGui::CloseCurrentPopup();
      accepted = true;
      tableUniqueId_ = generateUniqueId();
      notifyDataChanged();
    }
    ImGui::SameLine();
    if (ui::UI::Button("Cancel", {}, {}, "Discard changes and close popup")) {
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

    // --- 1. Compute Min/Max for Table Heatmap Normalization ---
    double minVal = (std::numeric_limits<double>::max)();
    double maxVal = -(std::numeric_limits<double>::max)();

    for (size_t r = 0; r < rows; ++r) {
      for (size_t c = 0; c < cols; ++c) {
        double v = table_.value(r, c);
        if (v < minVal) minVal = v;
        if (v > maxVal) maxVal = v;
      }
    }
    if (maxVal <= minVal) maxVal = minVal + 1.0;

    // Thermal Heatmap Gradient Generator (Cool Blue -> Green -> Yellow -> Orange -> Red)
    auto getHeatmapColor = [&](double value) -> ImU32 {
      float t = static_cast<float>((value - minVal) / (maxVal - minVal));
      if (t < 0.0f) t = 0.0f;
      if (t > 1.0f) t = 1.0f;

      float r = 0.0f, g = 0.0f, b = 0.0f;
      if (t < 0.25f) {
        float f = t / 0.25f;
        b = 0.8f; g = 0.2f + 0.6f * f; r = 0.1f;
      } else if (t < 0.50f) {
        float f = (t - 0.25f) / 0.25f;
        g = 0.8f; b = 0.8f * (1.0f - f); r = 0.1f + 0.7f * f;
      } else if (t < 0.75f) {
        float f = (t - 0.50f) / 0.25f;
        r = 0.8f + 0.2f * f; g = 0.8f * (1.0f - 0.5f * f); b = 0.0f;
      } else {
        float f = (t - 0.75f) / 0.25f;
        r = 1.0f; g = 0.4f * (1.0f - f); b = 0.0f;
      }

      // Use alpha = 0.35f so cell text and selections remain crisp and readable
      return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, 0.35f));
      };

    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
      ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
      ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings;

    std::string tableId = "ValueGrid###" + title() + "_" + tableUniqueId_;
    if (ImGui::BeginTable(tableId.c_str(), static_cast<int>(cols + 1), flags, ImVec2(0.0f, 400.0f))) {
      ImGui::TableSetupScrollFreeze(1, 1);
      ImGui::TableSetupColumn("Load \\ RPM", ImGuiTableColumnFlags_WidthFixed, 90.0f);

      for (size_t c = 0; c < cols; ++c) {
        std::string header = formatValue(xBreakpoints[c], xDecimalPlaces_);
        ImGui::TableSetupColumn(header.c_str(), ImGuiTableColumnFlags_WidthFixed, 60.0f);
      }
      ImGui::TableHeadersRow();

      ImGuiIO &io = ImGui::GetIO();

      for (size_t r = rows; r-- > 0; ) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%s", formatValue(yBreakpoints[r], yDecimalPlaces_).c_str());

        for (size_t c = 0; c < cols; ++c) {
          ImGui::TableSetColumnIndex(static_cast<int>(c + 1));
          ImGui::PushID(static_cast<int>(r * cols + c));

          double v = table_.value(r, c);

          const bool isPreviewingExtrapolation = showPreview_ && showExtrapolateModal_;

          if (isPreviewingExtrapolation && selectedCells_.count({ static_cast<int>(r), static_cast<int>(c) })) {
            // Highlight extrapolated cells in purple/magenta
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, IM_COL32(180, 50, 200, 150));
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%.2f", v); // Yellow text for changes
          } else {

            bool selected = isCellSelected(static_cast<int>(r), static_cast<int>(c));

            // --- 2. Apply Cell Heatmap Background ---
            if (selected) {
              ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, ImGui::GetColorU32(ImGuiCol_HeaderActive));
            } else {
              if (customHeatmapColorFunc_) {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, customHeatmapColorFunc_(v, r, c));
              } else {
                ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, getHeatmapColor(v));
              }
            }

            if (editingRow_ == static_cast<int>(r) && editingCol_ == static_cast<int>(c)) {
              ImGui::SetNextItemWidth(55.0f);
              ImGui::SetKeyboardFocusHere();

              bool committed = ImGui::InputDouble("##cell_edit", &v, 0.0, 0.0, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue);

              if (committed) {
                table_.setValue(r, c, v);
                notifyDataChanged();
                editingRow_ = -1;
                editingCol_ = -1;
              } else if (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsItemDeactivated()) {
                editingRow_ = -1;
                editingCol_ = -1;
              }
            } else {
              char cellText[32];
              std::snprintf(cellText, sizeof(cellText), "%.2f", v);

              bool hasCustomTextColor = false;
              if (customTextColorFunc_) {
                if (auto cclr = customTextColorFunc_(v, r, c)) {
                  ImGui::PushStyleColor(ImGuiCol_Text, *cclr);
                  hasCustomTextColor = true;
                }
              }

              ImGuiSelectableFlags selFlags = ImGuiSelectableFlags_AllowDoubleClick;
              ImGui::Selectable(cellText, selected, selFlags);

              if (hasCustomTextColor) {
                ImGui::PopStyleColor();
              }

              bool isHovered = ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
              bool isSelectionLogic = false;
              if (isHovered) {
                // 1. Drag Select
                if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && anchorRow_ >= 0 && !io.KeyCtrl && !io.KeyShift) {
                  selectRectangularRegion(anchorRow_, anchorCol_, static_cast<int>(r), static_cast<int>(c));
                  isSelectionLogic = true;
                }

                // 2. Initial Click Down
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                  if (io.KeyCtrl) {
                    std::pair<int, int> cellKey = { static_cast<int>(r), static_cast<int>(c) };
                    if (selectedCells_.count(cellKey)) {
                      selectedCells_.erase(cellKey);
                    } else {
                      selectedCells_.insert(cellKey);
                    }
                    anchorRow_ = static_cast<int>(r);
                    anchorCol_ = static_cast<int>(c);
                  } else if (io.KeyShift && anchorRow_ >= 0) {
                    selectRectangularRegion(anchorRow_, anchorCol_, static_cast<int>(r), static_cast<int>(c));
                  } else {
                    clearSelection();
                    anchorRow_ = static_cast<int>(r);
                    anchorCol_ = static_cast<int>(c);
                    selectedCells_.insert({ anchorRow_, anchorCol_ });
                  }

                  if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                    editingRow_ = static_cast<int>(r);
                    editingCol_ = static_cast<int>(c);
                  }
                  isSelectionLogic = true;
                }
              }

              if (ImGui::IsItemHovered() && customHoverTooltipFunc_) {
                auto tooltip = customHoverTooltipFunc_(v, r, c);
                if (!tooltip.empty()) {
                  ImGui::BeginTooltip();
                  ImGui::TextUnformatted(tooltip.c_str());
                  ImGui::EndTooltip();
                }
              }
            }
          }
          ImGui::PopID();
        }
      }
      ImGui::EndTable();
    }
  }

  void TableEditorPanel::render3DSurfaceMesh()
  {
    const size_t rows = table_.rowCount();
    const size_t cols = table_.columnCount();

    if (rows < 2 || cols < 2) {
      ImGui::TextDisabled("Table must have at least 2x2 dimensions for 3D surface rendering.");
      return;
    }

    ImGui::TextDisabled("LMB Drag: Rotate | RMB Drag: Pan | Mouse Wheel: Zoom");
    ImGui::SameLine();
    if (ui::UI::Button("Reset View##3d_reset")) {
      cameraYaw_ = 45.0f;
      cameraPitch_ = 30.0f;
      cameraZoom_ = 1.0f;
      cameraPan_ = ImVec2(0.0f, 0.0f);
    }

    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    if (canvasSize.x < 100.0f) canvasSize.x = 100.0f;
    if (canvasSize.y < 200.0f) canvasSize.y = 200.0f;

    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImGui::InvisibleButton("##3d_surface_canvas", canvasSize,
      ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);

    bool isHovered = ImGui::IsItemHovered();
    bool isActive = ImGui::IsItemActive();
    ImGuiIO &io = ImGui::GetIO();

    if (isActive && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
      cameraYaw_ += io.MouseDelta.x * 0.5f;
      cameraPitch_ += io.MouseDelta.y * 0.5f;
      if (cameraPitch_ < -89.0f) cameraPitch_ = -89.0f;
      if (cameraPitch_ > 89.0f) cameraPitch_ = 89.0f;
    }

    if (isActive && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
      cameraPan_.x += io.MouseDelta.x;
      cameraPan_.y += io.MouseDelta.y;
    }

    if (isHovered && io.MouseWheel != 0.0f) {
      cameraZoom_ += io.MouseWheel * 0.1f;
      if (cameraZoom_ < 0.2f) cameraZoom_ = 0.2f;
      if (cameraZoom_ > 5.0f) cameraZoom_ = 5.0f;
    }

    ImDrawList *drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), true);

    // Canvas background
    drawList->AddRectFilled(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(20, 22, 28, 255));
    drawList->AddRect(canvasPos, ImVec2(canvasPos.x + canvasSize.x, canvasPos.y + canvasSize.y), IM_COL32(60, 65, 80, 255));

    // Find table value bounds
    double minZ = (std::numeric_limits<double>::max)();
    double maxZ = -(std::numeric_limits<double>::max)();
    for (size_t r = 0; r < rows; ++r) {
      for (size_t c = 0; c < cols; ++c) {
        double v = table_.value(r, c);
        if (v < minZ) minZ = v;
        if (v > maxZ) maxZ = v;
      }
    }
    if (maxZ <= minZ) maxZ = minZ + 1.0;

    const float radYaw = cameraYaw_ * 0.0174532925f;
    const float radPitch = cameraPitch_ * 0.0174532925f;
    const float cosY = std::cos(radYaw), sinY = std::sin(radYaw);
    const float cosP = std::cos(radPitch), sinP = std::sin(radPitch);

    const ImVec2 centerPos(canvasPos.x + canvasSize.x * 0.5f + cameraPan_.x, canvasPos.y + canvasSize.y * 0.5f + cameraPan_.y);
    const float scale = (std::min)(canvasSize.x, canvasSize.y) * 0.35f * cameraZoom_;

    auto project3D = [&](float x, float y, float z, float &outDepth) -> ImVec2 {
      float x1 = x * cosY - y * sinY;
      float y1 = x * sinY + y * cosY;
      float z1 = z;

      float y2 = y1 * cosP - z1 * sinP;
      float z2 = y1 * sinP + z1 * cosP;

      outDepth = y2;
      return ImVec2(centerPos.x + x1 * scale, centerPos.y - z2 * scale);
      };

    auto getColor = [](float t) -> ImU32 {
      if (t < 0.0f) t = 0.0f;
      if (t > 1.0f) t = 1.0f;
      float r = 0.0f, g = 0.0f, b = 0.0f;
      if (t < 0.25f) {
        float f = t / 0.25f; b = 1.0f; g = f;
      } else if (t < 0.50f) {
        float f = (t - 0.25f) / 0.25f; g = 1.0f; b = 1.0f - f;
      } else if (t < 0.75f) {
        float f = (t - 0.50f) / 0.25f; g = 1.0f; r = f;
      } else {
        float f = (t - 0.75f) / 0.25f; r = 1.0f; g = 1.0f - f;
      }
      return IM_COL32(static_cast<int>(r * 255), static_cast<int>(g * 255), static_cast<int>(b * 255), 180);
      };

    // --- 1. Draw Bounding Axis Lines & Labels ---
    float dummyD = 0.0f;
    ImVec2 boxMin = project3D(-1.0f, -1.0f, -0.75f, dummyD);
    ImVec2 boxMaxX = project3D(1.0f, -1.0f, -0.75f, dummyD);
    ImVec2 boxMaxY = project3D(-1.0f, 1.0f, -0.75f, dummyD);
    ImVec2 boxMaxZ = project3D(-1.0f, -1.0f, 0.75f, dummyD);

    // Draw base axis wireframe
    drawList->AddLine(boxMin, boxMaxX, IM_COL32(200, 80, 80, 200), 2.0f);  // X axis (Red)
    drawList->AddLine(boxMin, boxMaxY, IM_COL32(80, 200, 80, 200), 2.0f);  // Y axis (Green)
    drawList->AddLine(boxMin, boxMaxZ, IM_COL32(80, 150, 255, 200), 2.0f); // Z axis (Blue)

    // Render Axis Labels
    const auto &xBp = table_.xBreakpoints();
    const auto &yBp = table_.yBreakpoints();

    std::string xLabel = "RPM (" + formatValue(xBp.front(), xDecimalPlaces_) + " .. " + formatValue(xBp.back(), xDecimalPlaces_) + ")";
    std::string yLabel = "Load (" + formatValue(yBp.front(), yDecimalPlaces_) + " .. " + formatValue(yBp.back(), yDecimalPlaces_) + ")";
    std::string zLabel = "Value (" + formatValue(minZ, 1) + " .. " + formatValue(maxZ, 1) + ")";

    drawList->AddText(boxMaxX, IM_COL32(240, 120, 120, 255), xLabel.c_str());
    drawList->AddText(boxMaxY, IM_COL32(120, 240, 120, 255), yLabel.c_str());
    drawList->AddText(boxMaxZ, IM_COL32(120, 180, 255, 255), zLabel.c_str());

    // --- 2. Build 3D Mesh Grid & Raycast Tooltip Search ---
    std::vector<std::vector<ImVec2>> screenPts(rows, std::vector<ImVec2>(cols));
    std::vector<std::vector<float>> depths(rows, std::vector<float>(cols));
    std::vector<std::vector<float>> normZ(rows, std::vector<float>(cols));

    ImVec2 mousePos = io.MousePos;
    int hoveredRow = -1;
    int hoveredCol = -1;
    float minHitDistSq = 400.0f; // 20px hover radius limit

    for (size_t r = 0; r < rows; ++r) {
      float ny = (rows > 1) ? (2.0f * static_cast<float>(r) / static_cast<float>(rows - 1) - 1.0f) : 0.0f;
      for (size_t c = 0; c < cols; ++c) {
        float nx = (cols > 1) ? (2.0f * static_cast<float>(c) / static_cast<float>(cols - 1) - 1.0f) : 0.0f;
        double val = table_.value(r, c);
        float nz = static_cast<float>((val - minZ) / (maxZ - minZ) * 1.5 - 0.75);
        normZ[r][c] = static_cast<float>((val - minZ) / (maxZ - minZ));

        ImVec2 pt = project3D(nx, ny, nz, depths[r][c]);
        screenPts[r][c] = pt;

        if (isHovered) {
          float dx = mousePos.x - pt.x;
          float dy = mousePos.y - pt.y;
          float distSq = dx * dx + dy * dy;
          if (distSq < minHitDistSq) {
            minHitDistSq = distSq;
            hoveredRow = static_cast<int>(r);
            hoveredCol = static_cast<int>(c);
          }
        }
      }
    }

    struct MeshQuad {
      size_t r, c;
      float avgDepth;
    };

    std::vector<MeshQuad> quads;
    quads.reserve((rows - 1) * (cols - 1));
    for (size_t r = 0; r < rows - 1; ++r) {
      for (size_t c = 0; c < cols - 1; ++c) {
        float avgD = (depths[r][c] + depths[r + 1][c] + depths[r][c + 1] + depths[r + 1][c + 1]) * 0.25f;
        quads.push_back({ r, c, avgD });
      }
    }

    std::sort(quads.begin(), quads.end(), [](const MeshQuad &a, const MeshQuad &b) {
      return a.avgDepth < b.avgDepth; // Painter's algorithm
      });

    for (const auto &q : quads) {
      size_t r = q.r;
      size_t c = q.c;
      ImVec2 p0 = screenPts[r][c];
      ImVec2 p1 = screenPts[r][c + 1];
      ImVec2 p2 = screenPts[r + 1][c + 1];
      ImVec2 p3 = screenPts[r + 1][c];

      float avgNormZ = (normZ[r][c] + normZ[r][c + 1] + normZ[r + 1][c + 1] + normZ[r + 1][c]) * 0.25f;
      drawList->AddQuadFilled(p0, p1, p2, p3, getColor(avgNormZ));
      drawList->AddQuad(p0, p1, p2, p3, IM_COL32(255, 255, 255, 50), 1.0f);
    }

    // --- 3. Render Hover Highlight & Tooltip ---
    if (hoveredRow >= 0 && hoveredCol >= 0) {
      ImVec2 hPt = screenPts[hoveredRow][hoveredCol];

      // Draw outer glowing ring on hovered node
      drawList->AddCircleFilled(hPt, 6.0f, IM_COL32(255, 255, 0, 220));
      drawList->AddCircle(hPt, 8.0f, IM_COL32(255, 255, 255, 255), 0, 2.0f);

      double xBpVal = (static_cast<size_t>(hoveredCol) < xBp.size()) ? xBp[hoveredCol] : 0.0;
      double yBpVal = (static_cast<size_t>(hoveredRow) < yBp.size()) ? yBp[hoveredRow] : 0.0;
      double cellVal = table_.value(hoveredRow, hoveredCol);

      ImGui::BeginTooltip();
      ImGui::Text("Cell [%d, %d]", hoveredRow, hoveredCol);
      ImGui::Separator();
      ImGui::Text("X (RPM):  %s", formatValue(xBpVal, xDecimalPlaces_).c_str());
      ImGui::Text("Y (Load): %s", formatValue(yBpVal, yDecimalPlaces_).c_str());
      ImGui::TextColored(ImVec4(0.4f, 0.9f, 1.0f, 1.0f), "Value:    %.2f", cellVal);
      ImGui::EndTooltip();
    }

    drawList->PopClipRect();
  }

  void TableEditorPanel::render(PlotCursor & /*cursor*/)
  {
    if (ui::UI::Button("Edit vertical axis")) {
      axisEditorValues_ = table_.yBreakpoints();
      axisEditorBinCount_ = static_cast<int>(axisEditorValues_.size());
      editingAxis_ = AxisEditing::Y;
      ImGui::OpenPopup(ui::popups::AxisEditor);
    }

    ImGui::SameLine();

    if (ui::UI::Button("Edit horizontal axis")) {
      axisEditorValues_ = table_.xBreakpoints();
      axisEditorBinCount_ = static_cast<int>(axisEditorValues_.size());
      editingAxis_ = AxisEditing::X;
      ImGui::OpenPopup(ui::popups::AxisEditor);
    }

    ImGui::SameLine();

    if (ui::UI::Button("Import / Export...")) {
      ImGui::OpenPopup(ui::popups::TableImportExportMenu);
    }

    ImGui::SameLine();

    if (ui::UI::Button(show3DView_ ? "Show 2D Table Grid" : "Show 3D Surface View")) {
      show3DView_ = !show3DView_;
    }

    if (ImGui::BeginPopup(ui::popups::TableImportExportMenu)) {
      if (ImGui::MenuItem("Copy Table / Selection")) {
        copyToClipboard(false);
        showCopyToast();
      }
      if (ImGui::MenuItem("Copy + Headers (Clipboard)")) {
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

    if (ImGui::BeginPopupModal(ui::popups::AxisEditor)) {
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

    if (show3DView_) {
      render3DSurfaceMesh();
    } else {
      renderBatchToolbar();
      ImGui::Separator();
      renderValueGrid();
      renderToast();
    }
  }

  nlohmann::json TableEditorPanel::saveState() const
  {
    nlohmann::json j = PlotPanel::saveState();

    nlohmann::json valuesGrid = nlohmann::json::array();
    for (size_t r = 0; r < table_.rowCount(); ++r) {
      nlohmann::json rowJson = nlohmann::json::array();
      for (size_t c = 0; c < table_.columnCount(); ++c) {
        rowJson.push_back(table_.value(r, c));
      }
      valuesGrid.push_back(rowJson);
    }

    j["xBreakpoints"] = table_.xBreakpoints();
    j["yBreakpoints"] = table_.yBreakpoints();
    j["values"] = valuesGrid;

    return j;
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