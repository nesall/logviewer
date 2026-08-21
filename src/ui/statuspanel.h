#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "core/logsession.h"
#include "ui/plotpanel.h"

namespace ui {

  // Grid of LED-style cells showing digital/status channel values at the
  // shared cursor's time position (e.g. "status: RPM Sync", "port:A0
  // Injector A"), similar to MLVHD/TunerStudio's status display.
  //
  // Defaults to auto-detected boolean channels (Channel::isBoolean()), but
  // any channel can be added/removed via the "Channels..." picker popup --
  // visibility is tracked per channel name and persists across session
  // reloads (a channel missing from a newly loaded log just doesn't render).
  class StatusPanel : public PlotPanel {
  public:
    explicit StatusPanel(std::string title = "Status");

    void render(PlotCursor &cursor) override;
    bool wantsClose() const override { return !open_; }

    void setSession(const core::LogSession *session) override;

    std::string panelTypeId() const override { return "StatusPanel"; }
    nlohmann::json saveState() const override;
    void loadState(const nlohmann::json &state) override;

  private:
    void renderChannelPickerPopup();
    void renderCellGrid(const PlotCursor &cursor);
    bool isVisible(const core::Channel &channel) const;
    void setVisible(const std::string &channelName, bool visible);
    size_t nearestIndex(const std::vector<double> &timeSec, double queryTime) const;

    const core::LogSession *session_ = nullptr;
    bool open_ = true;

    // Explicit per-channel overrides. A channel not present here falls
    // back to Channel::isBoolean() as its default visibility -- this is
    // what makes auto-detected boolean channels show up by default with
    // no seeding step needed.
    std::unordered_map<std::string, bool> visibilityOverrides_;

    std::string filterText_;
  };

} // namespace ui