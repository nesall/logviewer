#include "ui/veanalysispanel.h"
#include "ui/ui_helpers.h"
#include "3rdparty/nlohmann/json.hpp"
#include "3rdparty/IconsFontAwesome7.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include "imgui.h"
#include "imgui_internal.h"


namespace ui {

  VeAnalysisPanel::VeAnalysisPanel(std::string title)
    : PlotPanel(std::move(title))
    , targetAfrPanel_("Target AFR Editor", "TargetAfrTable", "Target AFR",
      core::Table2D(core::generateEvenBreakpoints(500, 7000, 16),
        core::generateEvenBreakpoints(20, 100, 16)))
    , baselineVePanel_("Baseline VE Editor", "VeTable", "Baseline VE",
      core::Table2D(core::generateEvenBreakpoints(500, 7000, 16),
        core::generateEvenBreakpoints(20, 100, 16)))
    , suggestedVePanel_("Suggested VE", "SuggestedVeTable", "Suggested VE",
      core::Table2D(core::generateEvenBreakpoints(500, 7000, 16),
        core::generateEvenBreakpoints(20, 100, 16)))
  {
    observedAfrTable_ = core::Table2D(core::generateEvenBreakpoints(500, 7000, 16), core::generateEvenBreakpoints(20, 100, 16));

    auto recalcCallback = [this]() {
      computeObservedAfr();
      computeAfrDelta();
      computeSuggestedVe();
      };
    targetAfrPanel_.setOnDataChangedCallback(recalcCallback);
    baselineVePanel_.setOnDataChangedCallback(recalcCallback);
    targetAfrPanel_.setCustomToolbar2Callback([this, recalcCallback] {
      ImGui::BeginDisabled(!hasBaselineVe() || session_ == nullptr);
      if (ui::UI::ButtonPrimary(ICON_FA_CHECK " Apply Changes", {}, "Recomputes AFR Delta and Suggested VE.")) {
        recalcCallback();
      }
      ImGui::EndDisabled();
      });
    baselineVePanel_.setCustomToolbar2Callback([this, recalcCallback] {
      ImGui::BeginDisabled(!hasTargetAfr() || session_ == nullptr);
      if (ui::UI::ButtonPrimary(ICON_FA_CHECK " Apply Changes", {}, "Recomputes AFR Delta and Suggested VE.")) {
        recalcCallback();
      }
      ImGui::EndDisabled();
      });
  }

  void VeAnalysisPanel::setSession(const core::LogSession *session)
  {
    session_ = session;
    targetAfrPanel_.setSession(session);
    baselineVePanel_.setSession(session);
    suggestedVePanel_.setSession(session);
    computeObservedAfr();
    computeAfrDelta();
    computeSuggestedVe();
  }

  void VeAnalysisPanel::computeObservedAfr()
  {
    if (!session_) {
      hasObservedData_ = false;
      return;
    }

    const core::Channel *rpmCh = session_->findChannel(session_->channelMapping().rpm);
    const core::Channel *loadCh = session_->findChannel(session_->channelMapping().load);
    const core::Channel *afrCh = session_->findChannel(session_->channelMapping().afr);

    if (!rpmCh || !loadCh || !afrCh) {
      hasObservedData_ = false;
      return;
    }

    std::vector<double> xBp;
    std::vector<double> yBp;

    if (hasBaselineVe()) {
      xBp = baselineVePanel_.table().xBreakpoints();
      yBp = baselineVePanel_.table().yBreakpoints();
    } else if (hasTargetAfr()) {
      xBp = targetAfrPanel_.table().xBreakpoints();
      yBp = targetAfrPanel_.table().yBreakpoints();
    } else {
      double minRpm = 10000.0, maxRpm = 0.0;
      double minLoad = 1000.0, maxLoad = 0.0;

      for (size_t i = 0; i < session_->rowCount(); ++i) {
        double r = rpmCh->values()[i];
        double l = loadCh->values()[i];
        if (!std::isnan(r) && r > maxRpm) maxRpm = r;
        if (!std::isnan(r) && r < minRpm) minRpm = r;
        if (!std::isnan(l) && l > maxLoad) maxLoad = l;
        if (!std::isnan(l) && l < minLoad) minLoad = l;
      }

      if (maxRpm <= minRpm || maxLoad <= minLoad) {
        hasObservedData_ = false;
        return;
      }

      xBp = core::generateEvenBreakpoints(minRpm, maxRpm, 16);
      yBp = core::generateEvenBreakpoints(minLoad, maxLoad, 16);
    }

    observedAfrTable_.setXBreakpoints(xBp);
    observedAfrTable_.setYBreakpoints(yBp);

    const size_t rows = yBp.size();
    const size_t cols = xBp.size();
    if (rows == 0 || cols == 0) return;

    std::vector<std::vector<double>> sumAfr(rows, std::vector<double>(cols, 0.0));
    std::vector<std::vector<size_t>> countAfr(rows, std::vector<size_t>(cols, 0));

    const size_t numSamples = session_->rowCount();
    engine::VeTransientFilter filter(*session_, config_);

    for (size_t i = 0; i < numSamples; ++i) {
      
      if (!session_->isRowInCropRange(i)) continue; // Respect active crop range

      double rpm = rpmCh->values()[i];
      double load = loadCh->values()[i];
      double afr = afrCh->values()[i];

      if (std::isnan(rpm) || std::isnan(load) || std::isnan(afr)) continue;

      if (filter.shouldIgnoreSample(i, load)) continue;

      size_t bestR = 0, bestC = 0;
      double minDist = std::numeric_limits<double>::max();

      for (size_t r = 0; r < rows; ++r) {
        for (size_t c = 0; c < cols; ++c) {
          double dRpm = (rpm - xBp[c]) / 1000.0;
          double dLoad = (load - yBp[r]) / 10.0;
          double dist = (dRpm * dRpm) + (dLoad * dLoad);

          if (dist < minDist) {
            minDist = dist;
            bestR = r;
            bestC = c;
          }
        }
      }

      sumAfr[bestR][bestC] += afr;
      countAfr[bestR][bestC]++;
    }

    minObservedRow_ = rows;
    maxObservedRow_ = 0;
    minObservedCol_ = cols;
    maxObservedCol_ = 0;
    hasObservedData_ = false;

    for (size_t r = 0; r < rows; ++r) {
      for (size_t c = 0; c < cols; ++c) {
        if (countAfr[r][c] >= config_.minSamplesPerBin) {
          double avgAfr = sumAfr[r][c] / static_cast<double>(countAfr[r][c]);
          observedAfrTable_.setValue(r, c, avgAfr);

          if (r < minObservedRow_) minObservedRow_ = r;
          if (r > maxObservedRow_) maxObservedRow_ = r;
          if (c < minObservedCol_) minObservedCol_ = c;
          if (c > maxObservedCol_) maxObservedCol_ = c;
          hasObservedData_ = true;
        } else {
          observedAfrTable_.setValue(r, c, 0.0);
        }
      }
    }
  }

  void VeAnalysisPanel::computeSuggestedVe()
  {
    if (!session_ || !hasTargetAfr() || !hasBaselineVe()) return;

    auto customHeatmap = [this](double value, size_t row, size_t col) -> ImU32 {
      auto getVeChangeHeatMapColor = [](double diff) -> ImU32 {
        float norm = static_cast<float>(diff / 10.0); // Scale across +/- 10 VE points
        if (norm > 1.0f) norm = 1.0f;
        if (norm < -1.0f) norm = -1.0f;

        float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.35f;

        if (norm > 0.0f) {
          // Adding fuel (Engine was lean) -> Red/Orange
          r = norm;
          g = 0.3f * norm;
          b = 0.1f * (1.0f - norm);
        } else {
          // Pulling fuel (Engine was rich) -> Green/Cyan
          float negNorm = -norm;
          r = 0.1f * (1.0f - negNorm);
          g = negNorm;
          b = 0.8f * negNorm;
        }
        return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a));
        };
      double sugVe = suggestedVePanel_.table().value(row, col);
      double curVe = baselineVePanel_.table().value(row, col);
      double diff = sugVe - curVe;
      return getVeChangeHeatMapColor(diff);
      };

    auto customTooltip = [this](double value, size_t row, size_t col) -> std::string {
      double curVe = baselineVePanel_.table().value(row, col);
      double diff = value - curVe;
      if (std::abs(diff) < 1e-5) return {};
      double percentChange = (curVe != 0.0) ? (diff / curVe) * 100.0 : 0.0;
      char buffer[64];
      snprintf(buffer, sizeof(buffer), "Baseline: %.2f\nChange: %+0.2f (%+0.2f%%)\nObs AFR: %.2f", curVe, diff, percentChange, observedAfrTable_.value(row, col));
      return std::string(buffer);
      };

    auto customTextColor = [this](double value, size_t row, size_t col) -> std::optional<ImU32> {
      double curVe = baselineVePanel_.table().value(row, col);
      double diff = value - curVe;
      bool selected = suggestedVePanel_.isCellSelected(row, col);
      if (std::abs(diff) < 1e-5) {
        return ImGui::ColorConvertFloat4ToU32(
          selected ? ImVec4{ 0.9f, 0.9f, 0.9f, 1.0f } : ImVec4{ 0.6f, 0.6f, 0.6f, 1.0f }
        );
      }
      return ImGui::ColorConvertFloat4ToU32(
        selected ? ImVec4{ 0.5f, 0.95f, 1.0f, 1.0f } : ImVec4{ 0.2f, 0.8f, 1.0f, 1.0f }
      );
      };

    auto customToolbar1 = [this]() {
      bool changed = false;
      ImGui::SameLine();

      ImGui::BeginDisabled(!hasTargetAfr() || !hasBaselineVe() || session_ == nullptr);
      if (ui::UI::ButtonDanger("Reset to Calculated VE", {}, "Recomputes Suggested VE from log data, discarding any manual edits.")) {
        computeSuggestedVe();
      }
      ImGui::EndDisabled();

      ImGui::SameLine();
      ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
      ImGui::SameLine();

      ImGui::SetNextItemWidth(70.0f);
      ImGui::InputInt("Passes##smooth_passes", &smoothIterations_);
      smoothIterations_ = std::clamp(smoothIterations_, 1, 10);

      ImGui::SameLine();
      ImGui::BeginDisabled(!hasTargetAfr() || !hasBaselineVe() || session_ == nullptr);
      if (ui::UI::ButtonPrimary(ICON_FA_WAND_MAGIC_SPARKLES " Smart Smooth", {}, "Smooths low-confidence cells while anchoring high-accuracy bins.")) {
        if (session_)
          for (int i = 0; i < smoothIterations_; ++i) {
            auto currentVe = suggestedVePanel_.table();
            auto smoothed = engine::VeAnalyzer::computeSmartSmoothedVe(*session_, currentVe, afrDeltaTable_, config_, suggestedVePanel_.selectedCells());
            suggestedVePanel_.pushUndoState();
            suggestedVePanel_.setTable(smoothed);
          }
      }
      ImGui::EndDisabled();
      };

    auto customToolbar2 = [this]() {
      bool changed = false;

      // Inline Controls for Correction Limits
      ImGui::SetNextItemWidth(110.0f);
      float gain = static_cast<float>(config_.adjustmentGain);
      if (ImGui::SliderFloat("Gain (α)##sug_gain", &gain, 0.1f, 1.0f, "%.2f")) {
        config_.adjustmentGain = static_cast<double>(gain);
        changed = true;
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Correction Gain (0.50 = applies 50% of calculated delta per pass).");
      }

      ImGui::SameLine();
      ImGui::SetNextItemWidth(90.0f);
      float maxChange = static_cast<float>(config_.maxPercentChange * 100.0);
      if (ImGui::InputFloat("Max Δ (%)##sug_max_change", &maxChange, 1.0f, 5.0f, "%.1f %%")) {
        maxChange = std::clamp(maxChange, 1.0f, 50.0f);
        config_.maxPercentChange = static_cast<double>(maxChange / 100.0);
        changed = true;
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Hard cap on maximum allowed VE percentage shift per pass.");
      }

      if (changed) {
        computeSuggestedVe();
      }

      ImGui::SameLine();
      ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
      ImGui::SameLine();

      if (ui::UI::ButtonSecondary("Select Unvisited Cells", {}, "Highlights cells that lacked enough log samples for direct AFR analysis.")) {
        selectUnvisitedCellsOnSuggestedVe();
      }
      };

    core::Table2D result = engine::VeAnalyzer::computeCorrectedVe(*session_, baselineVePanel_.table(), targetAfrPanel_.table(), config_);
    suggestedVePanel_.setCustomHeatmapColoring(customHeatmap);
    suggestedVePanel_.setCustomHoverTooltip(customTooltip);
    suggestedVePanel_.setCustomTextColoring(customTextColor);
    suggestedVePanel_.setCustomToolbar1Callback(customToolbar1);
    suggestedVePanel_.setCustomToolbar2Callback(customToolbar2);
    suggestedVePanel_.setTable(result);
    hasSuggestedVe_ = true;
  }

  void VeAnalysisPanel::selectUnvisitedCellsOnSuggestedVe()
  {
    if (!hasSuggestedVe_ || !hasBaselineVe() || !session_) return;

    const core::Table2D &veTable = baselineVePanel_.table();
    const size_t rows = veTable.rowCount();
    const size_t cols = veTable.columnCount();
    if (rows == 0 || cols == 0) return;

    const core::Channel *rpmCh = session_->findChannel(session_->channelMapping().rpm);
    const core::Channel *loadCh = session_->findChannel(session_->channelMapping().load);
    const core::Channel *afrCh = session_->findChannel(session_->channelMapping().afr);

    std::vector<std::vector<size_t>> sampleCounts(rows, std::vector<size_t>(cols, 0));

    if (rpmCh && loadCh && afrCh) {
      const auto &xBp = veTable.xBreakpoints();
      const auto &yBp = veTable.yBreakpoints();
      const size_t numSamples = session_->rowCount();
      engine::VeTransientFilter filter(*session_, config_);

      for (size_t i = 0; i < numSamples; ++i) {
        if (!session_->isRowInCropRange(i)) continue;

        double rpm = rpmCh->values()[i];
        double load = loadCh->values()[i];
        double afr = afrCh->values()[i];

        if (std::isnan(rpm) || std::isnan(load) || std::isnan(afr)) continue;
        if (filter.shouldIgnoreSample(i, load)) continue;

        size_t bestR = 0, bestC = 0;
        double minDist = std::numeric_limits<double>::max();

        for (size_t r = 0; r < rows; ++r) {
          for (size_t c = 0; c < cols; ++c) {
            double dRpm = (rpm - xBp[c]) / 1000.0;
            double dLoad = (load - yBp[r]) / 10.0;
            double dist = (dRpm * dRpm) + (dLoad * dLoad);

            if (dist < minDist) {
              minDist = dist;
              bestR = r;
              bestC = c;
            }
          }
        }
        sampleCounts[bestR][bestC]++;
      }
    }

    std::set<std::pair<int, int>> unvisited;
    for (size_t r = 0; r < rows; ++r) {
      for (size_t c = 0; c < cols; ++c) {
        if (sampleCounts[r][c] < config_.minSamplesPerBin) {
          unvisited.insert({ static_cast<int>(r), static_cast<int>(c) });
        }
      }
    }
    suggestedVePanel_.setSelection(unvisited);
  }

  void VeAnalysisPanel::renderObservedAfrTab()
  {
    if (session_ == nullptr) {
      ImGui::TextDisabled("No log file loaded.");
      return;
    }

    // --- Filter Configuration Collapsible Header ---
    if (ImGui::CollapsingHeader("Analysis & Transient Filters")) {
      ImGui::PushItemWidth(120.0f);

      bool changed = false;

      if (ImGui::BeginTabBar("ObservedAfrSettingsTabBar")) {

        if (ImGui::BeginTabItem("Filter Thresholds")) {
          ImGui::Spacing();
          ImGui::PushItemWidth(120.0f);

          changed |= ImGui::Checkbox("Filter Acceleration Transients (TPSdot)", &config_.enableTpsDotFilter);
          if (config_.enableTpsDotFilter) {
            ImGui::SameLine();
            changed |= ImGui::InputDouble("Max |TPSdot| (%/s)##max_tpsdot", &config_.maxTpsDot, 5.0, 10.0, "%.0f");
          }

          changed |= ImGui::Checkbox("Filter Cold Warmup (CLT)", &config_.enableCltFilter);
          if (config_.enableCltFilter) {
            ImGui::SameLine();
            changed |= ImGui::InputDouble("Min Coolant Temp##min_clt", &config_.minCoolantTemp, 5.0, 10.0, "%.0f");
          }

          changed |= ImGui::Checkbox("Filter Overrun Decel", &config_.enableOverrunFilter);
          if (config_.enableOverrunFilter) {
            ImGui::SameLine();
            changed |= ImGui::InputDouble("Min Load Threshold##min_load", &config_.minLoadThreshold, 1.0, 5.0, "%.1f");
          }

          int minSamples = static_cast<int>(config_.minSamplesPerBin);
          if (ImGui::InputInt("Min Samples / Bin", &minSamples)) {
            if (minSamples < 1) minSamples = 1;
            config_.minSamplesPerBin = static_cast<size_t>(minSamples);
            changed = true;
          }

          ImGui::PopItemWidth();
          ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
      }
      if (changed) {
        computeObservedAfr();
        if (hasTargetAfr()) computeAfrDelta();
        if (hasTargetAfr() && hasBaselineVe()) computeSuggestedVe();
      }
      ImGui::Separator();
    }

    if (ui::UI::ButtonPrimary(ICON_FA_REPEAT " Recalculate Observed AFR")) {
      computeObservedAfr();
    }

    if (!hasObservedData_) {
      ImGui::TextDisabled("No valid AFR log samples binned in current axes range.");
      return;
    }

    const auto &xBp = observedAfrTable_.xBreakpoints();
    const auto &yBp = observedAfrTable_.yBreakpoints();

    const size_t visibleCols = (maxObservedCol_ >= minObservedCol_) ? (maxObservedCol_ - minObservedCol_ + 1) : 0;

    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
      ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
      ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings;

    const ImU32 headerHighlightColor = IM_COL32(230, 160, 30, 220);
    int nextHoveredRow = -1;
    int nextHoveredCol = -1;

    if (ImGui::BeginTable("ObservedAfrGridCropped", static_cast<int>(visibleCols + 1), flags, ImVec2(0.0f, 400.0f))) {
      ImGui::TableSetupScrollFreeze(1, 1);
      ImGui::TableSetupColumn("MAP \\ RPM", ImGuiTableColumnFlags_WidthFixed, 90.0f);

      for (size_t c = minObservedCol_; c <= maxObservedCol_; ++c) {
        std::string header = std::to_string(static_cast<int>(xBp[c])) + "##col" + std::to_string(c);
        ImGui::TableSetupColumn(header.c_str(), ImGuiTableColumnFlags_WidthFixed, 65.0f);
      }
      ImGui::TableHeadersRow();

      const size_t nofCols = maxObservedCol_ - minObservedCol_ + 1;
      if (hoveredCol_ >= 0 && hoveredCol_ < static_cast<int>(nofCols)) {
        ImGui::TableSetColumnIndex(hoveredCol_ + 1);
        ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, headerHighlightColor);
      }

      // Render in descending order (highest MAP/load at top) to match Target AFR orientation
      if (maxObservedRow_ >= minObservedRow_) {
        for (size_t r = maxObservedRow_; ; --r) {
          ImGui::TableNextRow();                  
          ImGui::TableSetColumnIndex(0);
          if (hoveredRow_ == static_cast<int>(r)) {
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, headerHighlightColor);
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%.1f", yBp[r]);
          } else {
            ImGui::Text("%.1f", yBp[r]);
          }

          for (size_t c = minObservedCol_; c <= maxObservedCol_; ++c) {
            ImGui::TableSetColumnIndex(static_cast<int>(c - minObservedCol_ + 1));
            double val = observedAfrTable_.value(r, c);
            //if (val > 0.0) {
            //  ImGui::Text("%.2f", val);
            //} else {
            //  ImGui::TextDisabled(" - ");
            //}
            const std::string idSuffix = "##cell_" + std::to_string(r) + "_" + std::to_string(c);
            char cellText[64];
            if (0 < val) {
              snprintf(cellText, sizeof(cellText), "%.2f%s", val, idSuffix.c_str());
            } else {
              snprintf(cellText, sizeof(cellText), " - %s", idSuffix.c_str());
            }
            ImGui::Selectable(cellText, false, 0);

            if (ImGui::IsItemHovered()) {
              nextHoveredRow = static_cast<int>(r);
              nextHoveredCol = static_cast<int>(c);
            }
          }

          if (r == minObservedRow_) break;
        }
      }
      ImGui::EndTable();
      hoveredRow_ = nextHoveredRow;
      hoveredCol_ = nextHoveredCol;
    }
  }

  void VeAnalysisPanel::computeAfrDelta()
  {
    if (!session_ || !hasTargetAfr()) {
      hasAfrDelta_ = false;
      return;
    }

    const core::Channel *rpmCh = session_->findChannel(session_->channelMapping().rpm);
    const core::Channel *loadCh = session_->findChannel(session_->channelMapping().load);
    const core::Channel *afrCh = session_->findChannel(session_->channelMapping().afr);

    if (!rpmCh || !loadCh || !afrCh) {
      hasAfrDelta_ = false;
      return;
    }

    const bool useVeAxes = alignAfrDeltaToVeTable_ && hasBaselineVe();
    const core::Table2D &refTable = useVeAxes ? baselineVePanel_.table() : targetAfrPanel_.table();

    const auto &xBp = refTable.xBreakpoints();
    const auto &yBp = refTable.yBreakpoints();
    afrDeltaTable_.setXBreakpoints(xBp);
    afrDeltaTable_.setYBreakpoints(yBp);

    const size_t rows = yBp.size();
    const size_t cols = xBp.size();

    if (rows == 0 || cols == 0) return;

    std::vector<std::vector<double>> sumAfr(rows, std::vector<double>(cols, 0.0));

    // Resize sample count matrix to match grid dimensions
    afrDeltaSampleCounts_.assign(rows, std::vector<size_t>(cols, 0));

    const size_t numSamples = session_->rowCount();
    engine::VeTransientFilter filter(*session_, config_);

    for (size_t i = 0; i < numSamples; ++i) {
      double rpm = rpmCh->values()[i];
      double load = loadCh->values()[i];
      double afr = afrCh->values()[i];

      if (std::isnan(rpm) || std::isnan(load) || std::isnan(afr)) continue;
      if (filter.shouldIgnoreSample(i, load)) continue;

      size_t bestR = 0, bestC = 0;
      double minDist = std::numeric_limits<double>::max();

      for (size_t r = 0; r < rows; ++r) {
        for (size_t c = 0; c < cols; ++c) {
          double dRpm = (rpm - xBp[c]) / 1000.0;
          double dLoad = (load - yBp[r]) / 10.0;
          double dist = (dRpm * dRpm) + (dLoad * dLoad);

          if (dist < minDist) {
            minDist = dist;
            bestR = r;
            bestC = c;
          }
        }
      }

      sumAfr[bestR][bestC] += afr;
      afrDeltaSampleCounts_[bestR][bestC]++;
    }

    for (size_t r = 0; r < rows; ++r) {
      for (size_t c = 0; c < cols; ++c) {
        size_t count = afrDeltaSampleCounts_[r][c];
        if (count >= config_.minSamplesPerBin) {
          double obsAfr = sumAfr[r][c] / static_cast<double>(count);
          double tgtAfr = useVeAxes ? targetAfrPanel_.table().sample(xBp[c], yBp[r]) : targetAfrPanel_.table().value(r, c);
          if (tgtAfr > 0.0) {
            afrDeltaTable_.setValue(r, c, obsAfr - tgtAfr);
          } else {
            afrDeltaTable_.setValue(r, c, std::numeric_limits<double>::quiet_NaN());
          }
        } else {
          afrDeltaTable_.setValue(r, c, std::numeric_limits<double>::quiet_NaN());
        }
      }
    }

    hasAfrDelta_ = true;
  }

  void VeAnalysisPanel::renderAfrDeltaTab()
  {
    if (!hasTargetAfr()) {
      ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "Please set the 'Target AFR' table first.");
      return;
    }

    if (ui::UI::ButtonPrimary(ICON_FA_REPEAT " Recalculate AFR Delta")) {
      computeAfrDelta();
    }

    ImGui::SameLine();

    ImGui::BeginDisabled(!hasBaselineVe());
    if (!hasBaselineVe()) alignAfrDeltaToVeTable_ = false;
    if (ImGui::Checkbox("Align grid to Baseline VE Table", &alignAfrDeltaToVeTable_)) {
      computeAfrDelta();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::Checkbox("Scale intensity by sample count", &scaleDeltaIntensityByHitCount_);

    if (!hasBaselineVe()) {
      ImGui::TextDisabled("(Target AFR axes used - load Baseline VE to enable VE alignment)");
    }

    if (hasAfrDelta_) {
      const auto &xBp = afrDeltaTable_.xBreakpoints();
      const auto &yBp = afrDeltaTable_.yBreakpoints();
      const size_t cols = xBp.size();
      const size_t rows = yBp.size();

      if (cols == 0 || rows == 0) return;

      const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings;

      // Heat map generator with optional sample-density alpha scaling
      auto getDeltaHeatMapColor = [this](double delta, size_t sampleCount) -> ImU32 {
        float norm = static_cast<float>(delta / 2.0); // Scale across +/- 2 AFR units
        if (norm > 1.0f) norm = 1.0f;
        if (norm < -1.0f) norm = -1.0f;

        float baseAlpha = 0.35f;

        if (scaleDeltaIntensityByHitCount_) {
          // Scale alpha from 0.10 (at min samples) up to 0.60 (at 50+ samples)
          size_t minThreshold = config_.minSamplesPerBin;
          size_t maxThreshold = minThreshold + 40; // Full intensity saturates after 40 additional samples
          float countFactor = 0.0f;
          if (sampleCount > minThreshold) {
            countFactor = static_cast<float>(sampleCount - minThreshold) / static_cast<float>(maxThreshold - minThreshold);
            if (countFactor > 1.0f) countFactor = 1.0f;
          }
          baseAlpha = 0.10f + (0.50f * countFactor);
        }

        float r = 0.0f, g = 0.0f, b = 0.0f;
        if (norm > 0.0f) {
          r = norm;
          g = 0.1f * (1.0f - norm);
          b = 0.1f * (1.0f - norm);
        } else {
          float negNorm = -norm;
          r = 0.1f * (1.0f - negNorm);
          g = 0.1f * (1.0f - negNorm);
          b = negNorm;
        }
        return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, baseAlpha));
        };

      const ImU32 headerHighlightColor = IM_COL32(230, 160, 30, 220);
      int nextHoveredRow = -1;
      int nextHoveredCol = -1;

      if (ImGui::BeginTable("AfrDeltaGrid", static_cast<int>(cols + 1), flags, ImVec2(0.0f, 380.0f))) {
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn("MAP \\ RPM", ImGuiTableColumnFlags_WidthFixed, 90.0f);

        for (size_t c = 0; c < cols; ++c) {
          std::string header = std::to_string(static_cast<int>(xBp[c])) + "##col" + std::to_string(c);
          ImGui::TableSetupColumn(header.c_str(), ImGuiTableColumnFlags_WidthFixed, 65.0f);
        }
        ImGui::TableHeadersRow();

        if (hoveredCol_ >= 0 && hoveredCol_ < static_cast<int>(cols)) {
          ImGui::TableSetColumnIndex(hoveredCol_ + 1);
          ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, headerHighlightColor);
        }

        for (size_t r = rows; r-- > 0; ) {
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);

          ImGui::TableSetColumnIndex(0);
          if (hoveredRow_ == static_cast<int>(r)) {
            ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, headerHighlightColor);
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 1.0f, 1.0f), "%.1f", yBp[r]);
          } else {
            ImGui::Text("%.1f", yBp[r]);
          }

          for (size_t c = 0; c < cols; ++c) {
            ImGui::TableSetColumnIndex(static_cast<int>(c + 1));

            double delta = afrDeltaTable_.value(r, c);
            size_t hitCount = (r < afrDeltaSampleCounts_.size() && c < afrDeltaSampleCounts_[r].size())
              ? afrDeltaSampleCounts_[r][c] : 0;

            if (!std::isnan(delta)) {
              ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, getDeltaHeatMapColor(delta, hitCount));

              // 1. Build the unique ID suffix (hidden)
              const std::string idSuffix = "##cell_" + std::to_string(r) + "_" + std::to_string(c);

              char cellText[64];
              std::optional<ImVec4> textColor;

              // 2. Format: [Visible Text]##[Unique ID]
              if (delta > 0.3) {
                std::snprintf(cellText, sizeof(cellText), "+%.2f%s", delta, idSuffix.c_str());
                textColor = ImVec4(1.0f, 0.4f, 0.4f, 1.0f); // Lean
              } else if (delta < -0.3) {
                std::snprintf(cellText, sizeof(cellText), "%.2f%s", delta, idSuffix.c_str());
                textColor = ImVec4(0.4f, 0.7f, 1.0f, 1.0f); // Rich
              } else {
                std::snprintf(cellText, sizeof(cellText), "%.2f%s", delta, idSuffix.c_str());
              }

              // 3. Render Selectable filling the table cell
              if (textColor.has_value()) {
                ImGui::PushStyleColor(ImGuiCol_Text, textColor.value());
              }

              ImGui::Selectable(cellText, false, 0);

              if (textColor.has_value()) {
                ImGui::PopStyleColor();
              }

              // Hit count tooltip on cell hover
              if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Samples: %zu\nDelta: %+.2f AFR", hitCount, delta);
              }
            } else {
              ImGui::TextDisabled(" - ");
              if (ImGui::IsItemHovered() && hitCount > 0) {
                ImGui::SetTooltip("Samples: %zu (Below min threshold %zu)", hitCount, config_.minSamplesPerBin);
              }
            }

            if (ImGui::IsItemHovered()) {
              nextHoveredRow = static_cast<int>(r);
              nextHoveredCol = static_cast<int>(c);
            }
          }
        }
        ImGui::EndTable();
        hoveredRow_ = nextHoveredRow;
        hoveredCol_ = nextHoveredCol;
      }
    } else {
      ImGui::TextDisabled("No AFR Delta available. Ensure a log is loaded and Target AFR is set.");
    }
  }

  bool VeAnalysisPanel::hasTargetAfr() const
  {
    const auto &tbl = targetAfrPanel_.table();
    if (tbl.rowCount() == 0 || tbl.columnCount() == 0) return false;
    for (size_t r = 0; r < tbl.rowCount(); ++r) {
      for (size_t c = 0; c < tbl.columnCount(); ++c) {
        if (0 < tbl.value(r, c)) return true;
      }
    }
    return false;
  }

  void VeAnalysisPanel::onRegimesUpdated()
  {
    targetAfrPanel_.recomputeRegimeCoverage();
    baselineVePanel_.recomputeRegimeCoverage();
    suggestedVePanel_.recomputeRegimeCoverage();
  }

  bool VeAnalysisPanel::hasBaselineVe() const
  {
    const auto &tbl = baselineVePanel_.table();
    if (tbl.rowCount() == 0 || tbl.columnCount() == 0) return false;
    for (size_t r = 0; r < tbl.rowCount(); ++r) {
      for (size_t c = 0; c < tbl.columnCount(); ++c) {
        if (0 < tbl.value(r, c)) return true;
      }
    }
    return false;
  }

  void VeAnalysisPanel::renderTargetAfrTab()
  {
    PlotCursor dummyCursor;
    targetAfrPanel_.render(dummyCursor);
  }

  void VeAnalysisPanel::renderBaselineVeTab()
  {
    PlotCursor dummyCursor;
    baselineVePanel_.render(dummyCursor);
  }

  void VeAnalysisPanel::renderSuggestedVeTab()
  {
    if (!hasSuggestedVe_) {
      ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "Please set the 'Baseline VE' table first.");
      return;
    }
    PlotCursor dummyCursor;
    suggestedVePanel_.render(dummyCursor);
  }

  void VeAnalysisPanel::render(PlotCursor &cursor)
  {
    std::string windowLabel = "VE Analyzer###" + title();
    ImGui::Begin(windowLabel.c_str(), &open_);

    if (ImGui::BeginTabBar("VeAnalyzerTabBar")) {

      ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.50f, 0.20f, 0.70f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(0.65f, 0.30f, 0.85f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_TabActive, ImVec4(0.58f, 0.25f, 0.78f, 1.0f));
      if (ImGui::BeginTabItem("Observed AFR")) {
        ImGui::PopStyleColor(3);
        renderObservedAfrTab();
        ImGui::EndTabItem();
      } else {
        ImGui::PopStyleColor(3);
      }

      if (ImGui::BeginTabItem("Target AFR")) {
        renderTargetAfrTab();
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Baseline VE")) {
        renderBaselineVeTab();
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("AFR Delta")) {
        //computeAfrDelta();
        renderAfrDeltaTab();
        ImGui::EndTabItem();
      }

      ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.15f, 0.45f, 0.25f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(0.25f, 0.65f, 0.35f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_TabActive, ImVec4(0.20f, 0.55f, 0.30f, 1.0f));
      if (ImGui::BeginTabItem("Suggested VE")) {
        renderSuggestedVeTab();
        ImGui::EndTabItem();
      }
      ImGui::PopStyleColor(3);

      ImGui::EndTabBar();
    }

    ImGui::End();
  }

  nlohmann::json VeAnalysisPanel::saveState() const
  {
    auto j = PlotPanel::saveState();

    j["alignAfrDeltaToVeTable"] = alignAfrDeltaToVeTable_;

    j["config"] = {
      {"minSamplesPerBin", config_.minSamplesPerBin},
      {"adjustmentGain", config_.adjustmentGain},
      {"maxPercentChange", config_.maxPercentChange},
      {"enableTpsDotFilter", config_.enableTpsDotFilter},
      {"maxTpsDot", config_.maxTpsDot},
      {"enableCltFilter", config_.enableCltFilter},
      {"minCoolantTemp", config_.minCoolantTemp},
      {"enableOverrunFilter", config_.enableOverrunFilter},
      {"minLoadThreshold", config_.minLoadThreshold}
    };

    j["targetAfr"] = targetAfrPanel_.saveState();
    j["baselineVe"] = baselineVePanel_.saveState();

    return j;
  }

  void VeAnalysisPanel::loadState(const nlohmann::json &state)
  {
    PlotPanel::loadState(state);

    if (state.contains("alignAfrDeltaToVeTable") && state["alignAfrDeltaToVeTable"].is_boolean()) {
      alignAfrDeltaToVeTable_ = state["alignAfrDeltaToVeTable"].get<bool>();
    }

    if (state.contains("config") && state["config"].is_object()) {
      const auto &cfg = state["config"];
      if (cfg.contains("minSamplesPerBin")) config_.minSamplesPerBin = cfg["minSamplesPerBin"].get<size_t>();
      if (cfg.contains("adjustmentGain")) config_.adjustmentGain = cfg["adjustmentGain"].get<double>();
      if (cfg.contains("maxPercentChange")) config_.maxPercentChange = cfg["maxPercentChange"].get<double>();
      if (cfg.contains("enableTpsDotFilter")) config_.enableTpsDotFilter = cfg["enableTpsDotFilter"].get<bool>();
      if (cfg.contains("maxTpsDot")) config_.maxTpsDot = cfg["maxTpsDot"].get<double>();
      if (cfg.contains("enableCltFilter")) config_.enableCltFilter = cfg["enableCltFilter"].get<bool>();
      if (cfg.contains("minCoolantTemp")) config_.minCoolantTemp = cfg["minCoolantTemp"].get<double>();
      if (cfg.contains("enableOverrunFilter")) config_.enableOverrunFilter = cfg["enableOverrunFilter"].get<bool>();
      if (cfg.contains("minLoadThreshold")) config_.minLoadThreshold = cfg["minLoadThreshold"].get<double>();
    }

    if (state.contains("targetAfr")) {
      targetAfrPanel_.loadState(state["targetAfr"]);
    }
    if (state.contains("baselineVe")) {
      baselineVePanel_.loadState(state["baselineVe"]);
    }

    //if (session_ != nullptr) {
    //  computeObservedAfr();
    //  computeAfrDelta();
    //  computeSuggestedVe();
    //}
  }

} // namespace ui