#include "engine/decimator.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace engine {

  void Decimator::lttbRun(
    const std::vector<double> &xData,
    const std::vector<double> &yData,
    size_t begin, size_t end,
    int targetPoints,
    std::vector<double> &outX,
    std::vector<double> &outY)
  {
    const size_t n = end - begin;
    if (n == 0) return;

    if (targetPoints < 3) targetPoints = 3;
    const size_t m = static_cast<size_t>(targetPoints);

    // Nothing to decimate -- fewer samples in this run than the budget.
    if (n <= m) {
      for (size_t i = begin; i < end; ++i) {
        outX.push_back(xData[i]);
        outY.push_back(yData[i]);
      }
      return;
    }

    const double bucketSize = static_cast<double>(n - 2) / static_cast<double>(m - 2);

    outX.push_back(xData[begin]);
    outY.push_back(yData[begin]);

    size_t a = 0; // index of the previously-selected point, LOCAL to [begin, end)

    for (size_t i = 0; i < m - 2; ++i) {
      // Average point of the NEXT bucket, used as the fixed third vertex
      // of the triangle for every candidate in the current bucket.
      size_t avgRangeStart = static_cast<size_t>(std::floor((i + 1) * bucketSize)) + 1;
      size_t avgRangeEnd = static_cast<size_t>(std::floor((i + 2) * bucketSize)) + 1;
      avgRangeEnd = std::min(avgRangeEnd, n);
      if (avgRangeEnd <= avgRangeStart) avgRangeEnd = std::min(avgRangeStart + 1, n);

      double avgX = 0.0, avgY = 0.0;
      size_t avgCount = 0;
      for (size_t j = avgRangeStart; j < avgRangeEnd; ++j) {
        avgX += xData[begin + j];
        avgY += yData[begin + j];
        ++avgCount;
      }
      if (avgCount == 0) {
        avgX = xData[end - 1];
        avgY = yData[end - 1];
      } else {
        avgX /= static_cast<double>(avgCount);
        avgY /= static_cast<double>(avgCount);
      }

      // Candidate range for THIS bucket.
      size_t rangeStart = static_cast<size_t>(std::floor(i * bucketSize)) + 1;
      size_t rangeEnd = static_cast<size_t>(std::floor((i + 1) * bucketSize)) + 1;
      rangeEnd = std::min(rangeEnd, n - 1);
      if (rangeEnd <= rangeStart) rangeEnd = rangeStart + 1;

      const double pointAx = xData[begin + a];
      const double pointAy = yData[begin + a];

      double maxArea = -1.0;
      size_t nextA = rangeStart;
      for (size_t j = rangeStart; j < rangeEnd; ++j) {
        const double area = std::fabs(
          (pointAx - avgX) * (yData[begin + j] - pointAy) -
          (pointAx - xData[begin + j]) * (avgY - pointAy)
        ) * 0.5;
        if (area > maxArea) {
          maxArea = area;
          nextA = j;
        }
      }

      outX.push_back(xData[begin + nextA]);
      outY.push_back(yData[begin + nextA]);
      a = nextA;
    }

    outX.push_back(xData[end - 1]);
    outY.push_back(yData[end - 1]);
  }

  DecimatedSeries Decimator::decimate(
    const std::vector<double> &xData,
    const std::vector<double> &yData,
    double xMin,
    double xMax,
    int targetPoints,
    const std::vector<size_t> &discontinuityIndices)
  {
    DecimatedSeries result;
    if (xData.empty() || yData.empty() || xData.size() != yData.size()) {
      return result;
    }

    // Find range [begin, end) within [xMin, xMax]
    auto itBegin = std::lower_bound(xData.begin(), xData.end(), xMin);
    auto itEnd = std::upper_bound(xData.begin(), xData.end(), xMax);

    size_t begin = std::distance(xData.begin(), itBegin);
    size_t end = std::distance(xData.begin(), itEnd);

    if (begin >= end) return result;

    const size_t visibleCount = end - begin;
    if (visibleCount <= static_cast<size_t>(targetPoints)) {
      // If small enough, copy directly while injecting NaNs at discontinuities
      for (size_t i = begin; i < end; ++i) {
        if (std::binary_search(discontinuityIndices.begin(), discontinuityIndices.end(), i)) {
          result.x.push_back(xData[i]);
          result.y.push_back(std::numeric_limits<double>::quiet_NaN());
        }
        result.x.push_back(xData[i]);
        result.y.push_back(yData[i]);
      }
      return result;
    }

    size_t runStart = begin;
    size_t lastValidIdx = std::numeric_limits<size_t>::max();

    while (runStart < end) {
      // Skip NaNs
      if (std::isnan(yData[runStart])) {
        if (!result.y.empty() && !std::isnan(result.y.back())) {
          result.x.push_back(xData[runStart]);
          result.y.push_back(std::numeric_limits<double>::quiet_NaN());
        }
        ++runStart;
        continue;
      }

      // Find contiguous segment until NaN, big dt gap, OR a Stitch/Discontinuity index
      size_t runEnd = runStart + 1;
      while (runEnd < end && !std::isnan(yData[runEnd])) {
        // Check if current row is a known stitch / discontinuity boundary
        if (std::binary_search(discontinuityIndices.begin(), discontinuityIndices.end(), runEnd)) {
          break;
        }
        // Check for large time gaps / rollbacks
        double dt = xData[runEnd] - xData[runEnd - 1];
        if (dt > 0.250 || dt < -1e-5) {
          break;
        }
        ++runEnd;
      }

      const size_t runLen = runEnd - runStart;
      const int runBudget = std::max(3, static_cast<int>(
        (static_cast<double>(runLen) / static_cast<double>(visibleCount)) * targetPoints));

      // Inject NaN boundary between split runs
      if (lastValidIdx != std::numeric_limits<size_t>::max()) {
        if (!result.y.empty() && !std::isnan(result.y.back())) {
          result.x.push_back(xData[lastValidIdx]);
          result.y.push_back(std::numeric_limits<double>::quiet_NaN());
        }
      }

      // Downsample current segment with LTTB
      lttbRun(xData, yData, runStart, runEnd, runBudget, result.x, result.y);

      lastValidIdx = runEnd - 1;
      runStart = runEnd;
    }

    return result;
  }

} // namespace engine