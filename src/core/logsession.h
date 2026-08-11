#pragma once

#include <optional>
#include <string>
#include <vector>

#include "3rdparty/nlohmann/json_fwd.hpp"

#include "core/channel.h"
#include "core/regime.h"

namespace core {

  class LogSession;

  struct ChannelMapping {
    std::string rpm = "RPM";
    std::string load = "MAP";
    std::string afr = "AFR";
    std::string tps = "TPS";
    std::string tpsDot = "TPSdot";
    std::string clt = "CLT";
    std::string egt1 = "EGT1";
    std::string egt2 = "EGT2";
    std::string pw = "PW";
    std::string timing = "Ignition Timing";
    std::string mat = "MAT";
    std::string duty1 = "Duty Cycle1";
    std::string duty2 = "Duty Cycle2";

    static std::vector<std::string> allSlots();
    std::string &refSlot(const std::string &slotName);

    // Auto-detects defaults from a loaded log session
    void autoDetect(const LogSession &session);

    nlohmann::json toJson() const;
    static ChannelMapping fromJson(const nlohmann::json &j);
  };

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

    const ChannelMapping &channelMapping() const { return channelMapping_; }
    void setChannelMapping(ChannelMapping mapping) { channelMapping_ = std::move(mapping); }

    std::vector<RegimeSummary> &regimeSummaries() { return regimeSummaries_; }
    const std::vector<RegimeSummary> &regimeSummaries() const { return regimeSummaries_; }
    void setRegimeSummaries(std::vector<RegimeSummary> summaries) { regimeSummaries_ = std::move(summaries); }

  private:
    std::vector<Channel> channels_;
    ChannelMapping channelMapping_;
    size_t rowCount_ = 0;
    std::optional<size_t> timeChannelIndex_;

    std::string sourcePath_;
    std::string formatInfo_;
    std::string captureDate_;

    TimeCropRange cropRange_;
    std::vector<RegimeSummary> regimeSummaries_;
  };

} // namespace core