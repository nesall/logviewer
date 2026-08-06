#include "ui/timeseriespanel.h"
#include "ui/ui_helpers.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <utility>

#include "3rdparty/nlohmann/json.hpp"
#include "imgui.h"
#include "implot.h"

namespace ui {

  namespace {
    std::string toLower(const std::string &s) {
      std::string result = s;
      for (char &c : result) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      return result;
    }
  } // namespace

  TimeSeriesPanel::TimeSeriesPanel(std::string title, std::vector<std::string> initialChannelNames)
    : PlotPanel(std::move(title))
  {
    generatePlaceholderData();

    int colorIdx = 0;
    for (const auto &name : initialChannelNames) {
      ChannelState state;
      state.enabled = true;
      state.color = ImPlot::GetColormapColor(colorIdx++);
      channelStates_[name] = state;
    }
  }

  void TimeSeriesPanel::generatePlaceholderData()
  {
    constexpr int kSampleCount = 2000;
    constexpr double kDurationSec = 60.0;

    placeholderTimeSec_.resize(kSampleCount);
    placeholderValue_.resize(kSampleCount);

    for (int i = 0; i < kSampleCount; ++i) {
      double t = (static_cast<double>(i) / (kSampleCount - 1)) * kDurationSec;
      placeholderTimeSec_[i] = t;
      placeholderValue_[i] = 2500.0 + 1500.0 * std::sin(t * 0.3) + 200.0 * std::sin(t * 2.7);
    }
  }

  void TimeSeriesPanel::setSession(const core::LogSession *session)
  {
    session_ = session;
    rebindChannels();
  }

  void TimeSeriesPanel::rebindChannels()
  {
    cachedTimeSec_ = (session_ != nullptr) ? session_->timeSec() : nullptr;

    if (session_ != nullptr) {
      int colorIdx = static_cast<int>(channelStates_.size());
      for (const auto &channel : session_->channels()) {
        const std::string &name = channel.name();

        // Check for key existence BEFORE calling operator[]
        auto it = channelStates_.find(name);
        if (it == channelStates_.end()) {
          ChannelState state;
          state.enabled = false;
          state.color = ImPlot::GetColormapColor(colorIdx++);
          it = channelStates_.emplace(name, state).first;
        }

        // Cache min/max for fast sidebar/legend rendering
        auto &state = it->second;
        const auto &vals = channel.values();
        if (!vals.empty()) {
          auto [minIt, maxIt] = std::minmax_element(vals.begin(), vals.end());
          state.minIdx = static_cast<size_t>(minIt - vals.begin());
          state.maxIdx = static_cast<size_t>(maxIt - vals.begin());
          state.cachedMin = *minIt;
          state.cachedMax = *maxIt;
          state.hasRange = true;
        } else {
          state.hasRange = false;
        }
      }
    }
  }

  nlohmann::json TimeSeriesPanel::saveState() const
  {
    nlohmann::json j = PlotPanel::saveState();
    j["showSidebar"] = showSidebar_;
    j["maxChannelsPerPlot"] = maxChannelsPerPlot_;

    nlohmann::json channelsJson = nlohmann::json::object();
    for (const auto &[name, state] : channelStates_) {
      channelsJson[name] = {
        {"enabled", state.enabled},
        {"color", {state.color.x, state.color.y, state.color.z, state.color.w}},
        { "selectionOrder", state.selectionOrder }
      };
    }
    j["channels"] = channelsJson;
    j["xAxisMin"] = xAxisMin_;
    j["xAxisMax"] = xAxisMax_;
    j["yAxisMin"] = yAxisMin_;
    j["yAxisMax"] = yAxisMax_;
    j["sidebarWidth"] = sidebarWidth_;
    return j;
  }

  void TimeSeriesPanel::loadState(const nlohmann::json &state)
  {
    PlotPanel::loadState(state);
    if (state.contains("showSidebar") && state["showSidebar"].is_boolean()) {
      showSidebar_ = state["showSidebar"].get<bool>();
    }
    if (state.contains("maxChannelsPerPlot") && state["maxChannelsPerPlot"].is_number_integer()) {
      maxChannelsPerPlot_ = state["maxChannelsPerPlot"].get<int>();
    }
    int nofSavedLimits = 0;
    if (state.contains("xAxisMin") && state["xAxisMin"].is_number()) {
      xAxisMin_ = state["xAxisMin"].get<double>();
      nofSavedLimits++;
    }
    if (state.contains("xAxisMax") && state["xAxisMax"].is_number()) {
      xAxisMax_ = state["xAxisMax"].get<double>();
      nofSavedLimits++;
    }
    if (state.contains("yAxisMin") && state["yAxisMin"].is_number()) {
      yAxisMin_ = state["yAxisMin"].get<double>();
      nofSavedLimits++;
    }
    if (state.contains("yAxisMax") && state["yAxisMax"].is_number()) {
      yAxisMax_ = state["yAxisMax"].get<double>();
      nofSavedLimits++;
    }
    bPendingAfterLoad_ = false;
    if (nofSavedLimits == 4)
      bPendingAfterLoad_ = true;

    if (state.contains("sidebarWidth") && state["sidebarWidth"].is_number()) {
      sidebarWidth_ = state["sidebarWidth"].get<float>();
    }

    if (state.contains("channels") && state["channels"].is_object()) {
      for (auto it = state["channels"].begin(); it != state["channels"].end(); ++it) {
        ChannelState cs;
        cs.enabled = it.value().value("enabled", false);
        if (it.value().contains("color") && it.value()["color"].is_array() && it.value()["color"].size() == 4) {
          cs.color = ImVec4(
            it.value()["color"][0],
            it.value()["color"][1],
            it.value()["color"][2],
            it.value()["color"][3]
          );
        }
        if (it.value().contains("selectionOrder")) {
          cs.selectionOrder = it.value()["selectionOrder"].get<uint64_t>();
          nextSelectionOrder_ = std::max(nextSelectionOrder_, cs.selectionOrder + 1);
        }
        channelStates_[it.key()] = cs;
      }
    }
    rebindChannels();
  }

  size_t TimeSeriesPanel::getCursorIndex(double queryTime) const
  {
    if (!cachedTimeSec_ || cachedTimeSec_->empty()) return 0;
    auto it = std::lower_bound(cachedTimeSec_->begin(), cachedTimeSec_->end(), queryTime);
    size_t index = static_cast<size_t>(it - cachedTimeSec_->begin());
    if (index >= cachedTimeSec_->size()) {
      index = cachedTimeSec_->size() - 1;
    } else if (index > 0) {
      double distNext = std::fabs((*cachedTimeSec_)[index] - queryTime);
      double distPrev = std::fabs((*cachedTimeSec_)[index - 1] - queryTime);
      if (distPrev < distNext) {
        index -= 1;
      }
    }
    return index;
  }

  void TimeSeriesPanel::jumpCursorToIndex(size_t index, PlotCursor &cursor, const std::string &label)
  {
    if (cachedTimeSec_ != nullptr && index < cachedTimeSec_->size()) {
      double targetTime = (*cachedTimeSec_)[index];

      // Update active cursor
      cursor.timeSec = targetTime;
      cursor.active = true;

      // Set artifact cursor at the same spot
      artifactCursor_.timeSec = targetTime;
      artifactCursor_.active = true;
      artifactCursor_.label = label;

      // Signal plot renderer to center X-axis
      targetXCenterTime_ = targetTime;
      pendingXAxisCenter_ = true;
    }
  }

  void TimeSeriesPanel::renderHeaderControls(PlotCursor &cursor)
  {
    if (ui::UI::Button(showSidebar_ ? "< Hide Channels" : "> Show Channels")) {
      showSidebar_ = !showSidebar_;
    }

    ImGui::SameLine();
    ImGui::SetNextItemWidth(90.0f);
    if (ImGui::InputInt("Max/Plot", &maxChannelsPerPlot_)) {
      if (maxChannelsPerPlot_ < 1) maxChannelsPerPlot_ = 1;
      if (maxChannelsPerPlot_ > 16) maxChannelsPerPlot_ = 16;
      bPendingAfterLoad_ = true; // ensure ranges are reset on next render
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Maximum channels displayed per subplot before creating a new subplot.");
    }

    ImGui::SameLine();
    ImGui::Text("|");
    ImGui::SameLine();

    renderCropControls(cursor);
  }

  void TimeSeriesPanel::renderCropControls(PlotCursor &cursor)
  {
    if (session_ == nullptr) {
      return;
    }

    bool hasCrop = session_->cropRange().active;

    if (ui::UI::Button("Set [Start]")) {
      double start = cursor.active ? cursor.timeSec : 0.0;
      double end = hasCrop ? session_->cropRange().endSec
        : (cachedTimeSec_ && !cachedTimeSec_->empty() ? cachedTimeSec_->back() : 0.0);
      const_cast<core::LogSession *>(session_)->setCropRange(start, end);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Set crop start marker at current cursor time");

    ImGui::SameLine();
    if (ui::UI::Button("Set [End]")) {
      double start = hasCrop ? session_->cropRange().startSec
        : (cachedTimeSec_ && !cachedTimeSec_->empty() ? cachedTimeSec_->front() : 0.0);
      double end = cursor.active ? cursor.timeSec : 0.0;
      const_cast<core::LogSession *>(session_)->setCropRange(start, end);
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Set crop end marker at current cursor time");

    if (hasCrop) {
      ImGui::SameLine();
      if (ui::UI::Button("Reset Crop")) {
        const_cast<core::LogSession *>(session_)->resetCropRange();
      }

      ImGui::SameLine();
      ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f),
        "[Crop: %.2f s - %.2f s]", session_->cropRange().startSec, session_->cropRange().endSec);
    }
  }

  void TimeSeriesPanel::renderLeftSidebar(PlotCursor &cursor)
  {
    ImGui::Text("Channels");
    ImGui::Separator();

    ImGui::InputTextWithHint("##ChannelFilter", "Filter...", filterText_.data(), filterText_.capacity() + 1,
      ImGuiInputTextFlags_CallbackResize,
      [](ImGuiInputTextCallbackData *data) -> int {
        auto *str = static_cast<std::string *>(data->UserData);
        str->resize(static_cast<size_t>(data->BufTextLen));
        data->Buf = str->data();
        return 0;
      },
      &filterText_);

    ImGui::Separator();

    ImGui::BeginChild("ChannelListScroll", ImVec2(0, 0), false);

    const std::string filterLower = toLower(filterText_);

    if (session_ != nullptr && !session_->channels().empty()) {
      for (const auto &channel : session_->channels()) {
        const std::string &name = channel.name();
        if (!filterLower.empty() && toLower(name).find(filterLower) == std::string::npos) {
          continue;
        }

        auto &state = channelStates_[name];
        ImGui::PushID(name.c_str());

        // [checkbox] color-picker channel-name
        if (ImGui::Checkbox("##check", &state.enabled)) {
          if (state.enabled) {
            state.selectionOrder = nextSelectionOrder_++;
          } else {
            state.selectionOrder = 0;
          }
          bPendingAfterLoad_ = true; // ensure ranges are reset on next render
        }
        ImGui::SameLine();

        ImGui::ColorEdit4("##color", (float *)&state.color,
          ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoAlpha);
        ImGui::SameLine();

        ImGui::TextUnformatted(name.c_str());

        // Display Min/Max in sidebar as clickable text
        if (state.hasRange) {
          ImGui::SameLine();
          ImGui::PushID("jump_min");
          if (ImGui::SmallButton(std::to_string(static_cast<int>(state.cachedMin)).c_str())) {
            jumpCursorToIndex(state.minIdx, cursor);
          }
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Jump to Min (%.2f)", state.cachedMin);
          }
          ImGui::PopID();

          ImGui::SameLine();
          ImGui::PushID("jump_max");
          if (ImGui::SmallButton(std::to_string(static_cast<int>(state.cachedMax)).c_str())) {
            jumpCursorToIndex(state.maxIdx, cursor);
          }
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Jump to Max (%.2f)", state.cachedMax);
          }
          ImGui::PopID();
        }

        ImGui::PopID();
      }
    } else {
      ImGui::TextDisabled("No session loaded");
    }

    ImGui::EndChild();
  }

  void TimeSeriesPanel::renderPlotArea(PlotCursor &cursor)
  {
    const std::vector<double> &timeSec = (cachedTimeSec_ != nullptr && !cachedTimeSec_->empty())
      ? *cachedTimeSec_
      : placeholderTimeSec_;

    bool hasRealData = (session_ != nullptr && cachedTimeSec_ != nullptr);

    // Collect enabled channels
    std::vector<const core::Channel *> activeChannels;
    if (hasRealData) {
      for (const auto &channel : session_->channels()) {
        auto it = channelStates_.find(channel.name());
        if (it != channelStates_.end() && it->second.enabled) {
          activeChannels.push_back(&channel);
        }
      }

      std::sort(activeChannels.begin(), activeChannels.end(),
        [this](const core::Channel *a, const core::Channel *b) {
          return channelStates_[a->name()].selectionOrder < channelStates_[b->name()].selectionOrder;
        });
    }

    int numActive = static_cast<int>(activeChannels.size());
    int maxPerPlot = std::max(1, maxChannelsPerPlot_);
    int subplotCount = (numActive > 0) ? ((numActive + maxPerPlot - 1) / maxPerPlot) : 1;

    // Calculate new X limits BEFORE starting subplots if a jump was requested
    double newMinX = 0.0;
    double newMaxX = 0.0;
    if (pendingXAxisCenter_) {
      // Pick a reasonable view span (e.g., 20 seconds, or 10% of total log duration)
      double totalDuration = timeSec.back() - timeSec.front();
      double viewSpan = (totalDuration > 0.0) ? (totalDuration * 0.10) : 20.0;
      if (viewSpan < 1.0) viewSpan = 1.0; // clamp minimum span

      newMinX = targetXCenterTime_ - (viewSpan * 0.5);
      newMaxX = targetXCenterTime_ + (viewSpan * 0.5);
    }

    bool renderOverlay = false;
    ImVec2 lastPlotPos, lastPlotSize;

    if (ImPlot::BeginSubplots("##TimeSeriesSubplots", subplotCount, 1, ImVec2(-1, -1), ImPlotSubplotFlags_LinkAllX | ImPlotSubplotFlags_NoLegend)) {
      for (int plotIdx = 0; plotIdx < subplotCount; ++plotIdx) {
        std::string plotID = "##Plot_" + std::to_string(plotIdx);

        // Set next axis limits for the plot before BeginPlot
        if (pendingXAxisCenter_) {
          ImPlot::SetNextAxisLimits(ImAxis_X1, newMinX, newMaxX, ImGuiCond_Always);
        } else if (bPendingAfterLoad_) {
          ImPlot::SetNextAxisLimits(ImAxis_X1, xAxisMin_, xAxisMax_, ImGuiCond_Always);
          ImPlot::SetNextAxisLimits(ImAxis_Y1, yAxisMin_, yAxisMax_, ImGuiCond_Always);
          bPendingAfterLoad_ = false;
        }

        if (ImPlot::BeginPlot(plotID.c_str(), ImVec2(-1, -1))) {
          ImPlot::SetupAxes("Time (s)", "Normalized", ImPlotAxisFlags_None, ImPlotAxisFlags_NoTickLabels);

          auto plotLimits = ImPlot::GetPlotLimits();
          xAxisMin_ = plotLimits.X.Min;
          xAxisMax_ = plotLimits.X.Max;
          yAxisMin_ = plotLimits.Y.Min;
          yAxisMax_ = plotLimits.Y.Max;

          if (hasRealData && numActive > 0) {
            int startIdx = plotIdx * maxPerPlot;
            int endIdx = std::min(startIdx + maxPerPlot, numActive);

            for (int i = startIdx; i < endIdx; ++i) {
              const auto *ch = activeChannels[i];
              const auto &rawVals = ch->values();
              if (rawVals.empty()) continue;

              double minVal = channelStates_[ch->name()].cachedMin;
              double maxVal = channelStates_[ch->name()].cachedMax;
              double range = maxVal - minVal;

              std::vector<double> normVals(rawVals.size());
              for (size_t s = 0; s < rawVals.size(); ++s) {
                normVals[s] = (range > 1e-6) ? ((rawVals[s] - minVal) / range) : 0.5;
              }

              ImPlotSpec spec;
              spec.LineColor = channelStates_[ch->name()].color;
              ImPlot::PlotLine(ch->name().c_str(), timeSec.data(), normVals.data(), static_cast<int>(timeSec.size()), spec);

              if (session_ != nullptr && session_->cropRange().active) {
                double cropStart = session_->cropRange().startSec;
                double cropEnd = session_->cropRange().endSec;

                ImPlot::DragLineX(101, &cropStart, ImVec4(0.2f, 0.8f, 1.0f, 0.8f), 1.5f, ImPlotDragToolFlags_NoInputs);
                ImPlot::TagX(cropStart, ImVec4(0.2f, 0.8f, 1.0f, 0.8f), "Start");

                ImPlot::DragLineX(102, &cropEnd, ImVec4(0.2f, 0.8f, 1.0f, 0.8f), 1.5f, ImPlotDragToolFlags_NoInputs);
                ImPlot::TagX(cropEnd, ImVec4(0.2f, 0.8f, 1.0f, 0.8f), "End");
              }
            }
          } else {
            ImPlot::PlotLine("Placeholder", timeSec.data(), placeholderValue_.data(),
              static_cast<int>(timeSec.size()));
          }

          if (ImPlot::IsPlotHovered()) {
            cursor.timeSec = ImPlot::GetPlotMousePos().x;
            cursor.active = true;
          }

          if (artifactCursor_.active) {
            double artifactX = artifactCursor_.timeSec;
            // Draw a dashed or dimmed vertical line
            ImPlot::DragLineX(1, &artifactX, artifactCursor_.color, 1.0f, ImPlotDragToolFlags_NoInputs);

            // Optional: Tag annotation at the top of the plot
            if (!artifactCursor_.label.empty()) {
              ImPlot::TagX(artifactCursor_.timeSec, artifactCursor_.color, "%s", artifactCursor_.label.c_str());
            }
          }

          if (cursor.active) {
            double markerX = cursor.timeSec;
            ImPlot::DragLineX(0, &markerX, ImVec4(1.0f, 1.0f, 0.0f, 0.8f), 1.0f, ImPlotDragToolFlags_NoInputs);
          }

          if (ImPlot::IsPlotHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            artifactCursor_.active = false;
          }

          if (plotIdx == subplotCount - 1) {
            lastPlotPos = ImPlot::GetPlotPos();
            lastPlotSize = ImPlot::GetPlotSize();
            renderOverlay = true;
          }

          ImPlot::EndPlot();
        }
      }

      ImPlot::EndSubplots();
    }

    // Reset pending jump flag after applying limits to all subplots
    pendingXAxisCenter_ = false;

    // Common overlay window flags
    const ImGuiWindowFlags overlayFlags = ImGuiWindowFlags_NoDecoration |
      ImGuiWindowFlags_AlwaysAutoResize |
      ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoFocusOnAppearing |
      ImGuiWindowFlags_NoNav |
      ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoDocking;

    // --- Bottom-Left Legend Overlay (Channel Min/Max Range Summary) ---
    if (renderOverlay && hasRealData && !activeChannels.empty()) {
      ImVec2 bottomLeftPos = ImVec2(lastPlotPos.x + 10.0f, lastPlotPos.y + lastPlotSize.y - 10.0f);

      ImGui::SetNextWindowPos(bottomLeftPos, ImGuiCond_Always, ImVec2(0.0f, 1.0f));
      ImGui::SetNextWindowBgAlpha(0.75f);

      std::string rangeWindowID = "##RangeOverlay_" + title();
      bool open = true;

      if (ImGui::Begin(rangeWindowID.c_str(), &open, overlayFlags)) {
        ImGui::TextDisabled("Channel Ranges (Click to Jump)");
        ImGui::Separator();

        for (const auto *channel : activeChannels) {
          const auto &st = channelStates_[channel->name()];
          ImGui::PushID(channel->name().c_str());

          ImGui::TextColored(st.color, "%s:", channel->name().c_str());
          ImGui::SameLine();

          // Interactive Min label
          char minBuf[32];
          std::snprintf(minBuf, sizeof(minBuf), "[%.1f", st.cachedMin);
          ImGui::TextUnformatted(minBuf);
          if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("Click to jump to Min (%.2f)", st.cachedMin);
          }
          if (ImGui::IsItemClicked()) {
            jumpCursorToIndex(st.minIdx, cursor);
          }

          ImGui::SameLine(0, 2.0f);
          ImGui::TextUnformatted("-");
          ImGui::SameLine(0, 2.0f);

          // Interactive Max label
          char maxBuf[32];
          std::snprintf(maxBuf, sizeof(maxBuf), "%.1f]", st.cachedMax);
          ImGui::TextUnformatted(maxBuf);
          if (ImGui::IsItemHovered()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
            ImGui::SetTooltip("Click to jump to Max (%.2f)", st.cachedMax);
          }
          if (ImGui::IsItemClicked()) {
            jumpCursorToIndex(st.maxIdx, cursor);
          }

          ImGui::PopID();
        }
      }
      ImGui::End();
    }

    // --- Bottom-Right Legend Overlay (Cursor Values) ---
    if (renderOverlay && cursor.active) {
      size_t cursorIdx = getCursorIndex(cursor.timeSec);
      ImVec2 bottomRightPos = ImVec2(lastPlotPos.x + lastPlotSize.x - 10.0f, lastPlotPos.y + lastPlotSize.y - 10.0f);

      ImGui::SetNextWindowPos(bottomRightPos, ImGuiCond_Always, ImVec2(1.0f, 1.0f)); // Anchor bottom-right
      ImGui::SetNextWindowBgAlpha(0.85f);

      std::string overlayWindowID = "##CursorOverlay_" + title();
      bool open = true;

      if (ImGui::Begin(overlayWindowID.c_str(), &open, overlayFlags)) {
        ImGui::TextDisabled("Cursor @ %.3f s", cursor.timeSec);

        if (artifactCursor_.active) {
          double deltaTime = cursor.timeSec - artifactCursor_.timeSec;
          ImGui::SameLine();
          ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(dt: %+.3f s)", deltaTime);
        }

        ImGui::Separator();

        if (hasRealData) {
          if (!activeChannels.empty()) {
            for (const auto *channel : activeChannels) {
              const auto &st = channelStates_[channel->name()];
              double val = (cursorIdx < channel->values().size()) ? channel->values()[cursorIdx] : 0.0;

              ImGui::TextColored(st.color, "%s:", channel->name().c_str());
              ImGui::SameLine();
              if (!channel->unit().empty()) {
                ImGui::Text("%.2f %s", val, channel->unit().c_str());
              } else {
                ImGui::Text("%.2f", val);
              }
            }
          } else {
            ImGui::TextDisabled("No active channels");
          }
        } else {
          ImGui::Text("%.2f", placeholderValue_[cursorIdx % placeholderValue_.size()]);
        }
      }
      ImGui::End();
    }
  }

  void TimeSeriesPanel::render(PlotCursor &cursor)
  {
    std::string windowLabel = "Time Series###" + title();
    ImGui::Begin(windowLabel.c_str(), &open_);

    renderHeaderControls(cursor);
    ImGui::Separator();

    if (showSidebar_) {
      // 1. Enforce min/max sidebar width bounds
      const float minWidth = 120.0f;
      const float maxWidth = ImGui::GetContentRegionAvail().x - 150.0f; // Leave space for plot
      sidebarWidth_ = std::clamp(sidebarWidth_, minWidth, std::max(minWidth, maxWidth));

      // 2. Render Sidebar Child Window
      ImGui::BeginChild("SidebarChild", ImVec2(sidebarWidth_, 0.0f), true);
      renderLeftSidebar(cursor);
      ImGui::EndChild();

      ImGui::SameLine();

      // 3. Render Resizable Splitter
      const float splitterThickness = 4.0f;
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_SeparatorActive));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_SeparatorHovered));

      ImGui::Button("##Splitter", ImVec2(splitterThickness, -1.0f));

      ImGui::PopStyleColor(3);

      // Set resize cursor on hover/active
      if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
      }

      // Handle drag movement
      if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        sidebarWidth_ += ImGui::GetIO().MouseDelta.x;
      }

      ImGui::SameLine();
    }

    // 4. Render Main Plot Region with remaining width
    ImGui::BeginChild("PlotChild", ImVec2(0.0f, 0.0f), false);
    renderPlotArea(cursor);
    ImGui::EndChild();

    ImGui::End();
  }

} // namespace ui