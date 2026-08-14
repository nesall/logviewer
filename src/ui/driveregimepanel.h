#pragma once

#include <vector>
#include <string>
#include <functional>
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

    const std::vector<core::RegimeSummary> &regimes() const;
    std::vector<core::RegimeSummary> &regimes();

    void setOnRegimesChangedCallback(std::function<void()> callback) { onRegimesChanged_ = std::move(callback); }

  private:
    void reanalyze();
    void renderConfigModal();
    void notifyRegimesChanged() { if (onRegimesChanged_) onRegimesChanged_(); }
    std::function<void()> onRegimesChanged_;
    const core::LogSession *session_ = nullptr;
    bool open_ = true;    
    bool showConfigModal_ = false;
    std::vector<core::RegimeDef> userDefinitions_;
  };

} // namespace ui