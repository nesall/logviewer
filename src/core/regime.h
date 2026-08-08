#pragma once

#include <string>
#include <vector>
#include <imgui.h>

namespace core {

  struct TimeInterval {
    double startSec = 0.0;
    double endSec = 0.0;
  };

  enum class RegimeType {
    FreewayCruise,
    OverrunFuelCut,
    HighGearLowRpmBoost,
    HighRpmVacuum,
    IdleStability,
    TransientTipIn
  };

  struct RegimeSummary {
    RegimeType type;
    std::string id;            // Internal key e.g. "freeway_cruise"
    std::string displayName;   // UI Label e.g. "Freeway Cruise"

    ImVec4 color = ImVec4(0.2f, 0.6f, 0.9f, 0.30f); // Default shaded color
    bool showShading = false;   // Toggle timeline shading

    std::vector<TimeInterval> intervals;

    size_t sampleCount = 0;
    double totalDwellTimeSec = 0.0;
    double percentageOfLog = 0.0;

    // Aggregated Metrics
    double avgRpm = 0.0;
    double avgMap = 0.0;
    double avgAfr = 0.0;
    double avgTiming = 0.0;
    double peakEgt = 0.0;
    double peakKnock = 0.0;

    std::string warningMessage; // e.g., "High Cruise EGT: 1640°F Peak"
  };

} // namespace core