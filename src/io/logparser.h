#pragma once

#include <string>
#include <atomic>

#include "core/logsession.h"

namespace io {

  // Interface for a log file parser. Each format's quirks stay entirely
  // inside its own implementation; callers just get a populated LogSession
  // or an error string. Lets us add other ECU/log formats later without
  // touching core or ui.
  class LogParser {
  public:
    virtual ~LogParser() = default;

    // Returns true and populates outSession on success. Returns false and
    // populates errorOut on failure.
    virtual bool parse(const std::string &path, core::LogSession &outSession, std::string &errorOut, std::atomic<float> *progress = nullptr) = 0;
  };

} // namespace io