
#include "io/megasquirtcsvparser.h"
#include "utils/utils.h"

#include <cmath>
#include <cstdlib>
#include <fstream>
#include <limits>
#include <utility>
#include <filesystem>

namespace io {

  namespace {

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

    std::string buffer(fileSize, '\0');
    file.read(buffer.data(), static_cast<std::streamsize>(fileSize));

    std::string formatInfo;
    std::string captureDate;
    std::string headerLine;
    std::vector<std::string_view> headerTokens;
    bool foundHeader = false;
    uint64_t bytesRead = 0;

    size_t pos = 0;
    while (pos < buffer.size()) {
      size_t nl = buffer.find('\n', pos);
      std::string_view line = (nl == std::string_view::npos)
        ? std::string_view(buffer).substr(pos)
        : std::string_view(buffer).substr(pos, nl - pos);

      if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
      pos = (nl == std::string_view::npos) ? buffer.size() : nl + 1;

      bytesRead += line.size() + 1; // +1 for newline character
      if (line.empty()) {
        continue;
      }

      if (line.front() == '"') {
        std::string_view content = line;
        if (content.size() >= 2 && content.back() == '"') {
          content = content.substr(1, content.size() - 2);
        }
        if (content.rfind("Capture Date:", 0) == 0) {
          captureDate = std::string(content);
        } else if (formatInfo.empty()) {
          formatInfo = std::string(content);
        }
        continue;
      }
      if (line == "#") {
        continue;
      }
      headerLine = line;
      utils::str::splitTabDelimited(headerLine, headerTokens);
      foundHeader = true;
      break;
    }

    if (!foundHeader || headerTokens.empty()) {
      errorOut = "Could not find header row in: " + path;
      return false;
    }

    if (buffer.size() <= pos) {
      errorOut = "File ended after header row (missing units row): " + path;
      return false;
    }

    size_t nl = buffer.find('\n', pos);
    std::string_view line = (nl == std::string_view::npos)
      ? std::string_view(buffer).substr(pos)
      : std::string_view(buffer).substr(pos, nl - pos);
    pos = (nl == std::string_view::npos) ? buffer.size() : nl + 1;
    bytesRead += line.size() + 1;
    std::vector<std::string_view> unitTokens;
    utils::str::splitTabDelimited(line, unitTokens);

    const size_t columnCount = headerTokens.size();

    std::vector<core::Channel> channels;
    channels.reserve(columnCount);
    for (size_t i = 0; i < columnCount; ++i) {
      std::string unit = (i < unitTokens.size()) ? std::string(unitTokens[i]) : std::string();
      channels.emplace_back(std::string(headerTokens[i]), unit);
    }
    bool channelRowsEstimated = false;

    size_t rowCount = 0;
    std::vector<uint8_t> sawNumericToken(columnCount, 0);
    std::vector<uint8_t> sawBooleanToken(columnCount, 0);
    std::vector<std::string_view> tokens;

    while (pos < buffer.size()) {
      size_t nl = buffer.find('\n', pos);
      std::string_view line = (nl == std::string_view::npos)
        ? std::string_view(buffer).substr(pos)
        : std::string_view(buffer).substr(pos, nl - pos);

      if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
      pos = (nl == std::string_view::npos) ? buffer.size() : nl + 1;

      bytesRead += line.size() + 1;
      if (line.empty()) {
        continue;
      }

      if (!channelRowsEstimated) {
        size_t estimatedRows = fileSize / std::max<size_t>(line.size(), 1);
        for (auto &channel : channels) channel.values().reserve(estimatedRows);
        channelRowsEstimated = true;
      }

      if (progress && (rowCount % 500 == 0)) {
        float p = static_cast<float>(bytesRead) / static_cast<float>(fileSize);
        progress->store(std::min(p, 1.0f));
      }

      size_t columnIndex = 0;
      size_t start = 0;
      bool rowValid = true;

      while (start <= line.size()) {
        size_t tabPos = line.find('\t', start);
        std::string_view token = (tabPos == std::string_view::npos)
          ? line.substr(start)
          : line.substr(start, tabPos - start);

        if (columnIndex >= columnCount) { rowValid = false; break; }

        double value = std::numeric_limits<double>::quiet_NaN();
        double parsed = 0.0;
        char c = token.empty() ? '\0' : token.front();
        bool looksNumeric = (c == '-' || c == '+' || c == '.' || (c >= '0' && c <= '9'));

        if (looksNumeric) {
          if (utils::parse::parseNumericTokenFast(token, parsed) || utils::parse::parseNumericToken(token, parsed)) {
            value = parsed; sawNumericToken[columnIndex] = 1;
          } else if (!token.empty()) {
            channels[columnIndex].setIsNumeric(false);
          }
        } else if (!token.empty()) {
          if (utils::parse::tryParseBooleanToken(token, parsed)) {
            value = parsed; sawBooleanToken[columnIndex] = 1;
          } else {
            channels[columnIndex].setIsNumeric(false);
          }
        }
        channels[columnIndex].values().push_back(value);

        ++columnIndex;
        if (tabPos == std::string_view::npos) break;
        start = tabPos + 1;
      }

      if (columnIndex != columnCount) {
        // mismatched row (e.g. "MARK" annotation) — need to roll back the
        // partial push_backs you already did for columns 0..columnIndex-1
        for (size_t i = 0; i < columnIndex; ++i) channels[i].values().pop_back();
        continue;
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
