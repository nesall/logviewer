#pragma once

#include "ui/plotpanel.h"
#include "engine/virtualdyno.h"
#include "3rdparty/nlohmann/json_fwd.hpp"
#include <string>

namespace ui {

  class VirtualDynoPanel : public PlotPanel {
  public:
    explicit VirtualDynoPanel(const std::string &title);

    std::string panelTypeId() const override { return "VirtualDynoPanel"; }

    void render(PlotCursor &cursor) override;

    void setSession(const core::LogSession *session) override;

    nlohmann::json saveState() const override;
    void loadState(const nlohmann::json &state) override;

  private:
    void renderSidebar();
    void renderDynoPlot();
    void recomputeDyno();

    const core::LogSession *session_ = nullptr;
    engine::VehicleDynoProfile profile_;
    engine::DynoResult result_;

    // Pull Selection Source
    int pullMode_ = 0; // 0 = Manual Time Range, 1 = Drive Regime
    double manualStartSec_ = 0.0;
    double manualEndSec_ = 10.0;
    std::string selectedRegimeId_;   // RegimeDef::id of the chosen regime (mode 1)
    int regimeSubMode_ = 0;          // 0 = First Event, 1 = All Events (Averaged)
    bool open_ = false;
    bool showBoostCurve_ = true;
  };

} // namespace ui