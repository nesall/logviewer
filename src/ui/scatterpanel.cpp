#include "ui/scatterpanel.h"
#include "ui/ui_helpers.h"
#include "utils/utils.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <numeric>
#include <utility>

#include "3rdparty/nlohmann/json.hpp"
#include "3rdparty/IconsFontAwesome7.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "implot.h"

namespace ui {

  ScatterPanel::ScatterPanel(std::string title, std::string initialXChannel,
    std::string initialYChannel)
    : PlotPanel(std::move(title)),
    selectedXChannel_(std::move(initialXChannel)),
    selectedYChannel_(std::move(initialYChannel))
  {
    generatePlaceholderData();
  }

  void ScatterPanel::generatePlaceholderData()
  {
    constexpr int kSampleCount = 500;
    placeholderXValues_.resize(kSampleCount);
    placeholderYValues_.resize(kSampleCount);

    for (int i = 0; i < kSampleCount; ++i) {
      double t = static_cast<double>(i) / (kSampleCount - 1) * 60.0;
      placeholderXValues_[i] = 2500.0 + 1500.0 * std::sin(t * 0.3) + 200.0 * std::sin(t * 2.7);
      placeholderYValues_[i] = 50.0 + 30.0 * std::sin(t * 0.25 + 1.0);
    }
  }

  void ScatterPanel::setSession(const core::LogSession *session)
  {
    session_ = session;
    rebindChannels();
  }

  void ScatterPanel::rebindChannels()
  {
    cachedTimeSec_ = nullptr;
    cachedXValues_ = nullptr;
    cachedYValues_ = nullptr;
    cachedXUnit_.clear();
    cachedYUnit_.clear();
    cachedZUnit_.clear();

    if (session_ == nullptr) {
      rebuildColorData();
      return;
    }

    const core::Channel *xChannel = session_->findChannel(selectedXChannel_);
    const core::Channel *yChannel = session_->findChannel(selectedYChannel_);
    const std::vector<double> *timeSec = session_->timeSec();

    if (xChannel == nullptr || yChannel == nullptr || timeSec == nullptr || timeSec->empty()) {
      rebuildColorData();
      return;
    }
    if (xChannel->values().size() != timeSec->size() ||
      yChannel->values().size() != timeSec->size()) {
      rebuildColorData();
      return;
    }

    cachedTimeSec_ = timeSec;
    cachedXValues_ = &xChannel->values();
    cachedYValues_ = &yChannel->values();
    cachedXUnit_ = xChannel->unit();
    cachedYUnit_ = yChannel->unit();
    cachedZUnit_ = selectedColorChannel_.empty() ? "" : session_->findChannel(selectedColorChannel_)->unit();
    rebuildColorData();
  }

  nlohmann::json ScatterPanel::saveState() const
  {
    nlohmann::json j = PlotPanel::saveState();
    j["xChannel"] = selectedXChannel_;
    j["yChannel"] = selectedYChannel_;
    j["colorChannel"] = selectedColorChannel_;
    j["enableColorFilter"] = enableColorFilter_;
    j["colorFilterMin"] = colorFilterMin_;
    j["colorFilterMax"] = colorFilterMax_;
    return j;
  }

  void ScatterPanel::loadState(const nlohmann::json &state)
  {
    PlotPanel::loadState(state);
    bool changed = false;
    if (state.contains("xChannel") && state["xChannel"].is_string()) {
      selectedXChannel_ = state["xChannel"].get<std::string>();
      changed = true;
    }
    if (state.contains("yChannel") && state["yChannel"].is_string()) {
      selectedYChannel_ = state["yChannel"].get<std::string>();
      changed = true;
    }
    if (state.contains("colorChannel") && state["colorChannel"].is_string()) {
      selectedColorChannel_ = state["colorChannel"].get<std::string>();
    }
    if (state.contains("enableColorFilter") && state["enableColorFilter"].is_boolean()) {
      enableColorFilter_ = state["enableColorFilter"].get<bool>();
    }
    if (state.contains("colorFilterMin") && state["colorFilterMin"].is_number()) {
      colorFilterMin_ = state["colorFilterMin"].get<double>();
    }
    if (state.contains("colorFilterMax") && state["colorFilterMax"].is_number()) {
      colorFilterMax_ = state["colorFilterMax"].get<double>();
    }
    if (changed) {
      rebindChannels(); // also rebuilds color data
    } else {
      rebuildColorData();
    }
  }

  void ScatterPanel::rebuildColorData()
  {
    filteredXValues_.clear();
    filteredYValues_.clear();
    filteredPointColors_.clear();

    if (selectedColorChannel_.empty() || session_ == nullptr || cachedXValues_ == nullptr || cachedYValues_ == nullptr) {
      return;
    }

    const core::Channel *colorChannel = session_->findChannel(selectedColorChannel_);
    if (colorChannel == nullptr || colorChannel->values().size() != cachedXValues_->size()) {
      return;
    }

    const std::vector<double> &colorValues = colorChannel->values();
    const std::vector<double> &xValues = *cachedXValues_;
    const std::vector<double> &yValues = *cachedYValues_;

    double normMin = colorValues.front();
    double normMax = colorValues.front();

    for (double v : colorValues) {
      if (std::isnan(v)) continue;
      if (v < normMin) normMin = v;
      if (v > normMax) normMax = v;
    }

    colorChannelMin_ = normMin;
    colorChannelMax_ = normMax;

    if (!colorFilterMinMaxSet_) {
      colorFilterMin_ = normMin;
      colorFilterMax_ = normMax;
      colorFilterMinMaxSet_ = true;
    }

    const double range = normMax - normMin;
    size_t totalPoints = colorValues.size();

    filteredXValues_.reserve(totalPoints);
    filteredYValues_.reserve(totalPoints);
    filteredPointColors_.reserve(totalPoints);

    for (size_t i = 0; i < totalPoints; ++i) {
      double xVal = xValues[i];
      double yVal = yValues[i];
      double zVal = colorValues[i];

      if (std::isnan(xVal) || std::isnan(yVal) || std::isnan(zVal)) continue;

      if (enableColorFilter_) {
        if (zVal < colorFilterMin_ || zVal > colorFilterMax_) {
          continue;
        }
      }

      float t = (range > 1e-9) ? static_cast<float>((zVal - normMin) / range) : 0.5f;
      ImVec4 color = ImPlot::SampleColormap(t, ImPlotColormap_Viridis);

      filteredXValues_.push_back(xVal);
      filteredYValues_.push_back(yVal);
      filteredPointColors_.push_back(ImGui::ColorConvertFloat4ToU32(color));
    }
    computeFilteredStats();
  }

  void ScatterPanel::render(PlotCursor &cursor)
  {
    std::string legendLabel = std::string{ ICON_FA_CHART_SIMPLE } + " " + selectedXChannel_ + " vs " + selectedYChannel_;
    if (!selectedColorChannel_.empty()) {
      legendLabel += " vs " + selectedColorChannel_;
    }
    std::string windowLabel = legendLabel + "###" + title();
    ImGui::Begin(windowLabel.c_str(), &open_);

    const bool haveSession = (session_ != nullptr && !session_->channels().empty());

    if (haveSession) {
      if (ui::UI::Button("Channels...", {}, {}, "Open channel selection popup")) {
        ImGui::OpenPopup(ui::popups::ScatterPanelChannels);
      }

      ImGui::SameLine();
      ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
      ImGui::SameLine();
      ImGui::Checkbox("Overlay Stats", &showStatsOverlay_);

      if (!selectedColorChannel_.empty()) {
        ImGui::SameLine();
        ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
        ImGui::SameLine();

        if (ImGui::Checkbox("Filter Z", &enableColorFilter_)) {
          rebuildColorData();
        }

        if (enableColorFilter_) {
          ImGui::SameLine();
          ImGui::SetNextItemWidth(180.0f);

          // Dual-handle min/max range control
          float leftVal = static_cast<float>(colorFilterMin_);
          float rightVal = static_cast<float>(colorFilterMax_);
          if (ImGui::DragFloatRange2("##ZRange", &leftVal, &rightVal, 1.0f,
            static_cast<float>(colorChannelMin_),
            static_cast<float>(colorChannelMax_),
            "Min: %.0f", "Max: %.0f")) {
            colorFilterMin_ = static_cast<double>(leftVal);
            colorFilterMax_ = static_cast<double>(rightVal);
            rebuildColorData();
          }
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Filter %s values between Min and Max", selectedColorChannel_.c_str());
          }
        }
      }

      if (ImGui::BeginPopup(ui::popups::ScatterPanelChannels)) {
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::BeginCombo("X Axis", selectedXChannel_.c_str())) {
          for (const auto &channel : session_->channels()) {
            bool isSelected = (channel.name() == selectedXChannel_);
            if (ImGui::Selectable(channel.name().c_str(), isSelected)) {
              selectedXChannel_ = channel.name();
              rebindChannels();
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
          }
          ImGui::EndCombo();
        }

        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::BeginCombo("Y Axis", selectedYChannel_.c_str())) {
          for (const auto &channel : session_->channels()) {
            bool isSelected = (channel.name() == selectedYChannel_);
            if (ImGui::Selectable(channel.name().c_str(), isSelected)) {
              selectedYChannel_ = channel.name();
              rebindChannels();
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
          }
          ImGui::EndCombo();
        }

        ImGui::SetNextItemWidth(180.0f);
        const char *colorComboLabel = selectedColorChannel_.empty() ? "None" : selectedColorChannel_.c_str();
        if (ImGui::BeginCombo("Color (Z)", colorComboLabel)) {
          bool noneSelected = selectedColorChannel_.empty();
          if (ImGui::Selectable("None", noneSelected)) {
            selectedColorChannel_.clear();
            rebuildColorData();
          }
          if (noneSelected) ImGui::SetItemDefaultFocus();

          for (const auto &channel : session_->channels()) {
            bool isSelected = (channel.name() == selectedColorChannel_);
            if (ImGui::Selectable(channel.name().c_str(), isSelected)) {
              selectedColorChannel_ = channel.name();
              rebuildColorData();
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
          }
          ImGui::EndCombo();
        }

        ImGui::EndPopup();
      }
    } else {
      ImGui::TextDisabled("No log loaded yet.");
    }

    const bool haveRealData = (cachedXValues_ != nullptr && cachedYValues_ != nullptr);
    const std::vector<double> &xValues = haveRealData ? *cachedXValues_ : placeholderXValues_;
    const std::vector<double> &yValues = haveRealData ? *cachedYValues_ : placeholderYValues_;

    if (haveSession && !haveRealData) {
      ImGui::TextDisabled("One or both channels not found in this log.");
    }

    std::string xAxisLabel = selectedXChannel_;
    if (!cachedXUnit_.empty()) {
      xAxisLabel += " (" + cachedXUnit_ + ")";
    }
    std::string yAxisLabel = selectedYChannel_;
    if (!cachedYUnit_.empty()) {
      yAxisLabel += " (" + cachedYUnit_ + ")";
    }

    const bool haveColorData = !filteredPointColors_.empty() && filteredPointColors_.size() == filteredXValues_.size();

    if (haveColorData) {
      ImPlot::PushColormap(ImPlotColormap_Viridis);
      ImPlot::ColormapScale(selectedColorChannel_.c_str(), colorChannelMin_, colorChannelMax_, ImVec2(80, -1));
      ImGui::SameLine();
    }

    if (ImPlot::BeginPlot("##Scatter", ImVec2(-1, -1))) {
      ImPlot::SetupAxes(xAxisLabel.c_str(), yAxisLabel.c_str());

      const bool haveFiltered = !filteredPointColors_.empty();
      const double *plotX = haveFiltered ? filteredXValues_.data() : xValues.data();
      const double *plotY = haveFiltered ? filteredYValues_.data() : yValues.data();
      int plotCount = haveFiltered ? static_cast<int>(filteredXValues_.size()) : static_cast<int>(xValues.size());

      if (haveColorData) {
        const uint32_t *plotColors = filteredPointColors_.data();
        ImPlotSpec spec;
        spec.Marker = ImPlotMarker_Circle;
        spec.MarkerSize = 1.f;
        spec.MarkerFillColors = filteredPointColors_.data();
        spec.MarkerLineColors = filteredPointColors_.data();
        ImPlot::PlotScatter(legendLabel.c_str(), plotX, plotY, plotCount, spec);
      } else {
        ImPlot::PlotScatter(legendLabel.c_str(), plotX, plotY, plotCount);
      }


      // Highlight the sample nearest the shared cursor time
      if (haveRealData && cursor.active && cachedTimeSec_ != nullptr && !cachedTimeSec_->empty()) {
        auto it = std::lower_bound(cachedTimeSec_->begin(), cachedTimeSec_->end(), cursor.timeSec);
        size_t index = static_cast<size_t>(it - cachedTimeSec_->begin());
        if (index >= cachedTimeSec_->size()) {
          index = cachedTimeSec_->size() - 1;
        } else if (index > 0) {
          double distNext = std::fabs((*cachedTimeSec_)[index] - cursor.timeSec);
          double distPrev = std::fabs((*cachedTimeSec_)[index - 1] - cursor.timeSec);
          if (distPrev < distNext) {
            index -= 1;
          }
        }

        double highlightX = xValues[index];
        double highlightY = yValues[index];
        ImPlotSpec cursorSpec;
        cursorSpec.Marker = ImPlotMarker_Circle;
        cursorSpec.MarkerSize = 8.0f;
        cursorSpec.MarkerFillColor = ImVec4(1.0f, 1.0f, 0.0f, 1.0f);
        cursorSpec.LineWeight = 2.0f;
        ImPlot::PlotScatter("Cursor", &highlightX, &highlightY, 1, cursorSpec);
      }

      if (showStatsOverlay_ && stats_.valid) {
        ImVec2 plotPos = ImPlot::GetPlotPos();
        ImVec2 plotSize = ImPlot::GetPlotSize();
        ImVec2 overlayPos = ImVec2(plotPos.x + 10.0f, plotPos.y + plotSize.y - 10.0f);

        ImGui::SetNextWindowPos(overlayPos, ImGuiCond_Always, ImVec2(0.0f, 1.0f)); // Anchor bottom-left
        ImGui::SetNextWindowBgAlpha(0.75f);

        const ImGuiWindowFlags overlayFlags = ImGuiWindowFlags_NoDecoration |
          ImGuiWindowFlags_AlwaysAutoResize |
          ImGuiWindowFlags_NoSavedSettings |
          ImGuiWindowFlags_NoFocusOnAppearing |
          ImGuiWindowFlags_NoNav |
          ImGuiWindowFlags_NoMove |
          ImGuiWindowFlags_NoDocking;

        std::string windowId = "##ScatterStatsOverlay_" + title();
        if (ImGui::Begin(windowId.c_str(), nullptr, overlayFlags)) {
          ImGui::TextDisabled("Filtered Stats (%zu pts)", stats_.pointCount);
          ImGui::Separator();

          if (stats_.timeInZoneSec > 0.0) {
            ImGui::Text("Dwell Time: %.2f s", stats_.timeInZoneSec);
          }

          auto xUnitSanitized = utils::str::sanitizeToUtf8(cachedXUnit_);
          auto yUnitSanitized = utils::str::sanitizeToUtf8(cachedYUnit_);
          if (!selectedColorChannel_.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Peak %s: %.1f", selectedColorChannel_.c_str(), stats_.maxZ);
            ImGui::SameLine();
            ImGui::TextDisabled("(@ %.0f / %.1f)", stats_.maxXAtMaxZ, stats_.maxYAtMaxZ);

            ImGui::Text("Mean %s: %.1f", selectedColorChannel_.c_str(), stats_.avgZ);

            auto zUnitSanitized = utils::str::sanitizeToUtf8(cachedZUnit_);
            ImGui::Text("Centroid: %.0f %s | %.1f %s | %.2f %s",
              stats_.avgX, xUnitSanitized.c_str(),
              stats_.avgY, yUnitSanitized.c_str(),
              stats_.avgZ, zUnitSanitized.c_str()
            );
          } else {
            ImGui::Text("Centroid: %.0f %s | %.1f %s",
              stats_.avgX, xUnitSanitized.c_str(),
              stats_.avgY, yUnitSanitized.c_str()
            );
          }

          ImGui::Text("X/Y Correlation (r): %+.2f", stats_.correlationXY);

          ImGui::End();
        }
      }


      ImPlot::EndPlot();
    }

    if (haveColorData) {
      ImPlot::PopColormap();
    }

    ImGui::End();
  }

  void ScatterPanel::computeFilteredStats()
  {
    stats_ = ScatterStats{};

    const bool haveFiltered = enableColorFilter_ && !filteredPointColors_.empty();
    const double *xVals = haveFiltered ? filteredXValues_.data() : (cachedXValues_ ? cachedXValues_->data() : nullptr);
    const double *yVals = haveFiltered ? filteredYValues_.data() : (cachedYValues_ ? cachedYValues_->data() : nullptr);
    size_t count = haveFiltered ? filteredXValues_.size() : (cachedXValues_ ? cachedXValues_->size() : 0);

    if (!xVals || !yVals || count == 0) return;

    stats_.pointCount = count;
    stats_.valid = true;

    // Dwell Time calculation based on log duration
    if (cachedTimeSec_ && cachedTimeSec_->size() >= 2) {
      double totalLogDuration = cachedTimeSec_->back() - cachedTimeSec_->front();
      double dt = totalLogDuration / static_cast<double>(cachedTimeSec_->size());
      stats_.timeInZoneSec = count * dt;
    }

    const core::Channel *colorCh = session_ ? session_->findChannel(selectedColorChannel_) : nullptr;
    const double *zVals = colorCh ? colorCh->values().data() : nullptr;

    double sumX = 0.0, sumY = 0.0, sumZ = 0.0;
    double maxZ = -std::numeric_limits<double>::infinity();
    double maxXAtMaxZ = 0.0, maxYAtMaxZ = 0.0;

    for (size_t i = 0; i < count; ++i) {
      double x = xVals[i];
      double y = yVals[i];
      sumX += x;
      sumY += y;

      if (zVals) {
        double z = zVals[i];
        sumZ += z;
        if (z > maxZ) {
          maxZ = z;
          maxXAtMaxZ = x;
          maxYAtMaxZ = y;
        }
      }
    }

    stats_.avgX = sumX / count;
    stats_.avgY = sumY / count;
    if (zVals) {
      stats_.avgZ = sumZ / count;
      stats_.maxZ = maxZ;
      stats_.maxXAtMaxZ = maxXAtMaxZ;
      stats_.maxYAtMaxZ = maxYAtMaxZ;
    }

    // Pearson Correlation Coefficient (r) calculation between X and Y
    double varX = 0.0, varY = 0.0, covXY = 0.0;
    for (size_t i = 0; i < count; ++i) {
      double dx = xVals[i] - stats_.avgX;
      double dy = yVals[i] - stats_.avgY;
      covXY += dx * dy;
      varX += dx * dx;
      varY += dy * dy;
    }

    if (varX > 1e-9 && varY > 1e-9) {
      stats_.correlationXY = covXY / std::sqrt(varX * varY);
    }
  }

} // namespace ui