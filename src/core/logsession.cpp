#include "core/logsession.h"

#include <utility>

namespace core {

  void LogSession::addChannel(Channel channel) {
    channels_.push_back(std::move(channel));
  }

  const Channel *LogSession::findChannel(const std::string &name) const {
    for (const auto &channel : channels_) {
      if (channel.name() == name) {
        return &channel;
      }
    }
    return nullptr;
  }

  const std::vector<double> *LogSession::timeSec() const {
    if (!timeChannelIndex_.has_value() || *timeChannelIndex_ >= channels_.size()) {
      return nullptr;
    }
    return &channels_[*timeChannelIndex_].values();
  }

} // namespace core