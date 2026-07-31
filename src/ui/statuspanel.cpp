#include "ui/statuspanel.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <utility>

#include "3rdparty/nlohmann/json.hpp"
#include "imgui.h"

namespace ui {

  namespace {

    std::string toLower(const std::string &s) {
      std::string result = s;
      for (char &c : result) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      return result;
    }

    // Truncates text with a trailing "..." so it fits within maxWidth pixels
    // at the current font, rather than letting ImGui silently clip it.
    std::string truncateToWidth(const std::string &text, float maxWidth) {
      if (ImGui::CalcTextSize(text.c_str()).x <= maxWidth) {
        return text;
      }
      const char *kEllipsis = "...";
      const float ellipsisWidth = ImGui::CalcTextSize(kEllipsis).x;
      for (size_t len = text.size(); len > 0; --len) {
        std::string candidate = text.substr(0, len);
        if (ImGui::CalcTextSize(candidate.c_str()).x + ellipsisWidth <= maxWidth) {
          return candidate + kEllipsis;
        }
      }
      return kEllipsis;
    }

  } // namespace

  StatusPanel::StatusPanel(std::string title) : PlotPanel(std::move(title)) {}

  void StatusPanel::setSession(const core::LogSession *session) {
    session_ = session;
    // Deliberately not clearing visibilityOverrides_ here -- user choices
    // persist across reloads by channel name (see class comment).
  }

  bool StatusPanel::isVisible_(const core::Channel &channel) const {
    auto it = visibilityOverrides_.find(channel.name());
    if (it != visibilityOverrides_.end()) {
      return it->second;
    }
    return channel.isBoolean();
  }

  void StatusPanel::setVisible_(const std::string &channelName, bool visible) {
    visibilityOverrides_[channelName] = visible;
  }

  nlohmann::json StatusPanel::saveState() const {
    auto overrides = PlotPanel::saveState();
    for (const auto &[name, visible] : visibilityOverrides_) {
      overrides[name] = visible;
    }
    return nlohmann::json{ {"visibilityOverrides", overrides} };
  }

  void StatusPanel::loadState(const nlohmann::json &state) {
    PlotPanel::loadState(state);
    if (!state.contains("visibilityOverrides")) {
      return;
    }
    const auto &overrides = state["visibilityOverrides"];
    if (!overrides.is_object()) {
      return;
    }
    for (auto it = overrides.begin(); it != overrides.end(); ++it) {
      if (it.value().is_boolean()) {
        visibilityOverrides_[it.key()] = it.value().get<bool>();
      }
    }
  }

  size_t StatusPanel::nearestIndex_(const std::vector<double> &timeSec, double queryTime) const {
    auto it = std::lower_bound(timeSec.begin(), timeSec.end(), queryTime);
    size_t index = static_cast<size_t>(it - timeSec.begin());
    if (index >= timeSec.size()) {
      index = timeSec.size() - 1;
    } else if (index > 0) {
      double distNext = std::fabs(timeSec[index] - queryTime);
      double distPrev = std::fabs(timeSec[index - 1] - queryTime);
      if (distPrev < distNext) {
        index -= 1;
      }
    }
    return index;
  }

  void StatusPanel::renderChannelPickerPopup_() {
    if (ImGui::Button("Channels...")) {
      ImGui::OpenPopup("StatusPanelChannelsPopup");
    }

    if (ImGui::BeginPopup("StatusPanelChannelsPopup")) {
      if (session_ == nullptr || session_->channels().empty()) {
        ImGui::TextDisabled("No log loaded yet.");
        ImGui::EndPopup();
        return;
      }

      if (ImGui::Button("Select All")) {
        for (const auto &channel : session_->channels()) {
          setVisible_(channel.name(), true);
        }
      }
      ImGui::SameLine();
      if (ImGui::Button("Unselect All")) {
        for (const auto &channel : session_->channels()) {
          setVisible_(channel.name(), false);
        }
      }
      ImGui::SameLine();
      if (ImGui::Button("Status Channels Only")) {
        for (const auto &channel : session_->channels()) {
          setVisible_(channel.name(), channel.isBoolean());
        }
      }

      ImGui::InputTextWithHint("##Filter", "Filter channels...", filterText_.data(),
        filterText_.capacity() + 1,
        ImGuiInputTextFlags_CallbackResize,
        [](ImGuiInputTextCallbackData *data) -> int {
          auto *str = static_cast<std::string *>(data->UserData);
          str->resize(static_cast<size_t>(data->BufTextLen));
          data->Buf = str->data();
          return 0;
        },
        &filterText_);

      ImGui::BeginChild("StatusPanelChannelList", ImVec2(320, 360), ImGuiChildFlags_Borders);
      const std::string filterLower = toLower(filterText_);
      for (const auto &channel : session_->channels()) {
        if (!filterLower.empty() && toLower(channel.name()).find(filterLower) == std::string::npos) {
          continue;
        }
        bool visible = isVisible_(channel);
        if (ImGui::Checkbox(channel.name().c_str(), &visible)) {
          setVisible_(channel.name(), visible);
        }
      }
      ImGui::EndChild();

      ImGui::EndPopup();
    }
  }

  void StatusPanel::renderCellGrid_(const PlotCursor &cursor) {
    if (session_ == nullptr || session_->channels().empty()) {
      ImGui::TextDisabled("No log loaded yet.");
      return;
    }

    const std::vector<double> *timeSec = session_->timeSec();

    constexpr float kCellWidth = 150.0f;
    constexpr float kCellHeight = 40.0f;
    const float availWidth = ImGui::GetContentRegionAvail().x;
    const int columns = std::max(1, static_cast<int>(availWidth / kCellWidth));

    int col = 0;
    for (const auto &channel : session_->channels()) {
      if (!isVisible_(channel)) {
        continue;
      }

      bool haveValue = false;
      double value = 0.0;
      if (timeSec != nullptr && !timeSec->empty() && channel.values().size() == timeSec->size()) {
        size_t index = cursor.active ? nearestIndex_(*timeSec, cursor.timeSec) : timeSec->size() - 1;
        value = channel.values()[index];
        haveValue = true;
      }

      const bool active = haveValue && channel.isBoolean() && value >= 0.5;
      ImVec4 color;
      if (!haveValue) {
        color = ImVec4(0.25f, 0.25f, 0.25f, 1.0f); // no data
      } else if (channel.isBoolean()) {
        color = active ? ImVec4(0.20f, 0.65f, 0.25f, 1.0f) : ImVec4(0.35f, 0.35f, 0.35f, 1.0f);
      } else {
        color = ImVec4(0.25f, 0.35f, 0.55f, 1.0f); // neutral -- value shown as text instead
      }

      std::string label = channel.name();
      if (haveValue && !channel.isBoolean()) {
        char valueText[32];
        std::snprintf(valueText, sizeof(valueText), "%.1f", value);
        label += ": ";
        label += valueText;
      }

      ImGui::PushStyleColor(ImGuiCol_Button, color);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, color);
      const ImVec2 cellSize(kCellWidth - 8.0f, kCellHeight);
      std::string displayLabel = truncateToWidth(label, cellSize.x - 16.0f);
      ImGui::Button(displayLabel.c_str(), cellSize);
      ImGui::PopStyleColor(3);

      if (ImGui::IsItemHovered()) {
        if (!channel.unit().empty()) {
          ImGui::SetTooltip("%s %s", label.c_str(), channel.unit().c_str());
        } else {
          ImGui::SetTooltip("%s", label.c_str());
        }
      }

      ++col;
      if (col < columns) {
        ImGui::SameLine();
      } else {
        col = 0;
      }
    }
  }

  void StatusPanel::render(PlotCursor &cursor) {
    std::string windowLabel = "Status###" + title();
    ImGui::Begin(windowLabel.c_str(), &open_);

    renderChannelPickerPopup_();
    ImGui::Separator();
    renderCellGrid_(cursor);

    ImGui::End();
  }

} // namespace ui