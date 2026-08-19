#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <cmath>
#include <atomic>
#include "core/logsession.h"

namespace io {

  struct CsvExportOptions {
    bool cropOnly = true;
    bool selectedChannelsOnly = true;
    std::vector<std::string> targetChannelNames;
    bool includeUnitsRow = true;
    char delimiter = ',';
  };

  class CsvExporter {
  public:
    static bool write(
      const std::string &path,
      const core::LogSession &session,
      const CsvExportOptions &options,
      std::string &errorOut,
      std::atomic<float> *progress = nullptr,
      std::atomic<bool> *cancelRequested = nullptr);
  };

} // namespace io