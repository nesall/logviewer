#pragma once

#include <vector>

namespace engine {

  // Parallel x/y arrays ready to hand straight to ImPlot::PlotLine.
  struct DecimatedSeries {
    std::vector<double> x;
    std::vector<double> y;
  };

  class Decimator {
  public:
    // Downsamples the portion of (xData, yData) falling within
    // [xMin, xMax] (padded by one sample on each side so panned lines
    // don't visibly clip at the plot edge) to approximately targetPoints
    // points using Largest-Triangle-Three-Buckets.
    //
    // xData/yData must be the same length and xData must be sorted
    // ascending (true for any Channel's values against LogSession's
    // timeSec). NaN samples in yData are treated as gaps: the visible
    // slice is split into contiguous non-NaN runs, each run is decimated
    // independently (proportional share of targetPoints), and a single
    // NaN is re-inserted between runs so ImPlot still draws a break.
    //
    // If the visible slice already has <= targetPoints samples, it's
    // returned unchanged -- no decimation, no bucket math.
    static DecimatedSeries decimate(
      const std::vector<double> &xData,
      const std::vector<double> &yData,
      double xMin, double xMax,
      int targetPoints,
      const std::vector<size_t> &discontinuityIndices);

  private:
    // LTTB over a single contiguous, NaN-free run [begin, end) of indices
    // into xData/yData. Always keeps the run's first and last sample.
    // targetPoints is clamped to >= 3 (LTTB needs first + >=1 interior +
    // last to do bucket selection at all).
    static void lttbRun(
      const std::vector<double> &xData,
      const std::vector<double> &yData,
      size_t begin, size_t end,
      int targetPoints,
      std::vector<double> &outX,
      std::vector<double> &outY);
  };

} // namespace engine