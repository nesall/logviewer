#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <imgui.h>
#include "3rdparty/nlohmann/json_fwd.hpp"

namespace core {

  enum class MetricAgg {
    Average,
    Peak,
    Min
  };

  struct RegimeMetricRule {
    std::string channelName; // Matches LogSession channel or ChannelMapping slot
    MetricAgg aggregation = MetricAgg::Average;
    std::string customLabel; // Optional override label (e.g., "Peak EGT"), defaults to channel name if empty

    nlohmann::json toJson() const;
    static RegimeMetricRule fromJson(const nlohmann::json &j);
  };

  struct TimeInterval {
    double startSec = 0.0;
    double endSec = 0.0;
  };

  struct ChannelBoundRule {
    std::string channelName; // e.g., "RPM", "MAP", "TPSdot", or custom channel
    double minVal = std::numeric_limits<double>::quiet_NaN();
    double maxVal = std::numeric_limits<double>::quiet_NaN();

    nlohmann::json toJson() const;
    static ChannelBoundRule fromJson(const nlohmann::json &j);
  };

  struct RegimeDef {
    std::string id;          // e.g., "freeway_cruise", "custom_spool"
    std::string displayName; // e.g., "Freeway Cruise"
    ImVec4 color = ImVec4(0.2f, 0.6f, 0.9f, 0.25f);
    bool showShading = false;
    bool isBuiltIn = false;

    // Dynamic detection rules & card metrics
    std::vector<ChannelBoundRule> boundsRules;
    std::vector<RegimeMetricRule> configuredMetrics;

    // Warning alerts & ExprTk fallback
    double maxEgtWarning = 1600.0;
    double maxCltWarning = 220.0;
    double maxDutyWarning = 90.0;
    std::string customFormula;

    struct Ids {
      inline static constexpr std::string_view freeway_cruise{ "freeway_cruise" };
      inline static constexpr std::string_view overrun_fuel_cut{ "overrun_fuel_cut" };
    };

    nlohmann::json toJson() const;
    static RegimeDef fromJson(const nlohmann::json &j);
  };

  struct CalculatedMetricValue {
    RegimeMetricRule rule;
    double value = 0.0;
    std::string unit;
  };

  struct RegimeSummary {
    RegimeDef def;
    std::vector<TimeInterval> intervals;

    size_t sampleCount = 0;
    double totalDwellTimeSec = 0.0;
    double percentageOfLog = 0.0;

    // Evaluated results corresponding to def.configuredMetrics
    std::vector<CalculatedMetricValue> metricResults;

    std::string warningMessage; // e.g., "High Cruise EGT: 1640°F Peak"
  };

} // namespace core