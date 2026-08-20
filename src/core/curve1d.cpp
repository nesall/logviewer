#include "curve1d.h"
#include "3rdparty/nlohmann/json.hpp"

core::Curve1D::Curve1D(std::string name, std::string xName, std::string yName, std::string xUnit, std::string yUnit)
    : name_(std::move(name)), xChannelName_(std::move(xName)), yChannelName_(std::move(yName)),
    xUnit_(std::move(xUnit)), yUnit_(std::move(yUnit)) {}

void core::Curve1D::setPoints(std::vector<CurvePoint> pts)
{
  points_ = std::move(pts);
  sortPoints();
}

void core::Curve1D::addPoint(double x, double y)
{
  points_.push_back({ x, y });
  sortPoints();
}

void core::Curve1D::removePoint(size_t index)
{
  if (index < points_.size()) {
    points_.erase(points_.begin() + index);
  }
}

void core::Curve1D::updatePoint(size_t index, double x, double y)
{
  if (index < points_.size()) {
    points_[index] = { x, y };
    sortPoints();
  }
}

double core::Curve1D::evaluate(double x) const
{
  if (points_.empty()) return 0.0;
  if (points_.size() == 1 || x <= points_.front().x) return points_.front().y;
  if (x >= points_.back().x) return points_.back().y;

  // Find segment
  auto it = std::upper_bound(points_.begin(), points_.end(), CurvePoint{ x, 0.0 });
  size_t i1 = std::distance(points_.begin(), it);
  size_t i0 = i1 - 1;

  double x0 = points_[i0].x;
  double y0 = points_[i0].y;
  double x1 = points_[i1].x;
  double y1 = points_[i1].y;

  if (std::abs(x1 - x0) < 1e-9) return y0;

  double t = (x - x0) / (x1 - x0);

  if (interpMode_ == CurveInterpMode::PiecewiseLinear || points_.size() < 4) {
    return y0 + t * (y1 - y0);
  }

  // Catmull-Rom cubic interpolation for smooth splines
  size_t i_prev = (i0 > 0) ? i0 - 1 : i0;
  size_t i_next = (i1 + 1 < points_.size()) ? i1 + 1 : i1;

  double y_prev = points_[i_prev].y;
  double y_next = points_[i_next].y;

  double m0 = 0.5 * (y1 - y_prev);
  double m1 = 0.5 * (y_next - y0);

  double t2 = t * t;
  double t3 = t2 * t;

  double h00 = 2 * t3 - 3 * t2 + 1;
  double h10 = t3 - 2 * t2 + t;
  double h01 = -2 * t3 + 3 * t2;
  double h11 = t3 - t2;

  return h00 * y0 + h10 * m0 + h01 * y1 + h11 * m1;
}

nlohmann::json core::Curve1D::toJson() const
{
  nlohmann::json j;
  j["name"] = name_;
  j["xChannel"] = xChannelName_;
  j["yChannel"] = yChannelName_;
  j["xUnit"] = xUnit_;
  j["yUnit"] = yUnit_;
  j["interpMode"] = static_cast<int>(interpMode_);

  nlohmann::json ptsArr = nlohmann::json::array();
  for (const auto &p : points_) {
    ptsArr.push_back({ { "x", p.x },{ "y", p.y } });
  }
  j["points"] = ptsArr;
  return j;
}

core::Curve1D core::Curve1D::fromJson(const nlohmann::json &j)
{
  Curve1D c;
  if (j.contains("name")) c.name_ = j["name"].get<std::string>();
  if (j.contains("xChannel")) c.xChannelName_ = j["xChannel"].get<std::string>();
  if (j.contains("yChannel")) c.yChannelName_ = j["yChannel"].get<std::string>();
  if (j.contains("xUnit")) c.xUnit_ = j["xUnit"].get<std::string>();
  if (j.contains("yUnit")) c.yUnit_ = j["yUnit"].get<std::string>();
  if (j.contains("interpMode")) c.interpMode_ = static_cast<CurveInterpMode>(j["interpMode"].get<int>());

  if (j.contains("points") && j["points"].is_array()) {
    for (const auto &p : j["points"]) {
      c.points_.push_back({ p["x"].get<double>(), p["y"].get<double>() });
    }
    c.sortPoints();
  }
  return c;
}
