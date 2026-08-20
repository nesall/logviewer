#include "engine/virtualdyno.h"
#include "3rdparty/nlohmann/json.hpp"
#include <cmath>
#include <algorithm>
#include <numeric>
#include <limits>


namespace engine {

  nlohmann::json VehicleDynoProfile::toJson() const
  {
    nlohmann::json j;
    j["name"] = name;
    j["totalWeightLbs"] = totalWeightLbs;
    j["dragCoefficient"] = dragCoefficient;
    j["frontalAreaM2"] = frontalAreaM2;
    j["rollingResistance"] = rollingResistance;
    j["tireWidthMm"] = tireWidthMm;
    j["tireAspectRatio"] = tireAspectRatio;
    j["wheelRimInches"] = wheelRimInches;
    j["gearRatio"] = gearRatio;
    j["finalDriveRatio"] = finalDriveRatio;
    j["enableSaeCorrection"] = enableSaeCorrection;
    j["baroPressureKpa"] = baroPressureKpa;
    j["ambientTempF"] = ambientTempF;
    j["smoothingWindow"] = smoothingWindow;
    return j;
  }

  VehicleDynoProfile VehicleDynoProfile::fromJson(const nlohmann::json &j)
  {
    VehicleDynoProfile p;
    if (j.contains("name")) p.name = j["name"].get<std::string>();
    if (j.contains("totalWeightLbs")) p.totalWeightLbs = j["totalWeightLbs"].get<double>();
    if (j.contains("dragCoefficient")) p.dragCoefficient = j["dragCoefficient"].get<double>();
    if (j.contains("frontalAreaM2")) p.frontalAreaM2 = j["frontalAreaM2"].get<double>();
    if (j.contains("rollingResistance")) p.rollingResistance = j["rollingResistance"].get<double>();
    if (j.contains("tireWidthMm")) p.tireWidthMm = j["tireWidthMm"].get<int>();
    if (j.contains("tireAspectRatio")) p.tireAspectRatio = j["tireAspectRatio"].get<int>();
    if (j.contains("wheelRimInches")) p.wheelRimInches = j["wheelRimInches"].get<int>();
    if (j.contains("gearRatio")) p.gearRatio = j["gearRatio"].get<double>();
    if (j.contains("finalDriveRatio")) p.finalDriveRatio = j["finalDriveRatio"].get<double>();
    if (j.contains("enableSaeCorrection")) p.enableSaeCorrection = j["enableSaeCorrection"].get<bool>();
    if (j.contains("baroPressureKpa")) p.baroPressureKpa = j["baroPressureKpa"].get<double>();
    if (j.contains("ambientTempF")) p.ambientTempF = j["ambientTempF"].get<double>();
    if (j.contains("smoothingWindow")) p.smoothingWindow = j["smoothingWindow"].get<int>();
    return p;
  }

  DynoResult VirtualDyno::calculate(
    const core::LogSession &session,
    const VehicleDynoProfile &profile,
    double startSec,
    double endSec)
  {
    DynoResult res;

    const auto *rpmCh = session.findChannel(session.channelMapping().rpm);
    const auto *loadCh = session.findChannel(session.channelMapping().load);
    const auto *timeSec = session.timeSec();

    if (!rpmCh || !timeSec || timeSec->size() < 10) {
      res.errorMessage = "Missing RPM or Time channel in session.";
      return res;
    }

    // 1. Determine Window Bounds
    size_t startIdx = 0;
    size_t endIdx = timeSec->size() - 1;

    if (startSec >= 0.0 && endSec > startSec) {
      auto itS = std::lower_bound(timeSec->begin(), timeSec->end(), startSec);
      auto itE = std::upper_bound(timeSec->begin(), timeSec->end(), endSec);
      startIdx = std::distance(timeSec->begin(), itS);
      endIdx = std::min(timeSec->size() - 1, static_cast<size_t>(std::distance(timeSec->begin(), itE)));
    } else if (session.cropRange().active) {
      auto itS = std::lower_bound(timeSec->begin(), timeSec->end(), session.cropRange().startSec);
      auto itE = std::upper_bound(timeSec->begin(), timeSec->end(), session.cropRange().endSec);
      startIdx = std::distance(timeSec->begin(), itS);
      endIdx = std::min(timeSec->size() - 1, static_cast<size_t>(std::distance(timeSec->begin(), itE)));
    }

    if (endIdx <= startIdx + 10) {
      res.errorMessage = "Selected time range is too short for dyno analysis.";
      return res;
    }

    // 2. Constants & Unit Conversions
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kAirDensityStd = 1.184; // kg/m^3 @ 25C std pressure
    constexpr double kGravity = 9.80665;
    const double massKg = profile.totalWeightLbs * 0.45359237;
    const double tireRadiusM = profile.tireDiameterMeters() / 2.0;
    const double totalRatio = profile.gearRatio * profile.finalDriveRatio;

    // SAE J1349 Correction Factor
    double saeFactor = 1.0;
    if (profile.enableSaeCorrection) {
      double tempC = (profile.ambientTempF - 32.0) * (5.0 / 9.0);
      double tempK = tempC + 273.15;
      double pDry = profile.baroPressureKpa; // simplified dry baro in kPa
      if (pDry > 50.0) {
        saeFactor = 1.180 * (99.0 / pDry) * std::sqrt(tempK / 298.0) - 0.18;
        saeFactor = std::clamp(saeFactor, 0.85, 1.25);
      }
    }

    // 3. Extract Raw Series
    std::vector<double> rawT, rawRpm, rawV, rawBoost;
    for (size_t i = startIdx; i <= endIdx; ++i) {
      double rpm = rpmCh->values()[i];
      double t = (*timeSec)[i];
      if (std::isnan(rpm) || rpm < 500.0) continue;

      // Velocity from RPM (m/s)
      double v_mps = (2.0 * kPi * tireRadiusM * rpm) / (60.0 * totalRatio);

      double boostVal = 0.0;
      if (loadCh && i < loadCh->values().size() && !std::isnan(loadCh->values()[i])) {
        boostVal = (loadCh->values()[i] - profile.baroPressureKpa) * 0.145038;
      }

      rawT.push_back(t);
      rawRpm.push_back(rpm);
      rawV.push_back(v_mps);
      rawBoost.push_back(boostVal);
    }

    const size_t n = rawT.size();
    if (n < 15) {
      res.errorMessage = "Insufficient continuous samples in RPM pull.";
      return res;
    }

    // 4. Moving Polynomial / Linear Regression for Velocity Derivative (dv/dt)
    int win = std::max(5, profile.smoothingWindow | 1); // Enforce odd window size
    int halfWin = win / 2;

    std::vector<double> accel(n, 0.0);

    for (size_t i = 0; i < n; ++i) {
      int i0 = std::max<int>(0, static_cast<int>(i) - halfWin);
      int i1 = std::min<int>(static_cast<int>(n) - 1, static_cast<int>(i) + halfWin);

      // Linear regression slope over local window: dv / dt
      double sumT = 0.0, sumV = 0.0, sumT2 = 0.0, sumTV = 0.0;
      int count = 0;
      for (int k = i0; k <= i1; ++k) {
        double tk = rawT[k];
        double vk = rawV[k];
        sumT += tk;
        sumV += vk;
        sumT2 += tk * tk;
        sumTV += tk * vk;
        count++;
      }

      double denom = count * sumT2 - sumT * sumT;
      if (std::abs(denom) > 1e-9) {
        accel[i] = (count * sumTV - sumT * sumV) / denom;
      } else {
        accel[i] = 0.0;
      }
    }

    // 5. Physics Force Integration -> Power & Torque
    res.rpm.reserve(n);
    res.whp.reserve(n);
    res.wtq.reserve(n);
    res.boostPsi.reserve(n);

    for (size_t i = halfWin; i < n - halfWin; ++i) {
      double a = accel[i];
      if (a <= 0.01) continue; // Exclude decel or stall frames

      double v = rawV[i];
      double rpm = rawRpm[i];

      // Force components (Newtons)
      double fInertia = massKg * a * 1.03; // +3% for driveline/wheel rotational inertia
      double fAero = 0.5 * kAirDensityStd * profile.dragCoefficient * profile.frontalAreaM2 * (v * v);
      double fRoll = profile.rollingResistance * massKg * kGravity;

      double fTotal = fInertia + fAero + fRoll;
      double powerWatts = fTotal * v;

      // Convert Watts to Wheel Horsepower (1 hp = 745.699872 W)
      double whp = (powerWatts / 745.699872) * saeFactor;
      double wtq = (rpm > 100.0) ? (whp * 5252.113) / rpm : 0.0;

      if (whp > 0.0 && wtq > 0.0 && whp < 2500.0) {
        res.rpm.push_back(rpm);
        res.whp.push_back(whp);
        res.wtq.push_back(wtq);
        res.boostPsi.push_back(rawBoost[i]);

        if (whp > res.peakWhp) {
          res.peakWhp = whp;
          res.peakWhpRpm = rpm;
        }
        if (wtq > res.peakWtq) {
          res.peakWtq = wtq;
          res.peakWtqRpm = rpm;
        }
        if (rawBoost[i] > res.maxBoostPsi) {
          res.maxBoostPsi = rawBoost[i];
        }
      }
    }

    if (res.rpm.empty()) {
      res.errorMessage = "No positive acceleration detected in selected pull range.";
      return res;
    }

    res.valid = true;
    return res;
  }

  DynoResult VirtualDyno::calculateAveraged(
    const core::LogSession &session,
    const VehicleDynoProfile &profile,
    const std::vector<core::TimeInterval> &intervals)
  {
    DynoResult res;

    if (intervals.empty()) {
      res.errorMessage = "No intervals to average.";
      return res;
    }

    // 1. Run each pull independently.
    std::vector<DynoResult> runs;
    runs.reserve(intervals.size());
    for (const auto &iv : intervals) {
      DynoResult r = calculate(session, profile, iv.startSec, iv.endSec);
      if (r.valid && !r.rpm.empty()) runs.push_back(std::move(r));
    }

    if (runs.empty()) {
      res.errorMessage = "None of the selected pulls produced valid dyno data.";
      return res;
    }

    if (runs.size() == 1) {
      return runs.front();
    }

    // 2. Build a common RPM grid spanning the union of all pulls, at a
    //    fixed step. Each pull's rpm vector is ascending (see calculate()),
    //    so per-pull interpolation can walk it linearly.
    double rpmMin = std::numeric_limits<double>::max();
    double rpmMax = std::numeric_limits<double>::lowest();
    for (const auto &r : runs) {
      rpmMin = std::min(rpmMin, r.rpm.front());
      rpmMax = std::max(rpmMax, r.rpm.back());
    }

    constexpr double kStepRpm = 50.0;
    if (rpmMax <= rpmMin) {
      res.errorMessage = "Selected pulls have no overlapping RPM range.";
      return res;
    }

    const int steps = static_cast<int>(std::ceil((rpmMax - rpmMin) / kStepRpm)) + 1;

    // Linear interpolation helper: samples r.{field} at the given rpm using
    // r.rpm as the ascending x-axis. Returns false if rpm falls outside
    // this pull's covered range (so it's excluded from that bin's average).
    auto sampleAt = [](const DynoResult &r, const std::vector<double> &field, double rpm, double &out) -> bool {
      if (rpm < r.rpm.front() || rpm > r.rpm.back()) return false;
      auto it = std::lower_bound(r.rpm.begin(), r.rpm.end(), rpm);
      if (it == r.rpm.begin()) {
        out = field.front();
        return true;
      }
      size_t hi = std::distance(r.rpm.begin(), it);
      size_t lo = hi - 1;
      double x0 = r.rpm[lo], x1 = r.rpm[hi];
      double y0 = field[lo], y1 = field[hi];
      double t = (x1 > x0) ? (rpm - x0) / (x1 - x0) : 0.0;
      out = y0 + t * (y1 - y0);
      return true;
      };

    res.rpm.reserve(steps);
    res.whp.reserve(steps);
    res.wtq.reserve(steps);
    res.boostPsi.reserve(steps);

    for (int i = 0; i < steps; ++i) {
      double rpm = rpmMin + i * kStepRpm;
      if (rpm > rpmMax) rpm = rpmMax;

      double sumWhp = 0.0, sumWtq = 0.0, sumBoost = 0.0;
      int contributors = 0;
      for (const auto &r : runs) {
        double whp, wtq, boost;
        bool okWhp = sampleAt(r, r.whp, rpm, whp);
        bool okWtq = sampleAt(r, r.wtq, rpm, wtq);
        if (!okWhp || !okWtq) continue;
        sampleAt(r, r.boostPsi, rpm, boost); // best-effort; defaults to 0 if out of range
        sumWhp += whp;
        sumWtq += wtq;
        sumBoost += boost;
        contributors++;
      }

      if (contributors == 0) continue; // no pull covers this RPM bin

      double whpAvg = sumWhp / contributors;
      double wtqAvg = sumWtq / contributors;
      double boostAvg = sumBoost / contributors;

      res.rpm.push_back(rpm);
      res.whp.push_back(whpAvg);
      res.wtq.push_back(wtqAvg);
      res.boostPsi.push_back(boostAvg);

      if (whpAvg > res.peakWhp) {
        res.peakWhp = whpAvg;
        res.peakWhpRpm = rpm;
      }
      if (wtqAvg > res.peakWtq) {
        res.peakWtq = wtqAvg;
        res.peakWtqRpm = rpm;
      }
      if (boostAvg > res.maxBoostPsi) {
        res.maxBoostPsi = boostAvg;
      }
    }

    if (res.rpm.empty()) {
      res.errorMessage = "Selected pulls have no overlapping RPM range.";
      return res;
    }

    res.valid = true;
    return res;
  }

} // namespace engine