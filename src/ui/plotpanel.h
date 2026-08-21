#pragma once

#include "3rdparty/nlohmann/json_fwd.hpp"
#include <string>
#include <functional>
#include "imgui.h"

namespace core {
  class LogSession;
}

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
    std::string panelInstanceId() const { return panelInstanceId_; }
    std::string lastWindowLabel() const { return lastWindowLabel_; }
    void setLoadedFromWorkspace() { justLoaded_ = true; }
    void setActivateOnLoad() { activateOnLoad_ = true; }

  protected:
    virtual void notifyDataChanged() { if (onDataChanged_) onDataChanged_(); }
    std::string makeWindowLabel(const char *icon) const;
    void renderContextMenu();
    void renderCommonOps();
    ImGuiWindowFlags getAppearanceFlags() {
      ImGuiWindowFlags flags = justLoaded_ ? ImGuiWindowFlags_NoFocusOnAppearing : 0;
      justLoaded_ = false; // Consume the flag so it only applies to the first frame
      return flags;
    }
    virtual bool allowTitleEdit() const { return true; }

  private:
    std::string panelInstanceId_;
    std::string title_;
    std::string lastWindowLabel_;
    std::function<void()> onDataChanged_;
    bool showRenameModal_ = false;
    bool justLoaded_ = false;    
    bool activateOnLoad_ = false;
  };

} // namespace ui