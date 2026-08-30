#include "ui/veanalysispanel.h"
#include "ui/ui_helpers.h"
#include "utils/utils.h"
#include "3rdparty/nlohmann/json.hpp"
#include "3rdparty/IconsFontAwesome7.h"

#include <algorithm>
#include <cmath>
#include <sstream>
#include <iomanip>
#include <limits>

#include "imgui.h"
#include "imgui_internal.h"


namespace {
  ImU32 getDeltaHeatMapColor(double delta, size_t sampleCount, size_t minSamplesPerBin, bool scaleDeltaIntensityByHitCount) {
    float norm = static_cast<float>(delta / 2.0); // Scale across +/- 2 AFR units
    if (norm > 1.0f) norm = 1.0f;
    if (norm < -1.0f) norm = -1.0f;
    float baseAlpha = 0.35f;
    if (scaleDeltaIntensityByHitCount) {
      // Scale alpha from 0.10 (at min samples) up to 0.60 (at 50+ samples)
      size_t minThreshold = minSamplesPerBin;
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
} // anonymous namespace

namespace ui {

  VeAnalysisPanel::VeAnalysisPanel(std::string title)
    : PlotPanel(std::move(title))
    , observedAfrPanel_("Observed AFR", "ObservedAfrTable", "Observed AFR", {})
    , targetAfrPanel_("Target AFR Editor", "TargetAfrTable", "Target AFR",
      core::Table2D(core::generateEvenBreakpoints(500, 7000, 16), core::generateEvenBreakpoints(20, 100, 16)))
    , baselineVePanel_("Baseline VE Editor", "VeTable", "Baseline VE",
      core::Table2D(core::generateEvenBreakpoints(500, 7000, 16), core::generateEvenBreakpoints(20, 100, 16)))
    , suggestedVePanel_("Suggested VE", "SuggestedVeTable", "Suggested VE",
      core::Table2D(core::generateEvenBreakpoints(500, 7000, 16), core::generateEvenBreakpoints(20, 100, 16)))
    , deltaAfrPanel_("Delta AFR", "DeltaAfrTable", "Delta AFR", {})
    , lambdaDelayEditor_("Lambda Delay", "LambdaDelay", "Lambda Delay", config_.lambdaDelayTable)
  {
    //observedAfrTable_ = core::Table2D(core::generateEvenBreakpoints(500, 7000, 16), core::generateEvenBreakpoints(20, 100, 16));

    auto recalcCallback = [this]() {
      computeObservedAfr();
      computeAfrDelta();
      computeSuggestedVe();
      };
    targetAfrPanel_.setOnDataChangedCallback([this] {targetAfrDataModified_ = true; });
    baselineVePanel_.setOnDataChangedCallback(recalcCallback);
    targetAfrPanel_.setCustomToolbar2Callback([this, recalcCallback] {
      ImGui::BeginDisabled(!hasBaselineVe() || session_ == nullptr);
      std::string s {ICON_FA_CHECK " Apply Changes"};
      if (targetAfrDataModified_) s += "*";
      if (ui::UI::ButtonPrimary(s.c_str(), {}, "Recomputes AFR Delta and Suggested VE.")) {
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
    lambdaDelayEditor_.setOnDataChangedCallback([this] {
      config_.lambdaDelayTable = lambdaDelayEditor_.table();
      });

    deltaAfrPanel_.setReadOnly(true);
    deltaAfrPanel_.setCustomToolbar1Callback([this] {
      ImGui::SameLine();
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
      });
    deltaAfrPanel_.setCustomHeatmapColoring([this](double delta, size_t r, size_t c) -> ImU32 {
      const auto &afrHitCount = observedAfrData_.hitCount;
      size_t hitCount = (r < afrHitCount.size() && c < afrHitCount[r].size()) ? afrHitCount[r][c] : 0;
      if (!std::isnan(delta)) {
        return getDeltaHeatMapColor(delta, hitCount, config_.minSamplesPerBin, scaleDeltaIntensityByHitCount_);
      }
      return {};
      });
    deltaAfrPanel_.setCustomHoverTooltip([this](double delta, size_t r, size_t c) -> std::string {
      const auto &afrHitCount = observedAfrData_.hitCount;
      size_t hitCount = (r < afrHitCount.size() && c < afrHitCount[r].size()) ? afrHitCount[r][c] : 0;
      const bool useVeAxes = alignAfrDeltaToVeTable_ && hasBaselineVe();
      const auto &ref = useVeAxes ? baselineVePanel_.table() : targetAfrPanel_.table();
      double tgt = targetAfrPanel_.table().sample(ref.xBreakpoints()[c], ref.yBreakpoints()[r]);
      if (config_.minSamplesPerBin <= hitCount) {
        char buffer[96]{ 0 };
        snprintf(buffer, sizeof(buffer), "Samples: %zu\nTarget AFR: %.2f\nDelta: %+.2f AFR", hitCount, tgt, delta);
        return std::string(buffer);
      } else if (0 < hitCount) {
        char buffer[80]{ 0 };
        snprintf(buffer, sizeof(buffer), "Samples: %zu (below min threshold %zu)", hitCount, config_.minSamplesPerBin);
        return std::string(buffer);
      }
      return {};
      });
    deltaAfrPanel_.setCustomTextColoring([this](double delta, size_t r, size_t c) -> std::optional<ImU32> {
      std::optional<ImU32> clr;
      bool selected = deltaAfrPanel_.isCellSelected(r, c);
      if (delta > 0.3) {
        clr = selected ? ImGui::ColorConvertFloat4ToU32({ 1.0f, 0.6f, 0.6f, 1.0f }) : ImGui::ColorConvertFloat4ToU32({ 1.0f, 0.4f, 0.4f, 1.0f }); // Lean
      } else if (delta < -0.3) {
        clr = selected ? ImGui::ColorConvertFloat4ToU32({ 0.6f, 0.8f, 1.0f, 1.0f }) : ImGui::ColorConvertFloat4ToU32({ 0.4f, 0.7f, 1.0f, 1.0f }); // Rich
      }
      return clr;
    });

    observedAfrPanel_.setReadOnly(true);
    observedAfrPanel_.setCustomToolbar1Callback([this] {
      ImGui::SameLine();
      if (ui::UI::ButtonPrimary(ICON_FA_REPEAT " Recalculate Observed AFR")) {
        computeObservedAfr();
      }
      });
    observedAfrPanel_.setCustomHoverTooltip([this] (double v, size_t r, size_t c) {
      const auto &afrHitCount = observedAfrData_.hitCount;
      size_t hitCount = (r < afrHitCount.size() && c < afrHitCount[r].size()) ? afrHitCount[r][c] : 0;
      if (config_.minSamplesPerBin <= hitCount) {
        char buffer[64]{ 0 };
        snprintf(buffer, sizeof(buffer), "Samples: %zu", hitCount);
        return std::string(buffer);
      }
      return std::string();
      });
  }

  void VeAnalysisPanel::setSession(const core::LogSession *session)
  {
    session_ = session;
    targetAfrPanel_.setSession(session);
    baselineVePanel_.setSession(session);
    suggestedVePanel_.setSession(session);
    deltaAfrPanel_.setSession(session);
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

    core::Table2D observedAfrTable;
    observedAfrTable.setXBreakpoints(xBp);
    observedAfrTable.setYBreakpoints(yBp);

    const size_t rows = yBp.size();
    const size_t cols = xBp.size();
    if (rows == 0 || cols == 0) return;

    observedAfrData_ = engine::VeAnalyzer::computeObservedAfr(*session_, xBp, yBp, config_);

    hasObservedData_ = false;
    const auto &sumAfr = observedAfrData_.sumAfr;
    const auto &countAfr = observedAfrData_.hitCount;
    for (size_t r = 0; r < rows; ++r) {
      for (size_t c = 0; c < cols; ++c) {
        if (countAfr[r][c] >= config_.minSamplesPerBin) {
          double avgAfr = sumAfr[r][c] / static_cast<double>(countAfr[r][c]);
          observedAfrTable.setValue(r, c, avgAfr);
          hasObservedData_ = true;
        } else {
          observedAfrTable.setValue(r, c, std::numeric_limits<double>::quiet_NaN());
        }
      }
    }
    observedAfrPanel_.setTable(observedAfrTable);
    observedAfrPanel_.triggerUpdated();
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

    targetAfrDataModified_ = false;

    const bool useVeAxes = alignAfrDeltaToVeTable_ && hasBaselineVe();
    const core::Table2D &refTable = useVeAxes ? baselineVePanel_.table() : targetAfrPanel_.table();

    const auto &xBp = refTable.xBreakpoints();
    const auto &yBp = refTable.yBreakpoints();
    core::Table2D afrDeltaTable;
    afrDeltaTable.setXBreakpoints(xBp);
    afrDeltaTable.setYBreakpoints(yBp);

    const size_t rows = yBp.size();
    const size_t cols = xBp.size();

    if (rows == 0 || cols == 0) return;

    const auto &sumAfr = observedAfrData_.sumAfr;
    const auto &hitCount = observedAfrData_.hitCount;
    for (size_t r = 0; r < rows; ++r) {
      for (size_t c = 0; c < cols; ++c) {
        size_t count = hitCount[r][c];
        if (count >= config_.minSamplesPerBin) {
          double obsAfr = sumAfr[r][c] / static_cast<double>(count);
          double tgtAfr = useVeAxes ? targetAfrPanel_.table().sample(xBp[c], yBp[r]) : targetAfrPanel_.table().value(r, c);
          if (tgtAfr > 0.0) {
            afrDeltaTable.setValue(r, c, obsAfr - tgtAfr);
          } else {
            afrDeltaTable.setValue(r, c, std::numeric_limits<double>::quiet_NaN());
          }
        } else {
          afrDeltaTable.setValue(r, c, std::numeric_limits<double>::quiet_NaN());
        }
      }
    }
    
    deltaAfrPanel_.setTable(afrDeltaTable);
    deltaAfrPanel_.triggerUpdated();
    hasAfrDelta_ = true;
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
      char buffer[64]{ 0 };
      snprintf(buffer, sizeof(buffer), "Baseline: %.2f\nChange: %+0.2f (%+0.2f%%)\nObs AFR: %.2f", curVe, diff, percentChange, observedAfrPanel_.table().value(row, col));
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
      if (ui::UI::ButtonDanger(ICON_FA_ROTATE_LEFT " Reset to Calculated VE", {}, "Recomputes Suggested VE from log data, discarding any manual edits.")) {
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
            auto smoothed = engine::VeAnalyzer::computeSmartSmoothedVe(*session_, currentVe, deltaAfrPanel_.table(), config_, suggestedVePanel_.selectedCells());
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

    core::Table2D result = engine::VeAnalyzer::computeCorrectedVe(observedAfrData_, baselineVePanel_.table(), targetAfrPanel_.table(), config_);
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

      const auto *timeSec = session_->timeSec();
      for (size_t i = 0; i < numSamples; ++i) {
        if (!session_->isRowInCropRange(i)) continue;

        double rpm = rpmCh->values()[i];
        double load = loadCh->values()[i];
        double afr = afrCh->values()[i];
        double t = (timeSec && i < timeSec->size()) ? (*timeSec)[i] : 0.0;
        if (std::isnan(rpm) || std::isnan(load) || std::isnan(afr)) continue;
        if (filter.shouldIgnoreSample(i, rpm, load, t)) continue;

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

  void VeAnalysisPanel::renderObservedAfrTab(PlotCursor &cursor)
  {
    if (session_ == nullptr) {
      ImGui::TextDisabled("No log file loaded.");
      return;
    }

    // --- Filter Configuration Collapsible Header ---
    if (ImGui::CollapsingHeader("Analysis & Transient Filters")) {
      bool changed = false;

      if (ImGui::BeginTabBar("ObservedAfrSettingsTabBar")) {

        // --- Tab 1: Range & Environmental Limits ---
        if (ImGui::BeginTabItem("Telemetry Filters")) {
          ImGui::Spacing();
          ImGui::PushItemWidth(120.0f);

          // RPM Range
          ImGui::Text("RPM Limits:");
          ImGui::SameLine();
          changed |= ImGui::InputDouble("Min RPM##min_rpm", &config_.minRpm, 100.0, 500.0, "%.0f");
          ImGui::SameLine();
          changed |= ImGui::InputDouble("Max RPM##max_rpm", &config_.maxRpm, 100.0, 500.0, "%.0f");

          // MAP Range
          ImGui::Text("MAP Limits:");
          ImGui::SameLine();
          changed |= ImGui::Checkbox("Filter Min MAP", &config_.enableOverrunFilter);
          if (config_.enableOverrunFilter) {
            ImGui::SameLine();
            changed |= ImGui::InputDouble("Min MAP (kPa)##min_map", &config_.minMap, 1.0, 5.0, "%.1f");
          }
          ImGui::SameLine();
          changed |= ImGui::InputDouble("Max MAP (kPa)##max_map", &config_.maxMap, 5.0, 10.0, "%.1f");

          ImGui::Separator();

          // Transients
          changed |= ImGui::Checkbox("Filter Acceleration Transients (TPSdot)", &config_.enableTpsDotFilter);
          if (config_.enableTpsDotFilter) {
            ImGui::SameLine();
            changed |= ImGui::InputDouble("Max |TPSdot| (%/s)##max_tpsdot", &config_.maxTpsDot, 5.0, 10.0, "%.0f");
          }

          changed |= ImGui::Checkbox("Filter Cold Warmup (CLT)", &config_.enableCltFilter);
          if (config_.enableCltFilter) {
            ImGui::SameLine();
            changed |= ImGui::InputDouble("Min CLT##min_clt", &config_.minCoolantTemp, 5.0, 10.0, "%.0f");
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

        // --- Tab 2: Excluded Drive Regimes (Scrollable & Searchable) ---
        if (ImGui::BeginTabItem("Regime Exclusions")) {
          ImGui::Spacing();

          if (session_ && !session_->regimeSummaries().empty()) {
            const auto &summaries = session_->regimeSummaries();

            // Search & Bulk Select Bar
            ImGui::SetNextItemWidth(180.0f);
            ImGui::InputTextWithHint("##RegimeSearch", "Filter regimes...", regimeFilterText_.data(), regimeFilterText_.capacity() + 1,
              ImGuiInputTextFlags_CallbackResize,
              [](ImGuiInputTextCallbackData *data) -> int {
                auto *str = static_cast<std::string *>(data->UserData);
                str->resize(static_cast<size_t>(data->BufTextLen));
                data->Buf = str->data();
                return 0;
              }, &regimeFilterText_);

            ImGui::SameLine();
            if (ui::UI::Button("Exclude All")) {
              for (const auto &reg : summaries) config_.excludedRegimeIds.insert(reg.def.id);
              changed = true;
            }

            ImGui::SameLine();
            if (ui::UI::Button("Clear Exclusions")) {
              config_.excludedRegimeIds.clear();
              changed = true;
            }

            ImGui::Spacing();

            // Scrollable Regime Checkbox Area
            ImGui::BeginChild("RegimeExclusionScroll", ImVec2(0, 130.0f), true);

            std::string filterLower = utils::str::toLower(regimeFilterText_);

            for (const auto &reg : summaries) {
              if (!filterLower.empty() && utils::str::toLower(reg.def.displayName).find(filterLower) == std::string::npos) {
                continue;
              }

              bool isExcluded = config_.excludedRegimeIds.count(reg.def.id) > 0;
              std::string label = reg.def.displayName + " (" + std::to_string(reg.intervals.size()) + " events, " +
                std::to_string(static_cast<int>(reg.percentageOfLog)) + "% of log)";

              ImGui::PushID(reg.def.id.c_str());
              if (ImGui::Checkbox(label.c_str(), &isExcluded)) {
                if (isExcluded) config_.excludedRegimeIds.insert(reg.def.id);
                else config_.excludedRegimeIds.erase(reg.def.id);
                changed = true;
              }
              ImGui::PopID();
            }

            ImGui::EndChild();
          } else {
            ImGui::TextDisabled("No active drive regimes detected in this session.");
          }

          ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Lambda Delay")) {
          ImGui::Checkbox("Enable Transport Delay Compensation", &config_.enableLambdaDelay);
          if (config_.enableLambdaDelay) {
            ImGui::Separator();
            lambdaDelayEditor_.render(cursor);
          }
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

    if (hasObservedData_) {
      PlotCursor dummyCursor;
      observedAfrPanel_.render(dummyCursor);
    } else {
      ImGui::TextDisabled("No valid AFR log samples binned in current axes range.");
    }
  }

  void VeAnalysisPanel::renderAfrDeltaTab()
  {
    if (!hasTargetAfr()) {
      ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "Please set the 'Target AFR' table first.");
      return;
    }
    if (hasAfrDelta_) {
      PlotCursor dummyCursor;
      deltaAfrPanel_.render(dummyCursor);
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
    deltaAfrPanel_.recomputeRegimeCoverage();
  }

  void VeAnalysisPanel::copyTableToClipboard(const core::Table2D &table, bool includeHeaders, int decimalPlaces)
  {
    const auto &xBp = table.xBreakpoints();
    const auto &yBp = table.yBreakpoints();
    if (xBp.empty() || yBp.empty()) return;

    std::ostringstream ss;
    ss << std::fixed << std::setprecision(decimalPlaces);

    // 1. Column Headers (RPM)
    if (includeHeaders) {
      ss << "\t";
      for (size_t c = 0; c < xBp.size(); ++c) {
        ss << static_cast<int>(xBp[c]);
        if (c + 1 < xBp.size()) ss << "\t";
      }
      ss << "\n";
    }

    // 2. Rows in descending Y order (High load at top to match TunerStudio / Grid layout)
    for (size_t r = yBp.size(); r-- > 0; ) {
      if (includeHeaders) {
        ss << std::setprecision(1) << yBp[r] << "\t" << std::setprecision(decimalPlaces);
      }
      for (size_t c = 0; c < xBp.size(); ++c) {
        double val = table.value(r, c);
        if (std::isnan(val) || val <= 0.0) {
          ss << ""; // Or "-" depending on preference; empty tab aligns best in Excel/TunerStudio
        } else {
          ss << val;
        }
        if (c + 1 < xBp.size()) ss << "\t";
      }
      ss << "\n";
    }

    ImGui::SetClipboardText(ss.str().c_str());
    copyToastTimer_ = 1.5f;
  }

  void VeAnalysisPanel::renderToast()
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
    std::string windowLabel = makeWindowLabel(ICON_FA_SLIDERS);
    ImGui::Begin(windowLabel.c_str(), &open_, getAppearanceFlags());
    renderCommonOps();
    if (ImGui::BeginTabBar("VeAnalyzerTabBar")) {

      ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.50f, 0.20f, 0.70f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_TabHovered, ImVec4(0.65f, 0.30f, 0.85f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_TabActive, ImVec4(0.58f, 0.25f, 0.78f, 1.0f));
      if (ImGui::BeginTabItem("Observed AFR")) {
        ImGui::PopStyleColor(3);
        renderObservedAfrTab(cursor);
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
      {"minRpm", config_.minRpm},
      {"maxRpm", config_.maxRpm},
      {"minMap", config_.minMap},
      {"maxMap", config_.maxMap},
    };

    nlohmann::json excludedArr = nlohmann::json::array();
    for (const auto &id : config_.excludedRegimeIds) excludedArr.push_back(id);
    j["config"]["excludedRegimeIds"] = excludedArr;

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
      if (cfg.contains("minRpm")) config_.minRpm = cfg["minRpm"].get<double>();
      if (cfg.contains("maxRpm")) config_.maxRpm = cfg["maxRpm"].get<double>();
      if (cfg.contains("minMap")) config_.minMap = cfg["minMap"].get<double>();
      else if (cfg.contains("minLoadThreshold")) config_.minMap = cfg["minLoadThreshold"].get<double>(); // Backwards compat
      if (cfg.contains("maxMap")) config_.maxMap = cfg["maxMap"].get<double>();
      if (cfg.contains("excludedRegimeIds") && cfg["excludedRegimeIds"].is_array()) {
        config_.excludedRegimeIds.clear();
        for (const auto &item : cfg["excludedRegimeIds"]) {
          if (item.is_string()) config_.excludedRegimeIds.insert(item.get<std::string>());
        }
      }
    }

    if (state.contains("targetAfr")) {
      targetAfrPanel_.loadState(state["targetAfr"]);
    }
    if (state.contains("baselineVe")) {
      baselineVePanel_.loadState(state["baselineVe"]);
    }
  }

} // namespace ui