#pragma once

#include <vector>
#include <unordered_map>
#include "core/logsession.h"
#include "core/regime.h"

namespace engine {

  std::unordered_map<core::RegimeType, core::RegimeDef> getDefaultRegimeDefs();

  class RegimeAnalyzer {
  public:
    static std::vector<core::RegimeSummary> analyzeSession(const core::LogSession &session, const std::vector<core::RegimeDef> &definitions);
  };

} // namespace engine