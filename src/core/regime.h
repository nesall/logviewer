#pragma once

#include <string>
#include <vector>
#include <imgui.h>
#include "3rdparty/nlohmann/json_fwd.hpp"

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

  struct RegimeDef {
    RegimeType type;
    std::string id;          // e.g., "freeway_cruise", "custom_boost_spool"
    std::string displayName; // e.g., "Freeway Cruise"
    ImVec4 color = ImVec4(0.2f, 0.6f, 0.9f, 0.25f);
    bool showShading = false;
    bool isBuiltIn = false;  // Flag if you want to prevent deletion or reset to defaults

    // Core Channel Thresholds (leave NaN or nullopt if unused)
    double minRpm = std::numeric_limits<double>::quiet_NaN();
    double maxRpm = std::numeric_limits<double>::quiet_NaN();
    double minMap = std::numeric_limits<double>::quiet_NaN();
    double maxMap = std::numeric_limits<double>::quiet_NaN();
    double minTps = std::numeric_limits<double>::quiet_NaN();
    double maxTps = std::numeric_limits<double>::quiet_NaN();
    double minTpsDot = std::numeric_limits<double>::quiet_NaN();
    double maxTpsDot = std::numeric_limits<double>::quiet_NaN();
    double minClt = std::numeric_limits<double>::quiet_NaN();

    // Alert & Warning Thresholds (triggers card badges / warning messages)
    double maxEgtWarning = 1600.0;
    double maxCltWarning = 220.0;
    double maxDutyWarning = 90.0;

    // Optional: ExprTk Formula for complex logic beyond min/max bounds
    // e.g., "[MAP] > 110.0 & [RPM] < 3000.0 & [TPS] > 50.0"
    std::string customFormula;

    nlohmann::json toJson() const;
    static RegimeDef fromJson(const nlohmann::json &j);
  };

  struct RegimeSummary {
    RegimeDef def;

    std::vector<TimeInterval> intervals;

    size_t sampleCount = 0;
    double totalDwellTimeSec = 0.0;
    double percentageOfLog = 0.0;

    // Aggregated Metrics
    double avgRpm = 0.0;
    double avgMap = 0.0;
    double avgAfr = 0.0;
    double avgTiming = 0.0;
    double avgClt = 0.0;
    double avgMat = 0.0;
    double avgDuty = 0.0;
    double peakEgt = 0.0;
    double peakKnock = 0.0;
    double peakClt = 0.0;
    double peakMat = 0.0;
    double peakDuty = 0.0;

    std::string warningMessage; // e.g., "High Cruise EGT: 1640°F Peak"
  };

} // namespace core