#pragma once

#include <string>

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

    // Renders the panel's ImGui window and contents for this frame.
    virtual void render(PlotCursor &cursor) = 0;

    // True once the user has closed this panel (e.g. via the window's
    // close button). App removes panels that return true after render().
    // Default false for panel types that don't support closing.
    virtual bool wantsClose() const { return false; }

    // Called by App after a log loads (or reloads). Default no-op for
    // panel types that don't bind to session data directly.
    virtual void setSession(const core::LogSession * /*session*/) {}

    // Stable type tag used in saved workspace files to know which
    // concrete panel type to reconstruct on load. Keep existing values
    // stable across versions -- renaming breaks old workspace files.
    virtual std::string panelTypeId() const = 0;

    // Panel-specific selection state (which channel(s) are chosen, etc.)
    // for workspace save/load. Defaults to "nothing to save" for panel
    // types that don't need it.
    virtual nlohmann::json saveState() const;
    virtual void loadState(const nlohmann::json &state);

    // Called by App after Regimes change.
    virtual void onRegimesUpdated() {}

    const std::string &title() const { return title_; }

  private:
    std::string title_;
  };

} // namespace ui