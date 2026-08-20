#pragma once

#include <string>
#include <vector>
#include <algorithm>
#include <cmath>
#include "3rdparty/nlohmann/json_fwd.hpp"

namespace core {

  enum class CurveInterpMode {
    PiecewiseLinear,
    SmoothSpline
  };

  struct CurvePoint {
    double x = 0.0;
    double y = 0.0;
    bool operator<(const CurvePoint &other) const { return x < other.x; }
  };

  class Curve1D {
  public:
    Curve1D() = default;
    Curve1D(std::string name, std::string xName, std::string yName, std::string xUnit = "", std::string yUnit = "");

    const std::string &name() const { return name_; }
    void setName(const std::string &n) { name_ = n; }

    const std::string &xChannelName() const { return xChannelName_; }
    void setXChannelName(const std::string &x) { xChannelName_ = x; }

    const std::string &yChannelName() const { return yChannelName_; }
    void setYChannelName(const std::string &y) { yChannelName_ = y; }

    const std::string &xUnit() const { return xUnit_; }
    const std::string &yUnit() const { return yUnit_; }
    void setXUnit(std::string u) { xUnit_ = std::move(u); }
    void setYUnit(std::string u) { yUnit_ = std::move(u); }

    CurveInterpMode interpMode() const { return interpMode_; }
    void setInterpMode(CurveInterpMode m) { interpMode_ = m; }

    const std::vector<CurvePoint> &points() const { return points_; }

    void setPoints(std::vector<CurvePoint> pts);

    void addPoint(double x, double y);

    void removePoint(size_t index);

    void updatePoint(size_t index, double x, double y);

    // Evaluates y = f(x)
    double evaluate(double x) const;

    nlohmann::json toJson() const;

    static Curve1D fromJson(const nlohmann::json &j);

  private:
    void sortPoints() { std::stable_sort(points_.begin(), points_.end()); }

    std::string name_;
    std::string xChannelName_;
    std::string yChannelName_;
    std::string xUnit_;
    std::string yUnit_;
    CurveInterpMode interpMode_ = CurveInterpMode::PiecewiseLinear;
    std::vector<CurvePoint> points_;
  };

} // namespace core