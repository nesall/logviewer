#include "core/table2d.h"

#include <algorithm>
#include <utility>

namespace core {

  Table2D::Table2D(std::vector<double> xBreakpoints, std::vector<double> yBreakpoints)
    : xBreakpoints_(std::move(xBreakpoints)), yBreakpoints_(std::move(yBreakpoints))
  {
    ensureValuesSized();
  }

  void Table2D::setXBreakpoints(std::vector<double> breakpoints)
  {
    // If not sorted, sort breakpoints and re-order column values accordingly
    bool needSort = false;
    for (size_t i = 1; i < breakpoints.size(); ++i) {
      if (breakpoints[i] < breakpoints[i - 1]) {
        needSort = true;
        break;
      }
    }

    if (needSort) {
      // Create index permutation vector
      std::vector<size_t> p(breakpoints.size());
      for (size_t i = 0; i < p.size(); ++i) p[i] = i;
      std::sort(p.begin(), p.end(), [&breakpoints](size_t a, size_t b) {
        return breakpoints[a] < breakpoints[b];
        });

      std::vector<double> sortedX(breakpoints.size());
      for (size_t i = 0; i < p.size(); ++i) {
        sortedX[i] = breakpoints[p[i]];
      }

      // Reorder values matrix columns to match sorted X breakpoints
      for (auto &row : values_) {
        if (row.size() == breakpoints.size()) {
          std::vector<double> sortedRow(row.size());
          for (size_t i = 0; i < p.size(); ++i) {
            sortedRow[i] = row[p[i]];
          }
          row = std::move(sortedRow);
        }
      }
      xBreakpoints_ = std::move(sortedX);
    } else {
      xBreakpoints_ = std::move(breakpoints);
    }
    ensureValuesSized();
  }

  void Table2D::setYBreakpoints(std::vector<double> breakpoints)
  {
    bool needSort = false;
    for (size_t i = 1; i < breakpoints.size(); ++i) {
      if (breakpoints[i] < breakpoints[i - 1]) {
        needSort = true;
        break;
      }
    }

    if (needSort) {
      std::vector<size_t> p(breakpoints.size());
      for (size_t i = 0; i < p.size(); ++i) p[i] = i;
      std::sort(p.begin(), p.end(), [&breakpoints](size_t a, size_t b) {
        return breakpoints[a] < breakpoints[b];
        });

      std::vector<double> sortedY(breakpoints.size());
      for (size_t i = 0; i < p.size(); ++i) {
        sortedY[i] = breakpoints[p[i]];
      }

      if (values_.size() == breakpoints.size()) {
        std::vector<std::vector<double>> sortedValues(values_.size());
        for (size_t i = 0; i < p.size(); ++i) {
          sortedValues[i] = std::move(values_[p[i]]);
        }
        values_ = std::move(sortedValues);
      }
      yBreakpoints_ = std::move(sortedY);
    } else {
      yBreakpoints_ = std::move(breakpoints);
    }
    ensureValuesSized();
  }

  void Table2D::ensureValuesSized()
  {
    values_.resize(yBreakpoints_.size());
    for (auto &row : values_) {
      row.resize(xBreakpoints_.size(), 0.0);
    }
  }

  double Table2D::value(size_t row, size_t col) const
  {
    if (row >= values_.size() || col >= values_[row].size()) {
      return 0.0;
    }
    return values_[row][col];
  }

  void Table2D::setValue(size_t row, size_t col, double newValue)
  {
    if (row >= values_.size() || col >= values_[row].size()) {
      return;
    }
    values_[row][col] = newValue;
  }

  void Table2D::locate(const std::vector<double> &breakpoints, double queryValue, size_t &lowIndex, double &frac)
  {
    if (breakpoints.empty()) {
      lowIndex = 0;
      frac = 0.0;
      return;
    }
    if (breakpoints.size() == 1 || queryValue <= breakpoints.front()) {
      lowIndex = 0;
      frac = 0.0;
      return;
    }
    if (queryValue >= breakpoints.back()) {
      lowIndex = breakpoints.size() - 2;
      frac = 1.0;
      return;
    }

    // First breakpoint strictly greater than queryValue.
    auto it = std::upper_bound(breakpoints.begin(), breakpoints.end(), queryValue);
    size_t highIndex = static_cast<size_t>(it - breakpoints.begin());
    lowIndex = highIndex - 1;

    double lowValue = breakpoints[lowIndex];
    double highValue = breakpoints[highIndex];
    double span = highValue - lowValue;
    frac = (span > 1e-12) ? (queryValue - lowValue) / span : 0.0;
  }

  double Table2D::sample(double x, double y) const
  {
    if (xBreakpoints_.empty() || yBreakpoints_.empty()) {
      return 0.0;
    }

    size_t colLow = 0;
    double fracX = 0.0;
    locate(xBreakpoints_, x, colLow, fracX);
    size_t colHigh = std::min(colLow + 1, xBreakpoints_.size() - 1);

    size_t rowLow = 0;
    double fracY = 0.0;
    locate(yBreakpoints_, y, rowLow, fracY);
    size_t rowHigh = std::min(rowLow + 1, yBreakpoints_.size() - 1);

    double v00 = value(rowLow, colLow);
    double v01 = value(rowLow, colHigh);
    double v10 = value(rowHigh, colLow);
    double v11 = value(rowHigh, colHigh);

    double top = v00 + (v01 - v00) * fracX;
    double bottom = v10 + (v11 - v10) * fracX;
    return top + (bottom - top) * fracY;
  }

  Table2D Table2D::resampledTo(const std::vector<double> &newXBreakpoints, const std::vector<double> &newYBreakpoints) const
  {
    Table2D result(newXBreakpoints, newYBreakpoints);
    for (size_t row = 0; row < newYBreakpoints.size(); ++row) {
      for (size_t col = 0; col < newXBreakpoints.size(); ++col) {
        result.setValue(row, col, sample(newXBreakpoints[col], newYBreakpoints[row]));
      }
    }
    return result;
  }

  std::vector<double> generateEvenBreakpoints(double minValue, double maxValue, size_t count)
  {
    std::vector<double> breakpoints;
    if (count == 0) {
      return breakpoints;
    }
    breakpoints.resize(count);
    if (count == 1) {
      breakpoints[0] = minValue;
      return breakpoints;
    }
    const double step = (maxValue - minValue) / static_cast<double>(count - 1);
    for (size_t i = 0; i < count; ++i) {
      breakpoints[i] = minValue + step * static_cast<double>(i);
    }
    return breakpoints;
  }

} // namespace core