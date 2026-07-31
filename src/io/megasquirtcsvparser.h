#pragma once

#include "io/logparser.h"

namespace io {

  // Parses TunerStudio/MegaLogViewer .msl exports: tab-delimited, two quoted
  // metadata lines, a lone "#" line, a header row, a units row, then data
  // rows. This is specifically the CSV-like export MLVHD produces from a
  // captured .mlg session -- NOT the .mlg binary DataLogger format, and NOT
  // the raw MS3 ECU log format (both out of scope for now).
  class MegasquirtCsvParser : public LogParser {
  public:
    bool parse(const std::string &path, core::LogSession &outSession, std::string &errorOut) override;
  };

} // namespace io