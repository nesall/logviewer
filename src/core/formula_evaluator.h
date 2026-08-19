#pragma once

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace core {

  class Channel;
  class LogSession;

  struct CustomChannelDef {
    std::string name;
    std::string unit;
    std::string formula; // e.g. "[MAP] - 101.3" or "([RPM] * [PW]) / 12000"
  };

  struct MathPreset {
    std::string category;
    std::string name;
    std::string defaultUnit;
    std::string formulaTemplate; // Uses semantic tokens e.g. {LOAD}, {CLT}, {RPM}, {PW}
    std::string description;
  };

  class FormulaEvaluator {
  public:
    static Channel evaluate(const CustomChannelDef &def, const LogSession &session, std::string &errorOut);

    static const std::vector<MathPreset> &allPresets();
    static std::string resolvePresetFormula(const MathPreset &preset, const LogSession &session, std::string &missingChannel);
  };

} // namespace core