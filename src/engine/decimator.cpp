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
    double xMin, double xMax,
    int targetPoints)
  {
    DecimatedSeries result;
    const size_t n = xData.size();
    if (n == 0 || yData.size() != n || targetPoints <= 0) return result;

    // 1. Locate the visible range, padded by one sample on each side so
    //    lines into just-off-screen points don't appear clipped while panning.
    auto lowIt = std::lower_bound(xData.begin(), xData.end(), xMin);
    auto highIt = std::upper_bound(xData.begin(), xData.end(), xMax);

    size_t begin = (lowIt != xData.begin())
      ? static_cast<size_t>((lowIt - xData.begin()) - 1)
      : 0;
    size_t end = (highIt != xData.end())
      ? static_cast<size_t>((highIt - xData.begin()) + 1)
      : n;
    end = std::min(end, n);
    if (begin >= end) return result;

    const size_t visibleCount = end - begin;

    // Already under budget -- return the raw slice, no decimation needed.
    if (visibleCount <= static_cast<size_t>(targetPoints)) {
      result.x.assign(xData.begin() + begin, xData.begin() + end);
      result.y.assign(yData.begin() + begin, yData.begin() + end);
      return result;
    }

    result.x.reserve(static_cast<size_t>(targetPoints) + 8);
    result.y.reserve(static_cast<size_t>(targetPoints) + 8);

    // 2. Split into contiguous NaN-free runs so a genuine data gap never
    //    gets bridged by a decimated line, and give each run a share of
    //    the point budget proportional to its length.
    size_t runStart = begin;
    while (runStart < end) {
      if (std::isnan(yData[runStart])) {
        if (!result.y.empty() && !std::isnan(result.y.back())) {
          result.x.push_back(xData[runStart]);
          result.y.push_back(std::numeric_limits<double>::quiet_NaN());
        }
        ++runStart;
        continue;
      }

      size_t runEnd = runStart;
      while (runEnd < end && !std::isnan(yData[runEnd])) ++runEnd;

      const size_t runLen = runEnd - runStart;
      const int runBudget = std::max(3, static_cast<int>(
        (static_cast<double>(runLen) / static_cast<double>(visibleCount)) * targetPoints));

      lttbRun(xData, yData, runStart, runEnd, runBudget, result.x, result.y);

      runStart = runEnd;
    }

    return result;
  }

} // namespace engine