#include "ui/tableoverlaypanel.h"
#include "ui/ui_helpers.h"
#include "3rdparty/nlohmann/json.hpp"
#include "3rdparty/IconsFontAwesome7.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <cstdio>
#include "imgui.h"
#include "implot.h"

namespace ui {

  TableOverlayPanel::TableOverlayPanel(std::string title)
    : PlotPanel(std::move(title))
    , innerTable_("##InnerTable", "TableOverlayInner", "Overlay Table",
      core::Table2D(core::generateEvenBreakpoints(500, 7000, 16),
        core::generateEvenBreakpoints(20, 100, 16)))
  {
    applyCustomColoring();

    innerTable_.setOnDataChangedCallback([this]() {
      computeOverlayMatrix();
      });
    innerTable_.setCustomToolbar2Callback([this]() {
      ImGui::SameLine();
      if (ImGui::Checkbox("Filter Overlay", &enableOverlayFilter_)) {
        computeOverlayMatrix();
      }
      if (enableOverlayFilter_) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::DragFloatRange2("##OverlayRange", &overlayFilterMin_, &overlayFilterMax_, 1.0f,
          static_cast<float>(overlayMin_),
          static_cast<float>(overlayMax_),
          "Min: %.0f", "Max: %.0f")) {
          computeOverlayMatrix();
        }
      }
      });
    //innerTable_.setBatchToolbarVisible(false);
  }

  void TableOverlayPanel::setSession(const core::LogSession *session)
  {
    session_ = session;
    rebindChannels();
    computeOverlayMatrix();
  }

  void TableOverlayPanel::rebindChannels()
  {
    if (!session_) return;

    // Fallbacks if specified channels aren't present in current log
    if (!session_->findChannel(xAxisChannel_)) {
      if (session_->findChannel("RPM")) xAxisChannel_ = "RPM";
    }
    if (!session_->findChannel(yAxisChannel_)) {
      if (session_->findChannel("MAP")) yAxisChannel_ = "MAP";
      else if (session_->findChannel("Fuel - Load (MAP)")) yAxisChannel_ = "Fuel - Load (MAP)";
    }
    if (!session_->findChannel(overlayChannel_)) {
      if (session_->findChannel("EGT")) overlayChannel_ = "EGT";
      else if (session_->findChannel("EGT1")) overlayChannel_ = "EGT1";
    }
  }

  void TableOverlayPanel::computeOverlayMatrix()
  {
    const size_t rows = innerTable_.table().rowCount();
    const size_t cols = innerTable_.table().columnCount();

    binnedOverlayData_.assign(rows, std::vector<double>(cols, 0.0));
    binnedSampleCounts_.assign(rows, std::vector<size_t>(cols, 0));

    hasOverlayData_ = false;
    overlayMin_ = std::numeric_limits<double>::max();
    overlayMax_ = -std::numeric_limits<double>::max();

    if (!session_ || rows == 0 || cols == 0) return;

    const core::Channel *xCh = session_->findChannel(xAxisChannel_);
    const core::Channel *yCh = session_->findChannel(yAxisChannel_);
    const core::Channel *ovCh = session_->findChannel(overlayChannel_);

    if (!xCh || !yCh || !ovCh) return;

    const auto &xBp = innerTable_.table().xBreakpoints();
    const auto &yBp = innerTable_.table().yBreakpoints();
    const size_t numSamples = session_->rowCount();

    std::vector<std::vector<double>> sumVal(rows, std::vector<double>(cols, 0.0));
    std::vector<std::vector<double>> minVal(rows, std::vector<double>(cols, std::numeric_limits<double>::max()));
    std::vector<std::vector<double>> maxVal(rows, std::vector<double>(cols, -std::numeric_limits<double>::max()));

    for (size_t i = 0; i < numSamples; ++i) {
      if (!session_->isRowInCropRange(i)) continue;

      double x = xCh->values()[i];
      double y = yCh->values()[i];
      double z = ovCh->values()[i];

      if (std::isnan(x) || std::isnan(y) || std::isnan(z)) continue;

      // Find nearest grid bin (Euclidean distance on normalized space)
      size_t bestR = 0, bestC = 0;
      double minDist = std::numeric_limits<double>::max();

      for (size_t r = 0; r < rows; ++r) {
        for (size_t c = 0; c < cols; ++c) {
          double dX = (x - xBp[c]) / 1000.0;
          double dY = (y - yBp[r]) / 10.0;
          double dist = (dX * dX) + (dY * dY);

          if (dist < minDist) {
            minDist = dist;
            bestR = r;
            bestC = c;
          }
        }
      }

      sumVal[bestR][bestC] += z;
      if (z < minVal[bestR][bestC]) minVal[bestR][bestC] = z;
      if (z > maxVal[bestR][bestC]) maxVal[bestR][bestC] = z;
      binnedSampleCounts_[bestR][bestC]++;
    }

    for (size_t r = 0; r < rows; ++r) {
      for (size_t c = 0; c < cols; ++c) {
        size_t count = binnedSampleCounts_[r][c];
        if (count == 0) continue;

        hasOverlayData_ = true;
        double cellRes = 0.0;

        switch (aggregation_) {
        case OverlayAggregation::Max:
          cellRes = maxVal[r][c];
          break;
        case OverlayAggregation::Min:
          cellRes = minVal[r][c];
          break;
        case OverlayAggregation::Average:
          cellRes = sumVal[r][c] / static_cast<double>(count);
          break;
        case OverlayAggregation::Count:
          cellRes = static_cast<double>(count);
          break;
        }

        binnedOverlayData_[r][c] = cellRes;

        if (cellRes < overlayMin_) overlayMin_ = cellRes;
        if (cellRes > overlayMax_) overlayMax_ = cellRes;
      }
    }

    if (overlayMax_ <= overlayMin_) overlayMax_ = overlayMin_ + 1.0;
    if (enableOverlayFilter_ && !overlayFilterMinMaxSet_) {
      overlayFilterMin_ = static_cast<float>(overlayMin_);
      overlayFilterMax_ = static_cast<float>(overlayMax_);
      overlayFilterMinMaxSet_ = true;
    }
  }

  void TableOverlayPanel::applyCustomColoring()
  {
    // Custom cell background color derived from binned overlay data
    innerTable_.setCustomHeatmapColoring([this](double /*val*/, size_t row, size_t col) -> ImU32 {
      if (!hasOverlayData_ || row >= binnedSampleCounts_.size() || col >= binnedSampleCounts_[row].size()) {
        return IM_COL32(30, 30, 35, 255);
      }

      // Unvisited Cell Polish: Keep unvisited cells dark gray
      if (binnedSampleCounts_[row][col] == 0) {
        return IM_COL32(20, 22, 28, 255);
      }

      double overlayVal = binnedOverlayData_[row][col];

      // Range Filter Check: If value is outside [Min, Max], keep background dark
      if (enableOverlayFilter_) {
        if (overlayVal < overlayFilterMin_ || overlayVal > overlayFilterMax_) {
          return IM_COL32(25, 28, 35, 255);
        }
      }

      // Normalize t against overall overlay range
      float t = static_cast<float>((overlayVal - overlayMin_) / (overlayMax_ - overlayMin_));
      t = std::clamp(t, 0.0f, 1.0f);

      // Viridis Background with 0.45 Alpha (so text is sharp)
      ImVec4 color = ImPlot::SampleColormap(t, ImPlotColormap_Viridis);
      color.w = 0.45f;

      return ImGui::ColorConvertFloat4ToU32(color);
      });

    // Custom cell hover tooltip displaying overlay metrics
    innerTable_.setCustomHoverTooltip([this](double cellVal, size_t row, size_t col) -> std::string {
      if (row >= binnedSampleCounts_.size() || col >= binnedSampleCounts_[row].size()) return {};
      size_t hits = binnedSampleCounts_[row][col];
      if (hits == 0) return "Unvisited Cell";

      char buf[128];
      snprintf(buf, sizeof(buf), "Base Value: %.2f\n%s (%s): %.1f\nSamples: %zu",
        cellVal, overlayChannel_.c_str(),
        aggregation_ == OverlayAggregation::Max ? "Max" :
        aggregation_ == OverlayAggregation::Average ? "Avg" :
        aggregation_ == OverlayAggregation::Min ? "Min" : "Count",
        binnedOverlayData_[row][col], hits);
      return std::string(buf);
      });

    innerTable_.setCustomTextColoring([this](double /*val*/, size_t row, size_t col) -> ImU32 {
      if (row >= binnedSampleCounts_.size() || col >= binnedSampleCounts_[row].size()) return {};
      size_t hits = binnedSampleCounts_[row][col];
      bool selected = innerTable_.isCellSelected(row, col);
      if (hits == 0) return selected ? ImGui::ColorConvertFloat4ToU32(ImVec4{ 0.8f, 0.8f, 0.8f, 1.0f }) : ImGui::ColorConvertFloat4ToU32(ImVec4{ 0.6f, 0.6f, 0.6f, 1.0f });
      return selected ? ImGui::ColorConvertFloat4ToU32(ImVec4{ 1.0f, 1.0f, 1.0f, 1.0f }) : ImGui::ColorConvertFloat4ToU32(ImVec4{ 0.2f, 0.8f, 1.0f, 1.0f });
      });
  }

  void TableOverlayPanel::render(PlotCursor &cursor)
  {
    std::string windowLabel = makeWindowLabel(ICON_FA_LAYER_GROUP);
    ImGui::Begin(windowLabel.c_str(), &open_, getAppearanceFlags());
    renderCommonOps();
    if (session_ == nullptr) {
      ImGui::TextDisabled("No log session loaded.");
      ImGui::End();
      return;
    }

    // --- TOP TOOLBAR ---
    ImGui::PushItemWidth(130.0f);

    // X Axis Channel Picker
    if (ImGui::BeginCombo("##XCh", ("X: " + xAxisChannel_).c_str())) {
      for (const auto &ch : session_->channels()) {
        if (ImGui::Selectable(ch.name().c_str(), ch.name() == xAxisChannel_)) {
          xAxisChannel_ = ch.name();
          computeOverlayMatrix();
        }
      }
      ImGui::EndCombo();
    }

    ImGui::SameLine();

    // Y Axis Channel Picker
    if (ImGui::BeginCombo("##YCh", ("Y: " + yAxisChannel_).c_str())) {
      for (const auto &ch : session_->channels()) {
        if (ImGui::Selectable(ch.name().c_str(), ch.name() == yAxisChannel_)) {
          yAxisChannel_ = ch.name();
          computeOverlayMatrix();
        }
      }
      ImGui::EndCombo();
    }

    ImGui::SameLine();
    ImGui::Text("|");
    ImGui::SameLine();

    // Overlay Channel Picker
    if (ImGui::BeginCombo("##OvCh", ("Overlay: " + overlayChannel_).c_str())) {
      for (const auto &ch : session_->channels()) {
        if (ImGui::Selectable(ch.name().c_str(), ch.name() == overlayChannel_)) {
          overlayChannel_ = ch.name();
          computeOverlayMatrix();
        }
      }
      ImGui::EndCombo();
    }

    ImGui::SameLine();

    // Overlay Aggregation Picker
    const char *aggNames[] = { "Max", "Average", "Min", "Hit Count" };
    int currentAgg = static_cast<int>(aggregation_);
    if (ImGui::Combo("##Agg", &currentAgg, aggNames, IM_ARRAYSIZE(aggNames))) {
      aggregation_ = static_cast<OverlayAggregation>(currentAgg);
      computeOverlayMatrix();
    }

    ImGui::PopItemWidth();

    ImGui::SameLine();
    if (ui::UI::ButtonPrimary(ICON_FA_REPEAT " Recalculate")) {
      computeOverlayMatrix();
    }

    ImGui::Separator();

    // Delegate rendering to embedded TableEditorPanel
    innerTable_.render(cursor);

    ImGui::End();
  }

  nlohmann::json TableOverlayPanel::saveState() const
  {
    auto j = PlotPanel::saveState();
    j["xAxisChannel"] = xAxisChannel_;
    j["yAxisChannel"] = yAxisChannel_;
    j["overlayChannel"] = overlayChannel_;
    j["aggregation"] = static_cast<int>(aggregation_);
    j["baseSource"] = static_cast<int>(baseSource_);
    j["innerTable"] = innerTable_.saveState();
    j["enableOverlayFilter"] = enableOverlayFilter_;
    j["overlayFilterMin"] = overlayFilterMin_;
    j["overlayFilterMax"] = overlayFilterMax_;
    return j;
  }

  void TableOverlayPanel::loadState(const nlohmann::json &state)
  {
    PlotPanel::loadState(state);
    if (state.contains("xAxisChannel")) xAxisChannel_ = state["xAxisChannel"].get<std::string>();
    if (state.contains("yAxisChannel")) yAxisChannel_ = state["yAxisChannel"].get<std::string>();
    if (state.contains("overlayChannel")) overlayChannel_ = state["overlayChannel"].get<std::string>();
    if (state.contains("aggregation")) aggregation_ = static_cast<OverlayAggregation>(state["aggregation"].get<int>());
    if (state.contains("baseSource")) baseSource_ = static_cast<BaseTableSource>(state["baseSource"].get<int>());
    if (state.contains("innerTable")) innerTable_.loadState(state["innerTable"]);
    if (state.contains("enableOverlayFilter")) enableOverlayFilter_ = state["enableOverlayFilter"].get<bool>();
    if (state.contains("overlayFilterMin")) overlayFilterMin_ = state["overlayFilterMin"].get<float>();
    if (state.contains("overlayFilterMax")) overlayFilterMax_ = state["overlayFilterMax"].get<float>();
  }

} // namespace ui