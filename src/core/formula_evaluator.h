// src/core/formula_evaluator.h

#pragma once

#include <cmath>
#include <limits>
#include <string>

namespace core {

  class Channel;
  class LogSession;

  struct CustomChannelDef {
    std::string name;
    std::string unit;
    std::string formula; // e.g. "[MAP] - 101.3" or "([RPM] * [PW]) / 12000"
  };

  class FormulaEvaluator {
  public:
    static Channel evaluate(const CustomChannelDef &def, const LogSession &session, std::string &errorOut);
  };

} // namespace core