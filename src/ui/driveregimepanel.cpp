#include "ui/driveregimepanel.h"
#include "ui/ui_helpers.h"
#include "engine/regime_analyzer.h"
#include "3rdparty/nlohmann/json.hpp"
#include "3rdparty/IconsFontAwesome7.h"

#include <cstdio>
#include "imgui.h"

namespace {
  std::string dtostr(double d, int precision = 2) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", precision, d);
    return std::string(buffer);
  }
} // anonymous namespace

namespace ui {

  DriveRegimePanel::DriveRegimePanel(std::string title)
    : PlotPanel(std::move(title)) {}

  void DriveRegimePanel::setSession(const core::LogSession *session)
  {
    session_ = session;
    reanalyze();
  }

  void DriveRegimePanel::reanalyze()
  {
    if (session_) {
      auto mutableSession = const_cast<core::LogSession *>(session_);
      mutableSession->setRegimeSummaries(engine::RegimeAnalyzer::analyzeSession(*session_));
      summaries_ = mutableSession->regimeSummaries();
    } else {
      summaries_.clear();
    }
  }

  void DriveRegimePanel::render(PlotCursor &cursor)
  {
    std::string windowLabel = title() + "###" + panelTypeId();
    ImGui::Begin(windowLabel.c_str(), &open_);

    if (!session_) {
      ImGui::TextDisabled("No log session loaded.");
      ImGui::End();
      return;
    }

    // --- Top Control Bar ---
    if (ui::UI::ButtonPrimary(ICON_FA_REPEAT " Re-Analyze Session")) {
      reanalyze();
    }

    const auto *timeSec = session_->timeSec();
    if (timeSec && !timeSec->empty()) {
      ImGui::SameLine();
      ImGui::TextDisabled("| Total Log Time: %.1f s", timeSec->back() - timeSec->front());
    }

    ImGui::Separator();

    if (summaries_.empty()) {
      ImGui::TextDisabled("No regimes detected in the current log range.");
      ImGui::End();
      return;
    }

    // --- Regime Cards Loop ---
// --- Regime Cards Loop ---
    for (auto &reg : summaries_) {
      ImGui::PushID(reg.id.c_str());

      // 1. Build Header Title with Alert Badge (visible even when collapsed)
      std::string headerLabel = reg.displayName + "  (" + dtostr(reg.percentageOfLog, 1) + "% of log)";

      bool hasAlert = !reg.warningMessage.empty();
      if (hasAlert) {
        // Append critical alert text to the header title
        headerLabel += "  [ " + std::string(ICON_FA_TRIANGLE_EXCLAMATION) + " " + dtostr(reg.peakEgt, 0) + " °F ]";

        // Highlight header text color red/orange if critical alert is present
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.45f, 1.0f));
      }

      bool nodeOpen = ImGui::CollapsingHeader(headerLabel.c_str(), ImGuiTreeNodeFlags_DefaultOpen);

      if (hasAlert) {
        ImGui::PopStyleColor();
      }

      // 2. Expanded Card Body
      if (nodeOpen) {
        ImGui::Indent(8.0f);
        ImGui::Spacing();

        // Controls Row
        if (ImGui::Checkbox("Shade Timeline", &reg.showShading)) {
          if (session_) {
            const_cast<core::LogSession *>(session_)->setRegimeSummaries(summaries_);
          }
        }

        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        if (ImGui::ColorEdit4("Highlight Color", (float *)&reg.color,
          ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoAlpha)) {
          if (session_) {
            const_cast<core::LogSession *>(session_)->setRegimeSummaries(summaries_);
          }
        }

        ImGui::Spacing();

        // Detailed Warning Banner Inside Body
        if (hasAlert) {
          ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), ICON_FA_TRIANGLE_EXCLAMATION " %s", reg.warningMessage.c_str());
          ImGui::Spacing();
        }

        // Dwell Time Info
        ImGui::TextDisabled("Dwell Time: %s s across %zu intervals", dtostr(reg.totalDwellTimeSec, 1).c_str(), reg.intervals.size());
        ImGui::Spacing();

        // Compact 2-Pair Key/Value Grid
        if (ImGui::BeginTable("CompactMetrics", 4, ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV)) {
          ImGui::TableSetupColumn("K1", ImGuiTableColumnFlags_WidthFixed, 65.0f);
          ImGui::TableSetupColumn("V1", ImGuiTableColumnFlags_WidthFixed, 60.0f);
          ImGui::TableSetupColumn("K2", ImGuiTableColumnFlags_WidthFixed, 65.0f);
          ImGui::TableSetupColumn("V2", ImGuiTableColumnFlags_WidthFixed, 60.0f);

          // Row 1: Avg RPM & Avg MAP
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("Avg RPM");
          ImGui::TableSetColumnIndex(1); ImGui::Text("%s", dtostr(reg.avgRpm, 0).c_str());
          ImGui::TableSetColumnIndex(2); ImGui::TextDisabled("Avg MAP");
          ImGui::TableSetColumnIndex(3); ImGui::Text("%s kPa", dtostr(reg.avgMap, 1).c_str());

          // Row 2: Avg AFR & Avg Timing
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("Avg AFR");
          ImGui::TableSetColumnIndex(1); ImGui::Text("%s", dtostr(reg.avgAfr, 2).c_str());
          ImGui::TableSetColumnIndex(2); ImGui::TextDisabled("Avg Timing");
          ImGui::TableSetColumnIndex(3); ImGui::Text("%s°", dtostr(reg.avgTiming, 1).c_str());

          // Row 3: Peak EGT
          ImGui::TableNextRow();
          ImGui::TableSetColumnIndex(0); ImGui::TextDisabled("Peak EGT");
          ImGui::TableSetColumnIndex(1);
          if (reg.peakEgt > 1600.0) {
            ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.4f, 1.0f), "%s °F", dtostr(reg.peakEgt, 0).c_str());
          } else {
            ImGui::Text("%s °F", dtostr(reg.peakEgt, 0).c_str());
          }

          ImGui::EndTable();
        }

        ImGui::Spacing();

        // Cycle Jump Buttons
        if (!reg.intervals.empty()) {
          if (ui::UI::ButtonSecondary(ICON_FA_LOCATION_DOT " Jump Next Event")) {
            // Find next interval after cursor position
            double targetTime = reg.intervals.front().startSec;
            for (const auto &interval : reg.intervals) {
              if (interval.startSec > cursor.timeSec + 0.1) {
                targetTime = interval.startSec;
                break;
              }
            }
            cursor.timeSec = targetTime;
            cursor.active = true;
          }
        }

        ImGui::Unindent(8.0f);
      }

      // 3. 10px Vertical Spacing Between Cards
      ImGui::Dummy(ImVec2(0.0f, 10.0f));

      ImGui::PopID();
    }

    ImGui::End();
  }

  nlohmann::json DriveRegimePanel::saveState() const
  {
    auto j = PlotPanel::saveState();
    nlohmann::json regArray = nlohmann::json::array();
    for (const auto &reg : summaries_) {
      regArray.push_back({
        {"id", reg.id},
        {"showShading", reg.showShading},
        {"color", {reg.color.x, reg.color.y, reg.color.z, reg.color.w}}
        });
    }
    j["regimes"] = regArray;
    return j;
  }

  void DriveRegimePanel::loadState(const nlohmann::json &state)
  {
    PlotPanel::loadState(state);
    reanalyze();

    if (state.contains("regimes") && state["regimes"].is_array()) {
      for (const auto &item : state["regimes"]) {
        std::string id = item.value("id", "");
        for (auto &reg : summaries_) {
          if (reg.id == id) {
            reg.showShading = item.value("showShading", false);
            if (item.contains("color") && item["color"].is_array() && item["color"].size() == 4) {
              reg.color = ImVec4(item["color"][0], item["color"][1], item["color"][2], item["color"][3]);
            }
          }
        }
      }
    }
  }

} // namespace ui