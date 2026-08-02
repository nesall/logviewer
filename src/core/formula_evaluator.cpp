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
