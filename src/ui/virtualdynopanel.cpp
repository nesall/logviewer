#include "ui/virtualdynopanel.h"
#include "implot.h"
#include "3rdparty/IconsFontAwesome7.h"
#include "3rdparty/nlohmann/json.hpp"
#include "ui/ui_helpers.h"
#include <imgui.h>

namespace ui {

  VirtualDynoPanel::VirtualDynoPanel(const std::string &title)
    : PlotPanel(title)
  {
    profile_.name = "Sports Car (3rd Gear)";
    profile_.totalWeightLbs = 2950.0;
    profile_.dragCoefficient = 0.31;
    profile_.frontalAreaM2 = 1.79;
    profile_.tireWidthMm = 225;
    profile_.tireAspectRatio = 50;
    profile_.wheelRimInches = 16;
    profile_.gearRatio = 1.330;
    profile_.finalDriveRatio = 4.100;
    profile_.smoothingWindow = 25;
  }

  void VirtualDynoPanel::setSession(const core::LogSession *session)
  {
    session_ = session;
    if (session_) {
      recomputeDyno();
    }
  }

  void VirtualDynoPanel::recomputeDyno()
  {
    if (!session_) return;

    if (pullMode_ == 0) {
      // Manual Time Range
      result_ = engine::VirtualDyno::calculate(*session_, profile_, manualStartSec_, manualEndSec_);
      return;
    }

    // Drive Regime
    const core::RegimeSummary *regime = nullptr;
    for (const auto &r : session_->regimeSummaries()) {
      if (r.def.id == selectedRegimeId_) {
        regime = &r;
        break;
      }
    }

    if (!regime || regime->intervals.empty()) {
      result_ = engine::DynoResult{};
      result_.errorMessage = "Select a drive regime with at least one detected event.";
      return;
    }

    if (regimeSubMode_ == 0 || regime->intervals.size() == 1) {
      // First Event
      const auto &iv = regime->intervals.front();
      result_ = engine::VirtualDyno::calculate(*session_, profile_, iv.startSec, iv.endSec);
    } else {
      // All Events (Averaged)
      result_ = engine::VirtualDyno::calculateAveraged(*session_, profile_, regime->intervals);
    }
  }

  void VirtualDynoPanel::render(PlotCursor &cursor)
  {
    std::string windowLabel = makeWindowLabel(ICON_FA_GAUGE_HIGH);
    if (!ImGui::Begin(windowLabel.c_str(), &open_, getAppearanceFlags())) {
      ImGui::End();
      return;
    }
    renderCommonOps();
    // Left Configuration Panel
    ImGui::BeginChild("DynoSidebar", ImVec2(320.0f, 0.0f), true);
    renderSidebar();
    ImGui::EndChild();

    ImGui::SameLine();

    // Right Plot Area
    ImGui::BeginChild("DynoPlotArea", ImVec2(0.0f, 0.0f), false);
    renderDynoPlot();
    ImGui::EndChild();

    ImGui::End();
  }

  void VirtualDynoPanel::renderSidebar()
  {
    bool changed = false;

    ImGui::TextDisabled("PULL SELECTION");
    const char *modes[] = { "Manual Time Range", "Drive Regime" };
    if (ImGui::Combo("Source Window", &pullMode_, modes, 2)) changed = true;

    if (pullMode_ == 0) {
      ImGui::PushItemWidth(90.0f);
      if (ImGui::InputDouble("Start (s)", &manualStartSec_, 0.5, 2.0, "%.2f")) changed = true;
      ImGui::SameLine();
      if (ImGui::InputDouble("End (s)", &manualEndSec_, 0.5, 2.0, "%.2f")) changed = true;
      ImGui::PopItemWidth();
    } else if (session_) {
      const auto &regimes = session_->regimeSummaries();

      // Only offer regimes that actually have detected events.
      const core::RegimeSummary *current = nullptr;
      for (const auto &r : regimes) {
        if (r.def.id == selectedRegimeId_) { current = &r; break; }
      }
      if (!current) {
        // Default to the first non-empty regime, if any.
        for (const auto &r : regimes) {
          if (!r.intervals.empty()) { current = &r; selectedRegimeId_ = r.def.id; changed = true; break; }
        }
      }

      if (ImGui::BeginCombo("Drive Regime", current ? current->def.displayName.c_str() : "(none)")) {
        for (const auto &r : regimes) {
          if (r.intervals.empty()) continue;
          bool isSelected = (r.def.id == selectedRegimeId_);
          std::string label = r.def.displayName + " (" + std::to_string(r.intervals.size()) + (r.intervals.size() == 1 ? " event)" : " events)");
          if (ImGui::Selectable(label.c_str(), isSelected)) {
            selectedRegimeId_ = r.def.id;
            current = &r;
            changed = true;
          }
        }
        ImGui::EndCombo();
      }

      if (!current) {
        ImGui::TextDisabled("No drive regimes with detected events in log.");
      } else if (current->intervals.size() > 1) {
        const char *subModes[] = { "First Event", "All Events (Averaged)" };
        if (ImGui::Combo("Events", &regimeSubMode_, subModes, 2)) changed = true;
      } else {
        regimeSubMode_ = 0;
      }
    }

    ImGui::Separator();
    ImGui::TextDisabled("VEHICLE PROFILE & WEIGHT");

    ImGui::PushItemWidth(120.0f);
    if (ImGui::InputDouble("Total Weight (lbs)", &profile_.totalWeightLbs, 25.0, 100.0, "%.0f")) changed = true;
    if (ImGui::InputDouble("Drag Coeff (Cd)", &profile_.dragCoefficient, 0.01, 0.05, "%.3f")) changed = true;
    if (ImGui::InputDouble("Frontal Area (m²)", &profile_.frontalAreaM2, 0.05, 0.10, "%.2f")) changed = true;

    ImGui::Separator();
    ImGui::TextDisabled("GEARING & TIRE SPECS");

    if (ImGui::InputDouble("Gear Ratio", &profile_.gearRatio, 0.01, 0.10, "%.3f")) changed = true;
    if (ImGui::InputDouble("Final Drive", &profile_.finalDriveRatio, 0.01, 0.10, "%.3f")) changed = true;

    ImGui::Text("Tire: %d / %d R %d", profile_.tireWidthMm, profile_.tireAspectRatio, profile_.wheelRimInches);
    if (ImGui::InputInt("Width (mm)", &profile_.tireWidthMm, 5)) changed = true;
    if (ImGui::InputInt("Aspect (%)", &profile_.tireAspectRatio, 5)) changed = true;
    if (ImGui::InputInt("Rim (in)", &profile_.wheelRimInches, 1)) changed = true;

    ImGui::Separator();
    ImGui::TextDisabled("CORRECTION & SMOOTHING");

    if (ImGui::Checkbox("SAE J1349 Correction", &profile_.enableSaeCorrection)) changed = true;
    if (profile_.enableSaeCorrection) {
      if (ImGui::InputDouble("Ambient (°F)", &profile_.ambientTempF, 1.0, 5.0, "%.1f")) changed = true;
      if (ImGui::InputDouble("Baro (kPa)", &profile_.baroPressureKpa, 0.5, 2.0, "%.1f")) changed = true;
    }

    if (ImGui::SliderInt("Smoothing Window", &profile_.smoothingWindow, 5, 65)) changed = true;
    ImGui::Checkbox("Show Boost Curve", &showBoostCurve_);
    ImGui::PopItemWidth();

    if (changed) {
      recomputeDyno();
    }

    // Peak KPI Summary Card
    if (result_.valid) {
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::TextColored(ImVec4(1.0f, 0.45f, 0.2f, 1.0f), ICON_FA_BOLT " PEAK OUTPUT");
      ImGui::Text("Max Power:  %.1f WHP @ %.0f RPM", result_.peakWhp, result_.peakWhpRpm);
      ImGui::Text("Max Torque: %.1f lb-ft @ %.0f RPM", result_.peakWtq, result_.peakWtqRpm);
      if (result_.maxBoostPsi > 0.5) {
        ImGui::Text("Peak Boost: %.1f PSI", result_.maxBoostPsi);
      }
    }
  }

  void VirtualDynoPanel::renderDynoPlot()
  {
    if (!result_.valid) {
      ImGui::Spacing();
      ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION " %s",
        result_.errorMessage.empty() ? "No active data to calculate." : result_.errorMessage.c_str());
      ImGui::TextDisabled("Select a valid WOT acceleration run (e.g. 3rd gear pull) via Manual Time Range or a Drive Regime.");
      return;
    }

    if (ImPlot::BeginPlot("##DynoPowerPlot", ImVec2(-1, -1), ImPlotFlags_Crosshairs)) {
      ImPlot::SetupAxes("Engine RPM", "Power (WHP) / Torque (lb-ft)", ImPlotAxisFlags_None, ImPlotAxisFlags_None);

      if (showBoostCurve_ && result_.maxBoostPsi > 0.5) {
        ImPlot::SetupAxis(ImAxis_Y2, "Boost (PSI)", ImPlotAxisFlags_Opposite | ImPlotAxisFlags_NoGridLines);
      }

      int count = static_cast<int>(result_.rpm.size());

      // Wheel Horsepower (Red/Orange)
      ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
      ImPlotSpec spec;
      spec.MarkerLineColor = ImVec4(1.0f, 0.35f, 0.2f, 1.0f);
      spec.LineWeight = 3.0f;
      ImPlot::PlotLine("Wheel Horsepower (WHP)", result_.rpm.data(), result_.whp.data(), count, spec);

      // Wheel Torque (Blue)
      ImPlot::SetAxes(ImAxis_X1, ImAxis_Y1);
      spec.MarkerLineColor = ImVec4(0.2f, 0.65f, 1.0f, 1.0f);
      spec.LineWeight = 3.0f;
      ImPlot::PlotLine("Wheel Torque (lb-ft)", result_.rpm.data(), result_.wtq.data(), count, spec);

      // Manifold Boost Pressure (Green on Y2)
      if (showBoostCurve_ && result_.maxBoostPsi > 0.5) {
        ImPlot::SetAxes(ImAxis_X1, ImAxis_Y2);
        ImPlotSpec spec;
        spec.MarkerLineColor = ImVec4(0.2f, 0.9f, 0.4f, 0.75f);
        spec.LineWeight = 1.8f;
        ImPlot::PlotLine("Boost (PSI)", result_.rpm.data(), result_.boostPsi.data(), count, spec);
      }

      ImPlot::EndPlot();
    }
  }

  nlohmann::json VirtualDynoPanel::saveState() const
  {
    nlohmann::json j = PlotPanel::saveState();
    j["profile"] = profile_.toJson();
    j["pullMode"] = pullMode_;
    j["manualStartSec"] = manualStartSec_;
    j["manualEndSec"] = manualEndSec_;
    j["selectedRegimeId"] = selectedRegimeId_;
    j["regimeSubMode"] = regimeSubMode_;
    j["showBoostCurve"] = showBoostCurve_;
    return j;
  }

  void VirtualDynoPanel::loadState(const nlohmann::json &state)
  {
    PlotPanel::loadState(state);
    if (state.contains("profile")) profile_ = engine::VehicleDynoProfile::fromJson(state["profile"]);
    if (state.contains("pullMode")) pullMode_ = state["pullMode"].get<int>();
    if (state.contains("manualStartSec")) manualStartSec_ = state["manualStartSec"].get<double>();
    if (state.contains("manualEndSec")) manualEndSec_ = state["manualEndSec"].get<double>();
    if (state.contains("selectedRegimeId")) selectedRegimeId_ = state["selectedRegimeId"].get<std::string>();
    if (state.contains("regimeSubMode")) regimeSubMode_ = state["regimeSubMode"].get<int>();
    if (state.contains("showBoostCurve")) showBoostCurve_ = state["showBoostCurve"].get<bool>();
  }

} // namespace ui