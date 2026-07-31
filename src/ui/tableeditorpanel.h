#pragma once

#include <string>
#include <vector>

#include "3rdparty/nlohmann/json_fwd.hpp"
#include "core/table2d.h"
#include "ui/plotpanel.h"

namespace ui {

  class TableEditorPanel : public PlotPanel {
  public:
    TableEditorPanel(std::string title, std::string panelTypeIdValue, std::string displayName,
      core::Table2D initialTable, int xDecimalPlaces = 0, int yDecimalPlaces = 2);

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

  private:
    static std::string formatValue(double value, int decimalPlaces);
    void renderValueGrid();
    bool renderAxisEditorPopup(const char *title);

  private:
    std::string panelTypeIdValue_;
    std::string displayName_;
    core::Table2D table_;
    bool open_ = true;
    int xDecimalPlaces_ = 0;
    int yDecimalPlaces_ = 2;

    // Fast Cell Selection & Single-Cell Active Editing
    int selectedRow_ = -1;
    int selectedCol_ = -1;
    int editingRow_ = -1;
    int editingCol_ = -1;

    std::vector<double> axisEditorValues_;
    int axisEditorBinCount_ = 0;
    enum class AxisEditing {
      None,
      X,
      Y,
    };
    AxisEditing editingAxis_ = AxisEditing::None;

    float copyToastTimer_ = 0.0f;
  };

} // namespace ui