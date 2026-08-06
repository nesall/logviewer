#include "engine/ve_analyzer.h"
#include <cmath>
#include <limits>


namespace engine {

  void populateDefaultsForSession(VeAnalysisConfig &config, const core::LogSession &session) {
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
    config.rpmChannel = findFirstMatch({
      "RPM",                       // MegaSquirt / Haltech / Standard
      "Filtered RPM",              // Haltech alternative
      "Engine Speed"
      });

    config.loadChannel = findFirstMatch({
      "MAP",                       // MegaSquirt
      "Fuel - Load (MAP)",         // Haltech NSP[cite: 2]
      "Manifold Pressure",         // Haltech raw[cite: 2]
      "Ignition - Load (MAP)",     // Haltech alternative[cite: 2]
      "Engine Demand",             // Haltech TPS-based load[cite: 2]
      "TPS"                        // MegaSquirt Alpha-N
      });

    config.afrChannel = findFirstMatch({
      "AFR",                       // MegaSquirt
      "Wideband O2 1",             // Haltech[cite: 2]
      "Wideband O2 Overall",       // Haltech[cite: 2]
      "WBC1 Lambda",               // Haltech[cite: 2]
      "AFR1",
      "Lambda1"
      });

    config.tpsDotChannel = findFirstMatch({
      "TPSdot",                    // MegaSquirt[cite: 1]
      "Throttle Position Derivative", // Haltech[cite: 2]
      "Throttle Position Derivative - Cable", // Haltech[cite: 2]
      "MAPdot"
      });

    config.cltChannel = findFirstMatch({
      "CLT",                       // MegaSquirt[cite: 1]
      "Coolant Temperature"        // Haltech[cite: 2]
      });
  }


  VeTransientFilter::VeTransientFilter(const core::LogSession &session, const VeAnalysisConfig &config)
    : config_(config)
  {
    if (config_.enableTpsDotFilter) {
      tpsDotCh_ = session.findChannel(config_.tpsDotChannel);
    }
    if (config_.enableCltFilter) {
      cltCh_ = session.findChannel(config_.cltChannel);
    }
  }

  bool VeTransientFilter::shouldIgnoreSample(size_t rowIndex, double load) const
  {
    if (config_.enableOverrunFilter && load < config_.minLoadThreshold) {
      return true;
    }
    if (tpsDotCh_ && rowIndex < tpsDotCh_->values().size()) {
      double tpsDot = tpsDotCh_->values()[rowIndex];
      if (!std::isnan(tpsDot) && std::abs(tpsDot) > config_.maxTpsDot) {
        return true;
      }
    }
    if (cltCh_ && rowIndex < cltCh_->values().size()) {
      double clt = cltCh_->values()[rowIndex];
      if (!std::isnan(clt) && clt < config_.minCoolantTemp) {
        return true;
      }
    }
    return false;
  }

  core::Table2D VeAnalyzer::computeCorrectedVe(
    const core::LogSession &session,
    const core::Table2D &baselineVe,
    const core::Table2D &targetAfr,
    const VeAnalysisConfig &config)
  {
    core::Table2D correctedVe = baselineVe;

    const core::Channel *rpmCh = session.findChannel(config.rpmChannel);
    const core::Channel *loadCh = session.findChannel(config.loadChannel);
    const core::Channel *afrCh = session.findChannel(config.afrChannel);

    if (!rpmCh || !loadCh || !afrCh) {
      return correctedVe;
    }

    VeTransientFilter filter(session, config);

    const size_t rows = baselineVe.rowCount();
    const size_t cols = baselineVe.columnCount();

    std::vector<std::vector<double>> sumAfr(rows, std::vector<double>(cols, 0.0));
    std::vector<std::vector<size_t>> countAfr(rows, std::vector<size_t>(cols, 0));

    const size_t numSamples = session.rowCount();

    // 1. Binning phase
    for (size_t i = 0; i < numSamples; ++i) {
      
      if (!session.isRowInCropRange(i)) continue;

      double rpm = rpmCh->values()[i];
      double load = loadCh->values()[i];
      double afr = afrCh->values()[i];

      if (std::isnan(rpm) || std::isnan(load) || std::isnan(afr)) continue;
      if (filter.shouldIgnoreSample(i, load)) continue;

      size_t bestR = 0, bestC = 0;
      double minDist = std::numeric_limits<double>::max();

      for (size_t r = 0; r < rows; ++r) {
        for (size_t c = 0; c < cols; ++c) {
          double dRpm = (rpm - baselineVe.xBreakpoints()[c]) / 1000.0;
          double dLoad = (load - baselineVe.yBreakpoints()[r]) / 10.0;
          double dist = (dRpm * dRpm) + (dLoad * dLoad);

          if (dist < minDist) {
            minDist = dist;
            bestR = r;
            bestC = c;
          }
        }
      }

      sumAfr[bestR][bestC] += afr;
      countAfr[bestR][bestC]++;
    }

    // 2. Correction phase
    for (size_t r = 0; r < rows; ++r) {
      for (size_t c = 0; c < cols; ++c) {
        if (countAfr[r][c] >= config.minSamplesPerBin) {
          double avgObservedAfr = sumAfr[r][c] / static_cast<double>(countAfr[r][c]);

          double rpmBp = baselineVe.xBreakpoints()[c];
          double loadBp = baselineVe.yBreakpoints()[r];
          double tgtAfr = targetAfr.sample(rpmBp, loadBp);

          if (tgtAfr > 0.0) {
            double oldVe = baselineVe.value(r, c);
            double newVe = oldVe * (avgObservedAfr / tgtAfr);
            correctedVe.setValue(r, c, newVe);
          }
        }
      }
    }

    return correctedVe;
  }

} // namespace engine