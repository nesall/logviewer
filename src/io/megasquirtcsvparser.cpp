#include "io/megasquirtcsvparser.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <utility>
#include <filesystem>

namespace io {

  namespace {

    std::vector<std::string> splitTabDelimited(const std::string &line) {
      std::vector<std::string> tokens;
      size_t start = 0;
      while (true) {
        size_t tabPos = line.find('\t', start);
        if (tabPos == std::string::npos) {
          tokens.push_back(line.substr(start));
          break;
        }
        tokens.push_back(line.substr(start, tabPos - start));
        start = tabPos + 1;
      }
      return tokens;
    }

    bool parseNumericToken(const std::string &token, double &outValue) {
      if (token.empty()) {
        return false;
      }
      char *end = nullptr;
      outValue = std::strtod(token.c_str(), &end);
      return end != token.c_str() && *end == '\0';
    }

    bool tryParseBooleanToken(std::string token, double &outValue) {
      for (char &c : token) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
      }
      if (token == "yes" || token == "on" || token == "high" || token == "active" ||
        token == "true") {
        outValue = 1.0;
        return true;
      }
      if (token == "no" || token == "off" || token == "low" || token == "inactive" ||
        token == "false") {
        outValue = 0.0;
        return true;
      }
      return false;
    }

    // Observed MSL quirk: the first data row's "Time" value is sometimes an
    // unreliable placeholder (e.g. 0.000) while every following row continues
    // a consistent, much larger-magnitude trend. Detect a first-gap that's
    // wildly out of line with the established row spacing and reconstruct row
    // 0 from row 1's trend instead of trusting it literally.
    void fixFirstRowTimeAnomaly(std::vector<double> &timeValues) {
      if (timeValues.size() < 3) {
        return;
      }
      double delta01 = std::fabs(timeValues[1] - timeValues[0]);
      double delta12 = std::fabs(timeValues[2] - timeValues[1]);
      if (delta12 < 1e-9) {
        return;
      }
      if (delta01 > delta12 * 10.0) {
        timeValues[0] = timeValues[1] - delta12;
      }
    }

  } // namespace

  bool MegasquirtCsvParser::parse(const std::string &path, core::LogSession &outSession, std::string &errorOut, std::atomic<float> *progress) {
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
    std::string formatInfo;
    std::string captureDate;
    std::vector<std::string> headerTokens;
    bool foundHeader = false;
    uint64_t bytesRead = 0;

    // Scan defensively for the header row rather than hardcoding line
    // numbers, in case a future TunerStudio version adds/removes a
    // metadata line.
    while (std::getline(file, line)) {
      bytesRead += line.size() + 1; // +1 for newline character
      if (line.empty()) {
        continue;
      }

      if (line.front() == '"') {
        std::string content = line;
        if (content.size() >= 2 && content.back() == '"') {
          content = content.substr(1, content.size() - 2);
        }
        if (content.rfind("Capture Date:", 0) == 0) {
          captureDate = content;
        } else if (formatInfo.empty()) {
          formatInfo = content;
        }
        continue;
      }
      if (line == "#") {
        continue;
      }
      headerTokens = splitTabDelimited(line);
      foundHeader = true;
      break;
    }

    if (!foundHeader || headerTokens.empty()) {
      errorOut = "Could not find header row in: " + path;
      return false;
    }

    if (!std::getline(file, line)) {
      errorOut = "File ended after header row (missing units row): " + path;
      return false;
    }
    bytesRead += line.size() + 1;
    std::vector<std::string> unitTokens = splitTabDelimited(line);

    const size_t columnCount = headerTokens.size();

    std::vector<core::Channel> channels;
    channels.reserve(columnCount);
    for (size_t i = 0; i < columnCount; ++i) {
      std::string unit = (i < unitTokens.size()) ? unitTokens[i] : std::string();
      channels.emplace_back(headerTokens[i], unit);
    }

    size_t rowCount = 0;
    std::vector<bool> sawNumericToken(columnCount, false);
    std::vector<bool> sawBooleanToken(columnCount, false);

    while (std::getline(file, line)) {
      bytesRead += line.size() + 1;

      if (line.empty()) continue;

      if (progress && (rowCount % 500 == 0)) {
        float p = static_cast<float>(bytesRead) / static_cast<float>(fileSize);
        progress->store(std::min(p, 1.0f));
      }

      std::vector<std::string> tokens = splitTabDelimited(line);

      if (tokens.size() != columnCount) {
        // Not a real data row -- e.g. a "MARK <label>" annotation line
        // TunerStudio inserts when the user hits a mark key during
        // capture. Skip it rather than let a mismatched token corrupt
        // column alignment / flip a channel to non-numeric.
        continue;
      }

      for (size_t i = 0; i < columnCount; ++i) {
        double value = std::numeric_limits<double>::quiet_NaN();
        if (i < tokens.size()) {
          const std::string &token = tokens[i];
          double parsed = 0.0;
          if (parseNumericToken(token, parsed)) {
            value = parsed;
            sawNumericToken[i] = true;
          } else if (tryParseBooleanToken(token, parsed)) {
            value = parsed;
            sawBooleanToken[i] = true;
          } else if (!token.empty()) {
            channels[i].setIsNumeric(false);
          }
        }
        channels[i].values().push_back(value);
      }
      ++rowCount;
    }

    if (progress) progress->store(1.0f);

    int timeChannelIndex = -1;
    for (size_t i = 0; i < channels.size(); ++i) {
      channels[i].setIsBoolean(sawBooleanToken[i] && !sawNumericToken[i]);
      if (channels[i].name() == "Time") {
        timeChannelIndex = static_cast<int>(i);
      }
    }
    if (timeChannelIndex >= 0) {
      fixFirstRowTimeAnomaly(channels[static_cast<size_t>(timeChannelIndex)].values());
    }

    outSession = core::LogSession();
    outSession.setSourcePath(path);
    outSession.setFormatInfo(formatInfo);
    outSession.setCaptureDate(captureDate);
    outSession.setRowCount(rowCount);
    for (auto &channel : channels) {
      outSession.addChannel(std::move(channel));
    }
    if (timeChannelIndex >= 0) {
      outSession.setTimeChannelIndex(static_cast<size_t>(timeChannelIndex));
    }

    return true;
  }

} // namespace io