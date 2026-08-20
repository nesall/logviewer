#include "ui/app.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <cassert>

#include "3rdparty/nlohmann/json.hpp"
#include "3rdparty/portable-file-dialogs.h"
#include "3rdparty/WinDarkTitlebarImpl.h"
#include "3rdparty/IconsFontAwesome7.h"
#include "core/table2d.h"
#include "core/formula_evaluator.h"
#include "ui/ui_helpers.h"

#include "imgui.h"
#include "imgui_internal.h"
#include <GLFW/glfw3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

#include "ui/scatterpanel.h"
#include "ui/statuspanel.h"
#include "ui/tableeditorpanel.h"
#include "ui/timeseriespanel.h"
#include "ui/veanalysispanel.h"
#include "ui/tableoverlaypanel.h"
#include "ui/driveregimepanel.h"
#include "ui/curve2dpanel.h"
#include "utils/utils.h"

namespace ui {

  namespace {

    std::string currentDateStamp() {
      using namespace std::chrono;
      const auto today = floor<days>(system_clock::now());
      const year_month_day ymd{ today };
      char buf[16];
      std::snprintf(buf, sizeof(buf), "%04d%02d%02d", static_cast<int>(ymd.year()),
        static_cast<unsigned>(ymd.month()), static_cast<unsigned>(ymd.day()));
      return buf;
    }

    // Stored next to the executable for now -- simplest thing that works for
    // a dev-testing tool. Move to a proper per-user config directory
    // (%AppData% / ~/.config) if this ever ships beyond your own machine.
    const char *kSettingsFilePath = "logviewer_settings.json";

  } // namespace

  App::App(GLFWwindow *window) : window_(window)
  {
    loadSettings();
    updateWindowTitle();
#ifdef _WIN32
    HWND hWnd = glfwGetWin32Window(window_);
    WinDarkTitlebarImpl winDarkImpl;
    winDarkImpl.init();
    winDarkImpl.setTitleBarTheme(hWnd, true);
#endif
  }

  App::~App() = default;

  // ---------------------------------------------------------------------
  // File dialogs (non-blocking -- see the comment on pollPendingDialogs_
  // in app.h for why this matters)
  // ---------------------------------------------------------------------

  void App::beginOpenLogDialog()
  {
    if (pendingOpenLogDialog_) return;
    pendingOpenLogDialog_ = std::make_unique<pfd::open_file>(
      "Open Log File", ".",
      std::vector<std::string>{
      "All Supported Logs (*.plog, *.msl, *.dl)", "*.plog *.msl *.dl",
        "Binary Log Files (*.plog)", "*.plog",
        "MegaSquirt Log Files (*.msl)", "*.msl",
        "Haltech Log Files (*.dl)", "*.dl",
        "All Files", "*"
    });
  }

  void App::beginExportLogDialog()
  {
    if (pendingExportLogDialog_) return;
    pendingExportLogDialog_ = std::make_unique<pfd::save_file>(
      "Export Log File", "log-" + currentDateStamp() + ".plog",
      std::vector<std::string>{
      "Binary Log File (*.plog)", "*.plog",
        "All Files", "*"
    });
  }

  void App::beginSaveWorkspaceDialog()
  {
    if (pendingSaveWorkspaceDialog_) {
      return;
    }
    pendingSaveWorkspaceDialog_ = std::make_unique<pfd::save_file>(
      "Save Workspace", "workspace-" + currentDateStamp() + ".json",
      std::vector<std::string>{"Log Viewer Workspace (*.json)", "*.json", "All Files", "*"});
  }

  void App::beginLoadWorkspaceDialog()
  {
    if (pendingLoadWorkspaceDialog_) {
      return;
    }
    pendingLoadWorkspaceDialog_ = std::make_unique<pfd::open_file>(
      "Load Workspace", ".",
      std::vector<std::string>{"Log Viewer Workspace (*.json)", "*.json", "All Files", "*"});
  }

  void App::pollPendingDialogs()
  {
    if (pendingOpenLogDialog_ && pendingOpenLogDialog_->ready(0)) {
      auto selection = pendingOpenLogDialog_->result();
      pendingOpenLogDialog_.reset();
      if (!selection.empty()) {
        requestActionWithDirtyCheck(PendingAction::LoadLog, selection.front());
      }
    }

    if (pendingSaveWorkspaceDialog_ && pendingSaveWorkspaceDialog_->ready(0)) {
      std::string path = pendingSaveWorkspaceDialog_->result();
      pendingSaveWorkspaceDialog_.reset();
      if (!path.empty()) {
        saveWorkspaceFile(path);
        isDirty_ = false;
        if (pendingAction_ != PendingAction::None) {
          executePendingAction();
        }
      }
    }

    if (pendingLoadWorkspaceDialog_ && pendingLoadWorkspaceDialog_->ready(0)) {
      auto selection = pendingLoadWorkspaceDialog_->result();
      pendingLoadWorkspaceDialog_.reset();
      if (!selection.empty()) {
        // User selected a file from dialog -> route through dirty check
        requestActionWithDirtyCheck(PendingAction::LoadWorkspace, selection.front());
      }
    }

    if (pendingAppendLogDialog_ && pendingAppendLogDialog_->ready(0)) {
      auto selection = pendingAppendLogDialog_->result();
      pendingAppendLogDialog_.reset();
      if (!selection.empty()) {
        startAsyncConcatLoad(selection.front());
      }
    }

    if (pendingExportLogDialog_ && pendingExportLogDialog_->ready(0)) {
      std::string exportPath = pendingExportLogDialog_->result();
      pendingExportLogDialog_.reset();
      if (!exportPath.empty()) {
        savingPlogTask_ = std::async(std::launch::async, [this, exportPath]()
          {
            std::string err;
            bool res = io::PlogParser::write(exportPath, session_, err);
            if (!res) {
              saveErrorMessage_ = err;
              showSaveErrorPopup_ = true;
            } else {
              addRecentLog(exportPath);
            }
            isSavingPlog_ = false;
            return res;
          });
      }
    }
  }

  // ---------------------------------------------------------------------
  // Log loading
  // ---------------------------------------------------------------------

  void App::loadLogFile(const std::string &path)
  {
    startAsyncLogLoad(path);
  }

  // ---------------------------------------------------------------------
  // Workspace save/load
  // ---------------------------------------------------------------------

  void App::saveWorkspaceFile(const std::string &path)
  {
    nlohmann::json root;
    // NOTE: logFilePath is strictly omitted to decouple workspace from log session

    root["channelMapping"] = session_.channelMapping().toJson();

    nlohmann::json customChannelsJson = nlohmann::json::array();
    for (const auto &channel : session_.channels()) {
      if (channel.isCustom()) {
        customChannelsJson.push_back({
          {"name", channel.name()},
          {"unit", channel.unit()},
          {"formula", channel.formula()}
          });
      }
    }
    root["customChannels"] = customChannelsJson;

    nlohmann::json panelsJson = nlohmann::json::array();
    for (const auto &panel : panels_) {
      panelsJson.push_back({ {"type", panel->panelTypeId()}, {"state", panel->saveState()} });
#ifdef _DEBUG2
      std::cout << "Saving panel: " << panel->panelTypeId() << " state: " << panel->saveState().dump() << std::endl;
#endif
    }
    root["panels"] = panelsJson;

    root["cursor"] = {
      {"timeSec", cursor_.timeSec},
      {"active", cursor_.active}
    };

    // Capture ImGui docking / window positions. Restoring this depends on
    // each panel's window ID (its "###title" suffix) matching between
    // save and load -- title is restored via PlotPanel::loadState().
    size_t iniSize = 0;
    const char *iniData = ImGui::SaveIniSettingsToMemory(&iniSize);
    if (iniData != nullptr && iniSize > 0) {
      root["imguiLayout"] = std::string(iniData, iniSize);
    }

    std::ofstream file(path);
    if (!file.is_open()) {
      loadErrorMessage_ = "Could not write workspace file: " + path;
      showLoadErrorPopup_ = true;
      return;
    }
    file << root.dump(2);
    currentWorkspacePath_ = path;
    updateWindowTitle();
    addRecentWorkspace(path);
  }

  void App::loadWorkspaceFile(const std::string &path)
  {
    std::ifstream file(path);
    if (!file.is_open()) {
      loadErrorMessage_ = "Could not open workspace file: " + path;
      showLoadErrorPopup_ = true;
      return;
    }

    nlohmann::json root;
    try {
      file >> root;
    } catch (const std::exception &e) {
      loadErrorMessage_ = std::string("Invalid workspace file: ") + e.what();
      showLoadErrorPopup_ = true;
      return;
    }

    panels_.clear();
    ImGui::ClearIniSettings();

    addRecentWorkspace(path);
    currentWorkspacePath_ = path;
    updateWindowTitle();

    if (root.contains("customChannels") && root["customChannels"].is_array()) {
      for (const auto &customJson : root["customChannels"]) {
        if (!customJson.contains("name") || !customJson.contains("formula")) continue;

        core::CustomChannelDef def;
        def.name = customJson["name"].get<std::string>();
        def.unit = customJson.value("unit", "");
        def.formula = customJson["formula"].get<std::string>();

        std::string err;
        core::Channel customCh = core::FormulaEvaluator::evaluate(def, session_, err);
        if (err.empty()) {
          session_.addChannel(std::move(customCh));
        }
      }
    }

    if (root.contains("channelMapping") && root["channelMapping"].is_object()) {
      auto mapping = core::ChannelMapping::fromJson(root["channelMapping"]);
      session_.setChannelMapping(mapping);
    }

    if (root.contains("imguiLayout") && root["imguiLayout"].is_string()) {
      std::string iniData = root["imguiLayout"].get<std::string>();
      ImGui::LoadIniSettingsFromMemory(iniData.c_str(), iniData.size());
      ImGuiID hostId = ImHashStr("DockSpaceHost");
      ImGuiID dockspaceId = ImHashStr("MainDockSpace", 0, hostId);
      if (ImGuiDockNode *node = ImGui::DockBuilderGetNode(dockspaceId)) {
        ImGui::DockBuilderSetNodePos(dockspaceId, ImGui::GetMainViewport()->WorkPos);
        ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->WorkSize);
      }
    }

    if (root.contains("panels") && root["panels"].is_array()) {
      for (const auto &panelJson : root["panels"]) {
        if (!panelJson.contains("type") || !panelJson["type"].is_string()) continue;

        std::string type = panelJson["type"].get<std::string>();
        nlohmann::json state = panelJson.contains("state") ? panelJson["state"] : nlohmann::json::object();
        std::string savedTitle = state.value("title", "");

        std::unique_ptr<PlotPanel> panel;
        if (type == "TimeSeries") panel = std::make_unique<TimeSeriesPanel>(savedTitle);
        else if (type == "Scatter") panel = std::make_unique<ScatterPanel>(savedTitle, "", "");
        else if (type == "Status") panel = std::make_unique<StatusPanel>(savedTitle);
        else if (type == "VeAnalysisPanel") panel = std::make_unique<VeAnalysisPanel>(savedTitle);
        else if (type == "TableOverlayPanel") panel = std::make_unique<TableOverlayPanel>(savedTitle);
        else if (type == "DriveRegimePanel") panel = std::make_unique<DriveRegimePanel>();
        else if (type == "Curve2DPanel") panel = std::make_unique<Curve2DPanel>(savedTitle);

#ifdef _DEBUG2
        std::cout << "Restoring panel: " << type << " state: " << state.dump() << std::endl;
#endif

        if (panel) {
          panel->loadState(state);
          if (panel->panelTypeId() == "DriveRegimePanel") {
            auto regimePanel = static_cast<DriveRegimePanel *>(panel.get());
            regimePanel->setOnDataChangedCallback([this] { notifyRegimesUpdated(); });
            regimePanel->setSession(&session_);
          }
          panels_.push_back(std::move(panel));
        }
      }
    }

    for (auto &panel : panels_) {
      if (panel->panelTypeId() != "DriveRegimePanel") {
        panel->setOnDataChangedCallback([this, p = panel.get()]() { markDirty(); });
        panel->setSession(&session_);
      }
    }

    if (root.contains("cursor") && root["cursor"].is_object()) {
      const auto &cursorJson = root["cursor"];
      cursor_.timeSec = cursorJson.value("timeSec", 0.0);
      cursor_.active = cursorJson.value("active", false);
    }

    isDirty_ = false;
  }

  void App::addRecentWorkspace(const std::string &path)
  {
    // Move to front if already present, rather than allowing duplicates.
    recentWorkspacePaths_.erase(
      std::remove(recentWorkspacePaths_.begin(), recentWorkspacePaths_.end(), path),
      recentWorkspacePaths_.end());
    recentWorkspacePaths_.insert(recentWorkspacePaths_.begin(), path);
    if (recentWorkspacePaths_.size() > kMaxRecentWorkspaces) {
      recentWorkspacePaths_.resize(kMaxRecentWorkspaces);
    }
    saveSettings();
  }

  void App::addRecentLog(const std::string &rawPath)
  {
    if (rawPath.empty()) return;

    std::error_code ec;
    std::string path = std::filesystem::weakly_canonical(rawPath, ec).string();
    if (ec || path.empty()) path = rawPath;

    recentLogPaths_.erase(
      std::remove_if(recentLogPaths_.begin(), recentLogPaths_.end(),
        [&path](const std::string &existing) {
          if (existing.empty()) return true; // clean up any empty entries
          std::error_code ec2;
          return std::filesystem::weakly_canonical(existing, ec2).string() == path;
        }),
      recentLogPaths_.end());

    recentLogPaths_.insert(recentLogPaths_.begin(), path);
    if (recentLogPaths_.size() > kMaxRecentLogs) {
      recentLogPaths_.resize(kMaxRecentLogs);
    }
    saveSettings();
  }

  void App::loadSettings()
  {
    std::ifstream file(kSettingsFilePath);
    if (!file.is_open()) {
      return; // no settings file yet -- fine, just start empty
    }

    nlohmann::json root;
    try {
      file >> root;
    } catch (const std::exception &) {
      return; // corrupt/unreadable -- just start fresh rather than error out
    }

    if (window_ != nullptr && root.contains("window") && root["window"].is_object()) {
      const auto &wJson = root["window"];
      int width = wJson.value("width", 1280);
      int height = wJson.value("height", 800);
      bool maximized = wJson.value("maximized", false);

      if (width > 200 && height > 200) {
        if (maximized) {
          glfwMaximizeWindow(window_);
        } else {
          glfwSetWindowSize(window_, width, height);
        }
      }
    }

    if (root.contains("recentWorkspaces") && root["recentWorkspaces"].is_array()) {
      for (const auto &entry : root["recentWorkspaces"]) {
        if (entry.is_string()) recentWorkspacePaths_.push_back(entry.get<std::string>());
      }
      if (recentWorkspacePaths_.size() > kMaxRecentWorkspaces) {
        recentWorkspacePaths_.resize(kMaxRecentWorkspaces);
      }
    }
    if (root.contains("recentLogs") && root["recentLogs"].is_array()) {
      recentLogPaths_.clear();
      for (const auto &entry : root["recentLogs"]) {
        if (entry.is_string()) {
          std::string s = entry.get<std::string>();
          if (!s.empty()) {
            recentLogPaths_.push_back(s);
          }
        }
      }
      if (recentLogPaths_.size() > kMaxRecentLogs) {
        recentLogPaths_.resize(kMaxRecentLogs);
      }
    }
  }

  void App::saveSettings() const
  {
    if (window_ == nullptr) return;

    int width = 0, height = 0;
    glfwGetWindowSize(window_, &width, &height);
    int maximized = glfwGetWindowAttrib(window_, GLFW_MAXIMIZED);

    nlohmann::json root;
    root["window"] = {
    {"width", width},
    {"height", height},
    {"maximized", (maximized != 0)}
    };
    root["recentWorkspaces"] = recentWorkspacePaths_;
    root["recentLogs"] = recentLogPaths_;

    std::ofstream file(kSettingsFilePath);
    if (file.is_open()) {
      file << root.dump(2);
    }
    // Best-effort -- not worth an error popup if this fails to write.
  }

  const char *App::appBaseTitle()
  {
    return "Phenix LogView";
  }

  void App::requestExit()
  {
    requestActionWithDirtyCheck(PendingAction::Exit);
  }

  void App::updateWindowTitle()
  {
    if (!window_) return;
    std::string title{ App::appBaseTitle() };
    if (!currentWorkspacePath_.empty()) {
      title += " - " + utils::path::fileNameWithoutExtension(currentWorkspacePath_);
    }
    if (session_.rowCount() && !session_.sourcePath().empty()) {
      title += " [" + utils::path::fileNameOnly(session_.sourcePath()) + "]";
    }
    if (isDirty_) {
      title += "*";
    }
    glfwSetWindowTitle(window_, title.c_str());
  }

  // ---------------------------------------------------------------------
  // Panel management
  // ---------------------------------------------------------------------

  void App::refreshPanelsFromSession()
  {
    for (auto &panel : panels_) {
      panel->setSession(&session_);
    }
  }

  void App::notifyRegimesUpdated()
  {
    for (auto &panel : panels_) {
      panel->onRegimesUpdated();
    }
    markDirty();
  }

  void App::renderLoadProgressModal()
  {
    if (!showProgressModal_) return;

    ImGui::OpenPopup(ui::popups::Loading);

    // Center modal on screen
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(350, 0));

    if (ImGui::BeginPopupModal(ui::popups::Loading, nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
      ImGui::TextUnformatted(progressStatusText_.c_str());
      ImGui::Spacing();

      float p = loadProgress_.load();
      char progressBuf[32];
      std::snprintf(progressBuf, sizeof(progressBuf), "%.0f%%", p * 100.0f);
      ImGui::ProgressBar(p, ImVec2(-1.0f, 0.0f), progressBuf);

      // Check if task completed
      if (activeLoadTask_.valid() &&
        activeLoadTask_.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {

        LoadResult res = activeLoadTask_.get();
        showProgressModal_ = false;
        ImGui::CloseCurrentPopup();

        if (!res.success) {
          loadErrorMessage_ = res.error;
          showLoadErrorPopup_ = true;
          pendingOnCompleteCallback_ = nullptr;
        } else {
          // Check if this was a concat task or normal open task
          if (pendingAppendLogDialog_ || !incomingConcatPath_.empty() && showConcatModal_ == false && pendingOnCompleteCallback_) {
            incomingConcatSession_ = std::move(res.session);
            if (pendingOnCompleteCallback_) {
              auto cb = std::move(pendingOnCompleteCallback_);
              pendingOnCompleteCallback_ = nullptr;
              cb();
            }
          } else {
            session_ = std::move(res.session);
            addRecentLog(session_.sourcePath());
            refreshPanelsFromSession();
            if (pendingOnCompleteCallback_) {
              auto cb = std::move(pendingOnCompleteCallback_);
              pendingOnCompleteCallback_ = nullptr;
              cb();
            }
          }
        }
      }

      ImGui::EndPopup();
    }
  }

  void App::startAsyncLogLoad(const std::string &path, std::function<void()> onComplete)
  {
    showProgressModal_ = true;
    loadProgress_ = 0.0f;
    progressStatusText_ = "Loading " + utils::path::fileNameOnly(path) + "...";
    pendingOnCompleteCallback_ = std::move(onComplete);

    activeLoadTask_ = std::async(std::launch::async, [this, path]() {
      LoadResult res;
      if (path.size() >= 5 && path.substr(path.size() - 5) == ".plog") {
        res.success = plogParser_.parse(path, res.session, res.error, &loadProgress_);
      } else if (path.size() >= 3 && path.substr(path.size() - 3) == ".dl") {
        res.success = haltechParser_.parse(path, res.session, res.error, &loadProgress_);
      } else {
        res.success = mslParser_.parse(path, res.session, res.error, &loadProgress_);
      }
      core::ChannelMapping mapping;
      mapping.autoDetect(res.session);
      res.session.setChannelMapping(mapping);
      updateWindowTitle();
      return res;
      });
  }

  PlotPanel *App::addTimeSeriesPanel(const std::vector<std::string> &initialChannelNames, std::string explicitTitle)
  {
    markDirty();
    std::string panelId = explicitTitle.empty()
      ? "Time Series " + std::to_string(getNextPanelIdForPrefix("Time Series"))
      : explicitTitle;

    auto panel = std::make_unique<TimeSeriesPanel>(panelId, initialChannelNames);
    panel->setOnDataChangedCallback([this] { markDirty(); });
    panel->setSession(&session_);
    panels_.push_back(std::move(panel));
    return panels_.back().get();
  }

  PlotPanel *App::addScatterPanel(const std::string &initialXChannel,
    const std::string &initialYChannel,
    std::string explicitTitle)
  {
    markDirty();
    std::string panelId = explicitTitle.empty()
      ? "Scatter " + std::to_string(getNextPanelIdForPrefix("Scatter"))
      : explicitTitle;

    auto panel = std::make_unique<ScatterPanel>(panelId, initialXChannel, initialYChannel);
    panel->setOnDataChangedCallback([this] { markDirty(); });
    panel->setSession(&session_);
    panels_.push_back(std::move(panel));
    return panels_.back().get();
  }

  PlotPanel *App::addStatusPanel(std::string explicitTitle)
  {
    markDirty();
    std::string panelId = explicitTitle.empty()
      ? "Status " + std::to_string(getNextPanelIdForPrefix("Status"))
      : explicitTitle;
    auto panel = std::make_unique<StatusPanel>(panelId);
    panel->setOnDataChangedCallback([this] { markDirty(); });
    panel->setSession(&session_);
    panels_.push_back(std::move(panel));
    return panels_.back().get();
  }

  PlotPanel *App::addVeAnalysisPanel(std::string explicitTitle)
  {
    markDirty();
    std::string panelId = explicitTitle.empty()
      ? "VE Analyzer " + std::to_string(getNextPanelIdForPrefix("VE Analyzer"))
      : explicitTitle;
    auto panel = std::make_unique<VeAnalysisPanel>(panelId);
    panel->setOnDataChangedCallback([this] { markDirty(); });
    panel->setSession(&session_);
    panels_.push_back(std::move(panel));
    return panels_.back().get();
  }

  PlotPanel *App::addTableOverlayPanel(std::string explicitTitle)
  {
    markDirty();
    std::string panelId = explicitTitle.empty()
      ? "Table Overlay " + std::to_string(getNextPanelIdForPrefix("Table Overlay"))
      : explicitTitle;
    auto panel = std::make_unique<TableOverlayPanel>(panelId);
    panel->setOnDataChangedCallback([this] { markDirty(); });
    panel->setSession(&session_);
    panels_.push_back(std::move(panel));
    return panels_.back().get();
  }

  PlotPanel *App::addCurve2dPanel(std::string explicitTitle)
  {
    markDirty();
    std::string panelId = explicitTitle.empty()
      ? "Curve2d " + std::to_string(getNextPanelIdForPrefix("Curve2d"))
      : explicitTitle;
    auto panel = std::make_unique<Curve2DPanel>(panelId);
    panel->setOnDataChangedCallback([this] { markDirty(); });
    panel->setSession(&session_);
    panels_.push_back(std::move(panel));
    return panels_.back().get();
  }

  DriveRegimePanel *App::getOrAddDriveRegimePanel()
  {
    markDirty();
    for (const auto &panel : panels_) {
      if (panel->panelTypeId() == "DriveRegimePanel") {
        return static_cast<DriveRegimePanel *>(panel.get());
      }
    }
    auto regimePanel = std::make_unique<DriveRegimePanel>();
    regimePanel->setSession(&session_);
    regimePanel->setOnDataChangedCallback([this] { notifyRegimesUpdated(); });
    panels_.push_back(std::move(regimePanel));
    return static_cast<DriveRegimePanel *>(panels_.back().get());
  }

  int App::getNextPanelIdForPrefix(const std::string &prefix) const
  {
    int maxId = 0;
    for (const auto &panel : panels_) {
      const std::string &t = panel->title();
      if (t.rfind(prefix, 0) == 0) { // Starts with prefix
        size_t numPos = prefix.length();
        if (numPos < t.length() && t[numPos] == ' ') {
          try {
            int val = std::stoi(t.substr(numPos + 1));
            maxId = (std::max)(maxId, val);
          } catch (...) {}
        }
      }
    }
    return maxId + 1;
  }

  void App::saveWorkspace()
  {
    if (currentWorkspacePath_.empty()) {
      beginSaveWorkspaceDialog();
    } else {
      saveWorkspaceFile(currentWorkspacePath_);
      isDirty_ = false;
      updateWindowTitle();
    }
  }

  void App::renderSavePromptModal()
  {
    if (showSavePromptModal_) {
      ImGui::OpenPopup("Save Changes?###SavePromptModal");
      showSavePromptModal_ = false;
    }

    if (ImGui::BeginPopupModal("Save Changes?###SavePromptModal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      const char *promptText = "You have unsaved changes in your workspace.\nDo you want to save before continuing?";

      if (pendingAction_ == PendingAction::Exit) {
        promptText = "You have unsaved changes in your workspace.\nDo you want to save before exiting?";
      } else if (pendingAction_ == PendingAction::NewWorkspace) {
        promptText = "Creating a new workspace will discard current changes.\nDo you want to save first?";
      } else if (pendingAction_ == PendingAction::LoadWorkspace) {
        promptText = "Loading a workspace will replace your current workspace.\nDo you want to save your changes first?";
      } else if (pendingAction_ == PendingAction::CloseLog) {
        promptText = "Closing the log will discard unsaved workspace modifications.\nDo you want to save first?";
      }

      ImGui::TextUnformatted(promptText);
      ImGui::Spacing();

      // 1. Save and Proceed
      if (ui::UI::ButtonPrimary("Save", ImVec2(100, 0))) {
        ImGui::CloseCurrentPopup();
        if (currentWorkspacePath_.empty()) {
          pendingSaveBeforeExit_ = true;
          beginSaveWorkspaceDialog();
        } else {
          saveWorkspaceFile(currentWorkspacePath_);
          isDirty_ = false;
          executePendingAction();
        }
      }

      ImGui::SameLine();

      // 2. Discard and Proceed
      if (ui::UI::ButtonDanger("Don't Save", ImVec2(100, 0))) {
        ImGui::CloseCurrentPopup();
        isDirty_ = false;
        executePendingAction();
      }

      ImGui::SameLine();

      // 3. Cancel Action
      if (ui::UI::Button("Cancel", {}, ImVec2(90, 0))) {
        ImGui::CloseCurrentPopup();
        pendingAction_ = PendingAction::None;
      }

      ImGui::EndPopup();
    }
  }

  void App::beginAppendLogDialog()
  {
    if (pendingAppendLogDialog_) return;
    pendingAppendLogDialog_ = std::make_unique<pfd::open_file>(
      "Select Log to Stitch / Append", ".",
      std::vector<std::string>{
      "All Supported Logs (*.msl, *.dl)", "*.msl *.dl",
        "MegaSquirt Log Files (*.msl)", "*.msl",
        "Haltech Log Files (*.dl)", "*.dl",
        "All Files", "*"
    });
  }

  void App::initConcatModalResolutions()
  {
    concatResolutions_.clear();
    concatPosition_ = core::StitchPosition::AppendToEnd;

    // Build lookup of incoming channel names in lowercase
    std::unordered_map<std::string, std::string> incLower;
    for (const auto &ch : incomingConcatSession_.channels()) {
      incLower[utils::str::toLower(ch.name())] = ch.name();
    }

    for (const auto &curCh : session_.channels()) {
      core::ChannelMergeResolution res;
      res.activeChannelName = curCh.name();

      std::string lower = utils::str::toLower(curCh.name());
      if (incomingConcatSession_.findChannel(curCh.name())) {
        res.incomingChannelName = curCh.name();
      } else if (incLower.count(lower)) {
        res.incomingChannelName = incLower[lower];
      } else {
        // Check common alias match
        if (lower == "map" && incLower.count("fuel - load (map)")) res.incomingChannelName = incLower["fuel - load (map)"];
        else if (lower == "afr" && incLower.count("afr1")) res.incomingChannelName = incLower["afr1"];
        else if (lower == "afr" && incLower.count("wideband o2 1")) res.incomingChannelName = incLower["wideband o2 1"];
        else res.incomingChannelName = ""; // Unresolved -> requires user input or NaN choice
      }
      concatResolutions_.push_back(res);
    }
  }

  void App::renderConcatLogModal()
  {
    if (showConcatModal_) {
      ImGui::OpenPopup(ui::popups::ConcatLog);
    }

    ImGui::SetNextWindowSize(ImVec2(720, 520), ImGuiCond_FirstUseEver);

    if (ImGui::BeginPopupModal(ui::popups::ConcatLog, &showConcatModal_, ImGuiWindowFlags_None)) {
      ImGui::TextColored(ImVec4(0.4f, 0.8f, 1.0f, 1.0f), "Stitching: %s", utils::path::fileNameOnly(incomingConcatPath_).c_str());
      ImGui::Spacing();

      // 1. Order Selection Toggle
      ImGui::Text("Position:");
      ImGui::SameLine();
      int posInt = (concatPosition_ == core::StitchPosition::AppendToEnd) ? 0 : 1;
      if (ImGui::RadioButton("Append to End", &posInt, 0)) concatPosition_ = core::StitchPosition::AppendToEnd;
      ImGui::SameLine();
      if (ImGui::RadioButton("Prepend to Beginning", &posInt, 1)) concatPosition_ = core::StitchPosition::PrependToBeginning;

      ImGui::Separator();

      // 2. Info Comparison Bar
      double curDuration = 0.0;
      if (const auto *t = session_.timeSec(); t && t->size() > 1) {
        curDuration = t->back() - t->front();
      }
      double incDuration = 0.0;
      if (const auto *t = incomingConcatSession_.timeSec(); t && t->size() > 1) {
        incDuration = t->back() - t->front();
      }
      ImGui::TextDisabled("Active Log: %zu rows (%.1f s) | Incoming Log: %zu rows (%.1f s)", 
        session_.rowCount(), curDuration, incomingConcatSession_.rowCount(), incDuration);

      ImGui::Spacing();

      // 3. Channel Resolution Grid
      bool allResolved = true;
      size_t unresolvedCount = 0;

      const ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingFixedFit;

      if (ImGui::BeginTable("ConcatReconciliationTable", 4, tableFlags, ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 2))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 60.0f);
        ImGui::TableSetupColumn("Active Session Channel", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("Incoming Match / Assignment", ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn("Quick Action", ImGuiTableColumnFlags_WidthFixed, 90.0f);
        ImGui::TableHeadersRow();

        for (size_t i = 0; i < concatResolutions_.size(); ++i) {
          auto &res = concatResolutions_[i];
          ImGui::TableNextRow();
          ImGui::PushID(static_cast<int>(i));

          bool isMatched = !res.incomingChannelName.empty();
          bool isNaN = (res.incomingChannelName == "<NaN>");

          if (!isMatched) {
            allResolved = false;
            unresolvedCount++;
          }

          // Status column
          ImGui::TableSetColumnIndex(0);
          if (isNaN) {
            ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.4f, 1.0f), "[NaN]");
          } else if (isMatched) {
            ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "[OK]");
          } else {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "[MISSING]");
          }

          // Active channel
          ImGui::TableSetColumnIndex(1);
          ImGui::TextUnformatted(res.activeChannelName.c_str());

          // Incoming combo
          ImGui::TableSetColumnIndex(2);
          ImGui::SetNextItemWidth(-1.0f);
          const char *preview = res.incomingChannelName.empty() ? "(Select Mapping...)" : res.incomingChannelName.c_str();
          if (ImGui::BeginCombo("##inc_combo", preview)) {
            if (ImGui::Selectable("<Fill with NaN>", res.incomingChannelName == "<NaN>")) {
              res.incomingChannelName = "<NaN>";
            }
            ImGui::Separator();
            for (const auto &ch : incomingConcatSession_.channels()) {
              bool sel = (res.incomingChannelName == ch.name());
              if (ImGui::Selectable(ch.name().c_str(), sel)) {
                res.incomingChannelName = ch.name();
              }
              if (sel) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
          }

          // Quick action
          ImGui::TableSetColumnIndex(3);
          if (ImGui::SmallButton("Fill NaN")) {
            res.incomingChannelName = "<NaN>";
          }

          ImGui::PopID();
        }
        ImGui::EndTable();
      }

      ImGui::Separator();

      if (!allResolved) {
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%zu channel(s) unresolved. Assign a channel or set to <NaN> to proceed.", unresolvedCount);
      } else {
        ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "All channels mapped.");
      }

      ImGui::Spacing();

      // 4. Action Buttons
      ImGui::BeginDisabled(!allResolved);
      if (ui::UI::ButtonPrimary("Finalize & Stitch Logs", ImVec2(180, 0))) {
        std::string err;
        if (session_.stitchSession(incomingConcatSession_, concatPosition_, concatResolutions_, err)) {
          refreshPanelsFromSession();
          markDirty();
          showConcatModal_ = false;
          ImGui::CloseCurrentPopup();
        } else {
          loadErrorMessage_ = err;
          showLoadErrorPopup_ = true;
        }
      }
      ImGui::EndDisabled();

      ImGui::SameLine();
      if (ui::UI::ButtonSecondary("Fill All Unresolved with NaN", ImVec2(200, 0))) {
        for (auto &r : concatResolutions_) {
          if (r.incomingChannelName.empty()) {
            r.incomingChannelName = "<NaN>";
          }
        }
      }

      ImGui::SameLine();
      if (ui::UI::Button("Cancel", {}, ImVec2(90, 0))) {
        showConcatModal_ = false;
        incomingConcatSession_ = core::LogSession();
        ImGui::CloseCurrentPopup();
      }

      ImGui::EndPopup();
    }
  }

  void App::startAsyncConcatLoad(const std::string &path)
  {
    showProgressModal_ = true;
    loadProgress_ = 0.0f;
    progressStatusText_ = "Loading " + utils::path::fileNameOnly(path) + " for concat...";
    incomingConcatPath_ = path;

    activeLoadTask_ = std::async(std::launch::async, [this, path]() {
      LoadResult res;
      if (path.size() >= 3 && path.substr(path.size() - 3) == ".dl") {
        res.success = haltechParser_.parse(path, res.session, res.error, &loadProgress_);
      } else {
        res.success = mslParser_.parse(path, res.session, res.error, &loadProgress_);
      }
      core::ChannelMapping mapping;
      mapping.autoDetect(res.session);
      res.session.setChannelMapping(mapping);
      return res;
      });

    // Callback to trigger when parsing finishes
    pendingOnCompleteCallback_ = [this]() {
      initConcatModalResolutions();
      showConcatModal_ = true;
      };
  }

  void App::requestActionWithDirtyCheck(PendingAction action, std::string pathPayload)
  {
    if (isDirty_) {
      pendingAction_ = action;
      pendingActionPathPayload_ = std::move(pathPayload);
      showSavePromptModal_ = true;
    } else {
      pendingAction_ = action;
      pendingActionPathPayload_ = std::move(pathPayload);
      executePendingAction();
    }
  }

  void App::executePendingAction()
  {
    switch (pendingAction_) {
    case PendingAction::Exit:
      readyToExit_ = true;
      break;

    case PendingAction::NewWorkspace:
      panels_.clear();
      session_ = core::LogSession();
      currentWorkspacePath_.clear();
      isDirty_ = false;
      welcomeFadeTimer_ = 0.0f;
      updateWindowTitle();
      break;

    case PendingAction::LoadWorkspace:
      if (!pendingActionPathPayload_.empty()) {
        loadWorkspaceFile(pendingActionPathPayload_);
        isDirty_ = false;
      } else {
        beginLoadWorkspaceDialog();
      }
      break;

    case PendingAction::CloseLog:
      session_ = core::LogSession();
      refreshPanelsFromSession();
      isDirty_ = false;
      updateWindowTitle();
      break;

    case PendingAction::LoadLog:
      if (!pendingActionPathPayload_.empty()) {
        loadLogFile(pendingActionPathPayload_);
      } else {
        beginOpenLogDialog();
      }
      break;

    case PendingAction::None:
    default:
      break;
    }

    pendingAction_ = PendingAction::None;
    pendingActionPathPayload_.clear();
  }

  void App::markDirty()
  {
    isDirty_ = true;
    updateWindowTitle();
  }

  // ---------------------------------------------------------------------
  // Rendering
  // ---------------------------------------------------------------------

  void App::renderMenuBar()
  {
    ImGuiIO &io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false)) {
      if (io.KeyShift) beginSaveWorkspaceDialog();
      else saveWorkspace();
    }
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_O, false)) {
      beginOpenLogDialog();
    }

    bool hasSession = 0 < session_.rowCount();
    bool isPlogFile = hasSession &&
      session_.sourcePath().size() >= 5 &&
      session_.sourcePath().substr(session_.sourcePath().size() - 5) == ".plog";

    if (ImGui::BeginMenuBar()) {
      if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open Log...", "Ctrl+O")) {
          beginOpenLogDialog();
        }
        ImGui::BeginDisabled(!hasSession);
        {
          ImGui::BeginDisabled(!isPlogFile || isSavingPlog_);
          if (ImGui::MenuItem("Save Log", "Ctrl+Alt+S")) {
            isSavingPlog_ = true;
            savingPlogTask_ = std::async(std::launch::async, [this, sessionCopy = session_]()
              {
                std::string err;
                bool res = io::PlogParser::write(sessionCopy.sourcePath(), sessionCopy, err);
                if (!res) {
                  saveErrorMessage_ = err;
                  showSaveErrorPopup_ = true;
                }
                isSavingPlog_ = false;
                return res;
              });
          }
          ImGui::EndDisabled();
          if (ImGui::MenuItem("Save Log As / Export (.plog)...")) {
            beginExportLogDialog();
          }
          if (ImGui::MenuItem("Append / Prepend Log...")) {
            beginAppendLogDialog();
          }
          if (ImGui::MenuItem("Close Log")) {
            requestActionWithDirtyCheck(PendingAction::CloseLog);
          }
        }
        ImGui::EndDisabled();

        if (ImGui::BeginMenu("Recent Logs", !recentLogPaths_.empty())) {
          for (size_t i = 0; i < recentLogPaths_.size(); ++i) {
            const std::string &path = recentLogPaths_[i];
            if (path.empty()) continue;

            std::string label = utils::path::fileNameOnly(path) + "###recentlog" + std::to_string(i);
            if (ImGui::MenuItem(label.c_str())) {
              requestActionWithDirtyCheck(PendingAction::LoadLog, path);
            }
            if (ImGui::IsItemHovered()) {
              ImGui::SetTooltip("%s", path.c_str());
            }
          }
          ImGui::EndMenu();
        }

        ImGui::Separator();

        if (ImGui::MenuItem("New Workspace", "Ctrl+N")) {
          requestActionWithDirtyCheck(PendingAction::NewWorkspace);
        }
        if (ImGui::MenuItem("Load Workspace...", "Ctrl+W")) {
          requestActionWithDirtyCheck(PendingAction::LoadWorkspace);
        }
        if (ImGui::MenuItem("Save Workspace", "Ctrl+S")) {
          saveWorkspace();
        }
        if (ImGui::MenuItem("Save Workspace As...", "Ctrl+Shift+S")) {
          beginSaveWorkspaceDialog();
        }
        if (ImGui::BeginMenu("Recent Workspaces", !recentWorkspacePaths_.empty())) {
          for (size_t i = 0; i < recentWorkspacePaths_.size(); ++i) {
            const std::string &path = recentWorkspacePaths_[i];
            std::string label = utils::path::fileNameOnly(path) + "###recentws" + std::to_string(i);
            if (ImGui::MenuItem(label.c_str())) {
              requestActionWithDirtyCheck(PendingAction::LoadWorkspace, path);
            }
            if (ImGui::IsItemHovered()) {
              ImGui::SetTooltip("%s", path.c_str());
            }
          }
          ImGui::EndMenu();
        }

        ImGui::Separator();
        if (ImGui::MenuItem("Exit", "Alt+F4")) {
          requestExit();
        }
        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu("Panels")) {

        if (ImGui::MenuItem("Add Time Series Panel")) {
          addTimeSeriesPanel({ "RPM" });
        }
        if (ImGui::MenuItem("Add Scatter Panel")) {
          addScatterPanel("RPM", "MAP");
        }
        if (ImGui::MenuItem("Add Status Panel")) {
          addStatusPanel();
        }
        if (ImGui::MenuItem("Add VE Analyzer / Observed AFR")) {
          addVeAnalysisPanel();
        }
        if (ImGui::MenuItem("Add Table Overlay Panel")) {
          addTableOverlayPanel();
        }
        if (ImGui::MenuItem("Add Curve2D Panel")) {
          addCurve2dPanel();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Drive Regime Summary")) {
          getOrAddDriveRegimePanel();
        }
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Tools")) {
        if (ImGui::MenuItem("Custom Channels...")) {
          showCustomChannelModal_ = true;
        }
        if (ImGui::MenuItem("Channel Semantic Mapping...")) {
          showChannelMappingModal_ = true;
        }
        ImGui::EndMenu();
      }
      if (isSavingPlog_) {
        float time = (float)ImGui::GetTime();
        ImVec4 spinColor = ImVec4(0.3f, 0.7f, 1.0f, 0.5f + 0.5f * sinf(time * 6.0f));
        ImGui::SameLine(ImGui::GetWindowWidth() - 140.0f);
        ImGui::TextColored(spinColor, ICON_FA_FLOPPY_DISK " Saving .plog...");
      }
      ImGui::EndMenuBar();
    }
  }

  void App::renderDockspace()
  {
    ImGuiWindowFlags hostFlags =
      ImGuiWindowFlags_NoDocking |
      ImGuiWindowFlags_NoTitleBar |
      ImGuiWindowFlags_NoCollapse |
      ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoBringToFrontOnFocus |
      ImGuiWindowFlags_NoNavFocus |
      ImGuiWindowFlags_MenuBar;

    const ImGuiViewport *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

    ImGui::Begin("DockSpaceHost", nullptr, hostFlags);
    ImGui::PopStyleVar(3);

    renderMenuBar();

    ImGuiID dockspaceId = ImGui::GetID("MainDockSpace");
    ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

    if (session_.rowCount() == 0 && panels_.empty()) {
      renderWelcomeLanding();
    }

    ImGui::End();
  }

  void App::renderWelcomeLanding()
  {
    constexpr float kFadeDuration = 0.8f;
    if (welcomeFadeTimer_ < kFadeDuration) {
      welcomeFadeTimer_ += ImGui::GetIO().DeltaTime;
      if (welcomeFadeTimer_ > kFadeDuration) {
        welcomeFadeTimer_ = kFadeDuration;
      }
    }

    // Smooth quadratic ease-out: f(t) = 1 - (1 - t)^2
    float t = welcomeFadeTimer_ / kFadeDuration;
    float alpha = 1.0f - (1.0f - t) * (1.0f - t);

    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f));

    // 2. Scale card opacity with alpha
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 12.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(28.0f, 24.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0.0f, 10.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f);

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.13f, 0.14f, 0.17f, 0.95f));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.24f, 0.26f, 0.32f, 0.60f));

    ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
      ImGuiWindowFlags_NoMove |
      ImGuiWindowFlags_NoSavedSettings |
      ImGuiWindowFlags_NoDocking;

    if (ImGui::Begin("##WelcomeOverlay", nullptr, flags)) {

      float availWidth = ImGui::GetContentRegionAvail().x;
      const char *titleText = App::appBaseTitle();
      ImVec2 titleSize = ImGui::CalcTextSize(titleText);

      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availWidth - titleSize.x) * 0.5f);
      ImGui::TextColored(ImVec4(0.40f, 0.75f, 1.0f, 1.0f), "%s", titleText);

      const char *subText = "ECU Telemetry & Table Analysis Workbench";
      ImVec2 subSize = ImGui::CalcTextSize(subText);
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + (availWidth - subSize.x) * 0.5f);
      ImGui::TextColored(ImVec4(0.55f, 0.58f, 0.65f, 1.0f), "%s", subText);

      ImGui::Dummy(ImVec2(0.0f, 12.0f));

      const ImVec2 btnSize(availWidth, 40.0f);

      if (ui::UI::ButtonPrimary(ICON_FA_FOLDER_OPEN "   Open Log File...", btnSize, "Open MegaSquirt (.msl) or Haltech (.dl) log")) {
        beginOpenLogDialog();
      }

      if (ui::UI::ButtonSecondary(ICON_FA_FILE_INVOICE "   Load Workspace...", btnSize, "Load previously saved workspace")) {
        requestActionWithDirtyCheck(PendingAction::LoadWorkspace);
      }

      if (!recentWorkspacePaths_.empty()) {
        ImGui::Dummy(ImVec2(0.0f, 8.0f));
        ImGui::Separator();
        ImGui::Dummy(ImVec2(0.0f, 4.0f));

        ImGui::TextColored(ImVec4(0.50f, 0.53f, 0.60f, 1.0f), "Recent Workspaces");
        ImGui::Spacing();

        for (size_t i = 0; i < recentWorkspacePaths_.size(); ++i) {
          const auto &path = recentWorkspacePaths_[i];
          std::string label = std::string(ICON_FA_CLOCK_ROTATE_LEFT) + "  " + utils::path::fileNameOnly(path);

          ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(0.18f, 0.20f, 0.25f, 0.5f));
          ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(0.22f, 0.25f, 0.32f, 0.8f));
          ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(0.18f, 0.42f, 0.70f, 0.6f));

          if (ImGui::Selectable(label.c_str(), false, 0, ImVec2(availWidth, 26.0f))) {
            /*requestActionWithDirtyCheck(PendingAction::LoadWorkspace, path);*/
            pendingRecentWorkspaceLoad_ = path;
          }

          ImGui::PopStyleColor(3);

          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", path.c_str());
          }
        }
      }

      ImGui::End();
    }

    ImGui::PopStyleColor(2);
    ImGui::PopStyleVar(6);
  }

  void App::renderLoadErrorPopup()
  {
    if (showLoadErrorPopup_) {
      ImGui::OpenPopup(ui::popups::LoadError);
      showLoadErrorPopup_ = false;
    }

    if (ImGui::BeginPopupModal(ui::popups::LoadError, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextWrapped("%s", loadErrorMessage_.c_str());
      if (ui::UI::Button("OK", {}, ImVec2(120, 0))) {
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
  }

  void App::renderSaveErrorPopup()
  {
    if (showSaveErrorPopup_) {
      ImGui::OpenPopup(ui::popups::SaveError);
      showSaveErrorPopup_ = false;
    }

    if (ImGui::BeginPopupModal(ui::popups::SaveError, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextWrapped("%s", saveErrorMessage_.c_str());
      if (ui::UI::Button("OK", {}, ImVec2(120, 0))) {
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
  }

  void App::renderCustomChannelModal()
  {
    if (showCustomChannelModal_) {
      ImGui::OpenPopup(ui::popups::CustomChannels);
    }

    // Set a comfortable size for the dual-pane modal
    ImGui::SetNextWindowSize(ImVec2(700, 420), ImGuiCond_FirstUseEver);

    if (ImGui::BeginPopupModal(ui::popups::CustomChannels, &showCustomChannelModal_, ImGuiWindowFlags_None)) {

      // Track selected index (-1 means "New Channel" mode)
      static int selectedIndex = -1;
      static char nameBuf[64] = "";
      static char unitBuf[32] = "";
      static char formulaBuf[256] = "";

      // Helper to load selected channel into input buffers
      auto populateBuffers = [&](const core::Channel &ch) {
        std::snprintf(nameBuf, sizeof(nameBuf), "%s", ch.name().c_str());
        std::snprintf(unitBuf, sizeof(unitBuf), "%s", ch.unit().c_str());
        std::snprintf(formulaBuf, sizeof(formulaBuf), "%s", ch.formula().c_str());
        };

      auto clearBuffers = [&]() {
        nameBuf[0] = '\0';
        unitBuf[0] = '\0';
        formulaBuf[0] = '\0';
        customChannelError_.clear();
        };

      // --- LEFT COLUMN: Custom Channels List ---
      ImGui::BeginChild("CustomChannelList", ImVec2(220, -ImGui::GetFrameHeightWithSpacing()), true);

      if (ImGui::Selectable("+ New Custom Channel", selectedIndex == -1)) {
        selectedIndex = -1;
        clearBuffers();
      }
      ImGui::Separator();

      auto &channels = session_.channels();
      for (int i = 0; i < static_cast<int>(channels.size()); ++i) {
        if (!channels[i].isCustom()) continue;

        ImGui::PushID(i);
        std::string label = channels[i].name();
        if (!channels[i].unit().empty()) {
          label += " (" + channels[i].unit() + ")";
        }

        if (ImGui::Selectable(label.c_str(), selectedIndex == i)) {
          selectedIndex = i;
          populateBuffers(channels[i]);
          customChannelError_.clear();
        }
        ImGui::PopID();
      }
      ImGui::EndChild();

      ImGui::SameLine();

      // --- RIGHT COLUMN: Editor Form ---
      ImGui::BeginGroup();
      ImGui::BeginChild("CustomChannelEditor", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()), false);

      if (selectedIndex == -1) {
        ImGui::TextDisabled("Create New Calculated Channel");
      } else {
        ImGui::TextDisabled("Editing Channel: %s", channels[selectedIndex].name().c_str());
      }
      ImGui::Separator();
      ImGui::Spacing();

      ImGui::InputText("Channel Name", nameBuf, sizeof(nameBuf));
      ImGui::InputText("Unit", unitBuf, sizeof(unitBuf));
      ImGui::InputText("Formula", formulaBuf, sizeof(formulaBuf));
      ImGui::Spacing();

      if (ImGui::BeginCombo(ICON_FA_WAND_MAGIC_SPARKLES " Load Preset...", "Select a common math preset...")) {
        std::string lastCategory;
        for (const auto &preset : core::FormulaEvaluator::allPresets()) {
          if (preset.category != lastCategory) {
            if (!lastCategory.empty()) ImGui::Separator();
            ImGui::TextDisabled("%s", preset.category.c_str());
            lastCategory = preset.category;
          }

          if (ImGui::Selectable(preset.name.c_str())) {
            std::string missing;
            std::string resolved = core::FormulaEvaluator::resolvePresetFormula(preset, session_, missing);

            std::snprintf(nameBuf, sizeof(nameBuf), "%s", preset.name.c_str());
            std::snprintf(unitBuf, sizeof(unitBuf), "%s", preset.defaultUnit.c_str());

            if (!resolved.empty()) {
              std::snprintf(formulaBuf, sizeof(formulaBuf), "%s", resolved.c_str());
              customChannelError_.clear();
            } else {
              std::snprintf(formulaBuf, sizeof(formulaBuf), "%s", preset.formulaTemplate.c_str());
              customChannelError_ = "Unmapped slot " + missing + ". Check Channel Semantic Mapping.";
            }
          }
          if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s\nFormula: %s", preset.description.c_str(), preset.formulaTemplate.c_str());
          }
        }
        ImGui::EndCombo();
      }

      ImGui::Spacing();
      ImGui::TextWrapped("Use [ChannelName] for variables, e.g. [MAP] - 101.3 or ([RPM] * [PW]) / 12000");

      if (!customChannelError_.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s", customChannelError_.c_str());
      }

      ImGui::EndChild();
      ImGui::EndGroup();

      ImGui::Separator();

      // --- BOTTOM BUTTON BAR ---
      if (selectedIndex == -1) {
        // Add Mode
        if (ui::UI::Button(ICON_FA_PLUS " Add Channel", {}, ImVec2(120, 0))) {
          core::CustomChannelDef def{ nameBuf, unitBuf, formulaBuf };
          std::string err;

          core::Channel newCh = core::FormulaEvaluator::evaluate(def, session_, err);
          if (!err.empty()) {
            customChannelError_ = err;
          } else {
            session_.addChannel(std::move(newCh));
            refreshPanelsFromSession();
            clearBuffers();
            selectedIndex = static_cast<int>(session_.channels().size()) - 1;
          }
        }
      } else {
        // Edit / Update Mode
        if (ui::UI::Button(ICON_FA_ROTATE " Update / Re-evaluate", {}, ImVec2(160, 0))) {
          core::CustomChannelDef def{ nameBuf, unitBuf, formulaBuf };
          std::string err;

          core::Channel updatedCh = core::FormulaEvaluator::evaluate(def, session_, err);
          if (!err.empty()) {
            customChannelError_ = err;
          } else {
            // Replace existing channel in-place
            channels[selectedIndex] = std::move(updatedCh);
            refreshPanelsFromSession();
            customChannelError_.clear();
          }
        }

        ImGui::SameLine();

        // Delete Mode
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.7f, 0.2f, 0.2f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.9f, 0.3f, 0.3f, 1.0f));
        if (ui::UI::Button(ICON_FA_TRASH_CAN " Delete Channel", {}, ImVec2(120, 0))) {
          if (selectedIndex >= 0 && selectedIndex < static_cast<int>(channels.size())) {
            channels.erase(channels.begin() + selectedIndex);
            refreshPanelsFromSession(); // Rebind open panels so deleted channel is safely unbound
            selectedIndex = -1;
            clearBuffers();
          }
        }
        ImGui::PopStyleColor(2);
      }

      ImGui::SameLine();
      if (ui::UI::Button("Close", {}, ImVec2(100, 0))) {
        showCustomChannelModal_ = false;
        ImGui::CloseCurrentPopup();
      }

      ImGui::EndPopup();
    }
  }

  void App::renderChannelMappingModal()
  {
    if (showChannelMappingModal_) {
      ImGui::OpenPopup(ui::popups::ChannelMapping);
    }

    ImGui::SetNextWindowSize(ImVec2(480, 420), ImGuiCond_FirstUseEver);

    if (ImGui::BeginPopupModal(ui::popups::ChannelMapping, &showChannelMappingModal_, ImGuiWindowFlags_None)) {
      if (session_.rowCount() == 0) {
        ImGui::TextDisabled("No log session loaded.");
        if (ui::UI::Button("Close", {}, ImVec2(100, 0))) {
          showChannelMappingModal_ = false;
          ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
        return;
      }

      ImGui::TextDisabled("Map log channels to standard ECU telemetry slots:");
      ImGui::Spacing();

      // Use a persistent/static working copy while the modal is open so intermediate combo updates persist across frames
      static core::ChannelMapping workingMapping;
      static bool initialized = false;

      if (!initialized) {
        workingMapping = session_.channelMapping();
        initialized = true;
      }

      bool changed = false;

      auto renderMappingCombo = [this, &changed](const char *label, std::string &targetChannel) {
        ImGui::PushID(label);
        ImGui::SetNextItemWidth(220.0f);

        // Check if channel is non-empty AND missing from session
        bool isInvalid = !targetChannel.empty() && (session_.findChannel(targetChannel) == nullptr);

        if (isInvalid) {
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f)); // Bright Red
        }

        if (ImGui::BeginCombo(label, targetChannel.empty() ? "(None)" : targetChannel.c_str())) {
          if (isInvalid) {
            ImGui::PopStyleColor(); // Restore text color for dropdown options
          }

          if (ImGui::Selectable("(None)", targetChannel.empty())) {
            targetChannel.clear();
            changed = true;
          }

          for (const auto &ch : session_.channels()) {
            bool isSelected = (ch.name() == targetChannel);
            if (ImGui::Selectable(ch.name().c_str(), isSelected)) {
              targetChannel = ch.name();
              changed = true;
            }
            if (isSelected) ImGui::SetItemDefaultFocus();
          }
          ImGui::EndCombo();
        } else if (isInvalid) {
          ImGui::PopStyleColor(); // Restore text color if combo wasn't opened
        }

        ImGui::PopID();
        };

      for (const auto &slot : core::ChannelMapping::allSlots()) {
        auto &s = workingMapping.refSlot(slot);
        renderMappingCombo(slot.c_str(), s);
      }

      ImGui::Separator();
      ImGui::Spacing();

      if (ui::UI::ButtonSecondary("Auto-Detect Defaults")) {
        workingMapping.autoDetect(session_);
        session_.setChannelMapping(workingMapping);
        refreshPanelsFromSession(); // Triggers re-analysis across open panels
      }

      ImGui::SameLine();

      if (ui::UI::ButtonPrimary("Apply & Recalculate", ImVec2(140, 0))) {
        session_.setChannelMapping(workingMapping);
        refreshPanelsFromSession(); // Notify panels to rerun VeAnalyzer and RegimeAnalyzer
        initialized = false;
        showChannelMappingModal_ = false;
        ImGui::CloseCurrentPopup();
      }

      ImGui::SameLine();

      if (ui::UI::Button("Cancel", {}, ImVec2(90, 0))) {
        initialized = false;
        showChannelMappingModal_ = false;
        ImGui::CloseCurrentPopup();
      }

      ImGui::EndPopup();
    } else {
      // Reset initialization state when popup closes
      static bool initialized = false;
      initialized = false;
    }
  }

  void App::render()
  {
    if (!pendingRecentWorkspaceLoad_.empty()) {
      std::string path = std::move(pendingRecentWorkspaceLoad_);
      pendingRecentWorkspaceLoad_.clear();
      loadWorkspaceFile(path);
    }
    if (!pendingRecentLogLoad_.empty()) {
      std::string path = std::move(pendingRecentLogLoad_);
      pendingRecentLogLoad_.clear();
      loadLogFile(path);
    }

    pollPendingDialogs();

    if (!pendingOnCompleteCallback_) {
      renderDockspace();
      for (auto &panel : panels_) {
        panel->render(cursor_);
      }
    }

    renderSavePromptModal();
    renderLoadProgressModal();
    renderLoadErrorPopup();
    renderSaveErrorPopup();
    renderCustomChannelModal();
    renderChannelMappingModal();
    renderConcatLogModal();

    panels_.erase(
      std::remove_if(panels_.begin(), panels_.end(),
        [](const std::unique_ptr<PlotPanel> &panel) { return panel->wantsClose(); }),
      panels_.end());

    if (showDemoWindow_) {
      ImGui::ShowDemoWindow(&showDemoWindow_);
    }

    firstFrame_ = false;
  }

} // namespace ui