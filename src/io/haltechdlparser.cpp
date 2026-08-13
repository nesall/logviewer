#include "io/haltechdlparser.h"
#include "utils/utils.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <utility>
#include <filesystem>
#include <sstream>

namespace io {

  namespace {

    // Convert 'HH:MM:SS.mmm' timestamp string into total elapsed seconds
    bool parseHaltechTimeSec(std::string_view token, double &outSec) {
      int h = 0, m = 0;
      double s = 0.0;
      if (std::sscanf(token.data(), "%d:%d:%lf", &h, &m, &s) == 3) {
        outSec = h * 3600.0 + m * 60.0 + s;
        return true;
      }
      return false;
    }

    // Haltech uses sentinel numbers for invalid / disconnected channels
    bool isHaltechSentinel(double val) {
      return val == -2147483647.0 || val == -2147483628.0 ||
        val == -2147483638.0 || val == 8388607.0;
    }

  } // namespace

  bool HaltechDlParser::parse(const std::string &path, core::LogSession &outSession, std::string &errorOut, std::atomic<float> *progress)
  {
    std::error_code ec;
    const uint64_t fileSize = std::filesystem::file_size(path, ec);
    if (ec || fileSize == 0) {
      errorOut = "Could not determine file size or file is empty: " + path;
      return false;
    }

    std::ifstream file(path);
    if (!file.is_open()) {
      errorOut = "Could not open file: " + path;
      return false;
    }

    std::string line;
    std::string formatInfo = "Haltech NSP Log";
    std::string captureDate;
    std::vector<std::string> channelNames;
    uint64_t bytesRead = 0;
    bool inDataBlock = false;

    // Phase 1: Header parsing
    while (std::getline(file, line)) {
      bytesRead += line.size() + 1;
      if (line.empty()) continue;

      if (line.rfind("Software :", 0) == 0) {
        formatInfo = line;
      } else if (line.rfind("DownloadDateTime :", 0) == 0) {
        captureDate = line.substr(19);
      } else if (line.rfind("Channel :", 0) == 0) {
        std::string chName = line.substr(10);
        // Trim trailing CR if Windows line endings
        if (!chName.empty() && chName.back() == '\r') chName.pop_back();
        channelNames.push_back(chName);
      } else if (line.rfind("Log :", 0) == 0) {
        inDataBlock = true;
        break; // Reached data payload
      }
    }

    if (!inDataBlock || channelNames.empty()) {
      errorOut = "Failed to find 'Log :' data header or no channels defined in: " + path;
      return false;
    }

    // Build session channels (1 Time channel + N Data channels)
    std::vector<core::Channel> channels;
    channels.reserve(channelNames.size() + 1);
    channels.emplace_back("Time", "s"); // Injected calculated time channel

    for (const auto &name : channelNames) {
      channels.emplace_back(name, "");
    }

    size_t rowCount = 0;
    double initialTimeSec = -1.0;

    // Phase 2: Parse CSV rows
    while (std::getline(file, line)) {
      bytesRead += line.size() + 1;
      if (line.empty()) continue;

      if (progress && (rowCount % 500 == 0)) {
        float p = static_cast<float>(bytesRead) / static_cast<float>(fileSize);
        progress->store(std::min(p, 1.0f));
      }

      std::vector<std::string_view> tokens;
      utils::str::splitByDelimiter(line, tokens, ',');
      if (tokens.empty()) continue;

      // Token 0: Timestamp (HH:MM:SS.mmm)
      double absTime = 0.0;
      if (!parseHaltechTimeSec(tokens[0], absTime)) {
        continue;
      }

      if (initialTimeSec < 0.0) {
        initialTimeSec = absTime;
      }
      double relativeTimeSec = absTime - initialTimeSec;
      channels[0].values().push_back(relativeTimeSec);

      // Tokens 1..N: Data channels
      for (size_t i = 0; i < channelNames.size(); ++i) {
        double val = std::numeric_limits<double>::quiet_NaN();
        size_t tokenIdx = i + 1;
        if (tokenIdx < tokens.size() && !tokens[tokenIdx].empty()) {
          char *end = nullptr;
          double parsed = std::strtod(tokens[tokenIdx].data(), &end);
          if (end != tokens[tokenIdx].data() && !isHaltechSentinel(parsed)) {
            val = parsed;
          }
        }
        channels[i + 1].values().push_back(val);
      }
      ++rowCount;
    }

    if (progress) progress->store(1.0f);

    outSession = core::LogSession();
    outSession.setSourcePath(path);
    outSession.setFormatInfo(formatInfo);
    outSession.setCaptureDate(captureDate);
    outSession.setRowCount(rowCount);
    outSession.setTimeChannelIndex(0); // Injected time channel is at index 0

    for (auto &channel : channels) {
      outSession.addChannel(std::move(channel));
    }

    return true;
  }

} // namespace io