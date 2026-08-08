#pragma once

#include <vector>
#include "core/logsession.h"
#include "core/regime.h"

namespace engine {

  class RegimeAnalyzer {
  public:
    static std::vector<core::RegimeSummary> analyzeSession(const core::LogSession &session);
  };

} // namespace engine