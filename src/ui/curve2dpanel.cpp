#include "ui/curve2dpanel.h"
#include "implot.h"
#include "3rdparty/IconsFontAwesome7.h"
#include "3rdparty/nlohmann/json.hpp"
#include "core/logsession.h"
#include "ui/ui_helpers.h"
#include <imgui.h>
#include <cassert>

namespace {

  ImPlotSpec specFromParams(ImPlotMarker marker, float markerSize, const ImVec4 &markerFill, float lineWeight, const ImVec4 &markerOutline = {}) {
    ImPlotSpec spec;
    spec.Marker = marker;
    spec.MarkerSize = markerSize;
    spec.MarkerFillColor = markerFill;
    spec.LineWeight = lineWeight;
    spec.MarkerLineColor = markerOutline;
    return spec;
  }


} // anonymous namespace

namespace ui {

  Curve2DPanel::Curve2DPanel(const std::string &title) : PlotPanel(title)
  {
  }

  void Curve2DPanel::loadBuiltInPresets()
  {
    assert(session_);

    const auto &mp = session_->channelMapping();

    curves_.clear();

    // Preset 1: PWM Fan Control vs CLT
    core::Curve1D fanCurve("PWM Fan Duty vs CLT", mp.clt, mp.fan1duty, "°F", "%");
    fanCurve.addPoint(160.0, 0.0);
    fanCurve.addPoint(175.0, 0.0);
    fanCurve.addPoint(182.0, 15.0);
    fanCurve.addPoint(205.0, 100.0);
    fanCurve.addPoint(220.0, 100.0);
    fanCurve.setInterpMode(core::CurveInterpMode::PiecewiseLinear);
    curves_.push_back(fanCurve);

    // Preset 2: Warmup Enrichment (WUE)
    core::Curve1D wueCurve("Warmup Enrichment (WUE)", mp.clt, "WUE", "°F", "%");
    wueCurve.addPoint(-20.0, 180.0);
    wueCurve.addPoint(20.0, 155.0);
    wueCurve.addPoint(60.0, 135.0);
    wueCurve.addPoint(100.0, 120.0);
    wueCurve.addPoint(130.0, 110.0);
    wueCurve.addPoint(160.0, 100.0);
    wueCurve.setInterpMode(core::CurveInterpMode::SmoothSpline);
    curves_.push_back(wueCurve);

    // Preset 3: IAT Timing Retard
    core::Curve1D iatCurve("IAT Timing Retard", mp.mat, "Advance", "°F", "deg");
    iatCurve.addPoint(50.0, 0.0);
    iatCurve.addPoint(100.0, 0.0);
    iatCurve.addPoint(120.0, -1.0);
    iatCurve.addPoint(140.0, -3.0);
    iatCurve.addPoint(160.0, -6.0);
    iatCurve.setInterpMode(core::CurveInterpMode::PiecewiseLinear);
    curves_.push_back(iatCurve);
  }

  void Curve2DPanel::render(PlotCursor &cursor)
  {
    std::string windowLabel = makeWindowLabel(ICON_FA_CHART_LINE);
    if (!ImGui::Begin(windowLabel.c_str(), &open_, getAppearanceFlags())) {
      ImGui::End();
      return;
    }
    renderCommonOps();
    // Left Sidebar: Curve list & point table
    ImGui::BeginChild("CurveSidebar", ImVec2(280.0f, 0.0f), true);
    renderSidebar();
    ImGui::EndChild();

    ImGui::SameLine();

    // Right Area: ImPlot Curve + Live Scatter
    ImGui::BeginChild("CurvePlotArea", ImVec2(0.0f, 0.0f), false);
    renderCurvePlot(cursor);
    ImGui::EndChild();

    ImGui::End();
  }

  void Curve2DPanel::setSession(const core::LogSession *session)
  {
    session_ = session;
    if (session && curves_.empty()) {
      loadBuiltInPresets();
    }
  }

  void Curve2DPanel::renderSidebar()
  {
    ImGui::TextDisabled("CURVE SELECTION");

    // Selection Combo + Action Buttons (New / Duplicate / Delete)
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 84.0f);
    const char *previewName = (selectedCurveIdx_ >= 0 && selectedCurveIdx_ < static_cast<int>(curves_.size()))
      ? curves_[selectedCurveIdx_].name().c_str()
      : "No Curves Available";

    if (ImGui::BeginCombo("##CurveSelect", previewName)) {
      for (int i = 0; i < static_cast<int>(curves_.size()); ++i) {
        if (ImGui::Selectable(curves_[i].name().c_str(), selectedCurveIdx_ == i)) {
          selectedCurveIdx_ = i;
        }
      }
      ImGui::EndCombo();
    }

    // Action Buttons
    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_PLUS "##NewCurve")) {
      core::Curve1D newCurve("New Custom Curve", "RPM", "Target", "RPM", "%");
      newCurve.addPoint(0.0, 0.0);
      newCurve.addPoint(5000.0, 50.0);
      newCurve.addPoint(8000.0, 100.0);
      newCurve.setInterpMode(core::CurveInterpMode::PiecewiseLinear);
      curves_.push_back(newCurve);
      selectedCurveIdx_ = static_cast<int>(curves_.size()) - 1;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Create a new blank curve");

    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_CLONE "##DupCurve") && !curves_.empty()) {
      core::Curve1D dup = curves_[selectedCurveIdx_];
      dup.setName(dup.name() + " (Copy)");
      curves_.push_back(dup);
      selectedCurveIdx_ = static_cast<int>(curves_.size()) - 1;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Duplicate selected curve");

    ImGui::SameLine();
    if (ImGui::Button(ICON_FA_TRASH "##DelCurve") && curves_.size() > 1) {
      curves_.erase(curves_.begin() + selectedCurveIdx_);
      if (selectedCurveIdx_ >= static_cast<int>(curves_.size())) {
        selectedCurveIdx_ = static_cast<int>(curves_.size()) - 1;
      }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Delete selected curve");

    // Curve Settings and Node Editor
    if (selectedCurveIdx_ >= 0 && selectedCurveIdx_ < static_cast<int>(curves_.size())) {
      auto &c = curves_[selectedCurveIdx_];

      ImGui::Spacing();
      ImGui::TextDisabled("CHANNELS & AXES");
      char nameBuf[64];
      std::snprintf(nameBuf, sizeof(nameBuf), "%s", c.name().c_str());
      if (ImGui::InputText("Curve Name", nameBuf, sizeof(nameBuf))) {
        c.setName(nameBuf);
      }
      auto renderChannelCombo = [&](const char *label, const char *comboId, const std::string &currentVal, auto &&onSelect) {
        const char *preview = currentVal.empty() ? "(None)" : currentVal.c_str();
        if (ImGui::BeginCombo(comboId, preview)) {
          if (session_) {
            for (const auto &ch : session_->channels()) {
              bool isSelected = (currentVal == ch.name());
              char itemLabel[64];
              if (!ch.unit().empty()) {
                std::snprintf(itemLabel, sizeof(itemLabel), "%s [%s]", ch.name().c_str(), ch.unit().c_str());
              } else {
                std::snprintf(itemLabel, sizeof(itemLabel), "%s", ch.name().c_str());
              }
              if (ImGui::Selectable(itemLabel, isSelected)) {
                onSelect(ch.name(), ch.unit());
              }
              if (isSelected) ImGui::SetItemDefaultFocus();
            }
          } else {
            ImGui::TextDisabled("No Log Loaded");
          }
          ImGui::EndCombo();
        }
        ImGui::SameLine();
        ImGui::Text("%s", label);
        };
      renderChannelCombo("X (Input)", "##XChannelSelect", c.xChannelName(), [&](const std::string &name, const std::string &unit) {
        c.setXChannelName(name);
        if (!unit.empty()) c.setXUnit(unit);
        });
      renderChannelCombo("Y (Output)", "##YChannelSelect", c.yChannelName(), [&](const std::string &name, const std::string &unit) {
        c.setYChannelName(name);
        if (!unit.empty()) c.setYUnit(unit);
        });
      int mode = static_cast<int>(c.interpMode());
      const char *modes[] = { "Piecewise Linear", "Smooth Spline" };
      if (ImGui::Combo("Interpolation", &mode, modes, 2)) {
        c.setInterpMode(static_cast<core::CurveInterpMode>(mode));
      }

      ImGui::Separator();
      ImGui::Checkbox("Overlay Log Scatter", &showScatter_);
      ImGui::Checkbox("Track Log Cursor", &followPlotCursor_);

      ImGui::Separator();
      renderPointEditor();
    }
  }

  void Curve2DPanel::renderPointEditor()
  {
    auto &c = curves_[selectedCurveIdx_];
    ImGui::TextDisabled("BREAKPOINTS (%zu)", c.points().size());

    if (ui::UI::Button(ICON_FA_PLUS " Add Node")) {
      double nextX = c.points().empty() ? 0.0 : c.points().back().x + 10.0;
      double nextY = c.points().empty() ? 0.0 : c.points().back().y;
      c.addPoint(nextX, nextY);
    }

    ImGui::BeginChild("PointsList", ImVec2(0, 180.0f), true);
    if (ImGui::BeginTable("PointTable", 3, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("X", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("Y", ImGuiTableColumnFlags_WidthStretch);
      ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 30.0f);
      ImGui::TableHeadersRow();

      for (size_t i = 0; i < c.points().size(); ++i) {
        ImGui::TableNextRow();
        ImGui::PushID(static_cast<int>(i));

        double px = c.points()[i].x;
        double py = c.points()[i].y;

        ImGui::TableSetColumnIndex(0);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputDouble("##px", &px, 0, 0, "%.1f")) {
          c.updatePoint(i, px, py);
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-1);
        if (ImGui::InputDouble("##py", &py, 0, 0, "%.1f")) {
          c.updatePoint(i, px, py);
        }

        ImGui::TableSetColumnIndex(2);
        if (ImGui::Button(ICON_FA_TRASH)) {
          c.removePoint(i);
          ImGui::PopID();
          break;
        }

        ImGui::PopID();
      }
      ImGui::EndTable();
    }
    ImGui::EndChild();
  }

  void Curve2DPanel::renderCurvePlot(PlotCursor &cursor)
  {
    if (selectedCurveIdx_ < 0 || selectedCurveIdx_ >= static_cast<int>(curves_.size())) return;

    auto &c = curves_[selectedCurveIdx_];
    std::string plotTitle = c.name() + " (" + c.yChannelName() + " vs " + c.xChannelName() + ")";

    if (ImPlot::BeginPlot(plotTitle.c_str(), ImVec2(-1, -1), ImPlotFlags_Crosshairs)) {
      ImPlot::SetupAxes(c.xChannelName().c_str(), c.yChannelName().c_str(), ImPlotAxisFlags_None, ImPlotAxisFlags_None);

      // 1. Overlay Log Scatter points (if session loaded)
      if (showScatter_ && session_) {
        const auto *xCh = session_->findChannel(c.xChannelName());
        const auto *yCh = session_->findChannel(c.yChannelName());

        if (xCh && yCh && !xCh->values().empty()) {
          const auto &xVals = xCh->values();
          const auto &yVals = yCh->values();
          size_t total = std::min(xVals.size(), yVals.size());
          size_t step = std::max<size_t>(1, total / maxScatterPoints_);

          std::vector<double> sx, sy;
          sx.reserve(total / step);
          sy.reserve(total / step);

          for (size_t i = 0; i < total; i += step) {
            if (!session_->isRowInCropRange(i)) continue;
            if (std::isnan(xVals[i]) || std::isnan(yVals[i])) continue;
            sx.push_back(xVals[i]);
            sy.push_back(yVals[i]);
          }

          if (!sx.empty()) {
            auto spec = specFromParams(ImPlotMarker_Circle, 2.5f, ImVec4(0.3f, 0.7f, 1.0f, 0.35f), 0.0f);
            ImPlot::PlotScatter("Log Telemetry", sx.data(), sy.data(), static_cast<int>(sx.size()), spec);
          }
        }
      }

      // 2. High-resolution smooth curve rendering
      if (c.points().size() >= 2) {
        constexpr int kCurveDensity = 250;
        std::vector<double> curveX(kCurveDensity);
        std::vector<double> curveY(kCurveDensity);

        double minX = c.points().front().x;
        double maxX = c.points().back().x;
        double dx = (maxX - minX) / (kCurveDensity - 1);

        for (int i = 0; i < kCurveDensity; ++i) {
          double curX = minX + i * dx;
          curveX[i] = curX;
          curveY[i] = c.evaluate(curX);
        }

        ImPlotSpec spec;
        spec.MarkerLineColor = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);
        spec.LineWeight = 2.5f;
        ImPlot::PlotLine("Calibration Function", curveX.data(), curveY.data(), kCurveDensity, spec);
      }

      // 3. Interactive Draggable Breakpoint Nodes
      for (size_t i = 0; i < c.points().size(); ++i) {
        double px = c.points()[i].x;
        double py = c.points()[i].y;

        if (ImPlot::DragPoint(static_cast<int>(1000 + i), &px, &py, ImVec4(1.0f, 0.4f, 0.2f, 1.0f), 5.0f)) {
          c.updatePoint(i, px, py);
        }
      }

      // 4. Live Cursor Ball Tracking
      if (followPlotCursor_ && cursor.active && session_) {
        const auto *xCh = session_->findChannel(c.xChannelName());
        const auto *timeSec = session_->timeSec();

        if (xCh && timeSec && !timeSec->empty()) {
          // Find row closest to cursor.timeSec
          auto it = std::lower_bound(timeSec->begin(), timeSec->end(), cursor.timeSec);
          size_t idx = std::distance(timeSec->begin(), it);
          if (idx < xCh->values().size()) {
            double liveX = xCh->values()[idx];
            if (!std::isnan(liveX)) {
              double liveY = c.evaluate(liveX);

              auto spec = specFromParams(ImPlotMarker_Diamond, 8.0f, ImVec4(0.2f, 1.0f, 0.4f, 1.0f), 2.0f, ImVec4(0, 0, 0, 1));
              ImPlot::PlotScatter("Cursor Point", &liveX, &liveY, 1, spec);

              char tip[64];
              std::snprintf(tip, sizeof(tip), "%.1f %s -> %.1f %s", liveX, c.xUnit().c_str(), liveY, c.yUnit().c_str());
              ImPlot::PlotText(tip, liveX, liveY, ImVec2(0, -15));
            }
          }
        }
      }

      ImPlot::EndPlot();
    }
  }

  nlohmann::json Curve2DPanel::saveState() const
  {
    nlohmann::json j = PlotPanel::saveState();
    j["selectedCurveIdx"] = selectedCurveIdx_;
    j["showScatter"] = showScatter_;
    j["followPlotCursor"] = followPlotCursor_;
    nlohmann::json arr = nlohmann::json::array();
    for (const auto &c : curves_) {
      arr.push_back(c.toJson());
    }
    j["curves"] = arr;
    return j;
  }

  void Curve2DPanel::loadState(const nlohmann::json &state)
  {
    PlotPanel::loadState(state);
    if (state.contains("selectedCurveIdx")) selectedCurveIdx_ = state["selectedCurveIdx"].get<int>();
    if (state.contains("showScatter")) showScatter_ = state["showScatter"].get<bool>();
    if (state.contains("followPlotCursor")) followPlotCursor_ = state["followPlotCursor"].get<bool>();
    if (state.contains("curves") && state["curves"].is_array() && !state["curves"].empty()) {
      curves_.clear();
      for (const auto &cj : state["curves"]) {
        curves_.push_back(core::Curve1D::fromJson(cj));
      }
    }
  }

} // namespace ui