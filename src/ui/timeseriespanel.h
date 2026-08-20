#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "core/logsession.h"
#include "ui/plotpanel.h"
#include "3rdparty/portable-file-dialogs.h"

#include <imgui.h>

namespace ui {

  class TimeSeriesPanel : public PlotPanel {
  public:
    TimeSeriesPanel(std::string title, std::vector<std::string> initialChannelNames = {});

    void render(PlotCursor &cursor) override;
    bool wantsClose() const override { return !open_; }

    void setSession(const core::LogSession *session) override;

    std::string panelTypeId() const override { return "TimeSeriesPanel"; }
    nlohmann::json saveState() const override;
    void loadState(const nlohmann::json &state) override;

  private:
    struct ChannelState {
      bool enabled = false;
      ImVec4 color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
      double cachedMin = 0.0;
      double cachedMax = 0.0;
      size_t minIdx = 0; // Cached index for fast jump
      size_t maxIdx = 0; // Cached index for fast jump
      bool hasRange = false;
      uint64_t selectionOrder = 0;

      // Decimation cache
      const core::LogSession *decimatedForSession = nullptr;
      uint64_t decimatedForRevision = 0;
      double decimatedForXMin = 0.0;
      double decimatedForXMax = 0.0;
      float decimatedForWidthPx = 0.0f;
      bool hasDecimatedCache = false;
      std::vector<double> decimatedX;
      std::vector<double> decimatedYNorm;
    };

    struct ChannelStats {
      double min = 0.0;
      double max = 0.0;
      double mean = 0.0;
      double stdDev = 0.0;
      size_t sampleCount = 0;
      bool valid = false;
    };

    struct ArtifactCursor {
      double timeSec = 0.0;
      bool active = false;
      std::string label; // e.g., "RPM Max (7200.0)"
      ImVec4 color = ImVec4(0.9f, 0.9f, 0.3f, 0.4f); // Dimmed/grayed out
    } artifactCursor_;

    void rebindChannels();
    void renderHeaderControls(PlotCursor &cursor);
    void renderCropControls(PlotCursor &cursor);
    void renderLeftSidebar(PlotCursor &cursor);
    void renderPlotArea(PlotCursor &cursor);
    void renderAnnotationModal();
    void renderTimelineActionPopup(PlotCursor &cursor);
    size_t getCursorIndex(double queryTime) const;
    void jumpCursorToIndex(size_t index, PlotCursor &cursor, const std::string &label = "");
    void ensureDecimatedCache(ChannelState &state, const core::Channel &channel, double xMin, double xMax, float widthPx);
    void shiftXAxis(double shiftSeconds);
    void computeChannelStats(const core::Channel &channel, ChannelStats &outStats) const;
    void renderStatsOverlay(int plotIdx, const std::vector<const core::Channel *> &subplotChannels, const ImVec2 &plotPos, const ImVec2 &plotSize);
    void renderExportCsvModal();
    void pollExportCsvDialog();
    void renderExportProgressModal();
    void renderMinimap(PlotCursor &cursor, const ImVec2 &size);
    void updateMinimapOverviewCache();


    bool pendingXAxisCenter_ = false;
    //bool pendingXAxisShift_ = false;
    double targetXCenterTime_ = 0.0;
    
    const core::LogSession *session_ = nullptr;
    bool open_ = true;
    bool showSidebar_ = true;
    int maxChannelsPerPlot_ = 4;

    std::string filterText_;

    std::unordered_map<std::string, ChannelState> channelStates_;
    uint64_t nextSelectionOrder_ = 1;

    const std::vector<double> *cachedTimeSec_ = nullptr;
    std::vector<double> placeholderTimeSec_;
    std::vector<double> placeholderValue_;

    float sidebarWidth_ = 240.0f;

    // ImPlot states
    double xAxisMin_ = 0.0;
    double xAxisMax_ = 10.0;
    double yAxisMin_ = 0.0;
    double yAxisMax_ = 1.0;
    bool bPendingAfterLoad_ = false;

    bool showTimelineActionPopup_ = false;
    double timelineActionTime_ = 0.0;
    bool showAddAnnotationModal_ = false;
    double pendingAnnotationTime_ = 0.0;
    char annotationLabelBuf_[128] = "";
    ImVec4 annotationColor_ = ImVec4(1.0f, 0.8f, 0.2f, 1.0f);

    bool showStatsOverlay_ = false;
    // Cache stats per channel with dirty-tracking
    struct CachedStatsEntry {
      ChannelStats stats;
      uint64_t sessionRevision = 0;
      double cropStart = 0.0;
      double cropEnd = 0.0;
      bool cropActive = false;
    };
    mutable std::unordered_map<std::string, CachedStatsEntry> statsCache_;

    bool showExportCsvModal_ = false;
    int exportRangeMode_ = 0;
    int exportChannelMode_ = 0;
    bool exportIncludeUnits_ = true;
    int exportDelimiterIdx_ = 0;
    std::unique_ptr<pfd::save_file> pendingCsvSaveDialog_;
    std::string exportErrorMessage_;
    bool showExportError_ = false;
    // Async Export Progress State
    bool isExporting_ = false;
    std::atomic<float> exportProgress_{ 0.0f };
    std::atomic<bool> exportCancelRequested_{ false };
    struct ExportResult {
      bool success = false;
      std::string error;
    };
    std::future<ExportResult> activeExportTask_;

    bool showMinimap_ = true;
    float minimapHeight_ = 48.0f;
    // Background waveform cache for minimap
    std::vector<double> minimapX_;
    std::vector<double> minimapYNorm_;
    uint64_t minimapCacheRevision_ = 0;
    const core::LogSession *minimapCacheSession_ = nullptr;
    // Minimap drag interaction states
    enum class MinimapDragMode {
      None,
      PanWindow,
      ResizeLeft,
      ResizeRight
    };
    MinimapDragMode minimapDragMode_ = MinimapDragMode::None;
    double minimapDragInitialXMin_ = 0.0;
    double minimapDragInitialXMax_ = 0.0;
    float minimapDragStartMouseX_ = 0.0f;

    std::vector<size_t> discontinuedIndChache_;

  private:
    void generatePlaceholderData();
  };

} // namespace ui