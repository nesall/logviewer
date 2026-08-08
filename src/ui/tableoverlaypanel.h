#pragma once

#include <memory>
#include <string>
#include <vector>

#include "core/logsession.h"
#include "ui/plotpanel.h"
#include "ui/tableeditorpanel.h"

namespace ui {

  enum class BaseTableSource {
    ManualPasted,  // User-edited / imported table (e.g. pasted Ignition Timing)
    LoggedAverage, // Cell values show the average of a selected log channel
    Empty          // Plain grid showing only heatmap values
  };

  enum class OverlayAggregation {
    Max,    // Peak value per cell (ideal for EGT, Knock)
    Average,// Mean value per cell
    Min,    // Minimum value per cell
    Count   // Log sample hit count per cell
  };

  class TableOverlayPanel : public PlotPanel {
  public:
    explicit TableOverlayPanel(std::string title);

    void render(PlotCursor &cursor) override;
    bool wantsClose() const override { return !open_; }
    void setSession(const core::LogSession *session) override;

    std::string panelTypeId() const override { return "TableOverlayPanel"; }
    nlohmann::json saveState() const override;
    void loadState(const nlohmann::json &state) override;

  private:
    void rebindChannels();
    void computeOverlayMatrix();
    void applyCustomColoring();

    const core::LogSession *session_ = nullptr;
    bool open_ = true;

    // Axis & Channel selection
    std::string xAxisChannel_ = "RPM";
    std::string yAxisChannel_ = "MAP";
    std::string baseValueChannel_ = "Ignition"; // Used when BaseTableSource == LoggedAverage
    std::string overlayChannel_ = "EGT1";

    BaseTableSource baseSource_ = BaseTableSource::ManualPasted;
    OverlayAggregation aggregation_ = OverlayAggregation::Max;

    // Binned matrix storage aligned with innerTable_'s grid dimensions
    std::vector<std::vector<double>> binnedOverlayData_;
    std::vector<std::vector<size_t>> binnedSampleCounts_;
    double overlayMin_ = 0.0;
    double overlayMax_ = 0.0;
    bool hasOverlayData_ = false;

    bool enableOverlayFilter_ = false;
    float overlayFilterMin_ = 0.0f;
    float overlayFilterMax_ = 0.0f;
    bool overlayFilterMinMaxSet_ = false;

    // Embedded Table Editor for full grid manipulation, import/export, and rendering
    TableEditorPanel innerTable_;
  };

} // namespace ui