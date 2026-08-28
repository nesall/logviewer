#include "engine/ve_analyzer.h"
#include "core/regime.h"
#include <cmath>
#include <limits>
#include <algorithm>
#include <cassert>

namespace engine {

  VeAnalysisConfig::VeAnalysisConfig()
  {
    excludedRegimeIds.insert(std::string(core::RegimeDef::Ids::overrun_fuel_cut));
    // MLVHD Defaults: RPM [700, 2910, 9000], Load [30, 101, 260]
    lambdaDelayTable = core::Table2D({ 700.0, 2910.0, 9000.0 }, { 30.0, 101.0, 260.0 });
    lambdaDelayTable.setValue(0, 0, 350.0); lambdaDelayTable.setValue(0, 1, 300.0); lambdaDelayTable.setValue(0, 2, 200.0);
    lambdaDelayTable.setValue(1, 0, 250.0); lambdaDelayTable.setValue(1, 1, 150.0); lambdaDelayTable.setValue(1, 2, 100.0);
    lambdaDelayTable.setValue(2, 0, 200.0); lambdaDelayTable.setValue(2, 1, 70.0);  lambdaDelayTable.setValue(2, 2, 40.0);
  }


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

  ObservedAfrData VeAnalyzer::computeObservedAfr(
    const core::LogSession &session, 
    const std::vector<double> &xBp, 
    const std::vector<double> &yBp, 
    const VeAnalysisConfig &config)
  {
    ObservedAfrData res;
    const core::Channel *rpmCh = session.findChannel(session.channelMapping().rpm);
    const core::Channel *loadCh = session.findChannel(session.channelMapping().load);
    const core::Channel *afrCh = session.findChannel(session.channelMapping().afr);
    const auto *timeSec = session.timeSec();
    if (!rpmCh || !loadCh || !afrCh || !timeSec) {
      return res;
    }
    auto rows = yBp.size();
    auto cols = xBp.size();
    std::vector<std::vector<double>> sumAfr(rows, std::vector<double>(cols, 0.0));
    std::vector<std::vector<size_t>> countAfr(rows, std::vector<size_t>(cols, 0));
    VeTransientFilter filter(session, config);
    const size_t numSamples = session.rowCount();
    for (size_t i = 0; i < numSamples; ++i) {
      if (!session.isRowInCropRange(i)) continue;
      double rpm = rpmCh->values()[i];
      double load = loadCh->values()[i];
      double afr = afrCh->values()[i];
      double t = (*timeSec)[i];
      if (std::isnan(rpm) || std::isnan(load)) continue;
      if (filter.shouldIgnoreSample(i, rpm, load, t)) continue;
      if (config.enableLambdaDelay) {
        double delayMs = config.lambdaDelayTable.sample(rpm, load);
        double targetTime = t + (delayMs / 1000.0);
        auto it = std::lower_bound(timeSec->begin(), timeSec->end(), targetTime);
        if (it != timeSec->end()) {
          size_t targetIdx = std::distance(timeSec->begin(), it);
          // Snap to the genuinely closest sample, not just the next one
          if (0 < targetIdx) {
            double distNext = std::abs((*timeSec)[targetIdx] - targetTime);
            double distPrev = std::abs((*timeSec)[targetIdx - 1] - targetTime);
            if (distPrev < distNext) {
              targetIdx--;
            }
          }
          if (targetIdx < afrCh->values().size()) {
            afr = afrCh->values()[targetIdx];
          } else {
            continue; // Out of bounds, discard sample
          }
        } else {
          continue; // Target time is beyond the end of the log, discard sample
        }
        // If the future AFR is somehow invalid, skip this binning event
        if (std::isnan(afr)) continue;
      }
      size_t bestR = 0, bestC = 0;
      double minDist = std::numeric_limits<double>::max();
      for (size_t r = 0; r < rows; ++r) {
        for (size_t c = 0; c < cols; ++c) {
          double dRpm = (rpm - xBp[c]) / 1000.0;
          double dLoad = (load - yBp[r]) / 10.0;
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
    res.sumAfr = std::move(sumAfr);
    res.hitCount = std::move(countAfr);
    return res;
  }

  core::Table2D VeAnalyzer::computeCorrectedVe(
    const ObservedAfrData &obsAfrData,
    const core::Table2D &baselineVe,
    const core::Table2D &targetAfr,
    const VeAnalysisConfig &config)
  {
    core::Table2D correctedVe = baselineVe;
    if (obsAfrData.sumAfr.empty()) {
      return correctedVe;
    }
    const auto &sumAfr = obsAfrData.sumAfr;
    const auto &countAfr = obsAfrData.hitCount;
    const size_t rows = baselineVe.rowCount();
    const size_t cols = baselineVe.columnCount();
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

    // Bin sample counts across the grid to determine statistical density
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

    // Compute confidence weight matrix w_{r,c} \in [0.0, 1.0]
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

    // Perform weighted Laplacian/Bilateral smoothing update
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
