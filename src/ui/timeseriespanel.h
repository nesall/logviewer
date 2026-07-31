#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "core/logsession.h"
#include "ui/plotpanel.h"

#include <imgui.h>

namespace ui {

  class TimeSeriesPanel : public PlotPanel {
  public:
    TimeSeriesPanel(std::string title, std::vector<std::string> initialChannelNames = {});

    void render(PlotCursor &cursor) override;
    bool wantsClose() const override { return !open_; }

    void setSession(const core::LogSession *session) override;

    std::string panelTypeId() const override { return "TimeSeries"; }
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
    };

    struct ArtifactCursor {
      double timeSec = 0.0;
      bool active = false;
      std::string label; // e.g., "RPM Max (7200.0)"
      ImVec4 color = ImVec4(0.9f, 0.9f, 0.3f, 0.4f); // Dimmed/grayed out
    };

    ArtifactCursor artifactCursor_;

    void rebindChannels();
    void renderHeaderControls();
    void renderLeftSidebar(PlotCursor &cursor);
    void renderPlotArea(PlotCursor &cursor);
    size_t getCursorIndex(double queryTime) const;
    void jumpCursorToIndex(size_t index, PlotCursor &cursor, const std::string &label = "");

    bool pendingXAxisCenter_ = false;
    double targetXCenterTime_ = 0.0;
    
    const core::LogSession *session_ = nullptr;
    bool open_ = true;
    bool showSidebar_ = true;
    int maxChannelsPerPlot_ = 4;

    std::string filterText_;

    std::unordered_map<std::string, ChannelState> channelStates_;

    const std::vector<double> *cachedTimeSec_ = nullptr;
    std::vector<double> placeholderTimeSec_;
    std::vector<double> placeholderValue_;

    void generatePlaceholderData();
  };

} // namespace ui