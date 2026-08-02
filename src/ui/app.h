#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <future>

#include "core/logsession.h"
#include "io/megasquirtcsvparser.h"
#include "ui/plotpanel.h"

struct GLFWwindow;

namespace pfd {
  class open_file;
  class save_file;
} // namespace pfd

namespace ui {

  // Owns the main window's per-frame UI: dockspace + panels.
  // Does not own the GLFW/OpenGL/ImGui lifecycle -- that's main.cpp's job,
  // but it does hold the GLFWwindow* to update the title bar.
  class App {
  public:
    explicit App(GLFWwindow *window);
    ~App();

    // Called once per frame, between ImGui::NewFrame() and ImGui::Render().
    void render();

  private:
    void renderDockspace();
    void renderMenuBar();
    void renderLoadErrorPopup();
    void renderCustomChannelModal();

    // File dialogs are non-blocking: opening one just creates it, and
    // pollPendingDialogs_() checks each frame whether the user has
    // finished with it yet. Never call pfd's blocking .result() directly
    // from a menu click handler -- that freezes the render loop (and the
    // whole app) for as long as the native dialog is open, which is a
    // known source of crashes/hangs with ImGui's immediate-mode loop.
    void pollPendingDialogs();
    void beginOpenLogDialog();
    void beginSaveWorkspaceDialog();
    void beginLoadWorkspaceDialog();

    void loadLogFile(const std::string &path);
    void saveWorkspaceFile(const std::string &path);
    void loadWorkspaceFile(const std::string &path);
    void addRecentWorkspace(const std::string &path);
    void loadSettings();
    void saveSettings() const;
    void updateWindowTitle();

    void refreshPanelsFromSession();
    PlotPanel *addTimeSeriesPanel(const std::vector<std::string> &initialChannelNames);
    PlotPanel *addScatterPanel(const std::string &initialXChannel, const std::string &initialYChannel);
    PlotPanel *addStatusPanel();
    //PlotPanel *addTableEditorPanel(const std::string &panelTypeIdValue, const std::string &displayName);
    PlotPanel *addVeAnalysisPanel();
    // After a workspace load restores each panel's title() via loadState(),
    // bump nextPanelId_ past any trailing number found in those titles --
    // otherwise a panel added later via the Panels menu could reuse an ID
    // (and thus an ImGui window ID) that a reloaded panel already has.
    void bumpNextPanelIdPastLoadedTitles();

    bool showDemoWindow_ = false;
    bool firstFrame_ = true;

    bool showCustomChannelModal_ = false;
    char customNameBuf_[64] = "";
    char customUnitBuf_[32] = "";
    char customFormulaBuf_[256] = "";
    std::string customChannelError_;

    GLFWwindow *window_ = nullptr;
    std::string currentWorkspacePath_; // empty until a workspace is saved/loaded

    PlotCursor cursor_;
    std::vector<std::unique_ptr<PlotPanel>> panels_;
    int nextPanelId_ = 1;

    io::MegasquirtCsvParser parser_;
    core::LogSession session_;

    bool showLoadErrorPopup_ = false;
    std::string loadErrorMessage_;

    std::unique_ptr<pfd::open_file> pendingOpenLogDialog_;
    std::unique_ptr<pfd::save_file> pendingSaveWorkspaceDialog_;
    std::unique_ptr<pfd::open_file> pendingLoadWorkspaceDialog_;

    // Most-recent-first, capped at kMaxRecentWorkspaces. In-memory only
    // for now -- resets on app restart. Say the word if you'd rather this
    // persist across restarts (small addition: read/write a tiny settings
    // file alongside the executable).
    static constexpr size_t kMaxRecentWorkspaces = 5;
    std::vector<std::string> recentWorkspacePaths_;

    // Set when a "Recent Workspaces" menu item is clicked, consumed right
    // after the menu finishes rendering. Loading immediately from inside
    // the menu's loop would mutate recentWorkspacePaths_ (via
    // addRecentWorkspace_) while we're still iterating over it.
    std::string pendingRecentWorkspaceLoad_;

    void renderLoadProgressModal();
    void startAsyncLogLoad(const std::string &path, std::function<void()> onComplete = nullptr);

    // Async loader state
    bool showProgressModal_ = false;
    std::atomic<float> loadProgress_{ 0.0f };
    std::atomic<bool> loadCancelRequested_{ false };
    std::string progressStatusText_;

    struct LoadResult {
      bool success = false;
      core::LogSession session;
      std::string error;
    };
    std::future<LoadResult> activeLoadTask_;
    std::function<void()> pendingOnCompleteCallback_;
  };

} // namespace ui