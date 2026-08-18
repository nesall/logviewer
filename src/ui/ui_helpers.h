#pragma once

#include "imgui.h"

namespace ui {

  namespace popups {

    inline constexpr const char *Loading = "Loading###LoadingModal";
    inline constexpr const char *TargetAfrEditor = "Edit Target AFR###EditTargetAfrModal";
    inline constexpr const char *BaselineVeEditor = "Edit Baseline VE###EditBaselineVeModal";
    inline constexpr const char *AxisEditor = "Axis Editor###AxisEditorModal";
    inline constexpr const char *TableImportExportMenu = "Table Import / Export###TableImportExportMenu";
    inline constexpr const char *ExtrapolateVe = "Extrapolate VE###ExtrapolateVeModal";
    inline constexpr const char *CustomChannels = "Custom Calculated Channels###CustomChannelsModal";
    inline constexpr const char *LoadError = "Load Error###LoadErrorModal";
    inline constexpr const char *StatusPanelChannels = "Status Panel Channels###StatusPanelChannelsPopup";
    inline constexpr const char *ScatterPanelChannels = "Scatter Panel Channels###ScatterPanelChannelsPopup";
    inline constexpr const char *ChannelMapping = "Channel Semantic Mapping###ChannelMappingModal";
    inline constexpr const char *ConcatLog = "Stitch / Concat Log###ConcatLogModal";
  } // namespace popups


  enum class ButtonStyle {
    Default,   // Standard ImGui theme button
    Primary,   // Accent blue - primary workflows (e.g. Apply, Recalculate)
    Secondary, // Neutral - secondary workflows (e.g. Copy, Paste, Export)
    Success,   // Green - confirmation / generator actions (e.g. Select Unvisited)
    Danger,    // Red - destructive / reset actions (e.g. Reset to Calculated, Delete)
  };

  class UI {
    static ImVec4 Hover(ImVec4 c) {
      return ImVec4(
        (std::min)(c.x + 0.08f, 1.0f),
        (std::min)(c.y + 0.08f, 1.0f),
        (std::min)(c.z + 0.08f, 1.0f),
        c.w);
    } 
    static ImVec4 Active(ImVec4 c) {
      return ImVec4(
        c.x * 0.85f,
        c.y * 0.85f,
        c.z * 0.85f,
        c.w);
    }
  public:
    static bool Button(const char *label, ButtonStyle style = ButtonStyle::Default, const ImVec2 &size = ImVec2(0, 0), const char *tooltip = nullptr) {

      std::optional<ImVec4> normal;

      switch (style) {
      case ButtonStyle::Primary:
        normal = ImVec4(0.18f, 0.42f, 0.70f, 1.0f);
        break;
      case ButtonStyle::Secondary:
        normal = ImVec4(0.33f, 0.42f, 0.52f, 1.0f);
        break;
      case ButtonStyle::Success:
        normal = ImVec4(0.18f, 0.50f, 0.28f, 1.0f);
        break;
      case ButtonStyle::Danger:
        normal = ImVec4(0.60f, 0.20f, 0.20f, 1.0f);
        break;
      default:
        break;
      }

      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);

      if (normal) {
        ImGui::PushStyleColor(ImGuiCol_Button, *normal);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, Hover(*normal));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, Active(*normal));
      }

      bool clicked = ImGui::Button(label, size);

      if (ImGui::IsItemHovered() && tooltip && tooltip[0] != '\0') {
        ImGui::SetTooltip("%s", tooltip);
      }

      if (normal) {
        ImGui::PopStyleColor(3);
      }

      ImGui::PopStyleVar(1);
      
      return clicked;
    }

    static bool ButtonPrimary(const char *label, const ImVec2 &size = ImVec2(0, 0), const char *tooltip = nullptr) {
      return Button(label, ButtonStyle::Primary, size, tooltip);
    }

    static bool ButtonSuccess(const char *label, const ImVec2 &size = ImVec2(0, 0), const char *tooltip = nullptr) {
      return Button(label, ButtonStyle::Success, size, tooltip);
    }

    static bool ButtonSecondary(const char *label, const ImVec2 &size = ImVec2(0, 0), const char *tooltip = nullptr) {
      return Button(label, ButtonStyle::Secondary, size, tooltip);
    }

    static bool ButtonDanger(const char *label, const ImVec2 &size = ImVec2(0, 0), const char *tooltip = nullptr) {
      return Button(label, ButtonStyle::Danger, size, tooltip);
    }
  };

} // namespace ui
