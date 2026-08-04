#include "core/logsession.h"

#include <algorithm>
#include <utility>

namespace core {

  void LogSession::addChannel(Channel channel)
  {
    channels_.push_back(std::move(channel));
  }

  const Channel *LogSession::findChannel(const std::string &name) const
  {
    for (const auto &channel : channels_) {
      if (channel.name() == name) {
        return &channel;
      }
    }
    return nullptr;
  }

  const std::vector<double> *LogSession::timeSec() const
  {
    if (!timeChannelIndex_.has_value() || *timeChannelIndex_ >= channels_.size()) {
      return nullptr;
    }
    return &channels_[*timeChannelIndex_].values();
  }

  void LogSession::setCropRange(double startSec, double endSec)
  {
    if (startSec > endSec) std::swap(startSec, endSec);
    cropRange_.startSec = startSec;
    cropRange_.endSec = endSec;
    cropRange_.active = true;
  }

  void LogSession::resetCropRange()
  {
    cropRange_.active = false;
  }

  size_t LogSession::cropStartIndex() const
  {
    const auto *time = timeSec();
    if (!cropRange_.active || !time || time->empty()) return 0;
    auto it = std::lower_bound(time->begin(), time->end(), cropRange_.startSec);
    return static_cast<size_t>(std::distance(time->begin(), it));
  }

  size_t LogSession::cropEndIndex() const
  {
    const auto *time = timeSec();
    if (!cropRange_.active || !time || time->empty()) return rowCount_ > 0 ? rowCount_ - 1 : 0;
    auto it = std::upper_bound(time->begin(), time->end(), cropRange_.endSec);
    size_t idx = static_cast<size_t>(std::distance(time->begin(), it));
    return (idx > 0) ? idx - 1 : 0;
  }

  bool LogSession::isRowInCropRange(size_t rowIndex) const
  {
    if (!cropRange_.active) return true;
    return (rowIndex >= cropStartIndex() && rowIndex <= cropEndIndex());
  }

} // namespace core