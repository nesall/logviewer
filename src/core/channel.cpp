#include "core/channel.h"

#include <utility>

namespace core {

  Channel::Channel(std::string name, std::string unit)
    : name_(std::move(name)), unit_(std::move(unit)) {}

} // namespace core