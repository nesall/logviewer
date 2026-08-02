// src/engine/ve_analyzer.cpp

#include "engine/ve_analyzer.h"
#include <cmath>
#include <limits>

namespace engine {

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
    const core::Table2D &currentVe,
    const core::Table2D &targetAfr,
    const VeAnalysisConfig &config)
  {
    core::Table2D correctedVe = currentVe;

    const core::Channel *rpmCh = session.findChannel(config.rpmChannel);
    const core::Channel *loadCh = session.findChannel(config.loadChannel);
    const core::Channel *afrCh = session.findChannel(config.afrChannel);

    if (!rpmCh || !loadCh || !afrCh) {
      return correctedVe;
    }

    VeTransientFilter filter(session, config);

    const size_t rows = currentVe.rowCount();
    const size_t cols = currentVe.columnCount();

    std::vector<std::vector<double>> sumAfr(rows, std::vector<double>(cols, 0.0));
    std::vector<std::vector<size_t>> countAfr(rows, std::vector<size_t>(cols, 0));

    const size_t numSamples = session.rowCount();

    // 1. Binning phase
    for (size_t i = 0; i < numSamples; ++i) {
      double rpm = rpmCh->values()[i];
      double load = loadCh->values()[i];
      double afr = afrCh->values()[i];

      if (std::isnan(rpm) || std::isnan(load) || std::isnan(afr)) continue;

      if (filter.shouldIgnoreSample(i, load)) continue;

      size_t bestR = 0, bestC = 0;
      double minDist = std::numeric_limits<double>::max();

      for (size_t r = 0; r < rows; ++r) {
        for (size_t c = 0; c < cols; ++c) {
          double dRpm = (rpm - currentVe.xBreakpoints()[c]) / 1000.0;
          double dLoad = (load - currentVe.yBreakpoints()[r]) / 10.0;
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

          double rpmBp = currentVe.xBreakpoints()[c];
          double loadBp = currentVe.yBreakpoints()[r];
          double tgtAfr = targetAfr.sample(rpmBp, loadBp);

          if (tgtAfr > 0.0) {
            double oldVe = currentVe.value(r, c);
            double newVe = oldVe * (avgObservedAfr / tgtAfr);
            correctedVe.setValue(r, c, newVe);
          }
        }
      }
    }

    return correctedVe;
  }

} // namespace engine