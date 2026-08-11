#include "engine/ve_analyzer.h"
#include <cmath>
#include <limits>
#include <algorithm>

namespace engine {

  VeTransientFilter::VeTransientFilter(const core::LogSession &session, const VeAnalysisConfig &config)
    : config_(config)
  {
    if (config_.enableTpsDotFilter) {
      tpsDotCh_ = session.findChannel(session.channelMapping().tpsDot);
    }
    if (config_.enableCltFilter) {
      cltCh_ = session.findChannel(session.channelMapping().clt);
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

    const core::Channel *rpmCh = session.findChannel(session.channelMapping().rpm);
    const core::Channel *loadCh = session.findChannel(session.channelMapping().load);
    const core::Channel *afrCh = session.findChannel(session.channelMapping().afr);

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

          if (0 < tgtAfr) {
            double oldVe = baselineVe.value(r, c);
            double rawRatio = avgObservedAfr / tgtAfr;

            double weightedRatio = 1.0 + config.adjustmentGain * (rawRatio - 1.0);

            double minAllowedRatio = 1.0 - config.maxPercentChange;
            double maxAllowedRatio = 1.0 + config.maxPercentChange;
            double finalRatio = std::clamp(weightedRatio, minAllowedRatio, maxAllowedRatio);

            double newVe = oldVe * finalRatio;
            correctedVe.setValue(r, c, newVe);
          }
        }
      }
    }

    return correctedVe;
  }

} // namespace engine
