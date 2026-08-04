#include "ui/veanalysispanel.h"
#include "3rdparty/nlohmann/json.hpp"

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
    , currentVePanel_("Current VE Editor", "VeTable", "Current VE",
      core::Table2D(core::generateEvenBreakpoints(500, 7000, 16),
        core::generateEvenBreakpoints(20, 100, 16)))
    , suggestedVePanel_("Suggested VE", "SuggestedVeTable", "Suggested VE",
      core::Table2D(core::generateEvenBreakpoints(500, 7000, 16),
        core::generateEvenBreakpoints(20, 100, 16)))
  {
    observedAfrTable_ = core::Table2D(core::generateEvenBreakpoints(500, 7000, 16),
      core::generateEvenBreakpoints(20, 100, 16));
  }

  void VeAnalysisPanel::setSession(const core::LogSession *session)
  {
    session_ = session;
    if (session_ != nullptr) {
      engine::populateDefaultsForSession(config_, *session_);
    }
    computeObservedAfr();
  }

  void VeAnalysisPanel::computeObservedAfr()
  {
    if (!session_) {
      hasObservedData_ = false;
      return;
    }

    const core::Channel *rpmCh = session_->findChannel(config_.rpmChannel);
    const core::Channel *loadCh = session_->findChannel(config_.loadChannel);
    const core::Channel *afrCh = session_->findChannel(config_.afrChannel);

    if (!rpmCh || !loadCh || !afrCh) {
      hasObservedData_ = false;
      return;
    }

    std::vector<double> xBp;
    std::vector<double> yBp;

    if (hasCurrentVe_) {
      xBp = currentVePanel_.table().xBreakpoints();
      yBp = currentVePanel_.table().yBreakpoints();
    } else if (hasTargetAfr_) {
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
    if (!session_ || !hasTargetAfr_ || !hasCurrentVe_) return;

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
      double curVe = currentVePanel_.table().value(row, col);
      double diff = sugVe - curVe;
      return getVeChangeHeatMapColor(diff);
      };

    auto customTooltip = [this](double value, size_t row, size_t col) -> std::string {
      double curVe = currentVePanel_.table().value(row, col);
      double diff = value - curVe;
      if (std::abs(diff) < 1e-5) return {};
      char buffer[64];
      snprintf(buffer, sizeof(buffer), "Current: %.2f\nChange: %+0.2f", curVe, diff);
      return std::string(buffer);
      };

    auto customTextColor = [this](double value, size_t row, size_t col) -> std::optional<ImU32> {
      double curVe = currentVePanel_.table().value(row, col);
      double diff = value - curVe;
      if (std::abs(diff) < 1e-5) return std::nullopt; // No change, use default text color
      return ImGui::ColorConvertFloat4ToU32(ImVec4{ 0.6f, 0.85f, 1.0f, 1.0f });
      };

    core::Table2D result = engine::VeAnalyzer::computeCorrectedVe(*session_, currentVePanel_.table(), targetAfrPanel_.table(), config_);
    suggestedVePanel_.setCustomHeatmapColoring(customHeatmap);
    suggestedVePanel_.setCustomHoverTooltip(customTooltip);
    suggestedVePanel_.setCustomTextColoring(customTextColor);
    suggestedVePanel_.setTable(result);
    hasSuggestedVe_ = true;
  }

  void VeAnalysisPanel::selectUnvisitedCellsOnSuggestedVe()
  {
    if (!hasSuggestedVe_ || !hasCurrentVe_ || !session_) return;

    const core::Table2D &veTable = currentVePanel_.table();
    const size_t rows = veTable.rowCount();
    const size_t cols = veTable.columnCount();
    if (rows == 0 || cols == 0) return;

    const core::Channel *rpmCh = session_->findChannel(config_.rpmChannel);
    const core::Channel *loadCh = session_->findChannel(config_.loadChannel);
    const core::Channel *afrCh = session_->findChannel(config_.afrChannel);

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

      auto renderChannelCombo = [this, &changed](const char *label, std::string &selectedChannel) {
        if (ImGui::BeginCombo(label, selectedChannel.empty() ? "None" : selectedChannel.c_str())) {
          if (ImGui::Selectable("None", selectedChannel.empty())) {
            selectedChannel.clear();
            changed = true;
          }
          for (const auto &ch : session_->channels()) {
            bool isSelected = (ch.name() == selectedChannel);
            if (ImGui::Selectable(ch.name().c_str(), isSelected)) {
              selectedChannel = ch.name();
              changed = true;
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
          }
          ImGui::EndCombo();
        }
        };

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


        if (ImGui::BeginTabItem("Channel Mapping")) {
          ImGui::Spacing();
          renderChannelCombo("RPM Channel", config_.rpmChannel);
          renderChannelCombo("Load Channel", config_.loadChannel);
          renderChannelCombo("AFR Channel", config_.afrChannel);
          renderChannelCombo("TPSdot Channel", config_.tpsDotChannel);
          renderChannelCombo("CLT Channel", config_.cltChannel);
          ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
      }
      if (changed) {
        computeObservedAfr();
        if (hasTargetAfr_) computeAfrDelta();
        if (hasTargetAfr_ && hasCurrentVe_) computeSuggestedVe();
      }
      ImGui::Separator();
    }

    if (ImGui::Button("Recalculate Observed AFR")) {
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

    if (ImGui::BeginTable("ObservedAfrGridCropped", static_cast<int>(visibleCols + 1), flags, ImVec2(0.0f, 400.0f))) {
      ImGui::TableSetupScrollFreeze(1, 1);
      ImGui::TableSetupColumn("MAP \\ RPM", ImGuiTableColumnFlags_WidthFixed, 90.0f);

      for (size_t c = minObservedCol_; c <= maxObservedCol_; ++c) {
        std::string header = std::to_string(static_cast<int>(xBp[c])) + "##col" + std::to_string(c);
        ImGui::TableSetupColumn(header.c_str(), ImGuiTableColumnFlags_WidthFixed, 65.0f);
      }
      ImGui::TableHeadersRow();

      // Render in descending order (highest MAP/load at top) to match Target AFR orientation
      if (maxObservedRow_ >= minObservedRow_) {
        for (size_t r = maxObservedRow_; ; --r) {
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::Text("%.1f", yBp[r]);

          for (size_t c = minObservedCol_; c <= maxObservedCol_; ++c) {
            ImGui::TableSetColumnIndex(static_cast<int>(c - minObservedCol_ + 1));
            double val = observedAfrTable_.value(r, c);
            if (val > 0.0) {
              ImGui::Text("%.2f", val);
            } else {
              ImGui::TextDisabled(" - ");
            }
          }

          if (r == minObservedRow_) break;
        }
      }
      ImGui::EndTable();
    }
  }

  void VeAnalysisPanel::renderReadOnlyTableGrid(const core::Table2D &table)
  {
    const auto &xBp = table.xBreakpoints();
    const auto &yBp = table.yBreakpoints();
    const size_t cols = xBp.size();
    const size_t rows = yBp.size();

    if (cols == 0 || rows == 0) {
      ImGui::TextDisabled("Table is empty.");
      return;
    }

    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
      ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
      ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings;

    if (ImGui::BeginTable("ReadOnlyGrid", static_cast<int>(cols + 1), flags, ImVec2(0.0f, 380.0f))) {
      ImGui::TableSetupScrollFreeze(1, 1);
      ImGui::TableSetupColumn("MAP \\ RPM", ImGuiTableColumnFlags_WidthFixed, 90.0f);
      for (size_t c = 0; c < cols; ++c) {
        std::string header = std::to_string(static_cast<int>(xBp[c])) + "##col" + std::to_string(c);
        ImGui::TableSetupColumn(header.c_str(), ImGuiTableColumnFlags_WidthFixed, 65.0f);
      }
      ImGui::TableHeadersRow();

      // Render top-to-bottom matching TunerStudio layout (highest Y breakpoint at row 0)
      for (size_t r = rows; r-- > 0; ) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::Text("%.1f", yBp[r]);

        for (size_t c = 0; c < cols; ++c) {
          ImGui::TableSetColumnIndex(static_cast<int>(c + 1));
          ImGui::Text("%.2f", table.value(r, c));
        }
      }
      ImGui::EndTable();
    }
  }

  void VeAnalysisPanel::computeAfrDelta()
  {
    if (!session_ || !hasTargetAfr_) {
      hasAfrDelta_ = false;
      return;
    }

    const core::Channel *rpmCh = session_->findChannel(config_.rpmChannel);
    const core::Channel *loadCh = session_->findChannel(config_.loadChannel);
    const core::Channel *afrCh = session_->findChannel(config_.afrChannel);

    if (!rpmCh || !loadCh || !afrCh) {
      hasAfrDelta_ = false;
      return;
    }

    const bool useVeAxes = alignAfrDeltaToVeTable_ && hasCurrentVe_;
    const core::Table2D &refTable = useVeAxes ? currentVePanel_.table() : targetAfrPanel_.table();

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
    if (!hasTargetAfr_) {
      ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f), "Please set the 'Target AFR' table first.");
      return;
    }

    if (ImGui::Button("Recalculate AFR Delta")) {
      computeAfrDelta();
    }

    ImGui::SameLine();

    ImGui::BeginDisabled(!hasCurrentVe_);
    if (!hasCurrentVe_) alignAfrDeltaToVeTable_ = false;
    if (ImGui::Checkbox("Align grid to Current VE Table", &alignAfrDeltaToVeTable_)) {
      computeAfrDelta();
    }
    ImGui::EndDisabled();

    ImGui::SameLine();
    ImGui::Checkbox("Scale intensity by sample count", &scaleDeltaIntensityByHitCount_);

    if (!hasCurrentVe_) {
      ImGui::TextDisabled("(Target AFR axes used - load Current VE to enable VE alignment)");
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

      if (ImGui::BeginTable("AfrDeltaGrid", static_cast<int>(cols + 1), flags, ImVec2(0.0f, 380.0f))) {
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn("MAP \\ RPM", ImGuiTableColumnFlags_WidthFixed, 90.0f);

        for (size_t c = 0; c < cols; ++c) {
          std::string header = std::to_string(static_cast<int>(xBp[c])) + "##col" + std::to_string(c);
          ImGui::TableSetupColumn(header.c_str(), ImGuiTableColumnFlags_WidthFixed, 65.0f);
        }
        ImGui::TableHeadersRow();

        for (size_t r = rows; r-- > 0; ) {
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::Text("%.1f", yBp[r]);

          for (size_t c = 0; c < cols; ++c) {
            ImGui::TableSetColumnIndex(static_cast<int>(c + 1));

            double delta = afrDeltaTable_.value(r, c);
            size_t hitCount = (r < afrDeltaSampleCounts_.size() && c < afrDeltaSampleCounts_[r].size())
              ? afrDeltaSampleCounts_[r][c] : 0;

            if (!std::isnan(delta)) {
              ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, getDeltaHeatMapColor(delta, hitCount));

              if (delta > 0.3) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "+%.2f", delta); // Lean
              } else if (delta < -0.3) {
                ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "%.2f", delta);  // Rich
              } else {
                ImGui::Text("%.2f", delta);
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
          }
        }
        ImGui::EndTable();
      }
    } else {
      ImGui::TextDisabled("No AFR Delta available. Ensure a log is loaded and Target AFR is set.");
    }
  }

  void VeAnalysisPanel::renderReadOnlyTableTab(const char *tabName, TableEditorPanel &editorPanel, bool &tableSetFlag, const char *popupId)
  {
    std::string editBtnLabel = std::string("Edit / Change ") + tabName + "...";
    if (ImGui::Button(editBtnLabel.c_str())) {
      ImGui::OpenPopup(popupId);
    }

    if (tableSetFlag) {
      ImGui::SameLine();
      if (ImGui::Button("Copy to Clipboard")) {
        editorPanel.copyToClipboard(false);
        suggestedVePanel_.showCopyToast();
      }
      suggestedVePanel_.renderToast();

      renderReadOnlyTableGrid(editorPanel.table());
    } else {
      ImGui::Separator();
      ImGui::TextDisabled("No %s data set yet. Click '%s' to enter or paste values.", tabName, editBtnLabel.c_str());
    }

    // Modal Editor Window
    ImGui::SetNextWindowSize(ImVec2(800, 600), ImGuiCond_FirstUseEver);
    if (ImGui::BeginPopupModal(popupId, nullptr, ImGuiWindowFlags_NoDocking)) {
      PlotCursor dummyCursor;
      editorPanel.render(dummyCursor);

      ImGui::Separator();
      if (ImGui::Button("OK", ImVec2(120, 0))) {
        tableSetFlag = true;
        computeObservedAfr(); // Re-bins log
        computeAfrDelta();    // Re-syncs AFR Delta grid[cite: 2]
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
  }

#if 0
  void VeAnalysisPanel::renderSuggestedVeTab()
  {
    ImGui::BeginDisabled(!hasTargetAfr_ || !hasCurrentVe_ || session_ == nullptr);
    if (ImGui::Button("Calculate Suggested VE")) {
      computeSuggestedVe();
    }
    ImGui::EndDisabled();

    if (!hasTargetAfr_ || !hasCurrentVe_) {
      ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.2f, 1.0f),
        "Please enter both 'Target AFR' and 'Current VE' tables before calculating.");
      return;
    }

    if (hasSuggestedVe_) {
      ImGui::SameLine();
      if (ImGui::Button("Copy to Clipboard")) {
        suggestedVePanel_.copyToClipboard(false);
        suggestedVePanel_.showCopyToast();
      }
      suggestedVePanel_.renderToast();

      const auto &xBp = suggestedVePanel_.table().xBreakpoints();
      const auto &yBp = suggestedVePanel_.table().yBreakpoints();
      const size_t cols = xBp.size();
      const size_t rows = yBp.size();

      if (cols == 0 || rows == 0) return;

      // Heat map generator (Red/Orange = adding fuel, Green/Cyan = pulling fuel)
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

      const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollX | ImGuiTableFlags_ScrollY |
        ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_NoSavedSettings;

      if (ImGui::BeginTable("SuggestedVeGrid", static_cast<int>(cols + 1), flags, ImVec2(0.0f, 380.0f))) {
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn("MAP \\ RPM", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        for (size_t c = 0; c < cols; ++c) {
          std::string header = std::to_string(static_cast<int>(xBp[c])) + "##col" + std::to_string(c);
          ImGui::TableSetupColumn(header.c_str(), ImGuiTableColumnFlags_WidthFixed, 65.0f);
        }
        ImGui::TableHeadersRow();

        for (size_t r = rows; r-- > 0; ) {
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0);
          ImGui::Text("%.1f", yBp[r]);

          for (size_t c = 0; c < cols; ++c) {
            ImGui::TableSetColumnIndex(static_cast<int>(c + 1));
            double sugVe = suggestedVePanel_.table().value(r, c);
            double curVe = currentVePanel_.table().value(r, c);
            double diff = sugVe - curVe;

            // Apply cell background heatmap if changed
            if (std::abs(diff) > 0.01) {
              ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, getVeChangeHeatMapColor(diff));
              // Color text pale blue for changed cells
              ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "%.2f", sugVe);
            } else {
              ImGui::Text("%.2f", sugVe);
            }

            if (ImGui::IsItemHovered() && std::abs(diff) > 0.01) {
              ImGui::SetTooltip("Current: %.2f | Change: %+.2f", curVe, diff);
            }
          }
        }
        ImGui::EndTable();
      }
    }
  }
#else
  void VeAnalysisPanel::renderSuggestedVeTab()
  {
    ImGui::BeginDisabled(!hasTargetAfr_ || !hasCurrentVe_ || session_ == nullptr);
    if (ImGui::Button("Reset to Calculated VE")) {
      computeSuggestedVe();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Recomputes Suggested VE from log data, discarding any manual edits, interpolations, or extrapolations.");
    }
    ImGui::EndDisabled();

    if (hasSuggestedVe_) {
      ImGui::SameLine();
      if (ImGui::Button("Select Unvisited Cells")) {
        selectUnvisitedCellsOnSuggestedVe();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip("Highlights cells that lacked enough log samples for direct AFR analysis.");
      }

      ImGui::SameLine();
      ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
      ImGui::SameLine();

      // Render standard TableEditorPanel batch toolbar & grid directly
      // gives Extrapolate, Interpolate, Offset, Scale, and click-and-drag selections for free
      PlotCursor dummyCursor;
      suggestedVePanel_.render(dummyCursor);
    }
  }
#endif

  void VeAnalysisPanel::render(PlotCursor &cursor)
  {
    std::string windowLabel = "VE Analyzer###" + title();
    ImGui::Begin(windowLabel.c_str(), &open_);

    if (ImGui::BeginTabBar("VeAnalyzerTabBar")) {

      if (ImGui::BeginTabItem("Observed AFR")) {
        renderObservedAfrTab();
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Target AFR")) {
        renderReadOnlyTableTab("Target AFR", targetAfrPanel_, hasTargetAfr_, "EditTargetAfrModal");
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("AFR Delta")) {
        computeAfrDelta();
        renderAfrDeltaTab();
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Current VE")) {
        renderReadOnlyTableTab("Current VE", currentVePanel_, hasCurrentVe_, "EditCurrentVeModal");
        ImGui::EndTabItem();
      }

      if (ImGui::BeginTabItem("Suggested VE")) {
        renderSuggestedVeTab();
        ImGui::EndTabItem();
      }

      ImGui::EndTabBar();
    }

    ImGui::End();
  }

  nlohmann::json VeAnalysisPanel::saveState() const
  {
    auto j = PlotPanel::saveState();

    // Persist input flags and view settings
    j["hasTargetAfr"] = hasTargetAfr_;
    j["hasCurrentVe"] = hasCurrentVe_;
    j["alignAfrDeltaToVeTable"] = alignAfrDeltaToVeTable_;

    // Persist analysis channel / sample thresholds
    j["config"] = {
      {"rpmChannel", config_.rpmChannel},
      {"loadChannel", config_.loadChannel},
      {"afrChannel", config_.afrChannel},
      {"tpsDotChannel", config_.tpsDotChannel},
      {"cltChannel", config_.cltChannel},
      {"minSamplesPerBin", config_.minSamplesPerBin},
      {"enableTpsDotFilter", config_.enableTpsDotFilter},
      {"maxTpsDot", config_.maxTpsDot},
      {"enableCltFilter", config_.enableCltFilter},
      {"minCoolantTemp", config_.minCoolantTemp},
      {"enableOverrunFilter", config_.enableOverrunFilter},
      {"minLoadThreshold", config_.minLoadThreshold}
    };

    // Only serialize primary input tables (ignore calculated ones)
    j["targetAfr"] = targetAfrPanel_.saveState();
    j["currentVe"] = currentVePanel_.saveState();

    return j;
  }

  void VeAnalysisPanel::loadState(const nlohmann::json &state)
  {
    PlotPanel::loadState(state);

    if (state.contains("hasTargetAfr") && state["hasTargetAfr"].is_boolean()) {
      hasTargetAfr_ = state["hasTargetAfr"].get<bool>();
    }
    if (state.contains("hasCurrentVe") && state["hasCurrentVe"].is_boolean()) {
      hasCurrentVe_ = state["hasCurrentVe"].get<bool>();
    }
    if (state.contains("alignAfrDeltaToVeTable") && state["alignAfrDeltaToVeTable"].is_boolean()) {
      alignAfrDeltaToVeTable_ = state["alignAfrDeltaToVeTable"].get<bool>();
    }

    // Restore analysis config
    if (state.contains("config") && state["config"].is_object()) {
      const auto &cfg = state["config"];
      if (cfg.contains("rpmChannel")) config_.rpmChannel = cfg["rpmChannel"].get<std::string>();
      if (cfg.contains("loadChannel")) config_.loadChannel = cfg["loadChannel"].get<std::string>();
      if (cfg.contains("afrChannel")) config_.afrChannel = cfg["afrChannel"].get<std::string>();
      if (cfg.contains("tpsDotChannel")) config_.tpsDotChannel = cfg["tpsDotChannel"].get<std::string>();
      if (cfg.contains("cltChannel")) config_.cltChannel = cfg["cltChannel"].get<std::string>();
      if (cfg.contains("minSamplesPerBin")) config_.minSamplesPerBin = cfg["minSamplesPerBin"].get<size_t>();

      if (cfg.contains("enableTpsDotFilter")) config_.enableTpsDotFilter = cfg["enableTpsDotFilter"].get<bool>();
      if (cfg.contains("maxTpsDot")) config_.maxTpsDot = cfg["maxTpsDot"].get<double>();
      if (cfg.contains("enableCltFilter")) config_.enableCltFilter = cfg["enableCltFilter"].get<bool>();
      if (cfg.contains("minCoolantTemp")) config_.minCoolantTemp = cfg["minCoolantTemp"].get<double>();
      if (cfg.contains("enableOverrunFilter")) config_.enableOverrunFilter = cfg["enableOverrunFilter"].get<bool>();
      if (cfg.contains("minLoadThreshold")) config_.minLoadThreshold = cfg["minLoadThreshold"].get<double>();
    }

    // Restore input tables
    if (state.contains("targetAfr")) {
      targetAfrPanel_.loadState(state["targetAfr"]);
    }
    if (state.contains("currentVe")) {
      currentVePanel_.loadState(state["currentVe"]);
    }

    // Automatically recalculate all derived tables (Observed AFR, AFR Delta, Suggested VE)
    if (session_ != nullptr) {
      computeObservedAfr();
      if (hasTargetAfr_) {
        computeAfrDelta();
      }
      if (hasTargetAfr_ && hasCurrentVe_) {
        computeSuggestedVe();
      }
    }
  }

} // namespace ui