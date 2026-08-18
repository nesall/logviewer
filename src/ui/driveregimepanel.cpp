#include "ui/driveregimepanel.h"
#include "ui/ui_helpers.h"
#include "engine/regime_analyzer.h"
#include "3rdparty/nlohmann/json.hpp"
#include "3rdparty/IconsFontAwesome7.h"

#include <cstdio>
#include "imgui.h"

namespace {
  std::string dtostr(double d, int precision = 2) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", precision, d);
    return std::string(buffer);
  }
} // anonymous namespace

namespace ui {

  DriveRegimePanel::DriveRegimePanel(std::string title)
    : PlotPanel(std::move(title)) {}

  void DriveRegimePanel::setSession(const core::LogSession *session)
  {
    session_ = session;
    reanalyze();
  }

  void DriveRegimePanel::reanalyze()
  {
    if (session_) {
      auto mutableSession = const_cast<core::LogSession *>(session_);
      mutableSession->setRegimeSummaries(engine::RegimeAnalyzer::analyzeSession(*session_, userDefinitions_));
    }
  }

  const std::vector<core::RegimeSummary> &DriveRegimePanel::regimes() const
  {
    static const std::vector<core::RegimeSummary> empty;
    return session_ ? session_->regimeSummaries() : empty;
  }

  std::vector<core::RegimeSummary> &DriveRegimePanel::regimes()
  {
    static std::vector<core::RegimeSummary> empty;
    if (session_) {
      return const_cast<core::LogSession *>(session_)->regimeSummaries();
    }
    empty.clear();
    return empty;
  }

  void DriveRegimePanel::renderConfigModal()
  {
    if (showConfigModal_) {
      ImGui::OpenPopup("Configure Drive Regimes###RegimeConfigModal");
    }

    ImGui::SetNextWindowSize(ImVec2(750, 480), ImGuiCond_FirstUseEver);

    if (ImGui::BeginPopupModal("Configure Drive Regimes###RegimeConfigModal", &showConfigModal_, ImGuiWindowFlags_None)) {

      // Local working copy of regime definitions while editing
      static std::vector<core::RegimeDef> workingDefs;
      static int selectedIdx = 0;
      static bool initialized = false;

      if (!initialized) {
        workingDefs.clear();

        if (!userDefinitions_.empty()) {
          workingDefs = userDefinitions_;
        } else {
          auto defaultMap = engine::getDefaultRegimeDefs(*session_);
          for (const auto &def : defaultMap) {
            workingDefs.push_back(def);
          }
        }

        //selectedIdx = 0;
        initialized = true;
      }

      // --- LEFT COLUMN: Regime Selection List ---
      ImGui::BeginChild("RegimeListSidebar", ImVec2(240, -ImGui::GetFrameHeightWithSpacing()), true);

      for (int i = 0; i < static_cast<int>(workingDefs.size()); ++i) {
        ImGui::PushID(i);

        std::string label = workingDefs[i].displayName;
        if (workingDefs[i].isBuiltIn) {
          label += " (Built-in)";
        }

        if (ImGui::Selectable(label.c_str(), selectedIdx == i)) {
          selectedIdx = i;
        }
        ImGui::PopID();
      }

      ImGui::Separator();
      if (ImGui::Button("+ Add Custom Regime", ImVec2(-1, 0))) {
        core::RegimeDef newDef;
        newDef.id = "custom_regime_" + std::to_string(workingDefs.size() + 1);
        newDef.displayName = "New Custom Regime";
        newDef.isBuiltIn = false;
        workingDefs.push_back(newDef);
        selectedIdx = static_cast<int>(workingDefs.size()) - 1;
      }

      ImGui::EndChild();
      ImGui::SameLine();

      // --- RIGHT COLUMN: Editor Form ---
      ImGui::BeginGroup();
      ImGui::BeginChild("RegimeEditorForm", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false);

      if (selectedIdx >= 0 && selectedIdx < static_cast<int>(workingDefs.size())) {
        auto &def = workingDefs[selectedIdx];

        char nameBuf[64];
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", def.displayName.c_str());
        if (ImGui::InputText("Display Name", nameBuf, sizeof(nameBuf))) {
          def.displayName = nameBuf;
        }

        ImGui::ColorEdit4("Highlight Color", (float *)&def.color, ImGuiColorEditFlags_NoAlpha);

        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::SeparatorText("Alert & Warning Thresholds");

        // Helper for optional double inputs (NaN = Disabled)
        auto inputOptionalDouble = [](const char *label, double &val, double step = 1.0) {
          bool active = !std::isnan(val);
          if (ImGui::Checkbox((std::string("##enable_") + label).c_str(), &active)) {
            val = active ? 0.0 : std::numeric_limits<double>::quiet_NaN();
          }
          ImGui::SameLine();
          ImGui::BeginDisabled(!active);
          double temp = active ? val : 0.0;
          ImGui::SetNextItemWidth(140.0f);
          if (ImGui::InputDouble(label, &temp, step, step * 5.0, "%.1f")) {
            val = temp;
          }
          ImGui::EndDisabled();
          };

        inputOptionalDouble("Peak EGT Warning (°F)", def.maxEgtWarning, 10.0);
        inputOptionalDouble("Peak CLT Warning (°F)", def.maxCltWarning, 5.0);
        inputOptionalDouble("Peak Duty Warning (%)", def.maxDutyWarning, 5.0);

        {
          ImGui::Spacing();
          ImGui::Spacing();
          ImGui::SeparatorText("Detection Bounds");

          for (int i = 0; i < static_cast<int>(def.boundsRules.size()); ++i) {
            ImGui::PushID(i);
            auto &rule = def.boundsRules[i];

            // Channel Selector
            ImGui::SetNextItemWidth(150.0f);
            if (ImGui::BeginCombo("##bounds_ch", rule.channelName.c_str())) {
              for (const auto &ch : session_->channels()) {
                if (ImGui::Selectable(ch.name().c_str(), ch.name() == rule.channelName)) {
                  rule.channelName = ch.name();
                }
              }
              ImGui::EndCombo();
            }

            ImGui::SameLine();

            // Min Bound Input
            bool hasMin = !std::isnan(rule.minVal);
            if (ImGui::Checkbox("Min##has_min", &hasMin)) {
              rule.minVal = hasMin ? 0.0 : std::numeric_limits<double>::quiet_NaN();
            }
            ImGui::BeginDisabled(!hasMin);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            ImGui::InputDouble("##min_val", &rule.minVal, 0.0, 0.0, "%.1f");
            ImGui::EndDisabled();

            ImGui::SameLine();

            // Max Bound Input
            bool hasMax = !std::isnan(rule.maxVal);
            if (ImGui::Checkbox("Max##has_max", &hasMax)) {
              rule.maxVal = hasMax ? 0.0 : std::numeric_limits<double>::quiet_NaN();
            }
            
            ImGui::BeginDisabled(!hasMax);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(90.0f);
            ImGui::InputDouble("##max_val", &rule.maxVal, 0.0, 0.0, "%.1f");
            ImGui::EndDisabled();

            ImGui::SameLine();
            if (ui::UI::ButtonDanger("X##del_bound")) {
              def.boundsRules.erase(def.boundsRules.begin() + i);
              ImGui::PopID();
              break;
            }

            ImGui::PopID();
          }

          if (ImGui::Button("+ Add Detection Bound Rule")) {
            core::ChannelBoundRule newRule;
            if (!session_->channels().empty()) {
              newRule.channelName = session_->channels().front().name();
            }
            def.boundsRules.push_back(newRule);
          }
        }

        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::SeparatorText("Advanced Formula (ExprTk)");
        char formulaBuf[256];
        std::snprintf(formulaBuf, sizeof(formulaBuf), "%s", def.customFormula.c_str());
        if (ImGui::InputText("Custom Formula", formulaBuf, sizeof(formulaBuf))) {
          def.customFormula = formulaBuf;
        }
        ImGui::TextDisabled("Overrides numeric bounds above if non-empty.");

        {
          ImGui::Spacing();
          ImGui::Spacing();
          ImGui::SeparatorText("Card Metrics (From Session Channels)");

          // Render existing configured metric rules
          for (int m = 0; m < static_cast<int>(def.configuredMetrics.size()); ++m) {
            ImGui::PushID(m);
            auto &rule = def.configuredMetrics[m];

            // 1. Channel Selector Combo
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::BeginCombo("##ch_combo", rule.channelName.empty() ? "Select Channel..." : rule.channelName.c_str())) {
              for (const auto &ch : session_->channels()) {
                if (ImGui::Selectable(ch.name().c_str(), ch.name() == rule.channelName)) {
                  rule.channelName = ch.name();
                }
              }
              ImGui::EndCombo();
            }

            ImGui::SameLine();

            // 2. Aggregation Mode Combo
            ImGui::SetNextItemWidth(100.0f);
            const char *aggNames[] = { "Average", "Peak / Max", "Min" };
            int currentAgg = static_cast<int>(rule.aggregation);
            if (ImGui::Combo("##agg_combo", &currentAgg, aggNames, IM_ARRAYSIZE(aggNames))) {
              rule.aggregation = static_cast<core::MetricAgg>(currentAgg);
            }

            ImGui::SameLine();

            // 3. Remove Button
            if (ui::UI::ButtonDanger("X##remove_metric")) {
              def.configuredMetrics.erase(def.configuredMetrics.begin() + m);
              ImGui::PopID();
              break;
            }

            ImGui::PopID();
          }

          if (ImGui::Button("+ Add Metric Channel")) {
            core::RegimeMetricRule newRule;
            if (!session_->channels().empty()) {
              newRule.channelName = session_->channels().front().name();
            }
            def.configuredMetrics.push_back(newRule);
          }
        }

        if (!def.isBuiltIn) {
          ImGui::Spacing();
          ImGui::Spacing();
          ImGui::Separator();
          if (ui::UI::ButtonDanger("Delete Regime")) {
            workingDefs.erase(workingDefs.begin() + selectedIdx);
            if (selectedIdx >= static_cast<int>(workingDefs.size())) {
              selectedIdx = static_cast<int>(workingDefs.size()) - 1;
            }
          }
        }
      }

      ImGui::EndChild();
      ImGui::EndGroup();

      ImGui::Separator();

      // --- BOTTOM BAR ---
      if (ui::UI::ButtonPrimary("Apply", ImVec2(90, 0))) {
        userDefinitions_ = workingDefs;
        reanalyze();
        notifyDataChanged();
        initialized = false;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ui::UI::ButtonPrimary("Apply & Close", ImVec2(150, 0))) {
        userDefinitions_ = workingDefs;
        reanalyze();
        notifyDataChanged();
        initialized = false;
        showConfigModal_ = false;
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ui::UI::Button("Cancel", {}, ImVec2(90, 0))) {
        initialized = false;
        showConfigModal_ = false;
        ImGui::CloseCurrentPopup();
      }

      ImGui::EndPopup();
    }
  }

  void DriveRegimePanel::render(PlotCursor &cursor)
  {
    std::string windowLabel = std::string{ ICON_FA_FLAG } + " " + title() + "###" + panelTypeId();
    ImGui::Begin(windowLabel.c_str(), &open_);

    if (!session_) {
      ImGui::TextDisabled("No log session loaded.");
      ImGui::End();
      return;
    }

    // --- Top Control Bar ---
    if (ui::UI::ButtonPrimary(ICON_FA_REPEAT " Re-Analyze Session")) {
      reanalyze();
    }
    ImGui::SameLine();
    if (ui::UI::ButtonSecondary(ICON_FA_GEAR " Configure Regimes...")) {
      showConfigModal_ = true;
    }

    renderConfigModal();

    const auto *timeSec = session_->timeSec();
    if (timeSec && !timeSec->empty()) {
      //ImGui::SameLine();
      ImGui::TextDisabled("Total Log Time: %.1f s", timeSec->back() - timeSec->front());
    }

    ImGui::Separator();

    auto &summaries = regimes();

    if (summaries.empty()) {
      ImGui::TextDisabled("No regimes detected in the current log range.");
      ImGui::End();
      return;
    }

    // --- Regime Cards Loop ---
    for (size_t j = 0; j < summaries.size(); j ++) {
      const auto &reg = summaries[j];
      ImGui::PushID(reg.def.id.c_str());

      // 1. Build Header Title with Alert Badge (visible even when collapsed)
      std::string headerLabel = reg.def.displayName + "  (" + dtostr(reg.percentageOfLog, 1) + "% of log)";

      bool hasAlert = !reg.warningMessage.empty();
      if (hasAlert) {
        // Append critical alert text to the header title
        headerLabel += "  [ " + std::string(ICON_FA_TRIANGLE_EXCLAMATION)/* + " " + dtostr(reg.getMetric(core::RegimeMetric::PeakEgt), 0) + " °F ]"*/;

        // Highlight header text color red/orange if critical alert is present
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.45f, 1.0f));
      }

      bool nodeOpen = ImGui::CollapsingHeader(headerLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

      if (hasAlert) {
        ImGui::PopStyleColor();
      }

      // 2. Expanded Card Body
      if (nodeOpen) {
        ImGui::Indent(8.0f);
        ImGui::Spacing();

        auto syncToUserDefinitions = [this](const core::RegimeDef &updatedDef) {
          for (auto &def : userDefinitions_) {
            if (def.id == updatedDef.id) {
              def = updatedDef;
              break;
            }
          }
          };

        if (ImGui::Checkbox("Shade Timeline", &summaries.at(j).def.showShading)) {
          syncToUserDefinitions(reg.def);
          notifyDataChanged();
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::ColorEdit4("Highlight Color", (float *)&reg.def.color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoAlpha)) {
          syncToUserDefinitions(reg.def);
          notifyDataChanged();
        }

        ImGui::Spacing();

        // Detailed Warning Banner Inside Body
        if (hasAlert) {
          ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION " %s", reg.warningMessage.c_str());
          ImGui::Spacing();
        }

        // Dwell Time Info
        ImGui::TextDisabled("Dwell Time: %s s across %zu intervals", dtostr(reg.totalDwellTimeSec, 1).c_str(), reg.intervals.size());
        ImGui::Spacing();

        if (!reg.metricResults.empty()) {
          if (ImGui::BeginTable("DynamicChannelMetrics", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV)) {
            ImGui::TableSetupColumn("K1", ImGuiTableColumnFlags_WidthStretch, 1.2f);
            ImGui::TableSetupColumn("V1", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("K2", ImGuiTableColumnFlags_WidthStretch, 1.2f);
            ImGui::TableSetupColumn("V2", ImGuiTableColumnFlags_WidthStretch, 1.0f);

            for (size_t i = 0; i < reg.metricResults.size(); ++i) {
              const auto &res = reg.metricResults[i];

              if (i % 2 == 0) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
              } else {
                ImGui::TableSetColumnIndex(2);
              }

              // Display Label (e.g., "Peak EGT" or "Avg AFR")
              std::string aggPrefix = (res.rule.aggregation == core::MetricAgg::Peak) ? "Peak " :
                (res.rule.aggregation == core::MetricAgg::Min) ? "Min " : "Avg ";
              std::string label = res.rule.customLabel.empty() ? (aggPrefix + res.rule.channelName) : res.rule.customLabel;
              //float columnWidth = ImGui::GetContentRegionAvail().x;
              //float textWidth = ImGui::CalcTextSize(label.c_str()).x;
              //if (columnWidth > textWidth) {
              //  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (columnWidth - textWidth));
              //}
              ImGui::TextDisabled("%s", label.c_str());

              // Display Value
              ImGui::TableSetColumnIndex((i % 2 == 0) ? 1 : 3);
              if (res.unit.empty()) {
                ImGui::Text("%.1f", res.value);
              } else {
                ImGui::Text("%.1f %s", res.value, res.unit.c_str());
              }
            }

            ImGui::EndTable();
          }
        }

        ImGui::Spacing();

        // Cycle Jump Buttons
        if (!reg.intervals.empty()) {
          if (ui::UI::ButtonSecondary(ICON_FA_LOCATION_DOT " Jump Next Event")) {
            // Find next interval after cursor position
            double targetTime = reg.intervals.front().startSec;
            for (const auto &interval : reg.intervals) {
              if (interval.startSec > cursor.timeSec + 0.1) {
                targetTime = interval.startSec;
                break;
              }
            }
            cursor.timeSec = targetTime;
            cursor.active = true;
          }
        }

        ImGui::Unindent(8.0f);
      }

      // 3. 10px Vertical Spacing Between Cards
      ImGui::Dummy(ImVec2(0.0f, 10.0f));

      ImGui::PopID();
    }

    ImGui::End();
  }

  nlohmann::json DriveRegimePanel::saveState() const
  {
    auto j = PlotPanel::saveState();
    nlohmann::json defsArray = nlohmann::json::array();
    for (const auto &def : userDefinitions_) {
      defsArray.push_back(def.toJson());
    }
    j["definitions"] = defsArray;
    return j;
  }

  void DriveRegimePanel::loadState(const nlohmann::json &state)
  {
    PlotPanel::loadState(state);
    userDefinitions_.clear();
    if (state.contains("definitions") && state["definitions"].is_array()) {
      for (const auto &dJson : state["definitions"]) {
        userDefinitions_.push_back(core::RegimeDef::fromJson(dJson));
      }
    }
    //reanalyze();
  }

} // namespace ui