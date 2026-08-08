#include "engine/regime_analyzer.h"
#include <cmath>
#include <algorithm>
#include <limits>

namespace engine {

  namespace {
    // Helper to extract channel values safely (returns NaN if index out of range or null)
    double getVal(const core::Channel *ch, size_t idx) {
      if (!ch || idx >= ch->values().size()) return std::numeric_limits<double>::quiet_NaN();
      return ch->values()[idx];
    }
  } // namespace

  std::vector<core::RegimeSummary> RegimeAnalyzer::analyzeSession(const core::LogSession &session) {
    std::vector<core::RegimeSummary> summaries;
    if (session.rowCount() == 0) return summaries;

    const auto *timeSec = session.timeSec();
    if (!timeSec || timeSec->empty()) return summaries;

    double totalLogTime = timeSec->back() - timeSec->front();
    if (totalLogTime <= 0.0) totalLogTime = 1.0;

    // Find Channels
    const auto &mapping = session.channelMapping();
    const auto *rpmCh = session.findChannel(mapping.rpm);
    const auto *mapCh = session.findChannel(mapping.load);
    const auto *tpsCh = session.findChannel(mapping.tps);
    const auto *tpsDotCh = session.findChannel(mapping.tpsDot);
    const auto *afrCh = session.findChannel(mapping.afr);
    const auto *egtCh = session.findChannel(mapping.egt);
    const auto *pwCh = session.findChannel(mapping.pw);
    const auto *timingCh = session.findChannel(mapping.timing);

    // Initialize Preset Regimes
    core::RegimeSummary cruise{ core::RegimeType::FreewayCruise, "freeway_cruise", "Freeway Cruise", ImVec4(0.2f, 0.7f, 1.0f, 0.25f) };
    core::RegimeSummary overrun{ core::RegimeType::OverrunFuelCut, "overrun_fuel_cut", "Overrun Fuel Cut (DFCO)", ImVec4(0.8f, 0.3f, 0.3f, 0.25f) };
    core::RegimeSummary lspi{ core::RegimeType::HighGearLowRpmBoost, "lspi_risk", "Low RPM / High Boost (LSPI)", ImVec4(1.0f, 0.5f, 0.0f, 0.30f) };
    core::RegimeSummary highVac{ core::RegimeType::HighRpmVacuum, "high_rpm_vacuum", "High RPM Vacuum / Decel", ImVec4(0.6f, 0.4f, 0.8f, 0.25f) };
    core::RegimeSummary idle{ core::RegimeType::IdleStability, "idle_stability", "Idle Stability", ImVec4(0.3f, 0.8f, 0.4f, 0.25f) };
    core::RegimeSummary tipIn{ core::RegimeType::TransientTipIn, "transient_tipin", "Transient Tip-In", ImVec4(0.9f, 0.9f, 0.2f, 0.25f) };

    std::vector<core::RegimeSummary *> allRegimes = { &cruise, &overrun, &lspi, &highVac, &idle, &tipIn };

    const size_t rowCount = session.rowCount();
    std::vector<bool> matches(allRegimes.size(), false);

    std::vector<double> matchStartTimes(allRegimes.size(), 0.0);
    std::vector<bool> inInterval(allRegimes.size(), false);

    // Temp accumulation maps
    struct Acc { double sumRpm = 0, sumMap = 0, sumAfr = 0, sumTiming = 0, maxEgt = 0; size_t count = 0; };
    std::vector<Acc> accs(allRegimes.size());

    for (size_t i = 0; i < rowCount; ++i) {
      if (!session.isRowInCropRange(i)) continue;

      double t = (*timeSec)[i];
      double rpm = getVal(rpmCh, i);
      double map = getVal(mapCh, i);
      double tps = getVal(tpsCh, i);
      double tpsDot = getVal(tpsDotCh, i);
      double afr = getVal(afrCh, i);
      double egt = getVal(egtCh, i);
      double pw = getVal(pwCh, i);
      double timing = getVal(timingCh, i);

      // Evaluate Conditions
      matches[0] = (map < 60.0 && tps < 20.0 && std::abs(tpsDot) < 10.0 && rpm > 2000.0 && rpm < 4000.0); // Cruise
      matches[1] = (map < 22.0 && rpm > 1800.0 && (pw == 0.0 || tps < 2.0));                              // Overrun
      matches[2] = (rpm > 1500.0 && rpm < 3000.0 && map > 110.0 && tps > 50.0);                          // LSPI / Low RPM Boost
      matches[3] = (rpm > 4500.0 && map < 50.0 && tps < 10.0);                                            // High RPM Vac
      matches[4] = (rpm > 500.0 && rpm < 1100.0 && tps < 2.0 && map < 45.0);                              // Idle
      matches[5] = (std::abs(tpsDot) > 35.0);                                                             // Tip-In

      for (size_t r = 0; r < allRegimes.size(); ++r) {
        if (matches[r]) {
          if (!inInterval[r]) {
            inInterval[r] = true;
            matchStartTimes[r] = t;
          }
          accs[r].sumRpm += std::isnan(rpm) ? 0 : rpm;
          accs[r].sumMap += std::isnan(map) ? 0 : map;
          accs[r].sumAfr += std::isnan(afr) ? 0 : afr;
          accs[r].sumTiming += std::isnan(timing) ? 0 : timing;
          if (!std::isnan(egt) && egt > accs[r].maxEgt) accs[r].maxEgt = egt;
          accs[r].count++;
        } else {
          if (inInterval[r]) {
            inInterval[r] = false;
            double duration = t - matchStartTimes[r];
            if (duration >= 0.5) { // Filter out micro noise < 0.5 sec
              allRegimes[r]->intervals.push_back({ matchStartTimes[r], t });
            }
          }
        }
      }
    }

    // Finalize Summaries
    for (size_t r = 0; r < allRegimes.size(); ++r) {
      auto *reg = allRegimes[r];
      size_t count = accs[r].count;
      reg->sampleCount = count;

      for (const auto &inter : reg->intervals) {
        reg->totalDwellTimeSec += (inter.endSec - inter.startSec);
      }
      reg->percentageOfLog = (reg->totalDwellTimeSec / totalLogTime) * 100.0;

      if (count > 0) {
        reg->avgRpm = accs[r].sumRpm / count;
        reg->avgMap = accs[r].sumMap / count;
        reg->avgAfr = accs[r].sumAfr / count;
        reg->avgTiming = accs[r].sumTiming / count;
        reg->peakEgt = accs[r].maxEgt;
      }

      // Context Warnings
      if (reg->type == core::RegimeType::FreewayCruise && reg->peakEgt > 1600.0) {
        reg->warningMessage = "High Cruise EGT: " + std::to_string(static_cast<int>(reg->peakEgt)) + " °F";
      } else if (reg->type == core::RegimeType::HighGearLowRpmBoost && count > 0) {
        reg->warningMessage = "High Engine Load at Low RPM";
      }

      if (reg->totalDwellTimeSec > 0.0) {
        summaries.push_back(*reg);
      }
    }

    return summaries;
  }

} // namespace engine