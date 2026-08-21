#pragma once

#include <vector>
#include <string>
#include "ui/plotpanel.h"
#include "core/curve1d.h"
#include "3rdparty/nlohmann/json_fwd.hpp"

namespace core {
  class LogSession;
}

namespace ui {

  class Curve2DPanel : public PlotPanel {
  public:
    explicit Curve2DPanel(const std::string &title = "2D Calibration Curves");

    void render(PlotCursor &cursor) override;

    void setSession(const core::LogSession *session) override;
    std::string panelTypeId() const override { return "Curve2DPanel"; }

    nlohmann::json saveState() const override;
    void loadState(const nlohmann::json &state) override;

  private:
    void renderSidebar();
    void renderCurvePlot(PlotCursor &cursor);
    void renderPointEditor();
    void loadBuiltInPresets();

  private:
    const core::LogSession *session_ = nullptr;
    bool open_ = true;
    std::vector<core::Curve1D> curves_;
    int selectedCurveIdx_ = 0;

    bool showScatter_ = true;
    int maxScatterPoints_ = 1200;
    bool followPlotCursor_ = true;
  };

} // namespace ui