#pragma once

#include <vector>
#include <string>
#include "core/logsession.h"
#include "core/regime.h"
#include "ui/plotpanel.h"

namespace ui {

  class DriveRegimePanel : public PlotPanel {
  public:
    explicit DriveRegimePanel(std::string title = "Drive Regime Summary");

    void render(PlotCursor &cursor) override;
    bool wantsClose() const override { return !open_; }
    void setSession(const core::LogSession *session) override;

    std::string panelTypeId() const override { return "DriveRegimePanel"; }
    nlohmann::json saveState() const override;
    void loadState(const nlohmann::json &state) override;

    const std::vector<core::RegimeSummary> &regimes() const { return summaries_; }
    std::vector<core::RegimeSummary> &regimes() { return summaries_; }

  private:
    void reanalyze();

    const core::LogSession *session_ = nullptr;
    bool open_ = true;

    std::vector<core::RegimeSummary> summaries_;
  };

} // namespace ui