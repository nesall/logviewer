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
      "All Supported Logs (*.msl, *.dl)", "*.msl *.dl",
        "MegaSquirt Log Files (*.msl)", "*.msl",
        "Haltech Log Files (*.dl)", "*.dl",
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
    startAsyncLogLoad(path);
  }

  // ---------------------------------------------------------------------
  // Workspace save/load
  // ---------------------------------------------------------------------

  void App::saveWorkspaceFile(const std::string &path)
  {
    nlohmann::json root;
    root["logFilePath"] = session_.sourcePath();

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
    
    auto restoreWorkspaceState = [this, root]()
      {
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
          // Rebind panel session references after adding virtual channels
          refreshPanelsFromSession();
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

        assert(panels_.empty());
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

#ifdef _DEBUG2
            std::cout << "Restoring panel: " << type << " state: " << state.dump() << std::endl;
#endif

            if (panel) {
              panel->loadState(state);
              if (panel->panelTypeId() == "DriveRegimePanel") {
                static_cast<DriveRegimePanel *>(panel.get())->setOnRegimesChangedCallback([this] { notifyRegimesUpdated(); });
                panel->setSession(&session_); // populates session_.regimeSummaries()
              }
              panels_.push_back(std::move(panel));
            }
          }
        }

        // Bind remaining panels now that regime definitions and summaries are populated
        for (auto &panel : panels_) {
          if (panel->panelTypeId() != "DriveRegimePanel") {
            panel->setSession(&session_);
          }
        }

      };

    if (root.contains("logFilePath") && root["logFilePath"].is_string()) {
      std::string logPath = root["logFilePath"].get<std::string>();
      if (!logPath.empty()) {
        startAsyncLogLoad(logPath, restoreWorkspaceState);
      } else {
        restoreWorkspaceState();
      }
    } else {
      restoreWorkspaceState();
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

  void App::updateWindowTitle()
  {
    if (window_ == nullptr) {
      return;
    }
    std::string title{ App::appBaseTitle() };
    if (!currentWorkspacePath_.empty()) {
      title += " - " + utils::path::fileNameWithoutExtension(currentWorkspacePath_);
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
    progressStatusText_ = "Loading " + utils::path::fileNameOnly(path) + "...";
    pendingOnCompleteCallback_ = std::move(onComplete);

    activeLoadTask_ = std::async(std::launch::async, [this, path]() {
      LoadResult res;

      // Choose parser based on extension
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
  }

  PlotPanel *App::addTimeSeriesPanel(const std::vector<std::string> &initialChannelNames,
    std::string explicitTitle)
  {
    std::string panelId = explicitTitle.empty()
      ? "Time Series " + std::to_string(getNextPanelIdForPrefix("Time Series"))
      : explicitTitle;

    auto panel = std::make_unique<TimeSeriesPanel>(panelId, initialChannelNames);
    panel->setSession(&session_);
    panels_.push_back(std::move(panel));
    return panels_.back().get();
  }

  PlotPanel *App::addScatterPanel(const std::string &initialXChannel,
    const std::string &initialYChannel,
    std::string explicitTitle)
  {
    std::string panelId = explicitTitle.empty()
      ? "Scatter " + std::to_string(getNextPanelIdForPrefix("Scatter"))
      : explicitTitle;

    auto panel = std::make_unique<ScatterPanel>(panelId, initialXChannel, initialYChannel);
    panel->setSession(&session_);
    panels_.push_back(std::move(panel));
    return panels_.back().get();
  }

  PlotPanel *App::addStatusPanel(std::string explicitTitle)
  {
    std::string panelId = explicitTitle.empty()
      ? "Status " + std::to_string(getNextPanelIdForPrefix("Status"))
      : explicitTitle;
    auto panel = std::make_unique<StatusPanel>(panelId);
    panel->setSession(&session_);
    panels_.push_back(std::move(panel));
    return panels_.back().get();
  }

  PlotPanel *App::addVeAnalysisPanel(std::string explicitTitle)
  {
    std::string panelId = explicitTitle.empty()
      ? "VE Analyzer " + std::to_string(getNextPanelIdForPrefix("VE Analyzer"))
      : explicitTitle;
    auto panel = std::make_unique<VeAnalysisPanel>(panelId);
    panel->setSession(&session_);
    panels_.push_back(std::move(panel));
    return panels_.back().get();
  }

  PlotPanel *App::addTableOverlayPanel(std::string explicitTitle)
  {
    std::string panelId = explicitTitle.empty()
      ? "Table Overlay " + std::to_string(getNextPanelIdForPrefix("Table Overlay"))
      : explicitTitle;
    auto panel = std::make_unique<TableOverlayPanel>(panelId);
    panel->setSession(&session_);
    panels_.push_back(std::move(panel));
    return panels_.back().get();
  }

  DriveRegimePanel *App::getOrAddDriveRegimePanel()
  {
    for (const auto &panel : panels_) {
      if (panel->panelTypeId() == "DriveRegimePanel") {
        return static_cast<DriveRegimePanel *>(panel.get());
      }
    }
    auto regimePanel = std::make_unique<DriveRegimePanel>();
    regimePanel->setSession(&session_);
    regimePanel->setOnRegimesChangedCallback([this] { notifyRegimesUpdated(); });
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
            std::string label = utils::path::fileNameOnly(path) + "###recent" + std::to_string(i);
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
        if (ui::UI::Button("Add Channel", {}, ImVec2(120, 0))) {
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
        if (ui::UI::Button("Update / Re-evaluate", {}, ImVec2(160, 0))) {
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
        if (ui::UI::Button("Delete Channel", {}, ImVec2(120, 0))) {
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

    pollPendingDialogs();

    if (!pendingOnCompleteCallback_) {
      renderDockspace();
      for (auto &panel : panels_) {
        panel->render(cursor_);
      }
    }

    renderLoadProgressModal();
    renderLoadErrorPopup();
    renderCustomChannelModal();
    renderChannelMappingModal();

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