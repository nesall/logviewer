#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/logsession.h"
#include "ui/plotpanel.h"

namespace ui {

  // Scatter plot of one channel against another (e.g. RPM vs AFR), with
  // channel-picker dropdowns for X, Y, and an optional Color (Z) channel --
  // e.g. RPM x MAP colored by EGT. Reads but does not write the shared
  // PlotCursor: when the cursor is active, highlights the sample nearest
  // that time, so you can see where in the scatter the engine was operating
  // at that moment in the log.
  class ScatterPanel : public PlotPanel {
  public:
    ScatterPanel(std::string title, std::string initialXChannel, std::string initialYChannel);

    void render(PlotCursor &cursor) override;
    bool wantsClose() const override { return !open_; }

    void setSession(const core::LogSession *session) override;

    std::string panelTypeId() const override { return "ScatterPanel"; }
    nlohmann::json saveState() const override;
    void loadState(const nlohmann::json &state) override;

  private:
    void generatePlaceholderData();
    void rebindChannels();
    void rebuildColorData();
    void computeFilteredStats();
    void renderChannelsCombos(bool sameLine = false);
    void renderChannelsPopup();

    std::string selectedXChannel_;
    std::string selectedYChannel_;
    std::string selectedColorChannel_; // empty = "None" (flat color)
    const core::LogSession *session_ = nullptr;
    bool open_ = true;

    const std::vector<double> *cachedTimeSec_ = nullptr;
    const std::vector<double> *cachedXValues_ = nullptr;
    const std::vector<double> *cachedYValues_ = nullptr;
    std::string cachedXUnit_;
    std::string cachedYUnit_;
    std::string cachedZUnit_;

    // Only populated when selectedColorChannel_ is non-empty and found.
    double colorChannelMin_ = 0.0;
    double colorChannelMax_ = 0.0;

    std::vector<double> placeholderXValues_;
    std::vector<double> placeholderYValues_;

    bool enableColorFilter_ = false;
    double colorFilterMin_ = 0.0;
    double colorFilterMax_ = 0.0;
    bool colorFilterMinMaxSet_ = false;

    // Filtered buffer for plotting
    std::vector<double> filteredXValues_;
    std::vector<double> filteredYValues_;
    std::vector<uint32_t> filteredPointColors_;

    bool showStatsOverlay_ = true;
    struct ScatterStats {
      size_t pointCount = 0;
      double timeInZoneSec = 0.0;
      double avgX = 0.0;
      double avgY = 0.0;
      double avgZ = 0.0;
      double maxZ = 0.0;
      double maxXAtMaxZ = 0.0;
      double maxYAtMaxZ = 0.0;
      double correlationXY = 0.0; // Pearson r
      bool valid = false;
    } stats_;
  };

} // namespace ui