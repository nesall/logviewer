#include "engine/regime_analyzer.h"
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <limits>

#include "3rdparty/exprtk.hpp"
#include "core/channel.h"

namespace engine {

  namespace {

    constexpr double kNaN = std::numeric_limits<double>::quiet_NaN();

    double getVal(const core::Channel *ch, size_t idx) {
      if (!ch || idx >= ch->values().size()) return kNaN;
      return ch->values()[idx];
    }

    struct CompiledRegimeExpr {
      exprtk::symbol_table<double> symbolTable;
      exprtk::expression<double> expression;
      exprtk::parser<double> parser;

      struct BoundVar {
        std::string varName;
        const std::vector<double> *data = nullptr;
        double currentVal = 0.0;
      };

      std::vector<BoundVar> boundVars;
      bool valid = false;

      CompiledRegimeExpr() = default;
      CompiledRegimeExpr(CompiledRegimeExpr &&) noexcept = default;
      CompiledRegimeExpr &operator=(CompiledRegimeExpr &&) noexcept = default;

      // Explicitly delete copy operations to make move semantics explicit
      CompiledRegimeExpr(const CompiledRegimeExpr &) = delete;
      CompiledRegimeExpr &operator=(const CompiledRegimeExpr &) = delete;

      void compile(const std::string &formulaStr, const core::LogSession &session) {
        valid = false;
        boundVars.clear();
        if (formulaStr.empty()) return;

        std::string sanitizedFormula = formulaStr;
        size_t pos = 0;
        int varCounter = 0;

        while ((pos = sanitizedFormula.find('[', pos)) != std::string::npos) {
          size_t endPos = sanitizedFormula.find(']', pos);
          if (endPos == std::string::npos) return; // Mismatched brackets

          std::string chName = sanitizedFormula.substr(pos + 1, endPos - pos - 1);
          const core::Channel *ch = session.findChannel(chName);
          if (!ch) return; // Channel not found in active session

          std::string token = "[" + chName + "]";
          std::string varName = "v_" + std::to_string(varCounter++);

          boundVars.push_back({ varName, &ch->values(), 0.0 });

          // Replace all occurrences of this exact token (e.g. "[AFR]") in the formula
          size_t tokenPos = 0;
          while ((tokenPos = sanitizedFormula.find(token, tokenPos)) != std::string::npos) {
            sanitizedFormula.replace(tokenPos, token.length(), varName);
            tokenPos += varName.length();
          }

          // Reset pos to start of string so we catch the next bracketed variable correctly
          pos = 0;
        }

        for (auto &bv : boundVars) {
          symbolTable.add_variable(bv.varName, bv.currentVal);
        }
        symbolTable.add_constants();

        expression.register_symbol_table(symbolTable);

        if (parser.compile(sanitizedFormula, expression)) {
          valid = true;
        }
      }
    };

    bool evaluateRegime(const core::RegimeDef &def, CompiledRegimeExpr &compiledExpr, const core::LogSession &session, size_t rowIndex)
    {
      // 1. Custom ExprTk formula evaluation (if provided)
      if (!def.customFormula.empty()) {
        if (!compiledExpr.valid) return false;
        for (auto &bv : compiledExpr.boundVars) {
          double val = (*bv.data)[rowIndex];
          if (std::isnan(val)) return false;
          bv.currentVal = val;
        }
        double result = compiledExpr.expression.value();
        return (!std::isnan(result) && result != 0.0);
      }

      // 2. Abstracted Channel Bounds evaluation
      for (const auto &rule : def.boundsRules) {
        const auto *ch = session.findChannel(rule.channelName);
        if (!ch || rowIndex >= ch->values().size()) return false;

        double val = ch->values()[rowIndex];
        if (std::isnan(val)) return false;

        if (!std::isnan(rule.minVal) && val < rule.minVal) return false;
        if (!std::isnan(rule.maxVal) && val > rule.maxVal) return false;
      }

      return true;
    }

  } // namespace

  std::vector<core::RegimeDef> getDefaultRegimeDefs(const core::LogSession &session)
  {
    const auto cm = session.channelMapping();
    std::vector<core::RegimeDef> defaults;
    // 1. Freeway Cruise
    core::RegimeDef cruise;
    cruise.id = "freeway_cruise";
    cruise.displayName = "Freeway Cruise";
    cruise.color = ImVec4(0.2f, 0.7f, 1.0f, 0.25f);
    cruise.isBuiltIn = true;
    cruise.boundsRules = {
      { cm.rpm, 2000.0, 4000.0 },
      { cm.load, std::numeric_limits<double>::quiet_NaN(), 60.0 },
      { cm.tps, std::numeric_limits<double>::quiet_NaN(), 20.0 }
    };
    cruise.configuredMetrics = {
      { cm.rpm, core::MetricAgg::Average, "Avg RPM" },
      { cm.load, core::MetricAgg::Average, "Avg MAP" },
      { cm.afr, core::MetricAgg::Average, "Avg AFR" },
      { cm.timing, core::MetricAgg::Average, "Avg Timing" }
    };
    defaults.push_back(cruise);

    // 2. Overrun Fuel Cut (DFCO)
    core::RegimeDef dfco;
    dfco.id = "overrun_fuel_cut";
    dfco.displayName = "Overrun Fuel Cut (DFCO)";
    dfco.color = ImVec4(0.8f, 0.3f, 0.3f, 0.25f);
    dfco.isBuiltIn = true;
    dfco.boundsRules = {
      { cm.rpm, 1800.0, std::numeric_limits<double>::quiet_NaN() },
      { cm.load, std::numeric_limits<double>::quiet_NaN(), 22.0 },
      { cm.tps, std::numeric_limits<double>::quiet_NaN(), 2.0 }
    };
    dfco.configuredMetrics = {
      { cm.rpm, core::MetricAgg::Average, "Avg RPM" },
      { cm.load, core::MetricAgg::Average, "Avg MAP" }
    };
    defaults.push_back(dfco);

    return defaults;
  }

  std::vector<core::RegimeSummary> RegimeAnalyzer::analyzeSession(const core::LogSession &session, const std::vector<core::RegimeDef> &definitions)
  {
    std::vector<core::RegimeSummary> summaries;
    if (session.rowCount() == 0) return summaries;

    std::vector<core::RegimeDef> activeDefs = definitions;
    if (activeDefs.empty()) {
      activeDefs = getDefaultRegimeDefs(session);
    }

    const auto *timeSec = session.timeSec();
    if (!timeSec || timeSec->empty()) return summaries;

    double totalLogTime = timeSec->back() - timeSec->front();
    if (totalLogTime <= 0.0) totalLogTime = 1.0;

    const size_t numRegimes = activeDefs.size();
    const size_t rowCount = session.rowCount();

    std::vector<core::RegimeSummary> allRegimes(numRegimes);
    std::vector<CompiledRegimeExpr> compiledExprs(numRegimes);

    // Initialize accumulators
    for (size_t r = 0; r < numRegimes; ++r) {
      allRegimes[r].def = activeDefs[r];

      if (!activeDefs[r].customFormula.empty()) {
        compiledExprs[r].compile(activeDefs[r].customFormula, session);
      }

      // Pre-size and set unit meta for configured metrics
      for (const auto &rule : activeDefs[r].configuredMetrics) {
        core::CalculatedMetricValue res;
        res.rule = rule;
        const auto *ch = session.findChannel(rule.channelName);
        res.unit = ch ? ch->unit() : "";

        if (rule.aggregation == core::MetricAgg::Min) {
          res.value = std::numeric_limits<double>::infinity();
        } else if (rule.aggregation == core::MetricAgg::Peak) {
          res.value = -std::numeric_limits<double>::infinity();
        } else {
          res.value = 0.0;
        }
        allRegimes[r].metricResults.push_back(res);
      }
    }

    std::vector<bool> matches(numRegimes, false);
    std::vector<double> matchStartTimes(numRegimes, 0.0);
    std::vector<bool> inInterval(numRegimes, false);

    // Process log row-by-row
    for (size_t i = 0; i < rowCount; ++i) {
      if (!session.isRowInCropRange(i)) continue;

      double t = (*timeSec)[i];

      for (size_t r = 0; r < numRegimes; ++r) {
        matches[r] = evaluateRegime(allRegimes[r].def, compiledExprs[r], session, i);

        if (matches[r]) {
          if (!inInterval[r]) {
            inInterval[r] = true;
            matchStartTimes[r] = t;
          }

          allRegimes[r].sampleCount++;

          // Accumulate configured metric channels
          for (size_t m = 0; m < allRegimes[r].metricResults.size(); ++m) {
            auto &res = allRegimes[r].metricResults[m];
            const auto *ch = session.findChannel(res.rule.channelName);
            if (!ch) continue;

            double val = getVal(ch, i);
            if (std::isnan(val)) continue;

            if (res.rule.aggregation == core::MetricAgg::Average) {
              res.value += val;
            } else if (res.rule.aggregation == core::MetricAgg::Peak) {
              if (val > res.value) res.value = val;
            } else if (res.rule.aggregation == core::MetricAgg::Min) {
              if (val < res.value) res.value = val;
            }
          }

        } else {
          if (inInterval[r]) {
            inInterval[r] = false;
            double duration = t - matchStartTimes[r];
            if (duration >= 0.5) { // Filter micro-transients < 0.5s
              allRegimes[r].intervals.push_back({ matchStartTimes[r], t });
            }
          }
        }
      }
    }

    // Handle trailing active intervals at end of log
    for (size_t r = 0; r < numRegimes; ++r) {
      if (inInterval[r]) {
        double duration = timeSec->back() - matchStartTimes[r];
        if (duration >= 0.5) {
          allRegimes[r].intervals.push_back({ matchStartTimes[r], timeSec->back() });
        }
      }
    }

    // Finalize summaries
    for (size_t r = 0; r < numRegimes; ++r) {
      auto &reg = allRegimes[r];
      size_t count = reg.sampleCount;

      for (const auto &inter : reg.intervals) {
        reg.totalDwellTimeSec += (inter.endSec - inter.startSec);
      }
      reg.percentageOfLog = (reg.totalDwellTimeSec / totalLogTime) * 100.0;

      if (count > 0) {
        for (auto &res : reg.metricResults) {
          if (res.rule.aggregation == core::MetricAgg::Average) {
            res.value /= static_cast<double>(count);
          }
          if (std::isinf(res.value)) res.value = 0.0;
        }
      } else {
        for (auto &res : reg.metricResults) {
          if (std::isinf(res.value)) res.value = 0.0;
        }
      }

      if (reg.totalDwellTimeSec > 0.0) {
        summaries.push_back(reg);
      }
    }

    return summaries;
  }

} // namespace engine