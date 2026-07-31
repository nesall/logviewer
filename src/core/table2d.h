#pragma once

#include <cstddef>
#include <vector>

namespace core {

  // A 2D lookup table with arbitrary (not necessarily evenly-spaced) axis
  // breakpoints -- matches how real ECU tables (VE, target AFR, etc.) are
  // defined. Values are row-major: value(row, col), where rows correspond
  // to yBreakpoints() (load/MAP axis) and columns to xBreakpoints() (RPM
  // axis) -- matching TunerStudio's table orientation, so a resampled
  // clipboard export lines up with a pasted TunerStudio grid without any
  // transposition.
  class Table2D {
  public:
    Table2D() = default;
    Table2D(std::vector<double> xBreakpoints, std::vector<double> yBreakpoints);

    void setXBreakpoints(std::vector<double> breakpoints);
    void setYBreakpoints(std::vector<double> breakpoints);
    const std::vector<double> &xBreakpoints() const { return xBreakpoints_; }
    const std::vector<double> &yBreakpoints() const { return yBreakpoints_; }

    size_t columnCount() const { return xBreakpoints_.size(); }
    size_t rowCount() const { return yBreakpoints_.size(); }

    double value(size_t row, size_t col) const;
    void setValue(size_t row, size_t col, double newValue);

    // Bilinear-interpolated lookup at an arbitrary (x, y) point. Outside
    // the table's breakpoint range, the result clamps to the nearest edge
    // value rather than extrapolating -- matches how ECU tables actually
    // behave physically.
    double sample(double x, double y) const;

    // Builds a new table on the given breakpoints, resampling this
    // table's values onto them via sample(). Used both for "load channel
    // resolution is independent of the real VE table axes" and for
    // producing the final clipboard export sized to match a real tune.
    Table2D resampledTo(const std::vector<double> &newXBreakpoints,
      const std::vector<double> &newYBreakpoints) const;

  private:
    void ensureValuesSized();
    // Finds the breakpoint segment containing `queryValue`: lowIndex is
    // the index of the breakpoint at or before it, frac in [0,1] is the
    // fractional position within that segment. Clamps at the edges.
    static void locate(const std::vector<double> &breakpoints, double queryValue, size_t &lowIndex, double &frac);

    std::vector<double> xBreakpoints_;
    std::vector<double> yBreakpoints_;
    std::vector<std::vector<double>> values_; // values_[row][col]
  };

  // Convenience generator for a default evenly-spaced axis (a reasonable
  // seed to start from -- the table editor lets the user override any/all
  // of these to match their real tune's breakpoints).
  std::vector<double> generateEvenBreakpoints(double minValue, double maxValue, size_t count);

} // namespace core