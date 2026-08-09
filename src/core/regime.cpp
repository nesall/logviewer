// core/regime.cpp
#include "core/regime.h"
#include "3rdparty/nlohmann/json.hpp"

#include <cmath>
#include <limits>
#include <string>
#include <vector>


namespace core {

  namespace {
    std::string regimeTypeToString(RegimeType t) {
      switch (t) {
      case RegimeType::FreewayCruise: return "FreewayCruise";
      case RegimeType::OverrunFuelCut: return "OverrunFuelCut";
      case RegimeType::HighGearLowRpmBoost: return "HighGearLowRpmBoost";
      case RegimeType::HighRpmVacuum: return "HighRpmVacuum";
      case RegimeType::IdleStability: return "IdleStability";
      case RegimeType::TransientTipIn: return "TransientTipIn";
      default: return "Unknown";
      }
    }

    RegimeType regimeTypeFromString(const std::string &s) {
      if (s == "FreewayCruise") return RegimeType::FreewayCruise;
      if (s == "OverrunFuelCut") return RegimeType::OverrunFuelCut;
      if (s == "HighGearLowRpmBoost") return RegimeType::HighGearLowRpmBoost;
      if (s == "HighRpmVacuum") return RegimeType::HighRpmVacuum;
      if (s == "IdleStability") return RegimeType::IdleStability;
      if (s == "TransientTipIn") return RegimeType::TransientTipIn;
      return RegimeType::FreewayCruise;
    }
  } // anonymous namespace

  nlohmann::json RegimeDef::toJson() const {
    using nlohmann::json;
    json j;
    j["type"] = regimeTypeToString(type);
    j["id"] = id;
    j["displayName"] = displayName;

    // color as array [r,g,b,a]
    j["color"] = { color.x, color.y, color.z, color.w };

    j["showShading"] = showShading;
    j["isBuiltIn"] = isBuiltIn;

    auto putOptional = [&j](const char *key, double value) {
      if (!std::isnan(value)) j[key] = value;
      else j[key] = nullptr;
      };

    putOptional("minRpm", minRpm);
    putOptional("maxRpm", maxRpm);
    putOptional("minMap", minMap);
    putOptional("maxMap", maxMap);
    putOptional("minTps", minTps);
    putOptional("maxTps", maxTps);
    putOptional("minTpsDot", minTpsDot);
    putOptional("maxTpsDot", maxTpsDot);
    putOptional("minClt", minClt);

    // Warnings (always numeric by default)
    j["maxEgtWarning"] = maxEgtWarning;
    j["maxCltWarning"] = maxCltWarning;
    j["maxDutyWarning"] = maxDutyWarning;

    if (!customFormula.empty()) j["customFormula"] = customFormula;
    else j["customFormula"] = nullptr;

    return j;
  }

  RegimeDef RegimeDef::fromJson(const nlohmann::json &j) {
    RegimeDef d;

    if (j.contains("type") && j["type"].is_string()) {
      d.type = regimeTypeFromString(j["type"].get<std::string>());
    } else {
      d.type = RegimeType::FreewayCruise;
    }

    if (j.contains("id") && j["id"].is_string()) d.id = j["id"].get<std::string>();
    if (j.contains("displayName") && j["displayName"].is_string()) d.displayName = j["displayName"].get<std::string>();

    if (j.contains("color") && j["color"].is_array()) {
      const auto &arr = j["color"];
      float r = arr.size() > 0 && arr[0].is_number() ? arr[0].get<float>() : 0.2f;
      float g = arr.size() > 1 && arr[1].is_number() ? arr[1].get<float>() : 0.6f;
      float b = arr.size() > 2 && arr[2].is_number() ? arr[2].get<float>() : 0.9f;
      float a = arr.size() > 3 && arr[3].is_number() ? arr[3].get<float>() : 0.25f;
      d.color = ImVec4(r, g, b, a);
    }

    if (j.contains("showShading") && j["showShading"].is_boolean()) d.showShading = j["showShading"].get<bool>();
    if (j.contains("isBuiltIn") && j["isBuiltIn"].is_boolean()) d.isBuiltIn = j["isBuiltIn"].get<bool>();

    auto readOptional = [&j](const char *key) -> double {
      if (j.contains(key) && j[key].is_number()) return j[key].get<double>();
      return std::numeric_limits<double>::quiet_NaN();
      };

    d.minRpm = readOptional("minRpm");
    d.maxRpm = readOptional("maxRpm");
    d.minMap = readOptional("minMap");
    d.maxMap = readOptional("maxMap");
    d.minTps = readOptional("minTps");
    d.maxTps = readOptional("maxTps");
    d.minTpsDot = readOptional("minTpsDot");
    d.maxTpsDot = readOptional("maxTpsDot");
    d.minClt = readOptional("minClt");

    if (j.contains("maxEgtWarning") && j["maxEgtWarning"].is_number()) d.maxEgtWarning = j["maxEgtWarning"].get<double>();
    if (j.contains("maxCltWarning") && j["maxCltWarning"].is_number()) d.maxCltWarning = j["maxCltWarning"].get<double>();
    if (j.contains("maxDutyWarning") && j["maxDutyWarning"].is_number()) d.maxDutyWarning = j["maxDutyWarning"].get<double>();

    if (j.contains("customFormula") && j["customFormula"].is_string()) d.customFormula = j["customFormula"].get<std::string>();

    return d;
  }

} // namespace core