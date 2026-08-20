#include "engine/ve_analyzer.h"
#include <cmath>
#include <limits>
#include <algorithm>
#include <cassert>

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

    // Collect and merge intervals from excluded regimes
    if (!config_.excludedRegimeIds.empty()) {
      for (const auto &reg : session.regimeSummaries()) {
        if (config_.excludedRegimeIds.count(reg.def.id)) {
          excludedIntervals_.insert(excludedIntervals_.end(), reg.intervals.begin(), reg.intervals.end());
        }
      }

      // Sort intervals by start time for fast lookup
      std::sort(excludedIntervals_.begin(), excludedIntervals_.end(),
        [](const core::TimeInterval &a, const core::TimeInterval &b) {
          return a.startSec < b.startSec;
        });
    }
  }

  bool VeTransientFilter::shouldIgnoreSample(size_t rowIndex, double rpm, double mapVal, double timestampSec) const
  {
    if (rpm < config_.minRpm || rpm > config_.maxRpm) return true;
    if (mapVal > config_.maxMap) return true;
    if (config_.enableOverrunFilter && mapVal < config_.minMap) return true;

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

    if (!excludedIntervals_.empty()) {
      auto it = std::upper_bound(excludedIntervals_.begin(), excludedIntervals_.end(), timestampSec,
        [](double t, const core::TimeInterval &iv) {
          return t < iv.startSec;
        });

      if (it != excludedIntervals_.begin()) {
        const auto &prevIv = *(it - 1);
        if (timestampSec >= prevIv.startSec && timestampSec <= prevIv.endSec) {
          return true; // Timestamp falls inside an excluded regime
        }
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
    const auto *timeSec = session.timeSec();

    if (!rpmCh || !loadCh || !afrCh || !timeSec) {
      return correctedVe;
    }

    VeTransientFilter filter(session, config);

    const size_t rows = baselineVe.rowCount();
    const size_t cols = baselineVe.columnCount();

    std::vector<std::vector<double>> sumAfr(rows, std::vector<double>(cols, 0.0));
    std::vector<std::vector<size_t>> countAfr(rows, std::vector<size_t>(cols, 0));

    const size_t numSamples = session.rowCount();

    for (size_t i = 0; i < numSamples; ++i) {
      if (!session.isRowInCropRange(i)) continue;

      double rpm = rpmCh->values()[i];
      double load = loadCh->values()[i];
      double afr = afrCh->values()[i];
      double t = (*timeSec)[i];

      if (std::isnan(rpm) || std::isnan(load) || std::isnan(afr)) continue;
      if (filter.shouldIgnoreSample(i, rpm, load, t)) continue;

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

    for (size_t r = 0; r < rows; ++r) {
      for (size_t c = 0; c < cols; ++c) {
        if (countAfr[r][c] >= config.minSamplesPerBin) {
          double avgObservedAfr = sumAfr[r][c] / static_cast<double>(countAfr[r][c]);
          double rpmBp = baselineVe.xBreakpoints()[c];
          double loadBp = baselineVe.yBreakpoints()[r];
          double tgtAfr = targetAfr.sample(rpmBp, loadBp);

          if (0.0 < tgtAfr) {
            double oldVe = baselineVe.value(r, c);
            double rawRatio = avgObservedAfr / tgtAfr;
            double weightedRatio = 1.0 + config.adjustmentGain * (rawRatio - 1.0);
            double minAllowedRatio = 1.0 - config.maxPercentChange;
            double maxAllowedRatio = 1.0 + config.maxPercentChange;
            double finalRatio = std::clamp(weightedRatio, minAllowedRatio, maxAllowedRatio);

            correctedVe.setValue(r, c, oldVe * finalRatio);
          }
        }
      }
    }

    return correctedVe;
  }

  core::Table2D VeAnalyzer::computeSmartSmoothedVe(
    const core::LogSession &session,
    const core::Table2D &suggestedVe,
    const core::Table2D &deltaAfr,
    const VeAnalysisConfig &config,
    const std::set<std::pair<int, int>> &selectedCells)
  {
    assert(suggestedVe.rowCount() == deltaAfr.rowCount());
    assert(suggestedVe.columnCount() == deltaAfr.columnCount());

    const size_t rows = suggestedVe.rowCount();
    const size_t cols = suggestedVe.columnCount();

    if (rows == 0 || cols == 0) {
      return suggestedVe;
    }

    // 1. Bin sample counts across the grid to determine statistical density
    std::vector<std::vector<size_t>> sampleCounts(rows, std::vector<size_t>(cols, 0));
    const core::Channel *rpmCh = session.findChannel(session.channelMapping().rpm);
    const core::Channel *loadCh = session.findChannel(session.channelMapping().load);
    if (rpmCh && loadCh) {
      VeTransientFilter filter(session, config);
      const auto &xBp = suggestedVe.xBreakpoints();
      const auto &yBp = suggestedVe.yBreakpoints();
      const size_t numSamples = session.rowCount();

      auto findNearestAxisIndex = [](const std::vector<double> &bp, double val) -> size_t {
        auto it = std::lower_bound(bp.begin(), bp.end(), val);
        if (it == bp.begin()) return 0;
        if (it == bp.end()) return bp.size() - 1;

        double highDiff = *it - val;
        double lowDiff = val - *(it - 1);
        return (lowDiff <= highDiff) ? static_cast<size_t>(std::distance(bp.begin(), it - 1))
          : static_cast<size_t>(std::distance(bp.begin(), it));
        };

      const auto *timeSec = session.timeSec();
      for (size_t i = 0; i < numSamples; ++i) {
        if (!session.isRowInCropRange(i)) continue;

        double rpm = rpmCh->values()[i];
        double load = loadCh->values()[i];
        double t = (timeSec && i < timeSec->size()) ? (*timeSec)[i] : 0.0;
        if (std::isnan(rpm) || std::isnan(load)) continue;
        if (filter.shouldIgnoreSample(i, rpm, load, t)) continue;

        size_t bestC = findNearestAxisIndex(xBp, rpm);
        size_t bestR = findNearestAxisIndex(yBp, load);

        sampleCounts[bestR][bestC]++;
      }
    }

    // 2. Compute confidence weight matrix w_{r,c} \in [0.0, 1.0]
    const double minSamplesThreshold = static_cast<double>(std::max<size_t>(1, config.minSamplesPerBin));
    core::Table2D confidenceWeights(suggestedVe.xBreakpoints(), suggestedVe.yBreakpoints());
    for (size_t r = 0; r < rows; ++r) {
      for (size_t c = 0; c < cols; ++c) {
        size_t nSamples = sampleCounts[r][c];
        if (nSamples == 0) {
          confidenceWeights.setValue(r, c, 0.0);
          continue;
        }
        // Confidence scales purely with sample density up to the threshold
        double densityFactor = std::min(1.0, static_cast<double>(nSamples) / minSamplesThreshold);
        confidenceWeights.setValue(r, c, densityFactor);
      }
    }

    // 3. Perform weighted Laplacian/Bilateral smoothing update
    core::Table2D smoothedVe = suggestedVe;
    bool hasSelection = 1 < selectedCells.size();
    for (size_t r = 0; r < rows; ++r) {
      for (size_t c = 0; c < cols; ++c) {
        // Skip cells outside the active selection if a selection exists
        if (hasSelection && selectedCells.find({ static_cast<int>(r), static_cast<int>(c) }) == selectedCells.end()) {
          continue;
        }

        double wCurr = confidenceWeights.value(r, c);
        double vCurr = suggestedVe.value(r, c);

        double neighborWeightSum = 0.0;
        double neighborValSum = 0.0;

        // 8-way neighborhood lookup reads surrounding values (even unselected ones) to derive boundary context
        for (int dr = -1; dr <= 1; ++dr) {
          for (int dc = -1; dc <= 1; ++dc) {
            if (dr == 0 && dc == 0) continue;

            int nr = static_cast<int>(r) + dr;
            int nc = static_cast<int>(c) + dc;

            if (nr >= 0 && nr < static_cast<int>(rows) && nc >= 0 && nc < static_cast<int>(cols)) {
              double wNeigh = confidenceWeights.value(static_cast<size_t>(nr), static_cast<size_t>(nc));
              double vNeigh = suggestedVe.value(static_cast<size_t>(nr), static_cast<size_t>(nc));

              neighborWeightSum += wNeigh;
              neighborValSum += wNeigh * vNeigh;
            }
          }
        }

        if (neighborWeightSum > 1e-9) {
          double neighborAvg = neighborValSum / neighborWeightSum;
          double smoothedVal = (wCurr * vCurr) + ((1.0 - wCurr) * neighborAvg);
          smoothedVe.setValue(r, c, smoothedVal);
        }
      }
    }

    return smoothedVe;
  }

} // namespace engine
