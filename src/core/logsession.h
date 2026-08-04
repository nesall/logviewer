#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/channel.h"

namespace core {

  struct TimeCropRange {
    double startSec = 0.0;
    double endSec = 0.0;
    bool active = false;
  };

  class LogSession {
  public:
    void addChannel(Channel channel);

    size_t rowCount() const { return rowCount_; }
    void setRowCount(size_t rowCount) { rowCount_ = rowCount; }

    const std::vector<Channel> &channels() const { return channels_; }
    std::vector<Channel> &channels() { return channels_; }

    const Channel *findChannel(const std::string &name) const;

    const std::vector<double> *timeSec() const;
    void setTimeChannelIndex(std::optional<size_t> index) { timeChannelIndex_ = index; }

    const std::string &sourcePath() const { return sourcePath_; }
    void setSourcePath(std::string path) { sourcePath_ = std::move(path); }

    const std::string &formatInfo() const { return formatInfo_; }
    void setFormatInfo(std::string info) { formatInfo_ = std::move(info); }

    const std::string &captureDate() const { return captureDate_; }
    void setCaptureDate(std::string date) { captureDate_ = std::move(date); }

    // Log Trimming & Region Cropping API
    const TimeCropRange &cropRange() const { return cropRange_; }
    void setCropRange(double startSec, double endSec);
    void resetCropRange();

    size_t cropStartIndex() const;
    size_t cropEndIndex() const;
    bool isRowInCropRange(size_t rowIndex) const;

  private:
    std::vector<Channel> channels_;
    size_t rowCount_ = 0;
    std::optional<size_t> timeChannelIndex_;

    std::string sourcePath_;
    std::string formatInfo_;
    std::string captureDate_;

    TimeCropRange cropRange_;
  };

} // namespace core