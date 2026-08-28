#pragma once

#include <string>
#include <vector>
#include <set>
#include <utility>
#include <optional>
#include <functional>

#include "3rdparty/nlohmann/json_fwd.hpp"
#include "core/table2d.h"
#include "ui/plotpanel.h"

#include <imgui.h>

namespace core {
  class LogSession;
}

namespace ui {

  class TableEditorPanel : public PlotPanel {
  public:
    TableEditorPanel(std::string title, std::string panelTypeIdValue, std::string displayName,
      core::Table2D initialTable, int xDecimalPlaces = 0, int yDecimalPlaces = 2);

    void triggerUpdated();

    void render(PlotCursor &cursor) override;
    bool wantsClose() const override { return !open_; }

    std::string panelTypeId() const override { return panelTypeIdValue_; }
    nlohmann::json saveState() const override;
    void loadState(const nlohmann::json &state) override;

    const core::Table2D &table() const { return table_; }
    void setTable(core::Table2D newTable) { table_ = std::move(newTable); }

    void copyToClipboard(bool includeHeaders = true) const;
    void pasteFromClipboard();
    bool importTunerStudioXml(const std::string &xmlContent);

    void showCopyToast() { copyToastTimer_ = 1.5f; }
    void renderToast();

    // Batch editing API
    void applyBatchMultiply(double factor);
    void applyBatchOffset(double delta);
    void applyBatchSetValue(double value);
    void interpolateSelectedRegion();
    void interpolateSelectionVorH(bool vertical);
    void applyExtrapolationPreview();
    void setBatchToolbarVisible(bool visible) { batchToolbarVisible_ = visible; }
    bool isBatchToolbarVisible() const { return batchToolbarVisible_; }
    void setReadOnly(bool f);
    bool isReadOnly() const { return readOnly_; }

    void setSelection(const std::set<std::pair<int, int>> &cells);
    const std::set<std::pair<int, int>> &selectedCells() const { return selectedCells_; }
    bool isCellSelected(int r, int c) const;

    void setCustomTextColoring(std::function<std::optional<ImU32>(double, size_t, size_t)> colorFunc) { customTextColorFunc_ = std::move(colorFunc); }
    void setCustomHeatmapColoring(std::function<ImU32(double, size_t, size_t)> colorFunc) { customHeatmapColorFunc_ = std::move(colorFunc); }
    void setCustomHoverTooltip(std::function<std::string(double, size_t, size_t)> tooltipFunc) { customHoverTooltipFunc_ = std::move(tooltipFunc); }
    void setCustomToolbar1Callback(std::function<void()> callback) { customToolbar1Callback_ = std::move(callback); }
    void setCustomToolbar2Callback(std::function<void()> callback) { customToolbar2Callback_ = std::move(callback); }

    void setReplaceNanWith(std::string str) { nanReplacement_ = std::move(str); }

    void pushUndoState();

    void setSession(const core::LogSession *session) override;
    void setCustomAxisChannels(std::optional<std::string> xChannel = std::nullopt, std::optional<std::string> yChannel = std::nullopt);
    void recomputeRegimeCoverage();
    void selectCellsInRegime(const std::string &regimeId);

  private:
    static std::string formatValue(double value, int decimalPlaces);
    void renderValueGrid();
    void render3DSurfaceMesh();
    void renderBatchToolbar();
    bool renderAxisEditorPopup(const char *title);
    void renderExtrapolateModal();

    void clearSelection();
    void selectRectangularRegion(int r1, int c1, int r2, int c2, bool keepExisting = false);
    void getSelectionBounds(int &minR, int &maxR, int &minC, int &maxC) const;

    void undo();
    void redo();
    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }

    bool isQuadSelected(size_t r, size_t c) const;

  private:
    std::string tableUniqueId_;
    std::string panelTypeIdValue_;
    std::string displayName_;
    core::Table2D table_;
    bool open_ = true;
    bool show3DView_ = false;
    int xDecimalPlaces_ = 0;
    int yDecimalPlaces_ = 2;

    // 3D camera state
    float cameraYaw_ = 45.0f;
    float cameraPitch_ = 30.0f;
    float cameraZoom_ = 1.0f;
    ImVec2 cameraPan_ = ImVec2(0.0f, 0.0f);

    // Disjoint multi-cell selection tracking (row, col)
    std::set<std::pair<int, int>> selectedCells_;

    // Anchor cell for Shift-click and Drag operations
    int anchorRow_ = -1;
    int anchorCol_ = -1;

    // Single-cell active text edit mode
    int editingRow_ = -1;
    int editingCol_ = -1;

    // Batch modification buffer
    double batchValue_ = 1.05;

    core::Table2D tableBackup_;
    bool readOnly_ = false;
    bool batchToolbarVisible_ = true;
    bool showExtrapolateModal_ = false;
    bool wasExtrapolateModalOpen_ = false;
    bool showPreview_ = true;
    int slopeMode_ = 0;
    float guessTolerance_ = 10.f;
    float manualSlope_ = 1.f;

    int hoveredGridRow_ = -1;
    int hoveredGridCol_ = -1;

    std::vector<double> axisEditorValues_;
    int axisEditorBinCount_ = 0;
    enum class AxisEditing {
      None,
      X,
      Y,
    };
    AxisEditing editingAxis_ = AxisEditing::None;

    struct TableUndoSnapshot {
      core::Table2D table;
      std::set<std::pair<int, int>> selection;
    };
    std::vector<TableUndoSnapshot> undoStack_;
    std::vector<TableUndoSnapshot> redoStack_;
    static constexpr size_t kMaxUndoHistory = 50;
    float copyToastTimer_ = 0.0f;
    std::function<std::optional<ImU32>(double, size_t, size_t)> customTextColorFunc_;
    std::function<ImU32(double, size_t, size_t)> customHeatmapColorFunc_;
    std::function<std::string(double, size_t, size_t)> customHoverTooltipFunc_;
    std::function<void()> customToolbar1Callback_;
    std::function<void()> customToolbar2Callback_;
    std::string nanReplacement_{ "-" };

    const core::LogSession *session_ = nullptr;
    std::optional<std::string> customXAxisChannel_;
    std::optional<std::string> customYAxisChannel_;
    struct CellRegimeInfo {
      ImVec4 blendedColor = ImVec4(0, 0, 0, 0);
      double totalDwellSec = 0.0;
      std::vector<std::pair<std::string, double>> regimeDwellList; // Name -> Seconds
    };
    // Matrix matching grid [rows][cols]
    std::vector<std::vector<CellRegimeInfo>> regimeCoverageMatrix_;
  };

} // namespace ui