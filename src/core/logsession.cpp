#include "core/logsession.h"
#include "3rdparty/nlohmann/json.hpp"

#include <algorithm>
#include <utility>

namespace core {

  const std::string slotRPM = "RPM";
  const std::string slotLoad = "Load / MAP";
  const std::string slotAFR = "AFR / Lambda";
  const std::string slotTPS = "Throttle (TPS)";
  const std::string slotTPSDot = "Accel (TPSdot)";
  const std::string slotCLT = "Coolant (CLT)";
  const std::string slotEGT1 = "Exhaust Temp (EGT1)";
  const std::string slotEGT2 = "Exhaust Temp (EGT2)";
  const std::string slotPW = "Pulse Width (PW)";
  const std::string slotTiming = "Ignition Timing";
  const std::string slotMAT = "Manifold Air Temperature";
  const std::string slotDuty1 = "Duty Cycle1";
  const std::string slotDuty2 = "Duty Cycle2";

  std::vector<std::string> ChannelMapping::allSlots()
  {
    return {
      slotRPM,
      slotLoad,
      slotAFR,
      slotTPS,
      slotTPSDot,
      slotCLT,
      slotEGT1,
      slotEGT2,
      slotPW,
      slotTiming,
      slotMAT,
      slotDuty1,
      slotDuty2 
    };
  }
  
  std::string &ChannelMapping::refSlot(const std::string &slotName)
  {
    if (slotName == slotRPM) return rpm;
    if (slotName == slotLoad) return load;
    if (slotName == slotAFR) return afr;
    if (slotName == slotTPS) return tps;
    if (slotName == slotTPSDot) return tpsDot;
    if (slotName == slotCLT) return clt;
    if (slotName == slotEGT1) return egt1;
    if (slotName == slotEGT2) return egt2;
    if (slotName == slotPW) return pw;
    if (slotName == slotTiming) return timing;
    if (slotName == slotMAT) return mat;
    if (slotName == slotDuty1) return duty1;
    if (slotName == slotDuty2) return duty2;
    throw std::invalid_argument("Invalid slot name");
  }

  void ChannelMapping::autoDetect(const LogSession &session)
  {
    // Helper lambda to find the first channel present in the session from a candidate list
    auto findFirstMatch = [&session](const std::vector<std::string> &candidates) -> std::string {
      for (const auto &name : candidates) {
        if (session.findChannel(name) != nullptr) {
          return name;
        }
      }
      return "";
      };

    // Candidate lists ordered by format prevalence
    rpm = findFirstMatch({
      "RPM",                       // Megasqurt / Haltech / Standard
      "Filtered RPM",              // Haltech alternative
      "Engine Speed"
      });

    load = findFirstMatch({
      "MAP",                       // Megasqurt
      "Fuel - Load (MAP)",         // Haltech NSP
      "Manifold Pressure",         // Haltech raw
      "Ignition - Load (MAP)",     // Haltech alternative
      "Engine Demand",             // Haltech TPS-based load
      "TPS"                        // MegaSquirt Alpha-N
      });

    afr = findFirstMatch({
      "AFR",                       // Megasqurt
      "Wideband O2 1",             // Haltech
      "Wideband O2 Overall",       // Haltech
      "WBC1 Lambda",               // Haltech
      "AFR1",
      "Lambda1"
      });

    egt1 = findFirstMatch({
      "EGT1",                      // Megasqurt
      "Exhaust Gas Temperature 1"  // Haltech
      });

    egt2 = findFirstMatch({
      "EGT2",                      // Megasqurt
      "Exhaust Gas Temperature 2"  // Haltech
      });

    tps = findFirstMatch({
      "TPS",                       // Megasqurt
      "Throttle Position",         // Haltech
      "Throttle Position - Cable"  // Haltech
      });

    tpsDot = findFirstMatch({
      "TPSdot",                    // Megasqurt
      "Throttle Position Derivative", // Haltech
      "Throttle Position Derivative - Cable", // Haltech
      "MAPdot"
      });

    clt = findFirstMatch({
      "CLT",                       // Megasqurt
      "Coolant Temperature"        // Haltech
      });

    mat = findFirstMatch({
      "MAT",                      // Megasqurt
      "Intake Air Temperature",   // Haltech
      "Manifold Air Temperature"  // Haltech
      });

    timing = findFirstMatch({
      "SPK: Spark Advance",        // Megasqurt
      "Ignition Timing"            // Haltech
      });

    duty1 = findFirstMatch({
      "Duty Cycle1", // Megasqurt
      "Duty1"
      });

    duty2 = findFirstMatch({
      "Duty Cycle2", // Megasqurt
      "Duty2"
      });
  }

  nlohmann::json ChannelMapping::toJson() const
  {
    return nlohmann::json{
        {"rpm", rpm},
        {"load", load},
        {"afr", afr},
        {"tps", tps},
        {"tpsDot", tpsDot},
        {"clt", clt},
        {"egt1", egt1},
        {"egt2", egt2},
        {"pw", pw},
        {"timing", timing},
        {"mat", mat},
        {"duty1", duty1},
        {"duty2", duty2}
    };
  }

  ChannelMapping ChannelMapping::fromJson(const nlohmann::json &j)
  {
    ChannelMapping mapping;
    if (j.contains("rpm")) mapping.rpm = j["rpm"].get<std::string>();
    if (j.contains("load")) mapping.load = j["load"].get<std::string>();
    if (j.contains("afr")) mapping.afr = j["afr"].get<std::string>();
    if (j.contains("tps")) mapping.tps = j["tps"].get<std::string>();
    if (j.contains("tpsDot")) mapping.tpsDot = j["tpsDot"].get<std::string>();
    if (j.contains("clt")) mapping.clt = j["clt"].get<std::string>();
    if (j.contains("egt1")) mapping.egt1 = j["egt1"].get<std::string>();
    if (j.contains("egt2")) mapping.egt2 = j["egt2"].get<std::string>();
    if (j.contains("pw")) mapping.pw = j["pw"].get<std::string>();
    if (j.contains("timing")) mapping.timing = j["timing"].get<std::string>();
    if (j.contains("mat")) mapping.mat = j["mat"].get<std::string>();
    if (j.contains("duty1")) mapping.duty1 = j["duty1"].get<std::string>();
    if (j.contains("duty2")) mapping.duty2 = j["duty2"].get<std::string>();
    return mapping;
  }


  void LogSession::addChannel(Channel channel)
  {
    channels_.push_back(std::move(channel));
  }

  const Channel *LogSession::findChannel(const std::string &name) const
  {
    for (const auto &channel : channels_) {
      if (channel.name() == name) {
        return &channel;
      }
    }
    return nullptr;
  }

  const std::vector<double> *LogSession::timeSec() const
  {
    if (!timeChannelIndex_.has_value() || *timeChannelIndex_ >= channels_.size()) {
      return nullptr;
    }
    return &channels_[*timeChannelIndex_].values();
  }

  void LogSession::setCropRange(double startSec, double endSec)
  {
    if (startSec > endSec) std::swap(startSec, endSec);
    cropRange_.startSec = startSec;
    cropRange_.endSec = endSec;
    cropRange_.active = true;
  }

  void LogSession::resetCropRange()
  {
    cropRange_.active = false;
  }

  size_t LogSession::cropStartIndex() const
  {
    const auto *time = timeSec();
    if (!cropRange_.active || !time || time->empty()) return 0;
    auto it = std::lower_bound(time->begin(), time->end(), cropRange_.startSec);
    return static_cast<size_t>(std::distance(time->begin(), it));
  }

  size_t LogSession::cropEndIndex() const
  {
    const auto *time = timeSec();
    if (!cropRange_.active || !time || time->empty()) return rowCount_ > 0 ? rowCount_ - 1 : 0;
    auto it = std::upper_bound(time->begin(), time->end(), cropRange_.endSec);
    size_t idx = static_cast<size_t>(std::distance(time->begin(), it));
    return (idx > 0) ? idx - 1 : 0;
  }

  bool LogSession::isRowInCropRange(size_t rowIndex) const
  {
    if (!cropRange_.active) return true;
    return (rowIndex >= cropStartIndex() && rowIndex <= cropEndIndex());
  }

} // namespace core