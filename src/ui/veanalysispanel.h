#pragma once

#include <memory>
#include <string>
#include <vector>
#include "core/logsession.h"
#include "engine/ve_analyzer.h"
#include "ui/plotpanel.h"
#include "ui/tableeditorpanel.h"

namespace ui {

  class VeAnalysisPanel : public PlotPanel {
  public:
    explicit VeAnalysisPanel(std::string title);

    void render(PlotCursor &cursor) override;
    bool wantsClose() const override { return !open_; }
    void setSession(const core::LogSession *session) override;

    std::string panelTypeId() const override { return "VeAnalysisPanel"; }
    nlohmann::json saveState() const override;
    void loadState(const nlohmann::json &state) override;

  private:
    void computeObservedAfr();
    void computeSuggestedVe();
    void renderObservedAfrTab();
    void renderTargetAfrTab();
    void renderBaselineVeTab();
    void renderSuggestedVeTab();
    void selectUnvisitedCellsOnSuggestedVe();

    void computeAfrDelta();
    void renderAfrDeltaTab();

    bool hasBaselineVe() const;
    bool hasTargetAfr() const;

    void onRegimesUpdated() override;

    void copyTableToClipboard(const core::Table2D &table, bool includeHeaders, int decimalPlaces = 2);
    float copyToastTimer_ = 0.0f;
    void renderToast();

    const core::LogSession *session_ = nullptr;
    bool open_ = true;

    engine::VeAnalysisConfig config_;

    // Core tables
    core::Table2D observedAfrTable_;
    TableEditorPanel targetAfrPanel_;
    TableEditorPanel baselineVePanel_;
    TableEditorPanel suggestedVePanel_;
    core::Table2D afrDeltaTable_;
    bool hasAfrDelta_ = false;
    bool alignAfrDeltaToVeTable_ = true;
    bool scaleDeltaIntensityByHitCount_ = false;
    std::vector<std::vector<size_t>> afrDeltaSampleCounts_;

    // Editing buffers for popups
    core::Table2D targetAfrEditBuffer_;
    core::Table2D baselineVeEditBuffer_;

    bool hasSuggestedVe_ = false;
    int smoothIterations_ = 1;

    // Cropped axis bounds for Observed AFR
    size_t minObservedRow_ = 0;
    size_t maxObservedRow_ = 0;
    size_t minObservedCol_ = 0;
    size_t maxObservedCol_ = 0;
    bool hasObservedData_ = false;

    int hoveredRow_ = -1;
    int hoveredCol_ = -1;
  };

} // namespace ui