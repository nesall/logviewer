#include "ui/veanalysispanel.h"
#include "3rdparty/nlohmann/json.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include "imgui.h"

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

    // 1. Prefer user's explicit Current VE table axes if defined
    if (hasCurrentVe_) {
      xBp = currentVePanel_.table().xBreakpoints();
      yBp = currentVePanel_.table().yBreakpoints();
    }
    // 2. Fall back to user's Target AFR table axes if defined
    else if (hasTargetAfr_) {
      xBp = targetAfrPanel_.table().xBreakpoints();
      yBp = targetAfrPanel_.table().yBreakpoints();
    }
    // 3. Otherwise, dynamically auto-scale axes from the actual log range
    else {
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

      // Generate a 16x16 grid spanning the log's real min/max boundaries
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

    for (size_t i = 0; i < numSamples; ++i) {
      double rpm = rpmCh->values()[i];
      double load = loadCh->values()[i];
      double afr = afrCh->values()[i];

      if (std::isnan(rpm) || std::isnan(load) || std::isnan(afr)) continue;

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
    core::Table2D result = engine::VeAnalyzer::computeCorrectedVe(
      *session_, currentVePanel_.table(), targetAfrPanel_.table(), config_);
    suggestedVePanel_.setTable(result);
    hasSuggestedVe_ = true;
  }

  void VeAnalysisPanel::renderObservedAfrTab()
  {
    if (session_ == nullptr) {
      ImGui::TextDisabled("No log file loaded.");
      return;
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

    // 1. Determine reference grid axes
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
    std::vector<std::vector<size_t>> countAfr(rows, std::vector<size_t>(cols, 0));

    const size_t numSamples = session_->rowCount();

    // 2. Bin log data directly onto the selected reference grid
    for (size_t i = 0; i < numSamples; ++i) {
      double rpm = rpmCh->values()[i];
      double load = loadCh->values()[i];
      double afr = afrCh->values()[i];

      if (std::isnan(rpm) || std::isnan(load) || std::isnan(afr)) continue;

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

    // 3. Compute Delta safely
    for (size_t r = 0; r < rows; ++r) {
      for (size_t c = 0; c < cols; ++c) {
        if (countAfr[r][c] >= config_.minSamplesPerBin) {
          double obsAfr = sumAfr[r][c] / static_cast<double>(countAfr[r][c]);

          // Target AFR is a dense grid without zeros, so sample() works flawlessly here
          double tgtAfr = useVeAxes ? targetAfrPanel_.table().sample(xBp[c], yBp[r])
            : targetAfrPanel_.table().value(r, c);

          if (tgtAfr > 0.0) {
            afrDeltaTable_.setValue(r, c, obsAfr - tgtAfr);
          } else {
            afrDeltaTable_.setValue(r, c, std::numeric_limits<double>::quiet_NaN());
          }
        } else {
          // Flag empty bins with NaN to safely ignore them in the renderer
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

    // Checkbox toggle to enable/disable Current VE alignment
    ImGui::BeginDisabled(!hasCurrentVe_);
    if (!hasCurrentVe_)
      alignAfrDeltaToVeTable_ = false;
    if (ImGui::Checkbox("Align grid to Current VE Table", &alignAfrDeltaToVeTable_)) {
      computeAfrDelta(); // Immediately recalculate on toggle change
    }
    ImGui::EndDisabled();

    if (!hasCurrentVe_) {
      ImGui::SameLine();
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

      // Helper lambda for heat map background color based on delta (+ = lean/red, - = rich/blue)
      auto getDeltaHeatMapColor = [](double delta) -> ImU32 {
        float norm = static_cast<float>(delta / 2.0); // scale across +/- 2 AFR units
        if (norm > 1.0f) norm = 1.0f;
        if (norm < -1.0f) norm = -1.0f;

        float r = 0.0f, g = 0.0f, b = 0.0f, a = 0.35f; // alpha for subtle shading
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
        return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, a));
        };

      if (ImGui::BeginTable("AfrDeltaGrid", static_cast<int>(cols + 1), flags, ImVec2(0.0f, 380.0f))) {
        ImGui::TableSetupScrollFreeze(1, 1);
        ImGui::TableSetupColumn("MAP \\ RPM", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        //for (size_t c = 0; c < cols; ++c) {
        //  ImGui::TableSetupColumn(std::to_string(static_cast<int>(xBp[c])).c_str(), ImGuiTableColumnFlags_WidthFixed, 65.0f);
        //}
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

            // Render only if the bin actually captured log samples (is not NaN)
            if (!std::isnan(delta)) {
              ImGui::TableSetBgColor(ImGuiTableBgTarget_CellBg, getDeltaHeatMapColor(delta));

              if (delta > 0.3) {
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "+%.2f", delta); // Lean
              } else if (delta < -0.3) {
                ImGui::TextColored(ImVec4(0.4f, 0.7f, 1.0f, 1.0f), "%.2f", delta);  // Rich
              } else {
                ImGui::Text("%.2f", delta);
              }
            } else {
              ImGui::TextDisabled(" - ");
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
              ImGui::TextColored(ImVec4(0.6f, 0.85f, 1.0f, 1.0f), "%.1f", sugVe);
            } else {
              ImGui::Text("%.1f", sugVe);
            }

            if (ImGui::IsItemHovered() && std::abs(diff) > 0.01) {
              ImGui::SetTooltip("Current: %.1f | Change: %+.1f", curVe, diff);
            }
          }
        }
        ImGui::EndTable();
      }
    }
  }
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
    j["hasTargetAfr"] = hasTargetAfr_;
    j["hasCurrentVe"] = hasCurrentVe_;
    j["targetAfr"] = targetAfrPanel_.saveState();
    j["currentVe"] = currentVePanel_.saveState();
    return j;
  }

  void VeAnalysisPanel::loadState(const nlohmann::json &state)
  {
    PlotPanel::loadState(state);
    if (state.contains("hasTargetAfr")) hasTargetAfr_ = state["hasTargetAfr"].get<bool>();
    if (state.contains("hasCurrentVe")) hasCurrentVe_ = state["hasCurrentVe"].get<bool>();
    if (state.contains("targetAfr")) targetAfrPanel_.loadState(state["targetAfr"]);
    if (state.contains("currentVe")) currentVePanel_.loadState(state["currentVe"]);
  }

} // namespace ui