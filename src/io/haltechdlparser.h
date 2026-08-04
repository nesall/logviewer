#pragma once

#include "io/logparser.h"

namespace io {

  // Parses Haltech NSP / Elite .dl text logs.
  // Extract channel metadata from 'Channel : <Name>' tags and parses
  // comma-delimited time and data rows in the 'Log :' block.
  class HaltechDlParser : public LogParser {
  public:
    bool parse(const std::string &path, core::LogSession &outSession,
      std::string &errorOut, std::atomic<float> *progress = nullptr) override;
  };

} // namespace io