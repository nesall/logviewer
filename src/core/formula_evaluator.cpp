#include "core/formula_evaluator.h"
#include <vector>
#include "3rdparty/exprtk.hpp"
#include "core/channel.h"
#include "core/logsession.h"



core::Channel core::FormulaEvaluator::evaluate(const CustomChannelDef &def, const LogSession &session, std::string &errorOut)
{
  Channel resultChannel(def.name, def.unit);
  resultChannel.setIsCustom(true);
  resultChannel.setFormula(def.formula);

  std::string sanitizedFormula = def.formula;

  // 1. Parse bracketed channel names like [MAP] and map them to valid C++ identifiers
  struct BoundVar {
    std::string rawToken;      // e.g. "[MAP]"
    std::string varName;       // e.g. "var_MAP"
    const std::vector<double> *data = nullptr;
    double currentVal = 0.0;
  };

  std::vector<BoundVar> boundVars;
  size_t pos = 0;
  int varCounter = 0;

  while ((pos = sanitizedFormula.find('[', pos)) != std::string::npos) {
    size_t endPos = sanitizedFormula.find(']', pos);
    if (endPos == std::string::npos) {
      errorOut = "Mismatched brackets in formula";
      return resultChannel;
    }

    std::string chName = sanitizedFormula.substr(pos + 1, endPos - pos - 1);
    const Channel *ch = session.findChannel(chName);
    if (!ch) {
      errorOut = "Channel not found: " + chName;
      return resultChannel;
    }

    std::string token = "[" + chName + "]";
    std::string varName = "v_" + std::to_string(varCounter++);

    boundVars.push_back({ token, varName, &ch->values(), 0.0 });

    // Replace "[MAP]" with "v_0" in expression string
    while ((pos = sanitizedFormula.find(token)) != std::string::npos) {
      sanitizedFormula.replace(pos, token.length(), varName);
    }
  }

  // 2. Setup ExprTk Symbol Table
  exprtk::symbol_table<double> symbolTable;
  for (auto &bv : boundVars) {
    symbolTable.add_variable(bv.varName, bv.currentVal);
  }
  symbolTable.add_constants();

  exprtk::expression<double> expression;
  expression.register_symbol_table(symbolTable);

  exprtk::parser<double> parser;
  if (!parser.compile(sanitizedFormula, expression)) {
    errorOut = "ExprTk Syntax Error: " + parser.error();
    return resultChannel;
  }

  // 3. Evaluate row by row
  const size_t rowCount = session.rowCount();
  std::vector<double> &outVals = resultChannel.values();
  outVals.resize(rowCount, std::numeric_limits<double>::quiet_NaN());

  for (size_t row = 0; row < rowCount; ++row) {
    bool hasNaN = false;

    for (auto &bv : boundVars) {
      double val = (*bv.data)[row];
      if (std::isnan(val)) {
        hasNaN = true;
        break;
      }
      bv.currentVal = val;
    }

    if (!hasNaN) {
      outVals[row] = expression.value();
    }
  }

  resultChannel.setIsNumeric(true);
  return resultChannel;
}

const std::vector<core::MathPreset> &core::FormulaEvaluator::allPresets()
{
  static const std::vector<core::MathPreset> presets = {
    // --- Pressure & Boost ---
    { "Pressure", "Boost Pressure (PSI)", "psi", "([{LOAD}] - 101.325) * 0.145038", "Relative boost pressure from absolute MAP (assuming 101.3 kPa baro)." },
    { "Pressure", "Boost Pressure (Bar)", "bar", "([{LOAD}] - 101.325) / 100.0", "Relative boost in bar." },
    { "Pressure", "MAP (PSI Absolute)", "psia", "[{LOAD}] * 0.145038", "Converts kPa manifold pressure to absolute PSI." },

    // --- Air/Fuel & Lambda ---
    { "AFR / Lambda", "Lambda from Gasoline AFR", "λ", "[{AFR}] / 14.7", "Normalizes Gasoline 14.7 stoichiometric AFR to Lambda." },
    { "AFR / Lambda", "Lambda from E85 AFR", "λ", "[{AFR}] / 9.8", "Normalizes E85 9.8 stoichiometric AFR to Lambda." },
    { "AFR / Lambda", "AFR from Lambda", "AFR", "[{AFR}] * 14.7", "Converts Lambda (1.0) into Gasoline AFR." },

    // --- Temperatures ---
    { "Temperature", "CLT (°F to °C)", "°C", "([{CLT}] - 32) * (5 / 9)", "Converts Fahrenheit coolant to Celsius." },
    { "Temperature", "CLT (°C to °F)", "°F", "([{CLT}] * 9 / 5) + 32", "Converts Celsius coolant to Fahrenheit." },
    { "Temperature", "MAT (°F to °C)", "°C", "([{MAT}] - 32) * (5 / 9)", "Converts Fahrenheit intake air to Celsius." },
    { "Temperature", "MAT (°C to °F)", "°F", "([{MAT}] * 9 / 5) + 32", "Converts Celsius intake air to Fahrenheit." },

    // --- Fuel Dynamics & Math ---
    { "Fuel Dynamics", "Injector Duty Cycle (%)", "%", "([{RPM}] * [{PW}]) / 1200.0", "Calculates 4-stroke injector duty cycle from RPM and Pulse Width (ms)." },
    { "Fuel Dynamics", "Estimated Mass Airflow (g/s)", "g/s", "([{RPM}] * [{LOAD}] * 1.3) / 12000.0", "Approximate speed-density airflow estimation based on displacement constant." }
  };
  return presets;
}

std::string core::FormulaEvaluator::resolvePresetFormula(const MathPreset &preset, const LogSession &session, std::string &missingChannel)
{
  std::string resolved = preset.formulaTemplate;
  const auto &cm = session.channelMapping();

  auto replaceToken = [&](const std::string &token, const std::string &channelName) -> bool {
    size_t pos = resolved.find(token);
    if (pos != std::string::npos) {
      if (channelName.empty() || session.findChannel(channelName) == nullptr) {
        missingChannel = token;
        return false;
      }
      resolved.replace(pos, token.length(), channelName);
    }
    return true;
    };

  if (!replaceToken("{LOAD}", cm.load)) return "";
  if (!replaceToken("{RPM}", cm.rpm)) return "";
  if (!replaceToken("{AFR}", cm.afr)) return "";
  if (!replaceToken("{CLT}", cm.clt)) return "";
  if (!replaceToken("{MAT}", cm.mat)) return "";
  if (!replaceToken("{PW}", cm.pw)) return "";

  return resolved;
}
