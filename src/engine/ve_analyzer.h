#pragma once

#include <string>
#include <vector>
#include "core/logsession.h"
#include "core/table2d.h"

namespace engine {

  struct VeAnalysisConfig {
    std::string rpmChannel = "RPM";
    std::string loadChannel = "MAP";
    std::string afrChannel = "AFR";
    size_t minSamplesPerBin = 10;

    // Future expansion for transient filtering
    // double minCoolantTemp = 160.0;
    // double maxTpsDot = 50.0; 
  };

  class VeAnalyzer {
  public:
    // Returns a new Table2D with corrected VE values where sample counts meet the threshold.
    // Unchanged cells retain their original VE values.
    static core::Table2D computeCorrectedVe(
      const core::LogSession &session,
      const core::Table2D &currentVe,
      const core::Table2D &targetAfr,
      const VeAnalysisConfig &config);
  };

} // namespace engine