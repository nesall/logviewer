#include "ui/scatterpanel.h"
#include "ui/ui_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

#include "3rdparty/nlohmann/json.hpp"
#include "imgui.h"
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

    rebuildColorData();
  }

  nlohmann::json ScatterPanel::saveState() const
  {
    nlohmann::json j = PlotPanel::saveState();
    j["xChannel"] = selectedXChannel_;
    j["yChannel"] = selectedYChannel_;
    j["colorChannel"] = selectedColorChannel_;
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
    if (changed) {
      rebindChannels(); // also rebuilds color data
    } else {
      rebuildColorData();
    }
  }

  void ScatterPanel::rebuildColorData()
  {
    cachedPointColors_.clear();

    if (selectedColorChannel_.empty() || session_ == nullptr || cachedXValues_ == nullptr) {
      return; // "None" selected, or no base data to color yet
    }

    const core::Channel *colorChannel = session_->findChannel(selectedColorChannel_);
    if (colorChannel == nullptr || colorChannel->values().size() != cachedXValues_->size()) {
      return; // channel missing or misaligned -- fall back to flat color
    }

    const std::vector<double> &colorValues = colorChannel->values();

    colorChannelMin_ = colorValues.front();
    colorChannelMax_ = colorValues.front();
    for (double v : colorValues) {
      if (v < colorChannelMin_) colorChannelMin_ = v;
      if (v > colorChannelMax_) colorChannelMax_ = v;
    }

    const double range = colorChannelMax_ - colorChannelMin_;
    cachedPointColors_.resize(colorValues.size());
    for (size_t i = 0; i < colorValues.size(); ++i) {
      float t = (range > 1e-9) ? static_cast<float>((colorValues[i] - colorChannelMin_) / range)
        : 0.5f;
      ImVec4 color = ImPlot::SampleColormap(t, ImPlotColormap_Viridis);
      cachedPointColors_[i] = ImGui::ColorConvertFloat4ToU32(color);
    }
  }

  void ScatterPanel::render(PlotCursor &cursor)
  {
    std::string legendLabel = selectedXChannel_ + " vs " + selectedYChannel_;
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

    const bool haveColorData =
      !cachedPointColors_.empty() && cachedPointColors_.size() == xValues.size();

    if (haveColorData) {
      ImPlot::PushColormap(ImPlotColormap_Viridis);
      ImPlot::ColormapScale(selectedColorChannel_.c_str(), colorChannelMin_, colorChannelMax_, ImVec2(80, -1));
      ImGui::SameLine();
    }

    if (ImPlot::BeginPlot("##Scatter", ImVec2(-1, -1))) {
      ImPlot::SetupAxes(xAxisLabel.c_str(), yAxisLabel.c_str());

      if (haveColorData) {
        ImPlotSpec spec;
        spec.MarkerSize = 2.f;
        spec.MarkerFillColors = cachedPointColors_.data();
        ImPlot::PlotScatter(legendLabel.c_str(), xValues.data(), yValues.data(), static_cast<int>(xValues.size()), spec);
      } else {
        ImPlot::PlotScatter(legendLabel.c_str(), xValues.data(), yValues.data(), static_cast<int>(xValues.size()));
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

      ImPlot::EndPlot();
    }

    if (haveColorData) {
      ImPlot::PopColormap();
    }

    ImGui::End();
  }

} // namespace ui