#pragma once

#include <string>
#include <vector>
#include "core/logsession.h"
#include "core/table2d.h"

namespace engine {

  struct VeAnalysisConfig {
    size_t minSamplesPerBin = 10;

    double adjustmentGain = 0.50;  // Alpha (0.5 = apply 50% of correction)
    double maxPercentChange = 0.10; // Hard cap (+/- 10% max adjustment per pass)

    // Transient & Environmental Filtering Rules
    bool enableTpsDotFilter = true;
    double maxTpsDot = 30.0; // Ignore rapid throttle movements (e.g., > 30 %/s)

    bool enableCltFilter = true;
    double minCoolantTemp = 160.0; // Ignore cold start / warmup enrichment

    bool enableOverrunFilter = true;
    double minLoadThreshold = 15.0; // Ignore deep overrun / vacuum decel
  };

  //void populateDefaultsForSession(VeAnalysisConfig &config, const core::LogSession &session);

  // Reusable transient filter to share identical rules across different binning grids
  class VeTransientFilter {
  public:
    VeTransientFilter(const core::LogSession &session, const VeAnalysisConfig &config);

    // Returns true if the sample at `rowIndex` should be ignored based on active filter rules
    bool shouldIgnoreSample(size_t rowIndex, double load) const;

  private:
    const VeAnalysisConfig &config_;
    const core::Channel *tpsDotCh_ = nullptr;
    const core::Channel *cltCh_ = nullptr;
  };

  class VeAnalyzer {
  public:
    // Returns a new Table2D with corrected VE values where sample counts meet the threshold.
    // Unchanged cells retain their original VE values.
    static core::Table2D computeCorrectedVe(
      const core::LogSession &session,
      const core::Table2D &baselineVe,
      const core::Table2D &targetAfr,
      const VeAnalysisConfig &config);
  };

} // namespace engine