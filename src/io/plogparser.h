#pragma once

#include "io/logparser.h"

namespace io {

  class PlogParser : public LogParser {
  public:
    bool parse(const std::string &path, core::LogSession &outSession,
      std::string &errorOut, std::atomic<float> *progress = nullptr) override;

    static bool write(const std::string &path, const core::LogSession &session,
      std::string &errorOut, std::atomic<float> *progress = nullptr);
  };

} // namespace io