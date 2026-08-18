#pragma once

#include <string>
#include <functional>

#include "3rdparty/nlohmann/json_fwd.hpp"

namespace core {
  class LogSession;
} // namespace core

namespace ui {

  // Shared time cursor, synced across all panels. Panels write to it when
  // hovered, and read it to draw a marker even when a different panel is
  // being hovered.
  struct PlotCursor {
    double timeSec = 0.0;
    bool active = false;
  };

  // Base interface for a single dockable plot/data panel.
  class PlotPanel {
  public:
    explicit PlotPanel(std::string title);
    virtual ~PlotPanel() = default;

    // Render the panel for this frame. May update 'cursor'.
    virtual void render(PlotCursor &cursor) = 0;

    // True if the user closed the panel. Default: false.
    virtual bool wantsClose() const { return false; }

    // Called after a log loads/reloads. Default: no-op.
    virtual void setSession(const core::LogSession * /*session*/) {}

    // Stable identifier used in workspace files; keep stable across versions.
    virtual std::string panelTypeId() const = 0;

    // Persist/restore panel-specific selection state. Defaults represent "nothing to save".
    virtual nlohmann::json saveState() const;
    virtual void loadState(const nlohmann::json &state);

    // Notified when Regimes change. Default: no-op.
    virtual void onRegimesUpdated() {}

    void setOnDataChangedCallback(std::function<void()> callback) { onDataChanged_ = std::move(callback); }

    const std::string &title() const { return title_; }

  protected:
    virtual void notifyDataChanged() { if (onDataChanged_) onDataChanged_(); }

  private:
    std::string title_;
    std::function<void()> onDataChanged_;
  };

} // namespace ui