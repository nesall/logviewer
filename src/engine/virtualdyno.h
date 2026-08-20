#pragma once

#include <vector>
#include <string>
#include "3rdparty/nlohmann/json_fwd.hpp"
#include "core/logsession.h"

namespace engine {

  struct VehicleDynoProfile {
    std::string name = "Generic Vehicle";
    double totalWeightLbs = 3000.0;     // Vehicle curb weight + driver/passenger
    double dragCoefficient = 0.31;      // Cd
    double frontalAreaM2 = 1.80;        // A in m^2
    double rollingResistance = 0.015;   // Crr

    // Tire specs: e.g. 225 / 50 R 16
    int tireWidthMm = 225;
    int tireAspectRatio = 50;
    int wheelRimInches = 16;

    // Gearing
    double gearRatio = 1.330;           // Selected test gear ratio (e.g. 3rd gear)
    double finalDriveRatio = 4.100;

    // SAE Atmospheric Correction (optional)
    bool enableSaeCorrection = true;
    double baroPressureKpa = 101.325;
    double ambientTempF = 77.0;

    // Smoothing Filter Window (odd number, e.g. 15..51)
    int smoothingWindow = 25;

    double tireDiameterMeters() const {
      double sidewallMm = tireWidthMm * (tireAspectRatio / 100.0);
      double diameterMm = (sidewallMm * 2.0) + (wheelRimInches * 25.4);
      return diameterMm / 1000.0;
    }

    nlohmann::json toJson() const;
    static VehicleDynoProfile fromJson(const nlohmann::json &j);
  };

  struct DynoResult {
    std::vector<double> rpm;
    std::vector<double> whp;
    std::vector<double> wtq;
    std::vector<double> boostPsi;

    double peakWhp = 0.0;
    double peakWhpRpm = 0.0;
    double peakWtq = 0.0;
    double peakWtqRpm = 0.0;
    double maxBoostPsi = 0.0;

    bool valid = false;
    std::string errorMessage;
  };

  class VirtualDyno {
  public:
    static DynoResult calculate(
      const core::LogSession &session,
      const VehicleDynoProfile &profile,
      double startSec = -1.0,
      double endSec = -1.0);

    // Runs calculate() once per interval and averages the resulting curves
    // onto a common RPM grid. Intervals whose calculate() call fails are
    // skipped; if all fail, the returned result is invalid with an
    // aggregated error message.
    static DynoResult calculateAveraged(
      const core::LogSession &session,
      const VehicleDynoProfile &profile,
      const std::vector<core::TimeInterval> &intervals);
  };

} // namespace engine