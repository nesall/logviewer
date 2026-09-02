#include "utils.h"
#include <cmath>
#include <cstdlib>
#include <utility>
#include <filesystem>
#include <random>


namespace {
  // Days before the start of each month in non-leap years
  constexpr int kDaysBeforeMonth[12] = {
    0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
  };

  bool parseFixed2(const char *p, int &out) {
    uint8_t d0 = static_cast<uint8_t>(p[0] - '0');
    uint8_t d1 = static_cast<uint8_t>(p[1] - '0');
    if (d0 > 9 || d1 > 9) return false;
    out = d0 * 10 + d1;
    return true;
  }

  bool parseFixed4(const char *p, int &out) {
    uint8_t d0 = static_cast<uint8_t>(p[0] - '0');
    uint8_t d1 = static_cast<uint8_t>(p[1] - '0');
    uint8_t d2 = static_cast<uint8_t>(p[2] - '0');
    uint8_t d3 = static_cast<uint8_t>(p[3] - '0');
    if (d0 > 9 || d1 > 9 || d2 > 9 || d3 > 9) return false;
    out = d0 * 1000 + d1 * 100 + d2 * 10 + d3;
    return true;
  }
} // anonymous namespace


std::vector<std::string> utils::str::splitCsvLine(const std::string &line)
{
  std::vector<std::string> tokens;
  size_t start = 0;
  while (true) {
    size_t commaPos = line.find(',', start);
    if (commaPos == std::string::npos) {
      tokens.push_back(line.substr(start));
      break;
    }
    tokens.push_back(line.substr(start, commaPos - start));
    start = commaPos + 1;
  }
  return tokens;
}

void utils::str::splitByDelimiter(std::string_view line, std::vector<std::string_view> &out, char delimiter)
{
  out.clear();
  size_t start = 0;
  while (true) {
    size_t tabPos = line.find(delimiter, start);
    if (tabPos == std::string_view::npos) { out.push_back(line.substr(start)); break; }
    out.push_back(line.substr(start, tabPos - start));
    start = tabPos + 1;
  }
}

std::string utils::str::toLower(const std::string & s)
{
  std::string result = s;
  for (char &c : result) {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return result;
}

std::string utils::str::sanitizeToUtf8(const std::string &input)
{
  std::string output;
  output.reserve(input.size() * 2);
  for (size_t i = 0; i < input.size(); ++i) {
    unsigned char c = static_cast<unsigned char>(input[i]);
    // Check if it's a raw single-byte degree symbol (0xB0) in ANSI/Latin-1/Win-1252
    // and make sure it's not already part of a valid UTF-8 multi-byte sequence (0xC2 0xB0)
    if (c == 0xB0) {
      bool isAlreadyUtf8 = (i > 0 && static_cast<unsigned char>(input[i - 1]) == 0xC2);
      if (!isAlreadyUtf8) {
        output.push_back(static_cast<char>(0xC2)); // Inject UTF-8 leading byte
      }
    }
    output.push_back(input[i]);
  }
  return output;
}

std::string utils::str::generateUniqueId(size_t length)
{
  static const char charset[] =
    "0123456789"
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz";
  static std::mt19937 rng(std::random_device{}());
  static std::uniform_int_distribution<> dist(0, sizeof(charset) - 2);
  std::string id;
  id.reserve(length);
  for (size_t i = 0; i < length; ++i) {
    id += charset[dist(rng)];
  }
  return id;
}

bool utils::parse::parseNumericToken(std::string_view token, double &outValue)
{
  if (token.empty()) {
    return false;
  }
  char *end = nullptr;
  auto [ptr, ec] = std::from_chars(token.data(), token.data() + token.size(), outValue);
  return ec == std::errc{};
}

bool utils::parse::parseNumericTokenFast(std::string_view token, double &outValue)
{
  if (token.empty()) return false;

  const char *p = token.data();
  const char *end = p + token.size();

  bool negative = false;
  if (*p == '-') { negative = true; ++p; } else if (*p == '+') { ++p; }

  if (p == end) return false;

  uint64_t mantissa = 0;
  int digits = 0;
  int exponent = 0;
  bool seenDigit = false;
  bool seenDot = false;

  for (; p != end; ++p) {
    char c = *p;
    if (c == '.') {
      if (seenDot) return false;   // malformed: two dots
      seenDot = true;
      continue;
    }
    if (c < '0' || c > '9') return false;  // bail -> caller falls back to from_chars
    if (digits < 19) {                     // uint64_t safe up to ~19 digits
      mantissa = mantissa * 10 + static_cast<uint64_t>(c - '0');
      ++digits;
      if (seenDot) --exponent;
    }
    // else: silently drop excess precision (telemetry data won't need it)
    seenDigit = true;
  }

  if (!seenDigit) return false;

  double result = static_cast<double>(mantissa);
  if (exponent != 0) {
    // small power-of-10 table beats pow() for the common range
    static constexpr double kPow10[] = {
      1e0, 1e-1, 1e-2, 1e-3, 1e-4, 1e-5, 1e-6, 1e-7, 1e-8, 1e-9
    };
    if (exponent >= -9 && exponent <= 0) {
      result *= kPow10[-exponent];
    } else {
      result *= std::pow(10.0, exponent);  // rare fallback for unusual formats
    }
  }

  outValue = negative ? -result : result;
  return true;
}

bool utils::parse::tryParseBooleanToken(std::string_view token, double &outValue)
{
  auto ieq = [](std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
      if (std::tolower(static_cast<unsigned char>(a[i])) !=
        std::tolower(static_cast<unsigned char>(b[i]))) {
        return false;
      }
    }
    return true;
    };
  switch (token.size()) {
  case 2: if (ieq(token, "no")) { outValue = 0; return true; }
        if (ieq(token, "on")) { outValue = 1; return true; }
        return false;
  case 3: if (ieq(token, "yes")) { outValue = 1; return true; }
        if (ieq(token, "off")) { outValue = 0; return true; }
        if (ieq(token, "low")) { outValue = 0; return true; }
        return false;
  case 4: if (ieq(token, "high")) { outValue = 1; return true; }
        if (ieq(token, "true")) { outValue = 1; return true; }
        return false;
  case 6: if (ieq(token, "active")) { outValue = 1; return true; }
        return false;
  case 5: if (ieq(token, "false")) { outValue = 0; return true; }
        return false;
  case 8: if (ieq(token, "inactive")) { outValue = 0; return true; }
        return false;
  default: return false;
  }
  return false;
}

std::string utils::path::fileNameOnly(const std::string &path)
{
  size_t pos = path.find_last_of("/\\");
  return (pos == std::string::npos) ? path : path.substr(pos + 1);
}

std::string utils::path::fileNameWithoutExtension(const std::string &path)
{
  std::string name = fileNameOnly(path);
  size_t dot = name.find_last_of('.');
  return (dot == std::string::npos) ? name : name.substr(0, dot);
}

bool utils::time::tryParseIso8601ToEpoch(std::string_view token, double &outEpochSec)
{
  // Expected minimal format: "YYYY-MM-DDTHH:MM:SS" (19 chars)
  if (token.size() < 19) return false;
  const char *p = token.data();

  if (p[4] != '-' || p[7] != '-' || (p[10] != 'T' && p[10] != ' ') ||
    p[13] != ':' || p[16] != ':') {
    return false;
  }

  int year = 0, month = 0, day = 0, hour = 0, min = 0, sec = 0;
  if (!parseFixed4(p, year)) return false;
  if (!parseFixed2(p + 5, month) || month < 1 || month > 12) return false;
  if (!parseFixed2(p + 8, day) || day < 1 || day > 31) return false;
  if (!parseFixed2(p + 11, hour) || hour > 23) return false;
  if (!parseFixed2(p + 14, min) || min > 59) return false;
  if (!parseFixed2(p + 17, sec) || sec > 60) return false;

  // Fast calendar math (Civil date to Days since 1970-01-01)
  int y = year - (month <= 2 ? 1 : 0);
  int era = (y >= 0 ? y : y - 399) / 400;
  unsigned yoe = static_cast<unsigned>(y - era * 400);
  unsigned m = static_cast<unsigned>(month + (month > 2 ? -3 : 9));
  unsigned doy = (153 * m + 2) / 5 + day - 1;
  unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
  int64_t daysSinceEpoch = static_cast<int64_t>(era) * 146097 + static_cast<int64_t>(doe) - 719468;

  int64_t totalSeconds = daysSinceEpoch * 86400LL + hour * 3600LL + min * 60LL + sec;

  // Parse fractional seconds if present (.sss)
  double fracSec = 0.0;
  if (token.size() > 20 && p[19] == '.') {
    double div = 10.0;
    size_t idx = 20;
    while (idx < token.size() && p[idx] >= '0' && p[idx] <= '9') {
      fracSec += (p[idx] - '0') / div;
      div *= 10.0;
      ++idx;
    }
  }

  outEpochSec = static_cast<double>(totalSeconds) + fracSec;
  return true;
}

std::string utils::time::formatEpochToIso8601(double epochSec)
{
  if (std::isnan(epochSec)) return "";
  time_t fullSec = static_cast<time_t>(std::floor(epochSec));
  int ms = static_cast<int>(std::round((epochSec - static_cast<double>(fullSec)) * 1000.0));
  if (ms >= 1000) { fullSec += 1; ms -= 1000; }

  std::tm tm{};
#ifdef _WIN32
  gmtime_s(&tm, &fullSec);
#else
  gmtime_r(&fullSec, &tm);
#endif

  char buf[32];
  std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
    tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
    tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
  return std::string(buf);
}
