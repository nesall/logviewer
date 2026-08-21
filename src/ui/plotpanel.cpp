#include "ui/plotpanel.h"
#include "utils/utils.h"
#include "3rdparty/nlohmann/json.hpp"
#include "3rdparty/IconsFontAwesome7.h"
#include "imgui.h"
#include <utility>

namespace ui {

  PlotPanel::PlotPanel(std::string title)
    : title_(std::move(title)), panelInstanceId_(utils::str::generateUniqueId())
  {
  }

  nlohmann::json PlotPanel::saveState() const
  {
    return nlohmann::json{
      {"title", title_},
      {"instanceId", panelInstanceId_}
    };
  }

  void PlotPanel::loadState(const nlohmann::json & state)
  {
    if (state.contains("title") && state["title"].is_string()) {
      title_ = state["title"].get<std::string>();
    }
    if (state.contains("instanceId") && state["instanceId"].is_string()) {
      panelInstanceId_ = state["instanceId"].get<std::string>();
    }
  }

  std::string PlotPanel::makeWindowLabel(const char *icon) const
  {
    return const_cast<PlotPanel *>(this)->lastWindowLabel_ = std::string(icon) + " " + title_ + "###" + panelInstanceId_;
  }

  void PlotPanel::renderContextMenu()
  {
    static char renameBuf[256]{ 0 };
    if (ImGui::BeginPopupContextItem("##PanelContextMenu")) {
      if (ImGui::MenuItem(ICON_FA_PEN " Rename Panel...")) {
        showRenameModal_ = true;
        memset(renameBuf, 0, sizeof(renameBuf));
        std::snprintf(renameBuf, sizeof(renameBuf), "%s", title_.c_str());
      }
      ImGui::EndPopup();
    }

    if (showRenameModal_) {
      ImGui::OpenPopup("Rename Panel###RenamePanelModal");
      showRenameModal_ = false;
    }

    if (ImGui::BeginPopupModal("Rename Panel###RenamePanelModal", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::Text("Enter new title for this panel:");
      ImGui::SetNextItemWidth(260.0f);

      // Auto-focus on appear
      if (ImGui::IsWindowAppearing()) {
        ImGui::SetKeyboardFocusHere();
      }

      bool confirmed = ImGui::InputText("##NewTitle", renameBuf, sizeof(renameBuf),
        ImGuiInputTextFlags_EnterReturnsTrue);

      ImGui::Spacing();
      if (ImGui::Button("OK", ImVec2(90, 0)) || confirmed) {
        if (renameBuf[0] != '\0') {
          title_ = renameBuf;
          notifyDataChanged();
        }
        ImGui::CloseCurrentPopup();
      }

      ImGui::SameLine();
      if (ImGui::Button("Cancel", ImVec2(90, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        ImGui::CloseCurrentPopup();
      }

      ImGui::EndPopup();
    }
  }

  void PlotPanel::renderCommonOps()
  {
    if (allowTitleEdit()) {
      renderContextMenu();
    }
    if (activateOnLoad_) {
      ImGui::SetWindowFocus();   // no args = current window; sets it as the selected tab
      activateOnLoad_ = false;
    }
  }

} // namespace ui