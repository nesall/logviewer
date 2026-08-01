#include "ui/app.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>

#include "3rdparty/nlohmann/json.hpp"
#include "3rdparty/portable-file-dialogs.h"
#include "core/table2d.h"
#include "imgui.h"
#include <GLFW/glfw3.h>

#include "ui/scatterpanel.h"
#include "ui/statuspanel.h"
#include "ui/tableeditorpanel.h"
#include "ui/timeseriespanel.h"
#include "ui/veanalysispanel.h"

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

    std::string fileNameOnly(const std::string &path) {
      size_t pos = path.find_last_of("/\\");
      return (pos == std::string::npos) ? path : path.substr(pos + 1);
    }

    std::string fileNameWithoutExtension(const std::string &path) {
      std::string name = fileNameOnly(path);
      size_t dot = name.find_last_of('.');
      return (dot == std::string::npos) ? name : name.substr(0, dot);
    }

    // Single spot to change once a real app name is picked.
    constexpr const char *kAppBaseTitle = "logviewer";

    // Stored next to the executable for now -- simplest thing that works for
    // a dev-testing tool. Move to a proper per-user config directory
    // (%AppData% / ~/.config) if this ever ships beyond your own machine.
    const char *kSettingsFilePath = "logviewer_settings.json";

  } // namespace

  App::App(GLFWwindow *window) : window_(window)
  {
    loadSettings();
    updateWindowTitle();


    // Two starter panels; setSession() rebinds them to real data once a
    // file is loaded. More can be added at runtime via Panels menu.
    //addTimeSeriesPanel({ "RPM" });
    //addTimeSeriesPanel({ "MAP" });
  }

  App::~App() = default;

  // ---------------------------------------------------------------------
  // File dialogs (non-blocking -- see the comment on pollPendingDialogs_
  // in app.h for why this matters)
  // ---------------------------------------------------------------------

  void App::beginOpenLogDialog()
  {
    if (pendingOpenLogDialog_) {
      return; // one is already open
    }
    pendingOpenLogDialog_ = std::make_unique<pfd::open_file>(
      "Open Log File", ".",
      std::vector<std::string>{"MegaSquirt Log Files (*.msl)", "*.msl", "All Files", "*"});
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
        loadLogFile(selection.front());
      }
    }

    if (pendingSaveWorkspaceDialog_ && pendingSaveWorkspaceDialog_->ready(0)) {
      std::string path = pendingSaveWorkspaceDialog_->result();
      pendingSaveWorkspaceDialog_.reset();
      if (!path.empty()) {
        saveWorkspaceFile(path);
      }
    }

    if (pendingLoadWorkspaceDialog_ && pendingLoadWorkspaceDialog_->ready(0)) {
      auto selection = pendingLoadWorkspaceDialog_->result();
      pendingLoadWorkspaceDialog_.reset();
      if (!selection.empty()) {
        loadWorkspaceFile(selection.front());
      }
    }
  }

  // ---------------------------------------------------------------------
  // Log loading
  // ---------------------------------------------------------------------

  void App::loadLogFile(const std::string &path)
  {
    //core::LogSession newSession;
    //std::string error;
    //if (!parser_.parse(path, newSession, error)) {
    //  loadErrorMessage_ = error;
    //  showLoadErrorPopup_ = true;
    //  return;
    //}

    //session_ = std::move(newSession);
    //refreshPanelsFromSession();
    startAsyncLogLoad(path);
  }

  // ---------------------------------------------------------------------
  // Workspace save/load
  // ---------------------------------------------------------------------

  void App::saveWorkspaceFile(const std::string &path)
  {
    nlohmann::json root;
    root["logFilePath"] = session_.sourcePath();

    nlohmann::json panelsJson = nlohmann::json::array();
    for (const auto &panel : panels_) {
      panelsJson.push_back({ {"type", panel->panelTypeId()}, {"state", panel->saveState()} });
    }
    root["panels"] = panelsJson;

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

    addRecentWorkspace(path);
    currentWorkspacePath_ = path;
    updateWindowTitle();
    
    // Helper lambda to finish restoring panel states once log session loads
    auto restorePanelsAndLayout = [this, root]() {
      panels_.clear();

      if (root.contains("panels") && root["panels"].is_array()) {
        for (const auto &panelJson : root["panels"]) {
          if (!panelJson.contains("type") || !panelJson["type"].is_string()) continue;

          std::string type = panelJson["type"].get<std::string>();
          nlohmann::json state = panelJson.contains("state") ? panelJson["state"] : nlohmann::json::object();
          PlotPanel *panel = nullptr;

          if (type == "TimeSeries") panel = addTimeSeriesPanel({ "RPM" });
          else if (type == "Scatter") panel = addScatterPanel("RPM", "AFR");
          else if (type == "Status") panel = addStatusPanel();
          else if (type == "VeAnalysisPanel") panel = addVeAnalysisPanel();

          if (panel != nullptr) {
            panel->loadState(state);
          }
        }
      }

      if (root.contains("imguiLayout") && root["imguiLayout"].is_string()) {
        std::string iniData = root["imguiLayout"].get<std::string>();
        ImGui::LoadIniSettingsFromMemory(iniData.c_str(), iniData.size());
      }
      };
    
    //panels_.clear();
    //if (root.contains("logFilePath") && root["logFilePath"].is_string()) {
    //  std::string logPath = root["logFilePath"].get<std::string>();
    //  if (!logPath.empty()) {
    //    loadLogFile(logPath); // shows its own error popup on failure
    //  }
    //}
    //if (root.contains("panels") && root["panels"].is_array()) {
    //  for (const auto &panelJson : root["panels"]) {
    //    if (!panelJson.contains("type") || !panelJson["type"].is_string()) {
    //      continue;
    //    }
    //    std::string type = panelJson["type"].get<std::string>();
    //    nlohmann::json state = panelJson.contains("state") ? panelJson["state"] : nlohmann::json::object();
    //    PlotPanel *panel = nullptr;
    //    if (type == "TimeSeries") {
    //      panel = addTimeSeriesPanel({ "RPM" });
    //    } else if (type == "Scatter") {
    //      panel = addScatterPanel("RPM", "AFR");
    //    } else if (type == "Status") {
    //      panel = addStatusPanel();
    //    } else if (type == "VeAnalysisPanel") {
    //      panel = addVeAnalysisPanel();
    //    }
    //    if (panel != nullptr) {
    //      panel->loadState(state);
    //    }
    //  }
    //}

    // Restore ImGui docking / window layout -- must come after the panels
    // above are (re)created, since it applies by matching each window's
    // "###panelId" ID against what's in the ini blob.
    //if (root.contains("imguiLayout") && root["imguiLayout"].is_string()) {
    //  std::string iniData = root["imguiLayout"].get<std::string>();
    //  ImGui::LoadIniSettingsFromMemory(iniData.c_str(), iniData.size());
    //}

    if (root.contains("logFilePath") && root["logFilePath"].is_string()) {
      std::string logPath = root["logFilePath"].get<std::string>();
      if (!logPath.empty()) {
        // Async load log file, then restore panels
        startAsyncLogLoad(logPath, restorePanelsAndLayout);
      } else {
        restorePanelsAndLayout();
      }
    } else {
      restorePanelsAndLayout();
    }
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

    if (root.contains("recentWorkspaces") && root["recentWorkspaces"].is_array()) {
      for (const auto &entry : root["recentWorkspaces"]) {
        if (entry.is_string()) {
          recentWorkspacePaths_.push_back(entry.get<std::string>());
        }
      }
      if (recentWorkspacePaths_.size() > kMaxRecentWorkspaces) {
        recentWorkspacePaths_.resize(kMaxRecentWorkspaces);
      }
    }
  }

  void App::saveSettings() const
  {
    nlohmann::json root;
    root["recentWorkspaces"] = recentWorkspacePaths_;

    std::ofstream file(kSettingsFilePath);
    if (file.is_open()) {
      file << root.dump(2);
    }
    // Best-effort -- not worth an error popup if this fails to write.
  }

  void App::updateWindowTitle()
  {
    if (window_ == nullptr) {
      return;
    }
    std::string title = kAppBaseTitle;
    if (!currentWorkspacePath_.empty()) {
      title += " - " + fileNameWithoutExtension(currentWorkspacePath_);
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

  void App::bumpNextPanelIdPastLoadedTitles()
  {
    for (const auto &panel : panels_) {
      const std::string &t = panel->title();
      size_t i = t.size();
      while (i > 0 && std::isdigit(static_cast<unsigned char>(t[i - 1]))) {
        --i;
      }
      if (i < t.size()) {
        int trailingNumber = std::atoi(t.c_str() + i);
        nextPanelId_ = (std::max)(nextPanelId_, trailingNumber + 1);
      }
    }
  }

  void App::renderLoadProgressModal()
  {
    if (!showProgressModal_) return;

    ImGui::OpenPopup("Loading...");

    // Center modal on screen
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowSize(ImVec2(350, 0));

    if (ImGui::BeginPopupModal("Loading...", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
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
        } else {
          session_ = std::move(res.session);
          refreshPanelsFromSession();

          if (pendingOnCompleteCallback_) {
            pendingOnCompleteCallback_();
            pendingOnCompleteCallback_ = nullptr;
          }
        }
      }

      ImGui::EndPopup();
    }
  }

  void App::startAsyncLogLoad(const std::string & path, std::function<void()> onComplete)
  {
    showProgressModal_ = true;
    loadProgress_ = 0.0f;
    progressStatusText_ = "Loading " + fileNameOnly(path) + "...";
    pendingOnCompleteCallback_ = std::move(onComplete);

    // Launch thread
    activeLoadTask_ = std::async(std::launch::async, [this, path]() {
      LoadResult res;
      res.success = parser_.parse(path, res.session, res.error, &loadProgress_);
      return res;
      });
  }

  PlotPanel *App::addTimeSeriesPanel(const std::vector<std::string> &initialChannelNames)
  {
    std::string panelId = "Time Series " + std::to_string(nextPanelId_++);
    auto panel = std::make_unique<TimeSeriesPanel>(panelId, initialChannelNames);
    panel->setSession(&session_);
    panels_.push_back(std::move(panel));
    return panels_.back().get();
  }

  PlotPanel *App::addScatterPanel(const std::string &initialXChannel, const std::string &initialYChannel)
  {
    std::string panelId = "Scatter " + std::to_string(nextPanelId_++);
    auto panel = std::make_unique<ScatterPanel>(panelId, initialXChannel, initialYChannel);
    panel->setSession(&session_);
    panels_.push_back(std::move(panel));
    return panels_.back().get();
  }

  PlotPanel *App::addStatusPanel()
  {
    std::string panelId = "Status " + std::to_string(nextPanelId_++);
    auto panel = std::make_unique<StatusPanel>(panelId);
    panel->setSession(&session_);
    panels_.push_back(std::move(panel));
    return panels_.back().get();
  }

  //PlotPanel *App::addTableEditorPanel(const std::string &panelTypeIdValue, const std::string &displayName)
  //{
  //  std::string panelId = displayName + " " + std::to_string(nextPanelId_++);
  //  // 16x16 default (common MS3 table size) -- fully editable afterward.
  //  core::Table2D defaultTable(core::generateEvenBreakpoints(500, 7000, 16),
  //    core::generateEvenBreakpoints(20, 100, 16));
  //  auto panel = std::make_unique<TableEditorPanel>(panelId, panelTypeIdValue, displayName,
  //    std::move(defaultTable));
  //  panels_.push_back(std::move(panel));
  //  return panels_.back().get();
  //}

  PlotPanel *App::addVeAnalysisPanel()
  {
    std::string panelId = "VE Analyzer " + std::to_string(nextPanelId_++);
    auto panel = std::make_unique<VeAnalysisPanel>(panelId);
    panel->setSession(&session_);
    panels_.push_back(std::move(panel));
    return panels_.back().get();
  }

  // ---------------------------------------------------------------------
  // Rendering
  // ---------------------------------------------------------------------

  void App::renderMenuBar()
  {
    if (ImGui::BeginMenuBar()) {
      if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Open Log...")) {
          beginOpenLogDialog();
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Save Workspace...")) {
          beginSaveWorkspaceDialog();
        }
        if (ImGui::MenuItem("Load Workspace...")) {
          beginLoadWorkspaceDialog();
        }
        if (ImGui::BeginMenu("Recent Workspaces", !recentWorkspacePaths_.empty())) {
          for (size_t i = 0; i < recentWorkspacePaths_.size(); ++i) {
            const std::string &path = recentWorkspacePaths_[i];
            std::string label = fileNameOnly(path) + "###recent" + std::to_string(i);
            if (ImGui::MenuItem(label.c_str())) {
              pendingRecentWorkspaceLoad_ = path;
            }
            if (ImGui::IsItemHovered()) {
              ImGui::SetTooltip("%s", path.c_str());
            }
          }
          ImGui::EndMenu();
        }
        ImGui::Separator();
        ImGui::MenuItem("Exit", nullptr, false, false);
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem("ImGui Demo", nullptr, &showDemoWindow_);
        ImGui::EndMenu();
      }
      if (ImGui::BeginMenu("Panels")) {
        if (ImGui::MenuItem("Add Time Series Panel")) {
          addTimeSeriesPanel({ "RPM" });
        }
        if (ImGui::MenuItem("Add Scatter Panel")) {
          addScatterPanel("RPM", "AFR");
        }
        if (ImGui::MenuItem("Add Status Panel")) {
          addStatusPanel();
        }
        if (ImGui::MenuItem("Add VE Analyzer / Observed AFR")) {
          addVeAnalysisPanel();
        }
        ImGui::EndMenu();
      }
      ImGui::EndMenuBar();
    }
  }

  void App::renderDockspace() {
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

    ImGui::End();
  }

  void App::renderLoadErrorPopup()
  {
    if (showLoadErrorPopup_) {
      ImGui::OpenPopup("Load Error");
      showLoadErrorPopup_ = false;
    }

    if (ImGui::BeginPopupModal("Load Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextWrapped("%s", loadErrorMessage_.c_str());
      if (ImGui::Button("OK", ImVec2(120, 0))) {
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
  }

  void App::render()
  {
    pollPendingDialogs();

    renderDockspace();

    if (!pendingRecentWorkspaceLoad_.empty()) {
      std::string path = std::move(pendingRecentWorkspaceLoad_);
      pendingRecentWorkspaceLoad_.clear();
      loadWorkspaceFile(path);
    }

    renderLoadProgressModal();
    renderLoadErrorPopup();

    for (auto &panel : panels_) {
      panel->render(cursor_);
    }

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