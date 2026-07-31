#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/channel.h"

namespace core {

  // All channels parsed from one log file, plus a bit of file metadata.
  // Every Channel's values() vector is the same length (rowCount()), aligned
  // by row index -- row i across all channels is one sample instant.
  class LogSession {
  public:
    void addChannel(Channel channel);

    size_t rowCount() const { return rowCount_; }
    void setRowCount(size_t rowCount) { rowCount_ = rowCount; }

    const std::vector<Channel> &channels() const { return channels_; }
    std::vector<Channel> &channels() { return channels_; }

    // Returns nullptr if no channel with this exact name exists.
    const Channel *findChannel(const std::string &name) const;

    // Convenience accessor for the parsed/corrected time channel, if the
    // parser identified one (e.g. the "Time" column in an MSL file).
    const std::vector<double> *timeSec() const;
    void setTimeChannelIndex(std::optional<size_t> index) { timeChannelIndex_ = index; }

    const std::string &sourcePath() const { return sourcePath_; }
    void setSourcePath(std::string path) { sourcePath_ = std::move(path); }

    const std::string &formatInfo() const { return formatInfo_; }
    void setFormatInfo(std::string info) { formatInfo_ = std::move(info); }

    const std::string &captureDate() const { return captureDate_; }
    void setCaptureDate(std::string date) { captureDate_ = std::move(date); }

  private:
    std::vector<Channel> channels_;
    size_t rowCount_ = 0;
    std::optional<size_t> timeChannelIndex_;

    std::string sourcePath_;
    std::string formatInfo_;
    std::string captureDate_;
  };

} // namespace core