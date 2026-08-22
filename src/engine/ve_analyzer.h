#pragma once

#include <string>
#include <vector>
#include <set>
#include "core/logsession.h"
#include "core/table2d.h"

namespace engine {

  struct VeAnalysisConfig {
    VeAnalysisConfig();

    size_t minSamplesPerBin = 10;

    double adjustmentGain = 0.50;  // Alpha (0.5 = apply 50% of correction)
    double maxPercentChange = 0.10; // Hard cap (+/- 10% max adjustment per pass)

    // Dynamic Range Bounds
    double minRpm = 0.0;
    double maxRpm = 10000.0;
    double minMap = 15.0;           // Renamed from minLoadThreshold
    double maxMap = 300.0;          // Max MAP cap (kPa)

    // Transient & Environmental Filtering Rules
    bool enableTpsDotFilter = true;
    double maxTpsDot = 30.0; // Ignore rapid throttle movements (e.g., > 30 %/s)

    bool enableCltFilter = true;
    double minCoolantTemp = 160.0; // Ignore cold start / warmup enrichment

    bool enableOverrunFilter = true; // Governs minMap check

    std::set<std::string> excludedRegimeIds;

    bool enableLambdaDelay = true;
    core::Table2D lambdaDelayTable;
  };

  class VeTransientFilter {
  public:
    VeTransientFilter(const core::LogSession &session, const VeAnalysisConfig &config);

    // Returns true if the sample at `rowIndex` should be rejected
    bool shouldIgnoreSample(size_t rowIndex, double rpm, double mapVal, double timestampSec) const;

  private:
    const VeAnalysisConfig &config_;
    const core::Channel *tpsDotCh_ = nullptr;
    const core::Channel *cltCh_ = nullptr;

    // Fast interval lookup for excluded regimes
    std::vector<core::TimeInterval> excludedIntervals_;
  };

  struct ObservedAfrData {
    std::vector<std::vector<double>> sumAfr;
    std::vector<std::vector<size_t>> hitCount;
  };

  class VeAnalyzer {
  public:
    static ObservedAfrData computeObservedAfr(
      const core::LogSession &session,
      const std::vector<double> &xBp,
      const std::vector<double> &yBp,
      const VeAnalysisConfig &config
    );

    static core::Table2D computeCorrectedVe(
      const ObservedAfrData &obsAfrData,
      const core::Table2D &baselineVe,
      const core::Table2D &targetAfr,
      const VeAnalysisConfig &config);

    static core::Table2D computeSmartSmoothedVe(
      const core::LogSession &session, 
      const core::Table2D &suggestedVe,
      const core::Table2D &deltaAfr,
      const VeAnalysisConfig &config,
      const std::set<std::pair<int, int>> &selectedCells);
  };

} // namespace engine