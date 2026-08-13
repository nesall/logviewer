#include "utils.h"
#include <cmath>
#include <cstdlib>
#include <utility>
#include <filesystem>
#include <random>

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
