#include "ui/timeseriespanel.h"
#include "ui/ui_helpers.h"
#include "engine/decimator.h"
#include "utils/utils.h"
#include "io/csvexporter.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <utility>

#include "3rdparty/nlohmann/json.hpp"
#include "3rdparty/IconsFontAwesome7.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "implot.h"

namespace ui {

  namespace {
    constexpr int kMinDecimationTargetPoints = 64;
    constexpr int kPointsPerPixel = 2; // ~Nyquist for a 1px-wide line
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
        {"selectionOrder", state.selectionOrder}
      };
    }
    j["channels"] = channelsJson;
    j["xAxisMin"] = xAxisMin_;
    j["xAxisMax"] = xAxisMax_;
    j["yAxisMin"] = yAxisMin_;
    j["yAxisMax"] = yAxisMax_;
    j["sidebarWidth"] = sidebarWidth_;
    j["showStatsOverlay"] = showStatsOverlay_;
    j["showMinimap"] = showMinimap_;
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
    if (state.contains("showStatsOverlay") && state["showStatsOverlay"].is_boolean()) {
      showStatsOverlay_ = state["showStatsOverlay"].get<bool>();
    }
    if (state.contains("showMinimap") && state["showMinimap"].is_boolean()) {
      showMinimap_ = state["showMinimap"].get<bool>();
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
          nextSelectionOrder_ = (std::max)(nextSelectionOrder_, cs.selectionOrder + 1);
        }
        channelStates_[it.key()] = cs;
      }
    }
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

  void TimeSeriesPanel::ensureDecimatedCache(ChannelState &state, const core::Channel &channel, double xMin, double xMax, float widthPx)
  {
    constexpr double kEpsilon = 1e-9;

    const uint64_t currentRevision = session_ ? session_->revision() : 0;

    const bool cacheValid =
      state.hasDecimatedCache &&
      state.decimatedForSession == session_ &&
      state.decimatedForRevision == currentRevision &&
      std::fabs(state.decimatedForXMin - xMin) < kEpsilon &&
      std::fabs(state.decimatedForXMax - xMax) < kEpsilon &&
      std::fabs(state.decimatedForWidthPx - widthPx) < 1.0f; // sub-pixel churn isn't worth recomputing for

    if (cacheValid) return;

    state.decimatedForSession = session_;
    state.decimatedForRevision = currentRevision;
    state.decimatedForXMin = xMin;
    state.decimatedForXMax = xMax;
    state.decimatedForWidthPx = widthPx;
    state.hasDecimatedCache = true;

    const auto &rawVals = channel.values();
    if (cachedTimeSec_ == nullptr || rawVals.empty()) {
      state.decimatedX.clear();
      state.decimatedYNorm.clear();
      return;
    }

    const int targetPoints = (std::max)(kMinDecimationTargetPoints, static_cast<int>(widthPx) * kPointsPerPixel);

    engine::DecimatedSeries series = engine::Decimator::decimate(*cachedTimeSec_, rawVals, xMin, xMax, targetPoints);

    // Normalize AFTER decimation -- touches only the small decimated
    // buffer per frame instead of the full multi-hour channel.
    const double range = state.cachedMax - state.cachedMin;
    std::vector<double> normY(series.y.size());
    for (size_t i = 0; i < series.y.size(); ++i) {
      double v = series.y[i];
      normY[i] = std::isnan(v) ? v : (range > 1e-6 ? ((v - state.cachedMin) / range) : 0.5);
    }

    state.decimatedX = std::move(series.x);
    state.decimatedYNorm = std::move(normY);
  }

  void TimeSeriesPanel::shiftXAxis(double shiftSeconds)
  {
    if (!cachedTimeSec_ || cachedTimeSec_->empty()) return;

    // Calculate the new center position
    double currentSpan = xAxisMax_ - xAxisMin_;
    double currentCenter = (xAxisMax_ + xAxisMin_) * 0.5;
    double newCenter = currentCenter + shiftSeconds;

    // Clamp to valid time range (optional but prevents scrolling into nowhere)
    double minTime = cachedTimeSec_->front();
    double maxTime = cachedTimeSec_->back();
    double halfSpan = currentSpan * 0.5;

    // Keep at least half the span visible within the data range
    newCenter = std::clamp(newCenter, minTime + halfSpan, maxTime - halfSpan);

    // Update the axis limits via pending state
    targetXCenterTime_ = newCenter;
    pendingXAxisCenter_ = true;
  }

  void TimeSeriesPanel::computeChannelStats(const core::Channel &channel, ChannelStats &outStats) const
  {
    outStats = ChannelStats{};
    if (!session_ || channel.values().empty()) return;

    const auto &vals = channel.values();
    size_t startIdx = session_->cropStartIndex();
    size_t endIdx = session_->cropEndIndex();

    if (startIdx >= vals.size() || endIdx >= vals.size() || startIdx > endIdx) {
      startIdx = 0;
      endIdx = vals.size() - 1;
    }

    double minVal = (std::numeric_limits<double>::max)();
    double maxVal = -(std::numeric_limits<double>::max)();
    double sum = 0.0;
    size_t validCount = 0;

    // First pass: Min, Max, Mean
    for (size_t i = startIdx; i <= endIdx; ++i) {
      double v = vals[i];
      if (std::isnan(v)) continue;

      if (v < minVal) minVal = v;
      if (v > maxVal) maxVal = v;
      sum += v;
      ++validCount;
    }

    if (validCount == 0) return;

    double meanVal = sum / static_cast<double>(validCount);

    // Second pass: Variance / Standard Deviation
    double sumSqDiff = 0.0;
    for (size_t i = startIdx; i <= endIdx; ++i) {
      double v = vals[i];
      if (std::isnan(v)) continue;
      double diff = v - meanVal;
      sumSqDiff += diff * diff;
    }

    double variance = (validCount > 1) ? (sumSqDiff / static_cast<double>(validCount - 1)) : 0.0;

    outStats.min = minVal;
    outStats.max = maxVal;
    outStats.mean = meanVal;
    outStats.stdDev = std::sqrt(variance);
    outStats.sampleCount = validCount;
    outStats.valid = true;
  }

  void TimeSeriesPanel::renderStatsOverlay(int plotIdx, const std::vector<const core::Channel *> &subplotChannels, const ImVec2 &plotPos, const ImVec2 &plotSize)
  {
    if (!showStatsOverlay_ || subplotChannels.empty() || !session_) return;

    // Position: Top-right corner of the subplot
    ImVec2 topRightPos = ImVec2(plotPos.x + plotSize.x - 10.0f, plotPos.y + 10.0f);
    ImGui::SetNextWindowPos(topRightPos, ImGuiCond_Always, ImVec2(1.0f, 0.0f));
    ImGui::SetNextWindowBgAlpha(0.80f);

    const ImGuiWindowFlags overlayFlags = ImGuiWindowFlags_NoDecoration |
      ImGuiWindowFlags_AlwaysAutoResize |
      ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoFocusOnAppearing |
      ImGuiWindowFlags_NoNav |
      ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoDocking;

    std::string overlayID = "##StatsOverlay_" + title() + "_" + std::to_string(plotIdx);

    if (ImGui::Begin(overlayID.c_str(), nullptr, overlayFlags)) {
      const auto &crop = session_->cropRange();
      if (crop.active) {
        ImGui::TextDisabled("Crop Stats [%.1fs - %.1fs]", crop.startSec, crop.endSec);
      } else {
        ImGui::TextDisabled("Full Log Stats");
      }
      ImGui::Separator();

      const ImGuiTableFlags tableFlags = ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_RowBg;
      if (ImGui::BeginTable("StatsGrid", 5, tableFlags)) {
        ImGui::TableSetupColumn("Channel", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Min", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn("Max", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn("Avg", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableSetupColumn("StdDev", ImGuiTableColumnFlags_WidthFixed, 55.0f);
        ImGui::TableHeadersRow();

        uint64_t currentRev = session_->revision();

        for (const auto *ch : subplotChannels) {
          auto &state = channelStates_[ch->name()];
          auto &cached = statsCache_[ch->name()];

          // Check if cached stats are valid
          bool needsRecalc = !cached.stats.valid ||
            cached.sessionRevision != currentRev ||
            cached.cropActive != crop.active ||
            std::abs(cached.cropStart - crop.startSec) > 1e-6 ||
            std::abs(cached.cropEnd - crop.endSec) > 1e-6;

          if (needsRecalc) {
            computeChannelStats(*ch, cached.stats);
            cached.sessionRevision = currentRev;
            cached.cropActive = crop.active;
            cached.cropStart = crop.startSec;
            cached.cropEnd = crop.endSec;
          }

          if (!cached.stats.valid) continue;

          ImGui::TableNextRow();

          // 1. Channel Name
          ImGui::TableSetColumnIndex(0);
          ImGui::TextColored(state.color, "%s", ch->name().c_str());

          // 2. Min
          ImGui::TableSetColumnIndex(1);
          ImGui::Text("%.1f", cached.stats.min);

          // 3. Max
          ImGui::TableSetColumnIndex(2);
          ImGui::Text("%.1f", cached.stats.max);

          // 4. Mean
          ImGui::TableSetColumnIndex(3);
          ImGui::Text("%.1f", cached.stats.mean);

          // 5. StdDev (σ)
          ImGui::TableSetColumnIndex(4);
          ImGui::Text("%.2f", cached.stats.stdDev);
        }
        ImGui::EndTable();
      }
    }
    ImGui::End();
  }

  void TimeSeriesPanel::pollExportCsvDialog()
  {
    if (pendingCsvSaveDialog_ && pendingCsvSaveDialog_->ready(0)) {
      std::string path = pendingCsvSaveDialog_->result();
      pendingCsvSaveDialog_.reset();

      if (!path.empty() && session_) {
        io::CsvExportOptions opts;
        opts.cropOnly = (exportRangeMode_ == 0);
        opts.selectedChannelsOnly = (exportChannelMode_ == 0);
        opts.includeUnitsRow = exportIncludeUnits_;
        opts.delimiter = (exportDelimiterIdx_ == 1) ? '\t' : ',';

        if (opts.selectedChannelsOnly) {
          for (const auto &[name, state] : channelStates_) {
            if (state.enabled) {
              opts.targetChannelNames.push_back(name);
            }
          }
        }

        exportProgress_.store(0.0f);
        exportCancelRequested_.store(false);
        isExporting_ = true;

        activeExportTask_ = std::async(std::launch::async, [this, path, opts, sessionSnapshot = *session_]() {
          ExportResult res;
          res.success = io::CsvExporter::write(
            path,
            sessionSnapshot,
            opts,
            res.error,
            &exportProgress_,
            &exportCancelRequested_
          );
          return res;
          });
      }
    }
  }

  void TimeSeriesPanel::renderExportProgressModal()
  {
    if (isExporting_) {
      ImGui::OpenPopup(ui::popups::ExportCsvProgress);
    }

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(360, 0));

    if (ImGui::BeginPopupModal(ui::popups::ExportCsvProgress, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
      ImGui::Text("Exporting CSV Telemetry...");
      ImGui::Spacing();

      float p = exportProgress_.load();
      char progressBuf[32];
      std::snprintf(progressBuf, sizeof(progressBuf), "%.0f%%", p * 100.0f);
      ImGui::ProgressBar(p, ImVec2(-1.0f, 0.0f), progressBuf);

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      if (ui::UI::ButtonDanger("Stop Export", ImVec2(-1.0f, 0.0f))) {
        exportCancelRequested_.store(true);
      }

      if (activeExportTask_.valid() &&
        activeExportTask_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
        ExportResult res = activeExportTask_.get();
        isExporting_ = false;
        ImGui::CloseCurrentPopup();

        if (!res.success && !exportCancelRequested_.load()) {
          exportErrorMessage_ = res.error;
          showExportError_ = true;
        }
      }

      ImGui::EndPopup();
    }
  }

  void TimeSeriesPanel::renderMinimap(PlotCursor &cursor, const ImVec2 &size)
  {
    if (!session_ || session_->rowCount() == 0 || !cachedTimeSec_ || cachedTimeSec_->size() < 2) {
      return;
    }

    updateMinimapOverviewCache();

    double tStart = cachedTimeSec_->front();
    double tEnd = cachedTimeSec_->back();
    double totalSpan = tEnd - tStart;
    if (totalSpan <= 0.0) totalSpan = 1.0;

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4.0f);
    ImGui::InvisibleButton("##MinimapCanvas", size);
    ImVec2 pMin = ImGui::GetItemRectMin();
    ImVec2 pMax = ImGui::GetItemRectMax();
    ImGui::PopStyleVar();

    ImDrawList *drawList = ImGui::GetWindowDrawList();
    drawList->PushClipRect(pMin, pMax, true);

    // Background frame
    drawList->AddRectFilled(pMin, pMax, IM_COL32(20, 22, 28, 255), 4.0f);
    drawList->AddRect(pMin, pMax, IM_COL32(50, 54, 65, 255), 4.0f);

    float mapW = pMax.x - pMin.x;
    float mapH = pMax.y - pMin.y;

    auto timeToScreenX = [&](double t) -> float {
      float frac = static_cast<float>((t - tStart) / totalSpan);
      return pMin.x + std::clamp(frac, 0.0f, 1.0f) * mapW;
      };

    auto screenXToTime = [&](float x) -> double {
      float frac = std::clamp((x - pMin.x) / mapW, 0.0f, 1.0f);
      return tStart + frac * totalSpan;
      };

    // 1. Shaded Out-of-Crop Regions
    if (session_->cropRange().active) {
      float cMinX = timeToScreenX(session_->cropRange().startSec);
      float cMaxX = timeToScreenX(session_->cropRange().endSec);
      if (cMinX > pMin.x) {
        drawList->AddRectFilled(pMin, ImVec2(cMinX, pMax.y), IM_COL32(0, 0, 0, 110));
      }
      if (cMaxX < pMax.x) {
        drawList->AddRectFilled(ImVec2(cMaxX, pMin.y), pMax, IM_COL32(0, 0, 0, 110));
      }
    }

    // 2. Regime Shaded Bands
    for (const auto &reg : session_->regimeSummaries()) {
      if (!reg.def.showShading) continue;
      ImU32 regCol = ImGui::ColorConvertFloat4ToU32(ImVec4(reg.def.color.x, reg.def.color.y, reg.def.color.z, 0.35f));
      for (const auto &iv : reg.intervals) {
        float rX1 = timeToScreenX(iv.startSec);
        float rX2 = timeToScreenX(iv.endSec);
        drawList->AddRectFilled(ImVec2(rX1, pMin.y + 2), ImVec2(rX2, pMax.y - 2), regCol);
      }
    }

    // 3. Background Reference Waveform
    if (!minimapX_.empty() && minimapX_.size() == minimapYNorm_.size()) {
      for (size_t i = 0; i + 1 < minimapX_.size(); ++i) {
        if (std::isnan(minimapYNorm_[i]) || std::isnan(minimapYNorm_[i + 1])) continue;
        float x1 = timeToScreenX(minimapX_[i]);
        float y1 = pMax.y - 4.0f - static_cast<float>(minimapYNorm_[i]) * (mapH - 8.0f);
        float x2 = timeToScreenX(minimapX_[i + 1]);
        float y2 = pMax.y - 4.0f - static_cast<float>(minimapYNorm_[i + 1]) * (mapH - 8.0f);
        drawList->AddLine(ImVec2(x1, y1), ImVec2(x2, y2), IM_COL32(200, 205, 215, 70), 1.0f);
      }
    }

    // 4. Stitch Points
    for (double stitchMid : session_->stitchPoints()) {
      float sX = timeToScreenX(stitchMid);
      drawList->AddLine(ImVec2(sX, pMin.y), ImVec2(sX, pMax.y), IM_COL32(180, 110, 240, 180), 1.5f);
    }

    // 5. Annotations / Bookmarks
    for (const auto &a : session_->annotations()) {
      float aX = timeToScreenX(a.timeSec);
      ImU32 aCol = ImGui::ColorConvertFloat4ToU32(a.color);
      drawList->AddLine(ImVec2(aX, pMin.y), ImVec2(aX, pMax.y), aCol, 1.5f);
      drawList->AddTriangleFilled(ImVec2(aX - 3.5f, pMin.y), ImVec2(aX + 3.5f, pMin.y), ImVec2(aX, pMin.y + 5.0f), aCol);
    }

    // 6. Viewport Window (Visible Range)
    float winX1 = timeToScreenX(xAxisMin_);
    float winX2 = timeToScreenX(xAxisMax_);
    if (winX2 < winX1) std::swap(winX1, winX2);
    if (winX2 - winX1 < 6.0f) winX2 = winX1 + 6.0f; // Minimum visual grab width

    // Shaded body
    drawList->AddRectFilled(ImVec2(winX1, pMin.y + 1), ImVec2(winX2, pMax.y - 1), IM_COL32(65, 140, 240, 60), 2.0f);
    drawList->AddRect(ImVec2(winX1, pMin.y + 1), ImVec2(winX2, pMax.y - 1), IM_COL32(90, 170, 255, 200), 2.0f, 0, 1.5f);

    // Grab handles
    float handleW = 4.0f;
    drawList->AddRectFilled(ImVec2(winX1, pMin.y + 3), ImVec2(winX1 + handleW, pMax.y - 3), IM_COL32(120, 190, 255, 220), 1.0f);
    drawList->AddRectFilled(ImVec2(winX2 - handleW, pMin.y + 3), ImVec2(winX2, pMax.y - 3), IM_COL32(120, 190, 255, 220), 1.0f);

    // 7. Cursor Line
    if (cursor.active) {
      float curX = timeToScreenX(cursor.timeSec);
      drawList->AddLine(ImVec2(curX, pMin.y), ImVec2(curX, pMax.y), IM_COL32(255, 255, 60, 220), 1.0f);
    }

    // -------------------------------------------------------------------------
    // Interaction & Event Handling
    // -------------------------------------------------------------------------
    bool isHovered = ImGui::IsItemHovered();
    ImVec2 mousePos = ImGui::GetIO().MousePos;
    constexpr float kHandleDetectPx = 6.0f;

    bool nearLeft = std::abs(mousePos.x - winX1) <= kHandleDetectPx;
    bool nearRight = std::abs(mousePos.x - winX2) <= kHandleDetectPx;
    bool insideWindow = (mousePos.x >= winX1 && mousePos.x <= winX2);

    // Set contextual cursor on hover
    if (isHovered && minimapDragMode_ == MinimapDragMode::None) {
      if (nearLeft || nearRight) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
      } else if (insideWindow) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
      } else {
        ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
      }
    }

    // Mouse Wheel Interactions
    if (isHovered && ImGui::GetIO().MouseWheel != 0.0f) {
      float wheel = ImGui::GetIO().MouseWheel;
      double currentSpan = xAxisMax_ - xAxisMin_;

      if (insideWindow) {
        // Zoom in / out
        double zoomFactor = (wheel > 0.0f) ? 0.85 : 1.15;
        double newSpan = std::clamp(currentSpan * zoomFactor, 0.5, totalSpan);
        double center = (xAxisMin_ + xAxisMax_) * 0.5;
        xAxisMin_ = std::clamp(center - newSpan * 0.5, tStart, tEnd - newSpan);
        xAxisMax_ = xAxisMin_ + newSpan;
        bPendingAfterLoad_ = true;
      } else {
        // Pan left / right
        double panStep = currentSpan * 0.15 * (wheel > 0.0f ? -1.0 : 1.0);
        shiftXAxis(panStep);
      }
    }

    // Mouse Click & Drag
    if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      minimapDragStartMouseX_ = mousePos.x;
      minimapDragInitialXMin_ = xAxisMin_;
      minimapDragInitialXMax_ = xAxisMax_;

      if (nearLeft) {
        minimapDragMode_ = MinimapDragMode::ResizeLeft;
      } else if (nearRight) {
        minimapDragMode_ = MinimapDragMode::ResizeRight;
      } else if (insideWindow) {
        minimapDragMode_ = MinimapDragMode::PanWindow;
      } else {
        // Click outside -> instant recenter
        double clickedT = screenXToTime(mousePos.x);
        double currentSpan = xAxisMax_ - xAxisMin_;
        xAxisMin_ = std::clamp(clickedT - currentSpan * 0.5, tStart, tEnd - currentSpan);
        xAxisMax_ = xAxisMin_ + currentSpan;
        bPendingAfterLoad_ = true;
      }
    }

    if (minimapDragMode_ != MinimapDragMode::None) {
      if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        float dxPx = mousePos.x - minimapDragStartMouseX_;
        double dt = (dxPx / mapW) * totalSpan;
        double currentSpan = minimapDragInitialXMax_ - minimapDragInitialXMin_;

        if (minimapDragMode_ == MinimapDragMode::PanWindow) {
          xAxisMin_ = std::clamp(minimapDragInitialXMin_ + dt, tStart, tEnd - currentSpan);
          xAxisMax_ = xAxisMin_ + currentSpan;
          bPendingAfterLoad_ = true;
        } else if (minimapDragMode_ == MinimapDragMode::ResizeLeft) {
          xAxisMin_ = std::clamp(minimapDragInitialXMin_ + dt, tStart, xAxisMax_ - 0.2);
          bPendingAfterLoad_ = true;
        } else if (minimapDragMode_ == MinimapDragMode::ResizeRight) {
          xAxisMax_ = std::clamp(minimapDragInitialXMax_ + dt, xAxisMin_ + 0.2, tEnd);
          bPendingAfterLoad_ = true;
        }
      } else {
        minimapDragMode_ = MinimapDragMode::None;
      }
    }

    drawList->PopClipRect();
  }

  void TimeSeriesPanel::updateMinimapOverviewCache()
  {
    uint64_t currentRev = session_ ? session_->revision() : 0;
    if (minimapCacheSession_ == session_ && minimapCacheRevision_ == currentRev) {
      return;
    }

    minimapCacheSession_ = session_;
    minimapCacheRevision_ = currentRev;
    minimapX_.clear();
    minimapYNorm_.clear();

    if (!session_ || session_->rowCount() == 0 || !cachedTimeSec_ || cachedTimeSec_->empty()) {
      return;
    }

    // Find RPM or fallback to first available channel
    const core::Channel *refCh = session_->findChannel(session_->channelMapping().rpm);
    if (!refCh && !session_->channels().empty()) {
      refCh = &session_->channels().front();
    }
    if (!refCh || refCh->values().empty()) return;

    double tMin = cachedTimeSec_->front();
    double tMax = cachedTimeSec_->back();
    constexpr int kMinimapPoints = 600;

    engine::DecimatedSeries decimated = engine::Decimator::decimate(*cachedTimeSec_, refCh->values(), tMin, tMax, kMinimapPoints);

    if (decimated.x.empty()) return;

    auto [minIt, maxIt] = std::minmax_element(refCh->values().begin(), refCh->values().end());
    double vMin = *minIt;
    double vMax = *maxIt;
    double span = vMax - vMin;

    minimapX_ = std::move(decimated.x);
    minimapYNorm_.resize(decimated.y.size());

    for (size_t i = 0; i < decimated.y.size(); ++i) {
      double v = decimated.y[i];
      minimapYNorm_[i] = std::isnan(v) ? v : (span > 1e-6 ? (v - vMin) / span : 0.5);
    }
  }

  void TimeSeriesPanel::renderExportCsvModal()
  {
    pollExportCsvDialog();

    if (showExportCsvModal_) {
      ImGui::OpenPopup(ui::popups::ExportCsv);
      showExportCsvModal_ = false;
    }

    if (ImGui::BeginPopupModal(ui::popups::ExportCsv, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextDisabled("Export Session Telemetry to CSV");
      ImGui::Separator();
      ImGui::Spacing();

      // 1. Data Span Selection
      bool hasCrop = session_ && session_->cropRange().active;
      if (!hasCrop) {
        exportRangeMode_ = 1; // Force Full Session if no crop active
      }

      ImGui::Text("Data Range:");
      ImGui::BeginDisabled(!hasCrop);
      ImGui::RadioButton("Active Crop Range Only", &exportRangeMode_, 0);
      ImGui::EndDisabled();

      ImGui::RadioButton("Full Log Session", &exportRangeMode_, 1);
      if (!hasCrop) {
        ImGui::SameLine();
        ImGui::TextDisabled("(No crop active)");
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      // 2. Channel Subset Selection
      ImGui::Text("Channels:");
      ImGui::RadioButton("Active / Plotted Channels Only", &exportChannelMode_, 0);
      ImGui::RadioButton("All Available Log Channels", &exportChannelMode_, 1);

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      // 3. Format Options
      ImGui::Checkbox("Include Units Row", &exportIncludeUnits_);
      ImGui::SetNextItemWidth(140.0f);
      const char *delimiters[] = { "Comma (,)", "Tab (\\t)" };
      ImGui::Combo("Delimiter", &exportDelimiterIdx_, delimiters, IM_ARRAYSIZE(delimiters));

      if (showExportError_) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "Error: %s", exportErrorMessage_.c_str());
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      if (ui::UI::ButtonPrimary(ICON_FA_FLOPPY_DISK " Export...", ImVec2(120, 0))) {
        showExportError_ = false;
        std::string defaultName = utils::path::fileNameWithoutExtension(session_ ? session_->sourcePath() : "export") + "_export.csv";
        pendingCsvSaveDialog_ = std::make_unique<pfd::save_file>(
          "Export CSV", defaultName,
          std::vector<std::string>{"CSV Files (*.csv)", "*.csv", "All Files", "*"});
        ImGui::CloseCurrentPopup();
      }

      ImGui::SameLine();
      if (ui::UI::Button("Cancel", {}, ImVec2(80, 0))) {
        showExportError_ = false;
        ImGui::CloseCurrentPopup();
      }

      ImGui::EndPopup();
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
    ImGui::Checkbox("Stats", &showStatsOverlay_);
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip("Show floating min/max/avg/stddev overlay card for active channels.");
    }
    ImGui::SameLine();
    ImGui::Checkbox("Minimap", &showMinimap_);

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();
    if (ui::UI::ButtonSecondary(ICON_FA_FILE_CSV " Export CSV")) {
      showExportCsvModal_ = true;
    }

    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();
    ImGuiIO &io = ImGui::GetIO();
    ImGui::SameLine();
    if (ui::UI::Button(ICON_FA_CHEVRON_LEFT, {}, {}, "Shift view backward by 10 seconds (Ctrl: 60 seconds)")) {
      shiftXAxis(io.KeyCtrl ? -60.0 : -10.0);
    }
    ImGui::SameLine();
    if (ui::UI::Button(ICON_FA_CHEVRON_RIGHT, {}, {}, "Shift view forward by 10 seconds (Ctrl: 60 seconds)")) {
      shiftXAxis(io.KeyCtrl ? 60.0 : 10.0);
    }

    ImGui::SameLine();
    renderCropControls(cursor);

    if (session_ && !session_->stitchPoints().empty()) {
      ImGui::SameLine();
      ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(160.0f);
      if (ImGui::BeginCombo("##StitchSelector", ICON_FA_CODE_MERGE " Stitch Points")) {
        const auto &points = session_->stitchPoints();
        for (size_t i = 0; i < points.size(); ++i) {
          double stitchTime = points[i];
          std::string label = "Stitch #" + std::to_string(i + 1) + " (" + std::to_string(static_cast<int>(stitchTime)) + " s)";
          if (ImGui::Selectable(label.c_str())) {
            std::string tag = "Stitch #" + std::to_string(i + 1);
            jumpCursorToIndex(getCursorIndex(stitchTime), cursor, tag);
          }
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Jump cursor and center view on stitch at %.3f s", stitchTime);
          }
        }
        ImGui::EndCombo();
      }
    }


    if (session_ && !session_->annotations().empty()) {
      ImGui::SameLine();
      ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(160.0f);
      if (ImGui::BeginCombo("##AnnotationSelector", ICON_FA_BOOKMARK " Bookmarks")) {
        auto &annots = const_cast<core::LogSession *>(session_)->annotations();
        for (size_t i = 0; i < annots.size(); ++i) {
          ImGui::PushID(static_cast<int>(i));
          std::string itemLabel = annots[i].label + " (" + std::to_string(static_cast<int>(annots[i].timeSec)) + " s)";
          if (ImGui::Selectable(itemLabel.c_str())) {
            jumpCursorToIndex(getCursorIndex(annots[i].timeSec), cursor, annots[i].label);
          }
          // RMB on combo item to delete
          if (ImGui::BeginPopupContextItem("AnnotContextMenu")) {
            if (ImGui::MenuItem("Delete Bookmark")) {
              const_cast<core::LogSession *>(session_)->removeAnnotation(i);
              notifyDataChanged();
              ImGui::EndPopup();
              ImGui::PopID();
              break;
            }
            ImGui::EndPopup();
          }

          ImGui::PopID();
        }
        ImGui::EndCombo();
      }
    }

    if (session_ && !session_->regimeSummaries().empty()) {
      ImGui::SameLine();
      ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
      ImGui::SameLine();
      for (const auto &reg : session_->regimeSummaries()) {
        if (!reg.def.showShading || reg.intervals.empty()) continue;
        ImGui::PushStyleColor(ImGuiCol_Button, reg.def.color);
        std::string btnLabel = std::string(ICON_FA_FLAG) + "###" + reg.def.displayName;
        if (ImGui::Button(btnLabel.c_str())) {
          // Find the next interval start time after the current cursor position
          double targetTime = reg.intervals.front().startSec; // Fallback / wrap to first
          for (const auto &interval : reg.intervals) {
            // Use a tiny epsilon (+0.1s) so pressing the button while standing on a start marker jumps to the NEXT one
            if (interval.startSec > cursor.timeSec + 0.1) {
              targetTime = interval.startSec;
              break;
            }
          }
          jumpCursorToIndex(getCursorIndex(targetTime), cursor, reg.def.displayName);
        }
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered()) {
          if (!reg.warningMessage.empty()) {
            // Show warning banner first if present
            ImGui::SetTooltip("%s (%zu events)\nDwell: %.1f s\nWarning: %s\nClick to jump to next event (cycles)",
              reg.def.displayName.c_str(),
              reg.intervals.size(),
              reg.totalDwellTimeSec,
              reg.warningMessage.c_str());
          } else {
            // Standard tooltip
            ImGui::SetTooltip("%s (%zu events)\nDwell: %.1f s\nClick to jump to next event (cycles)",
              reg.def.displayName.c_str(),
              reg.intervals.size(),
              reg.totalDwellTimeSec);
          }
        }
        ImGui::SameLine();
      }
    }
  }

  void TimeSeriesPanel::renderCropControls(PlotCursor & /*cursor*/)
  {
    if (session_ && session_->cropRange().active) {
      if (ui::UI::Button("Clear Crop", ui::ButtonStyle::Danger)) {
        const_cast<core::LogSession *>(session_)->resetCropRange();
        notifyDataChanged();
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

    const std::string filterLower = utils::str::toLower(filterText_);

    if (session_ != nullptr && !session_->channels().empty()) {
      for (const auto &channel : session_->channels()) {
        const std::string &name = channel.name();
        if (!filterLower.empty() && utils::str::toLower(name).find(filterLower) == std::string::npos) {
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
    int maxPerPlot = (std::max)(1, maxChannelsPerPlot_);
    int subplotCount = (numActive > 0) ? ((numActive + maxPerPlot - 1) / maxPerPlot) : 1;

    // Capture state BEFORE the loop so all subplots behave identically this frame
    bool doJump = pendingXAxisCenter_;
    bool doLoadLimits = bPendingAfterLoad_;

    double newMinX = 0.0;
    double newMaxX = 0.0;

    if (doJump) {
      double currentSpan = xAxisMax_ - xAxisMin_;
      if (currentSpan <= 0.1) {
        double totalDuration = (cachedTimeSec_ && !cachedTimeSec_->empty()) ? (cachedTimeSec_->back() - cachedTimeSec_->front()) : 20.0;
        currentSpan = (totalDuration > 0.0) ? (totalDuration * 0.10) : 20.0;
        if (currentSpan < 1.0) currentSpan = 1.0;
      }
      double halfSpan = currentSpan * 0.5;
      newMinX = targetXCenterTime_ - halfSpan;
      newMaxX = targetXCenterTime_ + halfSpan;
    }

    if (ImPlot::BeginSubplots("##TimeSeriesSubplots", subplotCount, 1, ImVec2(-1, -1), ImPlotSubplotFlags_LinkAllX | ImPlotSubplotFlags_NoLegend)) {
      for (int plotIdx = 0; plotIdx < subplotCount; ++plotIdx) {
        std::string plotID = "##Plot_" + std::to_string(plotIdx);

        if (doJump) {
          ImPlot::SetNextAxisLimits(ImAxis_X1, newMinX, newMaxX, ImGuiCond_Always);
        } else if (doLoadLimits) {
          ImPlot::SetNextAxisLimits(ImAxis_X1, xAxisMin_, xAxisMax_, ImGuiCond_Always);
          ImPlot::SetNextAxisLimits(ImAxis_Y1, yAxisMin_, yAxisMax_, ImGuiCond_Always);
        }

        if (ImPlot::BeginPlot(plotID.c_str(), ImVec2(-1, -1))) {
          ImPlot::SetupAxes("Time (s)", "Normalized", ImPlotAxisFlags_None, ImPlotAxisFlags_NoTickLabels);

          // Store bounds only from the primary (first) subplot to prevent cross-contamination
          if (plotIdx == 0) {
            auto plotLimits = ImPlot::GetPlotLimits();
            xAxisMin_ = plotLimits.X.Min;
            xAxisMax_ = plotLimits.X.Max;
            yAxisMin_ = plotLimits.Y.Min;
            yAxisMax_ = plotLimits.Y.Max;
          }

          // Determine channel subset for THIS specific subplot
          int startIdx = plotIdx * maxPerPlot;
          int endIdx = (std::min)(startIdx + maxPerPlot, numActive);

          std::vector<const core::Channel *> subplotChannels;
          if (hasRealData && numActive > 0) {
            for (int i = startIdx; i < endIdx; ++i) {
              subplotChannels.push_back(activeChannels[i]);
            }
          }

          // Plot channels for this subplot
          const float plotWidthPx = ImPlot::GetPlotSize().x;
          for (const auto *ch : subplotChannels) {
            if (ch->values().empty()) continue;
            auto &state = channelStates_[ch->name()];
            ensureDecimatedCache(state, *ch, xAxisMin_, xAxisMax_, plotWidthPx);
            if (state.decimatedX.empty()) continue;
            ImPlotSpec spec;
            spec.LineColor = state.color;
            ImPlot::PlotLine(ch->name().c_str(), state.decimatedX.data(), state.decimatedYNorm.data(), static_cast<int>(state.decimatedX.size()), spec);
          }

          // Handle hover cursor, crop markers, and shaded regimes ...
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

          if (session_) {
            // Render shaded vertical bands for active regimes
            for (const auto &reg : session_->regimeSummaries()) {
              if (!reg.def.showShading) continue;

              for (const auto &interval : reg.intervals) {
                double xMin = interval.startSec;
                double xMax = interval.endSec;

                // Draw vertical shaded region spanning full Y plot height
                ImPlot::PlotInfLines("##Shade", &xMin, 1); // Or draw using ImPlot::PlotRect / DrawList
                ImVec2 pMin = ImPlot::PlotToPixels(ImPlotPoint(xMin, yAxisMax_));
                ImVec2 pMax = ImPlot::PlotToPixels(ImPlotPoint(xMax, yAxisMin_));

                ImDrawList *drawList = ImPlot::GetPlotDrawList();
                drawList->AddRectFilled(pMin, pMax, ImGui::ColorConvertFloat4ToU32(reg.def.color));
              }
            }
          }

          // Query current subplot geometry for overlays
          ImVec2 currentPlotPos = ImPlot::GetPlotPos();
          ImVec2 currentPlotSize = ImPlot::GetPlotSize();

          const ImGuiWindowFlags overlayFlags = ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_AlwaysAutoResize |
            ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav |
            ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoDocking;

          // -------------------------------------------------------------------
          // 1. Bottom-Left Overlay: Channel Ranges for THIS Subplot
          // -------------------------------------------------------------------
          if (hasRealData && !subplotChannels.empty()) {
            ImVec2 bottomLeftPos = ImVec2(currentPlotPos.x + 10.0f, currentPlotPos.y + currentPlotSize.y - 10.0f);

            ImGui::SetNextWindowPos(bottomLeftPos, ImGuiCond_Always, ImVec2(0.0f, 1.0f));
            ImGui::SetNextWindowBgAlpha(0.75f);
            std::string rangeWindowID = "##RangeOverlay_" + title() + "_" + std::to_string(plotIdx);
            bool open = true;

            if (ImGui::Begin(rangeWindowID.c_str(), &open, overlayFlags)) {
              for (const auto *channel : subplotChannels) {
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

          // -------------------------------------------------------------------
          // 2. Bottom-Right Overlay: Cursor Readouts for THIS Subplot
          // -------------------------------------------------------------------
          if (cursor.active && !subplotChannels.empty()) {
            size_t cursorIdx = getCursorIndex(cursor.timeSec);
            ImVec2 bottomRightPos = ImVec2(currentPlotPos.x + currentPlotSize.x - 10.0f, currentPlotPos.y + currentPlotSize.y - 10.0f);

            ImGui::SetNextWindowPos(bottomRightPos, ImGuiCond_Always, ImVec2(1.0f, 1.0f));
            ImGui::SetNextWindowBgAlpha(0.85f);
            ImGui::SetNextWindowSizeConstraints({ 200.f, 30.f }, { FLT_MAX, FLT_MAX });

            std::string overlayWindowID = "##CursorOverlay_" + title() + "_" + std::to_string(plotIdx);
            bool open = true;

            if (ImGui::Begin(overlayWindowID.c_str(), &open, overlayFlags)) {
              ImGui::TextDisabled("Cursor @ %.3f s", cursor.timeSec);
              
              if (artifactCursor_.active) {
                double deltaTime = cursor.timeSec - artifactCursor_.timeSec;
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(dt: %+.3f s)", deltaTime);
              }

              for (const auto *channel : subplotChannels) {
                const auto &st = channelStates_[channel->name()];
                double val = (cursorIdx < channel->values().size()) ? channel->values()[cursorIdx] : 0.0;
                ImGui::TextColored(st.color, "%s:", channel->name().c_str());
                ImGui::SameLine();
                if (!channel->unit().empty()) {
                  ImGui::Text("%.2f %s", val, utils::str::sanitizeToUtf8(channel->unit()).c_str());
                } else {
                  ImGui::Text("%.2f", val);
                }
              }
              if (subplotChannels.empty()) {
                ImGui::TextDisabled("No active channels in this subplot");
              }
            }
            ImGui::End();
          }

          renderStatsOverlay(plotIdx, subplotChannels, currentPlotPos, currentPlotSize);

          if (session_) {
            const auto &stitchPoints = session_->stitchPoints();
            ImDrawList *drawList = ImPlot::GetPlotDrawList();
            auto localLimits = ImPlot::GetPlotLimits(); // fresh, scoped to THIS subplot's current Y zoom/pan

            for (size_t i = 0; i < stitchPoints.size(); ++i) {
              double stitchMid = stitchPoints[i];
              double xMin = stitchMid - 0.5; // 1.0s gap width
              double xMax = stitchMid + 0.5;

              // Shaded gap band across full subplot height
              ImVec2 pMin = ImPlot::PlotToPixels(ImPlotPoint(xMin, localLimits.Y.Max));
              ImVec2 pMax = ImPlot::PlotToPixels(ImPlotPoint(xMax, localLimits.Y.Min));
              drawList->AddRectFilled(pMin, pMax, IM_COL32(160, 90, 220, 55));

              // Bounding seam edges
              ImPlot::DragLineX(static_cast<int>(100 + i * 2), &xMin, ImVec4(0.65f, 0.35f, 0.85f, 0.40f), 1.0f, ImPlotDragToolFlags_NoInputs);
              ImPlot::DragLineX(static_cast<int>(100 + i * 2 + 1), &xMax, ImVec4(0.65f, 0.35f, 0.85f, 0.40f), 1.0f, ImPlotDragToolFlags_NoInputs);

              // Axis header tag at midpoint
              ImPlot::TagX(stitchMid, ImVec4(0.65f, 0.35f, 0.85f, 0.85f), "Stitch #%d", static_cast<int>(i + 1));
            }
          }

          if (ImPlot::IsPlotHovered() && ImGui::GetIO().KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            timelineActionTime_ = ImPlot::GetPlotMousePos().x;
            annotationLabelBuf_[0] = '\0';
            showTimelineActionPopup_ = true;
          }
          if (ImPlot::IsPlotHovered() && session_) {
            auto *s = const_cast<core::LogSession *>(session_);
            double hoverT = ImPlot::GetPlotMousePos().x;

            if (ImGui::IsKeyPressed(ImGuiKey_LeftBracket, false)) { // '[' -> Crop Start
              double curEnd = s->cropRange().active ? s->cropRange().endSec
                : (cachedTimeSec_ && !cachedTimeSec_->empty() ? cachedTimeSec_->back() : hoverT);
              s->setCropRange(hoverT, curEnd);
              notifyDataChanged();
            } else if (ImGui::IsKeyPressed(ImGuiKey_RightBracket, false)) { // ']' -> Crop End
              double curStart = s->cropRange().active ? s->cropRange().startSec
                : (cachedTimeSec_ && !cachedTimeSec_->empty() ? cachedTimeSec_->front() : 0.0);
              s->setCropRange(curStart, hoverT);
              notifyDataChanged();
            }
          }
          if (session_ && session_->cropRange().active) {
            double cStart = session_->cropRange().startSec;
            double cEnd = session_->cropRange().endSec;
            const ImVec4 cropLineCol = ImVec4(0.2f, 0.7f, 1.0f, 0.65f);

            // Vertical boundary lines & tags
            ImPlot::DragLineX(300, &cStart, cropLineCol, 1.5f, ImPlotDragToolFlags_NoInputs);
            ImPlot::DragLineX(301, &cEnd, cropLineCol, 1.5f, ImPlotDragToolFlags_NoInputs);
            ImPlot::TagX(cStart, cropLineCol, "[Crop Start]");
            ImPlot::TagX(cEnd, cropLineCol, "[Crop End]");
          }
          if (session_) {
            const auto &annots = session_->annotations();
            for (size_t i = 0; i < annots.size(); ++i) {
              const auto &a = annots[i];
              double aTime = a.timeSec;

              ImPlot::DragLineX(static_cast<int>(500 + i), &aTime, a.color, 1.5f, ImPlotDragToolFlags_NoInputs);
              ImPlot::TagX(a.timeSec, a.color, "%s", a.label.c_str());

              // Right-click on tag to delete
              ImGui::PushID(static_cast<int>(500 + i));
              if (ImPlot::IsPlotHovered() && std::abs(ImPlot::GetPlotMousePos().x - a.timeSec) < (xAxisMax_ - xAxisMin_) * 0.015) {
                if (ImGui::BeginPopupContextItem("AnnotTagContext")) {
                  ImGui::TextDisabled("Bookmark: %s", a.label.c_str());
                  ImGui::Separator();
                  if (ImGui::MenuItem("Delete Bookmark")) {
                    const_cast<core::LogSession *>(session_)->removeAnnotation(i);
                    notifyDataChanged();
                    ImGui::EndPopup();
                    ImGui::PopID();
                    break;
                  }
                  ImGui::EndPopup();
                }
              }
              ImGui::PopID();
            }
          }

          ImPlot::EndPlot();
        }
      }
      ImPlot::EndSubplots();
    }
    if (doJump) pendingXAxisCenter_ = false;
    if (doLoadLimits && hasRealData) bPendingAfterLoad_ = false;
  }

  void TimeSeriesPanel::renderAnnotationModal()
  {
    if (showAddAnnotationModal_) {
      ImGui::OpenPopup(ui::popups::AddAnnotation);
    }

    ImGui::SetNextWindowSize(ImVec2(360, 0), ImGuiCond_Always);
    if (ImGui::BeginPopupModal(ui::popups::AddAnnotation, &showAddAnnotationModal_, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("Time: %.3f s", pendingAnnotationTime_);
      ImGui::Spacing();

      ImGui::SetNextItemWidth(-1.0f);
      if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
      }
      ImGui::InputTextWithHint("##AnnotLabel", "Annotation text...", annotationLabelBuf_, sizeof(annotationLabelBuf_));

      ImGui::Spacing();
      ImGui::ColorEdit4("Color", (float *)&annotationColor_, ImGuiColorEditFlags_NoAlpha);

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      if (ui::UI::ButtonPrimary("Add", ImVec2(100, 0))) {
        if (session_ && annotationLabelBuf_[0] != '\0') {
          core::Annotation a;
          a.timeSec = pendingAnnotationTime_;
          a.label = annotationLabelBuf_;
          a.color = annotationColor_;
          const_cast<core::LogSession *>(session_)->addAnnotation(std::move(a));
          notifyDataChanged();
        }
        showAddAnnotationModal_ = false;
        ImGui::CloseCurrentPopup();
      }

      ImGui::SameLine();
      if (ui::UI::Button("Cancel", {}, ImVec2(80, 0))) {
        showAddAnnotationModal_ = false;
        ImGui::CloseCurrentPopup();
      }

      ImGui::EndPopup();
    }
  }

  void TimeSeriesPanel::renderTimelineActionPopup(PlotCursor &cursor)
  {
    if (showTimelineActionPopup_) {
      ImGui::OpenPopup(ui::popups::TimelineAction);
      showTimelineActionPopup_ = false;
    }

    if (ImGui::BeginPopup(ui::popups::TimelineAction)) {
      ImGui::TextDisabled("Timeline @ %.3f s", timelineActionTime_);
      ImGui::Separator();

      if (session_) {
        auto *s = const_cast<core::LogSession *>(session_);
        bool hasCrop = s->cropRange().active;

        // 1. Set Crop Start
        if (ImGui::MenuItem(ICON_FA_ARROW_RIGHT_TO_BRACKET "  Set Crop [Start]")) {
          double curEnd = hasCrop ? s->cropRange().endSec
            : (cachedTimeSec_ && !cachedTimeSec_->empty() ? cachedTimeSec_->back() : timelineActionTime_);
          s->setCropRange(timelineActionTime_, curEnd);
          notifyDataChanged();
        }

        // 2. Set Crop End
        if (ImGui::MenuItem(ICON_FA_ARROW_RIGHT_FROM_BRACKET "  Set Crop [End]")) {
          double curStart = hasCrop ? s->cropRange().startSec
            : (cachedTimeSec_ && !cachedTimeSec_->empty() ? cachedTimeSec_->front() : 0.0);
          s->setCropRange(curStart, timelineActionTime_);
          notifyDataChanged();
        }

        // 3. Zoom / Snap View to Crop Range
        ImGui::BeginDisabled(!hasCrop);
        if (ImGui::MenuItem(ICON_FA_MAGNIFYING_GLASS_PLUS "  Zoom to Crop Range")) {
          xAxisMin_ = s->cropRange().startSec;
          xAxisMax_ = s->cropRange().endSec;
          bPendingAfterLoad_ = true;
        }

        // 4. Reset Crop
        if (ImGui::MenuItem(ICON_FA_XMARK "  Clear Crop")) {
          s->resetCropRange();
          notifyDataChanged();
        }
        ImGui::EndDisabled();

        ImGui::Separator();
        ImGui::TextDisabled("Add Bookmark:");
        ImGui::Spacing();

        ImGui::SetNextItemWidth(220.0f);
        ImGui::InputTextWithHint("##AnnotLabel", "Bookmark label...", annotationLabelBuf_, sizeof(annotationLabelBuf_));

        ImGui::SameLine();
        ImGui::ColorEdit4("##AnnotColor", (float *)&annotationColor_, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoAlpha);

        ImGui::Spacing();
        if (ui::UI::ButtonPrimary(ICON_FA_PLUS " Add Bookmark", ImVec2(-1.0f, 0.0f))) {
          if (annotationLabelBuf_[0] != '\0') {
            core::Annotation a;
            a.timeSec = timelineActionTime_;
            a.label = annotationLabelBuf_;
            a.color = annotationColor_;
            s->addAnnotation(std::move(a));
            notifyDataChanged();
            ImGui::CloseCurrentPopup();
          }
        }
      }

      ImGui::EndPopup();
    } else {
      showTimelineActionPopup_ = false;
    }
  }

  void TimeSeriesPanel::render(PlotCursor &cursor)
  {
    std::string windowLabel = std::string{ ICON_FA_CHART_LINE } + " Time Series###" + title();
    ImGui::Begin(windowLabel.c_str(), &open_);

    renderHeaderControls(cursor);
    renderTimelineActionPopup(cursor);
    renderExportCsvModal();
    renderExportProgressModal();

    ImGui::Separator();

    float totalAvailHeight = ImGui::GetContentRegionAvail().y;
    float plotAreaHeight = (showMinimap_ && session_ && session_->rowCount() > 0)
      ? (totalAvailHeight - minimapHeight_ - ImGui::GetStyle().ItemSpacing.y)
      : totalAvailHeight;

    // Sidebar + Main Plot Area Child
    ImGui::BeginChild("UpperPlotSplit", ImVec2(0.0f, plotAreaHeight), false);

    if (showSidebar_) {
      const float minWidth = 120.0f;
      const float maxWidth = ImGui::GetContentRegionAvail().x - 150.0f;
      sidebarWidth_ = std::clamp(sidebarWidth_, minWidth, (std::max)(minWidth, maxWidth));

      ImGui::BeginChild("SidebarChild", ImVec2(sidebarWidth_, 0.0f), true);
      renderLeftSidebar(cursor);
      ImGui::EndChild();

      ImGui::SameLine();

      const float splitterThickness = 4.0f;
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0, 0, 0, 0));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_SeparatorActive));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_SeparatorHovered));

      ImGui::Button("##Splitter", ImVec2(splitterThickness, -1.0f));
      ImGui::PopStyleColor(3);

      if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
      }
      if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        sidebarWidth_ += ImGui::GetIO().MouseDelta.x;
      }

      ImGui::SameLine();
    }

    ImGui::BeginChild("PlotChild", ImVec2(0.0f, 0.0f), false);
    renderPlotArea(cursor);
    ImGui::EndChild();

    ImGui::EndChild();

    // Bottom Minimap Strip
    if (showMinimap_ && session_ && session_->rowCount() > 0) {
      ImGui::Spacing();
      renderMinimap(cursor, ImVec2(ImGui::GetContentRegionAvail().x, minimapHeight_));
    }

    ImGui::End();
  }

} // namespace ui