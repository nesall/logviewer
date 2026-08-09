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

    // Helper to extract channel values safely (returns NaN if index out of range or null)
    double getVal(const core::Channel *ch, size_t idx) {
      if (!ch || idx >= ch->values().size()) return kNaN;
      return ch->values()[idx];
    }

    // Helper structure for pre-compiled ExprTk custom regime expressions
// 1. Define CompiledRegimeExpr as move-constructible
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

    bool evaluateRegime(const core::RegimeDef &def, CompiledRegimeExpr &compiledExpr, size_t rowIndex, double rpm, double map, double tps, double tpsDot) {
      // 1. If custom ExprTk formula is specified, evaluate it
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

      // 2. Fast C++ path for standard min/max threshold bounds
      if (!std::isnan(def.minRpm) && rpm < def.minRpm) return false;
      if (!std::isnan(def.maxRpm) && rpm > def.maxRpm) return false;
      if (!std::isnan(def.minMap) && map < def.minMap) return false;
      if (!std::isnan(def.maxMap) && map > def.maxMap) return false;
      if (!std::isnan(def.minTps) && tps < def.minTps) return false;
      if (!std::isnan(def.maxTps) && tps > def.maxTps) return false;
      if (!std::isnan(def.minTpsDot) && std::abs(tpsDot) < def.minTpsDot) return false;
      if (!std::isnan(def.maxTpsDot) && std::abs(tpsDot) > def.maxTpsDot) return false;

      return true;
    }

  } // namespace

  std::unordered_map<core::RegimeType, core::RegimeDef> engine::getDefaultRegimeDefs()
  {
    return {
      { core::RegimeType::FreewayCruise, { core::RegimeType::FreewayCruise, "freeway_cruise", "Freeway Cruise", ImVec4(0.2f, 0.7f, 1.0f, 0.25f), false, true, 2000, 4000, kNaN, 60, kNaN, 20, kNaN, 10, kNaN, 1600.0 /* EGT Warning */ } },
      { core::RegimeType::OverrunFuelCut, { core::RegimeType::OverrunFuelCut, "overrun_fuel_cut", "Overrun Fuel Cut (DFCO)", ImVec4(0.8f, 0.3f, 0.3f, 0.25f), false, true, 1800, kNaN, kNaN, 22, kNaN, 2 } },
      { core::RegimeType::HighGearLowRpmBoost, { core::RegimeType::HighGearLowRpmBoost, "lspi_risk", "Low RPM / High Boost (LSPI)", ImVec4(1.0f, 0.5f, 0.0f, 0.30f), false, true, 1500, 3000, 110, kNaN, 50, kNaN } },
      { core::RegimeType::HighRpmVacuum, { core::RegimeType::HighRpmVacuum, "high_rpm_vacuum", "High RPM Vacuum / Decel", ImVec4(0.6f, 0.4f, 0.8f, 0.25f), false, true, 4500, kNaN, kNaN, 50, kNaN, 10 } },
      { core::RegimeType::IdleStability, { core::RegimeType::IdleStability, "idle_stability", "Idle Stability", ImVec4(0.3f, 0.8f, 0.4f, 0.25f), false, true, 500, 1100, kNaN, 45, kNaN, 2 } },
      { core::RegimeType::TransientTipIn, { core::RegimeType::TransientTipIn, "transient_tipin", "Transient Tip-In", ImVec4(0.9f, 0.9f, 0.2f, 0.25f), false, true, kNaN, kNaN, kNaN, kNaN, kNaN, kNaN, 35 } }
    };
  }

  std::vector<core::RegimeSummary> RegimeAnalyzer::analyzeSession(const core::LogSession &session, const std::vector<core::RegimeDef> &definitions)
  {
    std::vector<core::RegimeSummary> summaries;
    if (session.rowCount() == 0) return summaries;

    std::vector<core::RegimeDef> activeDefs = definitions;
    if (activeDefs.empty()) {
      auto defaultMap = engine::getDefaultRegimeDefs();
      for (auto &[type, def] : defaultMap) {
        activeDefs.push_back(def);
      }
    }

    const auto *timeSec = session.timeSec();
    if (!timeSec || timeSec->empty()) return summaries;

    double totalLogTime = timeSec->back() - timeSec->front();
    if (totalLogTime <= 0.0) totalLogTime = 1.0;

    // Find Channels
    const auto &mapping = session.channelMapping();
    const auto *rpmCh = session.findChannel(mapping.rpm);
    const auto *mapCh = session.findChannel(mapping.load);
    const auto *tpsCh = session.findChannel(mapping.tps);
    const auto *tpsDotCh = session.findChannel(mapping.tpsDot);
    const auto *afrCh = session.findChannel(mapping.afr);
    const auto *egtCh = session.findChannel(mapping.egt);
    const auto *pwCh = session.findChannel(mapping.pw);
    const auto *timingCh = session.findChannel(mapping.timing);
    const auto *cltCh = session.findChannel(mapping.clt);
    const auto *matCh = session.findChannel(mapping.mat);
    const auto *dutyCh = session.findChannel(mapping.duty);

    // Initialize summaries array dynamically from activeDefs
    std::vector<core::RegimeSummary> allRegimes(activeDefs.size());
    std::vector<CompiledRegimeExpr> compiledExprs(activeDefs.size());

    for (size_t r = 0; r < activeDefs.size(); ++r) {
      allRegimes[r].def = activeDefs[r];
      if (!activeDefs[r].customFormula.empty()) {
        compiledExprs[r].compile(activeDefs[r].customFormula, session);
      }
    }

    const size_t numRegimes = allRegimes.size();
    const size_t rowCount = session.rowCount();

    std::vector<bool> matches(numRegimes, false);
    std::vector<double> matchStartTimes(numRegimes, 0.0);
    std::vector<bool> inInterval(numRegimes, false);

    struct Acc {
      double sumRpm = 0, sumMap = 0, sumAfr = 0, sumTiming = 0, sumClt = 0, sumMat = 0, sumDuty = 0;
      double maxEgt = 0, maxClt = 0, maxMat = 0, maxDuty = 0;
      size_t count = 0;
    };
    std::vector<Acc> accs(numRegimes);

    for (size_t i = 0; i < rowCount; ++i) {
      if (!session.isRowInCropRange(i)) continue;

      double t = (*timeSec)[i];
      double rpm = getVal(rpmCh, i);
      double map = getVal(mapCh, i);
      double tps = getVal(tpsCh, i);
      double tpsDot = getVal(tpsDotCh, i);
      double afr = getVal(afrCh, i);
      double egt = getVal(egtCh, i);
      double clt = getVal(cltCh, i);
      double mat = getVal(matCh, i);
      double duty = getVal(dutyCh, i);
      double timing = getVal(timingCh, i);

      // Evaluate active regimes
      for (size_t r = 0; r < numRegimes; ++r) {
        matches[r] = evaluateRegime(allRegimes[r].def, compiledExprs[r], i, rpm, map, tps, tpsDot);

        if (matches[r]) {
          if (!inInterval[r]) {
            inInterval[r] = true;
            matchStartTimes[r] = t;
          }
          accs[r].sumRpm += std::isnan(rpm) ? 0 : rpm;
          accs[r].sumMap += std::isnan(map) ? 0 : map;
          accs[r].sumAfr += std::isnan(afr) ? 0 : afr;
          accs[r].sumTiming += std::isnan(timing) ? 0 : timing;
          accs[r].sumClt += std::isnan(clt) ? 0 : clt;
          accs[r].sumMat += std::isnan(mat) ? 0 : mat;
          accs[r].sumDuty += std::isnan(duty) ? 0 : duty;

          if (!std::isnan(egt) && egt > accs[r].maxEgt) accs[r].maxEgt = egt;
          if (!std::isnan(clt) && clt > accs[r].maxClt) accs[r].maxClt = clt;
          if (!std::isnan(mat) && mat > accs[r].maxMat) accs[r].maxMat = mat;
          if (!std::isnan(duty) && duty > accs[r].maxDuty) accs[r].maxDuty = duty;
          accs[r].count++;
        } else {
          if (inInterval[r]) {
            inInterval[r] = false;
            double duration = t - matchStartTimes[r];
            if (duration >= 0.5) { // Filter out micro noise < 0.5 sec
              allRegimes[r].intervals.push_back({ matchStartTimes[r], t });
            }
          }
        }
      }
    }

    // Finalize Summaries
    for (size_t r = 0; r < numRegimes; ++r) {
      auto &reg = allRegimes[r];
      size_t count = accs[r].count;
      reg.sampleCount = count;

      for (const auto &inter : reg.intervals) {
        reg.totalDwellTimeSec += (inter.endSec - inter.startSec);
      }
      reg.percentageOfLog = (reg.totalDwellTimeSec / totalLogTime) * 100.0;

      if (count > 0) {
        reg.avgRpm = accs[r].sumRpm / count;
        reg.avgMap = accs[r].sumMap / count;
        reg.avgAfr = accs[r].sumAfr / count;
        reg.avgTiming = accs[r].sumTiming / count;
        reg.avgClt = accs[r].sumClt / count;
        reg.avgMat = accs[r].sumMat / count;
        reg.avgDuty = accs[r].sumDuty / count;
        reg.peakEgt = accs[r].maxEgt;
        reg.peakClt = accs[r].maxClt;
        reg.peakMat = accs[r].maxMat;
        reg.peakDuty = accs[r].maxDuty;
      }

      // Context Warnings
      if (!std::isnan(reg.def.maxEgtWarning) && reg.peakEgt > reg.def.maxEgtWarning) {
        reg.warningMessage = "High EGT: " + std::to_string(static_cast<int>(reg.peakEgt)) + " °F (Limit: " +
          std::to_string(static_cast<int>(reg.def.maxEgtWarning)) + " °F)";
      } else if (!std::isnan(reg.def.maxCltWarning) && reg.peakClt > reg.def.maxCltWarning) {
        reg.warningMessage = "High CLT: " + std::to_string(static_cast<int>(reg.peakClt)) + " °F";
      } else if (!std::isnan(reg.def.maxDutyWarning) && reg.peakDuty > reg.def.maxDutyWarning) {
        reg.warningMessage = "High Duty Cycle: " + std::to_string(static_cast<int>(reg.peakDuty)) + " %";
      } else if (reg.def.type == core::RegimeType::HighGearLowRpmBoost && count > 0) {
        reg.warningMessage = "High Engine Load at Low RPM";
      }

      if (reg.totalDwellTimeSec > 0.0) {
        summaries.push_back(reg);
      }
    }

    return summaries;
  }

} // namespace engine