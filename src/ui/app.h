#pragma once

#include <memory>
#include <string>
#include <vector>
#include <functional>
#include <atomic>
#include <future>

#include "core/logsession.h"
#include "ui/plotpanel.h"
#include "io/megasquirtcsvparser.h"
#include "io/haltechdlparser.h"
#include "io/plogparser.h"


struct GLFWwindow;

namespace pfd {
  class open_file;
  class save_file;
} // namespace pfd

namespace ui {
  class DriveRegimePanel;

  // Owns the main window's per-frame UI: dockspace + panels.
  // Does not own the GLFW/OpenGL/ImGui lifecycle -- that's main.cpp's job,
  // but it does hold the GLFWwindow* to update the title bar.
  class App {
  public:
    explicit App(GLFWwindow *window);
    ~App();

    // Called once per frame, between ImGui::NewFrame() and ImGui::Render().
    void render();
    void saveSettings() const;

    static const char *appBaseTitle();

    void requestExit();
    bool wantsExit() const { return readyToExit_; }

  private:
    enum class PendingAction {
      None,
      Exit,
      NewWorkspace,
      LoadWorkspace,
      CloseLog,
      LoadLog
    };

  private:
    void renderDockspace();
    void renderWelcomeLanding();
    void renderMenuBar();
    void renderLoadErrorPopup();
    void renderCustomChannelModal();
    void renderChannelMappingModal();

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
    void updateWindowTitle();

    void refreshPanelsFromSession();
    void notifyRegimesUpdated();
    PlotPanel *addTimeSeriesPanel(const std::vector<std::string> &initialChannelNames = {}, std::string explicitTitle = "");
    PlotPanel *addScatterPanel(const std::string &initialXChannel = "", const std::string &initialYChannel = "", std::string explicitTitle = "");
    PlotPanel *addStatusPanel(std::string explicitTitle = "");
    PlotPanel *addVeAnalysisPanel(std::string explicitTitle = "");
    PlotPanel *addTableOverlayPanel(std::string explicitTitle = "");
    DriveRegimePanel *getOrAddDriveRegimePanel();

    int getNextPanelIdForPrefix(const std::string &prefix) const;

    void saveWorkspace(); // Direct Save (Ctrl+S) or fallback to Save As...
    void markDirty();

    void beginAppendLogDialog();
    void initConcatModalResolutions();
    void renderConcatLogModal();
    void startAsyncConcatLoad(const std::string &path);

    void renderLoadProgressModal();
    void startAsyncLogLoad(const std::string &path, std::function<void()> onComplete = nullptr);

    void requestActionWithDirtyCheck(PendingAction action, std::string pathPayload = "");
    void executePendingAction();
    void renderSavePromptModal();

    void beginExportLogDialog();
    void addRecentLog(const std::string &path);

  private:
    bool showDemoWindow_ = false;
    bool firstFrame_ = true;
    float welcomeFadeTimer_ = 0.0f;

    bool showCustomChannelModal_ = false;
    bool showChannelMappingModal_ = false;
    char customNameBuf_[64] = "";
    char customUnitBuf_[32] = "";
    char customFormulaBuf_[256] = "";
    std::string customChannelError_;

    GLFWwindow *window_ = nullptr;
    std::string currentWorkspacePath_;

    PlotCursor cursor_;
    std::vector<std::unique_ptr<PlotPanel>> panels_;

    io::MegasquirtCsvParser mslParser_;
    io::HaltechDlParser haltechParser_;
    io::PlogParser plogParser_;

    core::LogSession session_;

    bool showLoadErrorPopup_ = false;
    std::string loadErrorMessage_;

    std::unique_ptr<pfd::open_file> pendingOpenLogDialog_;
    std::unique_ptr<pfd::save_file> pendingSaveWorkspaceDialog_;
    std::unique_ptr<pfd::open_file> pendingLoadWorkspaceDialog_;

    static constexpr size_t kMaxRecentWorkspaces = 5;
    std::vector<std::string> recentWorkspacePaths_;
    std::vector<std::string> recentLogPaths_;
    static constexpr size_t kMaxRecentLogs = 5;

    std::string pendingRecentLogLoad_;
    std::string pendingRecentWorkspaceLoad_;

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

    bool isDirty_ = false;
    bool readyToExit_ = false;
    bool pendingSaveBeforeExit_ = false;

    PendingAction pendingAction_ = PendingAction::None;
    std::string pendingActionPathPayload_;
    bool showSavePromptModal_ = false;

  private:
    std::unique_ptr<pfd::open_file> pendingAppendLogDialog_;
    core::LogSession incomingConcatSession_;
    std::string incomingConcatPath_;
    bool showConcatModal_ = false;

    core::StitchPosition concatPosition_ = core::StitchPosition::AppendToEnd;
    std::vector<core::ChannelMergeResolution> concatResolutions_;

    std::unique_ptr<pfd::save_file> pendingExportLogDialog_;
  };

} // namespace ui