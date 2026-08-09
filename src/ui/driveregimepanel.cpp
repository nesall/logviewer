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
          // 1. If custom/edited definitions exist, use them directly
          workingDefs = userDefinitions_;
        } else {
          // 2. Otherwise, seed from the full default built-in map
          auto defaultMap = engine::getDefaultRegimeDefs();
          for (auto &[type, def] : defaultMap) {
            workingDefs.push_back(def);
          }
        }

        selectedIdx = 0;
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

        ImGui::SeparatorText("Detection Bounds (Leave unchecked if unused)");

        inputOptionalDouble("Min RPM", def.minRpm, 100.0);
        inputOptionalDouble("Max RPM", def.maxRpm, 100.0);
        inputOptionalDouble("Min MAP (kPa)", def.minMap, 5.0);
        inputOptionalDouble("Max MAP (kPa)", def.maxMap, 5.0);
        inputOptionalDouble("Min TPS (%)", def.minTps, 5.0);
        inputOptionalDouble("Max TPS (%)", def.maxTps, 5.0);
        inputOptionalDouble("Min TPSdot (%/s)", def.minTpsDot, 5.0);
        inputOptionalDouble("Max TPSdot (%/s)", def.maxTpsDot, 5.0);

        ImGui::SeparatorText("Advanced Formula (ExprTk)");
        char formulaBuf[256];
        std::snprintf(formulaBuf, sizeof(formulaBuf), "%s", def.customFormula.c_str());
        if (ImGui::InputText("Custom Formula", formulaBuf, sizeof(formulaBuf))) {
          def.customFormula = formulaBuf;
        }
        ImGui::TextDisabled("Overrides numeric bounds above if non-empty.");

        if (!def.isBuiltIn) {
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
      if (ui::UI::ButtonPrimary("Apply & Re-Analyze", ImVec2(160, 0))) {
        userDefinitions_ = workingDefs; // Persist full set of definitions on panel
        reanalyze();                    // Re-runs analyzer with userDefinitions_
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
    std::string windowLabel = title() + "###" + panelTypeId();
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
// --- Regime Cards Loop ---
    for (auto &reg : summaries) {
      ImGui::PushID(reg.def.id.c_str());

      // 1. Build Header Title with Alert Badge (visible even when collapsed)
      std::string headerLabel = reg.def.displayName + "  (" + dtostr(reg.percentageOfLog, 1) + "% of log)";

      bool hasAlert = !reg.warningMessage.empty();
      if (hasAlert) {
        // Append critical alert text to the header title
        headerLabel += "  [ " + std::string(ICON_FA_TRIANGLE_EXCLAMATION) + " " + dtostr(reg.peakEgt, 0) + " °F ]";

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

        // Controls Row
        if (ImGui::Checkbox("Shade Timeline", &reg.def.showShading)) {
          syncToUserDefinitions(reg.def);
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::ColorEdit4("Highlight Color", (float *)&reg.def.color, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoAlpha)) {
          syncToUserDefinitions(reg.def);
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

        // Compact 2-Pair Key/Value Grid
        if (ImGui::BeginTable("CompactMetrics", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV)) {
          ImGui::TableSetupColumn("K1", ImGuiTableColumnFlags_WidthFixed, 65.0f);
          ImGui::TableSetupColumn("V1", ImGuiTableColumnFlags_WidthFixed, 60.0f);
          ImGui::TableSetupColumn("K2", ImGuiTableColumnFlags_WidthFixed, 65.0f);
          ImGui::TableSetupColumn("V2", ImGuiTableColumnFlags_WidthFixed, 60.0f);

          // Row 1: Avg RPM & Avg MAP
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("Avg RPM");
          ImGui::TableSetColumnIndex(1); ImGui::Text("%s", dtostr(reg.avgRpm, 0).c_str());
          ImGui::TableSetColumnIndex(2); ImGui::TextDisabled("Avg MAP");
          ImGui::TableSetColumnIndex(3); ImGui::Text("%s kPa", dtostr(reg.avgMap, 1).c_str());

          // Row 2: Avg AFR & Avg Timing
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("Avg AFR");
          ImGui::TableSetColumnIndex(1); ImGui::Text("%s", dtostr(reg.avgAfr, 2).c_str());
          ImGui::TableSetColumnIndex(2); ImGui::TextDisabled("Avg Timing");
          ImGui::TableSetColumnIndex(3); ImGui::Text("%s°", dtostr(reg.avgTiming, 1).c_str());

          // Row 3: Engine Temperatures
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("Avg CLT");
          ImGui::TableSetColumnIndex(1); ImGui::Text("%s °F", dtostr(reg.avgClt, 0).c_str());
          ImGui::TableSetColumnIndex(2); ImGui::TextDisabled("Avg MAT");
          ImGui::TableSetColumnIndex(3); ImGui::Text("%s °F", dtostr(reg.avgMat, 0).c_str());

          // Row 4: Fuel Headroom & Thermal Peak
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("Peak Duty");
          ImGui::TableSetColumnIndex(1); ImGui::Text("%s%%", dtostr(reg.peakDuty, 1).c_str());
          ImGui::TableSetColumnIndex(2); ImGui::TextDisabled("Peak EGT");
          ImGui::TableSetColumnIndex(3);
          if (reg.peakEgt > 1600.0) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s °F", dtostr(reg.peakEgt, 0).c_str());
          } else {
            ImGui::Text("%s °F", dtostr(reg.peakEgt, 0).c_str());
          }

          ImGui::EndTable();
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

    // Save the full list of definitions (which includes their active color/shading properties)
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

    // 1. Restore all Regime Definitions via RegimeDef::fromJson()
    userDefinitions_.clear();
    if (state.contains("definitions") && state["definitions"].is_array()) {
      for (const auto &dJson : state["definitions"]) {
        userDefinitions_.push_back(core::RegimeDef::fromJson(dJson));
      }
    }

    // 2. Re-evaluate session with restored definitions
    reanalyze();
  }

} // namespace ui