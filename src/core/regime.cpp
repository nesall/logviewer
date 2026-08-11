#include "core/regime.h"
#include "3rdparty/nlohmann/json.hpp"

#include <cmath>
#include <limits>
#include <string>
#include <vector>


namespace core {

  nlohmann::json RegimeDef::toJson() const
  {
    nlohmann::json j;
    j["id"] = id;
    j["displayName"] = displayName;
    j["color"] = { color.x, color.y, color.z, color.w };
    j["showShading"] = showShading;
    j["isBuiltIn"] = isBuiltIn;

    nlohmann::json boundsArr = nlohmann::json::array();
    for (const auto &b : boundsRules) boundsArr.push_back(b.toJson());
    j["boundsRules"] = boundsArr;

    nlohmann::json metricsArr = nlohmann::json::array();
    for (const auto &m : configuredMetrics) metricsArr.push_back(m.toJson());
    j["configuredMetrics"] = metricsArr;

    if (!customFormula.empty()) j["customFormula"] = customFormula;
    return j;
  }

  RegimeDef RegimeDef::fromJson(const nlohmann::json &j)
  {
    RegimeDef d;
    if (j.contains("id") && j["id"].is_string()) d.id = j["id"].get<std::string>();
    if (j.contains("displayName") && j["displayName"].is_string()) d.displayName = j["displayName"].get<std::string>();
    if (j.contains("color") && j["color"].is_array() && j["color"].size() == 4) {
      d.color.x = j["color"][0].get<float>();
      d.color.y = j["color"][1].get<float>();
      d.color.z = j["color"][2].get<float>();
      d.color.w = j["color"][3].get<float>();
    }
    if (j.contains("showShading") && j["showShading"].is_boolean()) d.showShading = j["showShading"].get<bool>();
    if (j.contains("isBuiltIn") && j["isBuiltIn"].is_boolean()) d.isBuiltIn = j["isBuiltIn"].get<bool>();
    if (j.contains("boundsRules") && j["boundsRules"].is_array()) {
      for (const auto &bJson : j["boundsRules"]) {
        d.boundsRules.push_back(ChannelBoundRule::fromJson(bJson));
      }
    }
    if (j.contains("configuredMetrics") && j["configuredMetrics"].is_array()) {
      for (const auto &mJson : j["configuredMetrics"]) {
        d.configuredMetrics.push_back(RegimeMetricRule::fromJson(mJson));
      }
    }
    if (j.contains("customFormula") && j["customFormula"].is_string()) d.customFormula = j["customFormula"].get<std::string>();
    return d;
  }

  nlohmann::json RegimeMetricRule::toJson() const
  {
    using nlohmann::json;
    json j;
    j["channelName"] = channelName;
    switch (aggregation) {
    case MetricAgg::Average: j["aggregation"] = "Average"; break;
    case MetricAgg::Peak: j["aggregation"] = "Peak"; break;
    case MetricAgg::Min: j["aggregation"] = "Min"; break;
    default: j["aggregation"] = "Average"; break;
    }
    if (!customLabel.empty()) j["customLabel"] = customLabel;
    else j["customLabel"] = nullptr;
    return j;
  }

  RegimeMetricRule RegimeMetricRule::fromJson(const nlohmann::json &j)
  {
    RegimeMetricRule rule;
    if (j.contains("channelName") && j["channelName"].is_string()) rule.channelName = j["channelName"].get<std::string>();
    if (j.contains("aggregation") && j["aggregation"].is_string()) {
      const std::string &aggStr = j["aggregation"].get<std::string>();
      if (aggStr == "Average") rule.aggregation = MetricAgg::Average;
      else if (aggStr == "Peak") rule.aggregation = MetricAgg::Peak;
      else if (aggStr == "Min") rule.aggregation = MetricAgg::Min;
    }
    if (j.contains("customLabel") && j["customLabel"].is_string()) rule.customLabel = j["customLabel"].get<std::string>();
    return rule;
  }

  nlohmann::json ChannelBoundRule::toJson() const
  {
    using nlohmann::json;
    json j;
    j["channelName"] = channelName;
    j["minVal"] = minVal;
    j["maxVal"] = maxVal;
    return j;
  }

  ChannelBoundRule ChannelBoundRule::fromJson(const nlohmann::json &j)
  {
    ChannelBoundRule rule;
    if (j.contains("channelName") && j["channelName"].is_string()) rule.channelName = j["channelName"].get<std::string>();
    if (j.contains("minVal") && j["minVal"].is_number()) rule.minVal = j["minVal"].get<double>();
    if (j.contains("maxVal") && j["maxVal"].is_number()) rule.maxVal = j["maxVal"].get<double>();
    return rule;
  }

} // namespace core