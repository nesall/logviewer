#include <vector>
#include <string>
#include <string_view>


namespace utils {
  namespace str {
    std::vector<std::string> splitCsvLine(const std::string &line);
    void splitByDelimiter(std::string_view line, std::vector<std::string_view> &out, char delimiter = '\t');
    std::string toLower(const std::string &s);
    std::string sanitizeToUtf8(const std::string &input);
    std::string generateUniqueId(size_t length = 8);
  }

  namespace parse {
    bool parseNumericToken(std::string_view token, double &outValue);
    bool parseNumericTokenFast(std::string_view token, double &outValue);
    bool tryParseBooleanToken(std::string_view token, double &outValue);
  }

  namespace path {
    std::string fileNameOnly(const std::string &path);
    std::string fileNameWithoutExtension(const std::string &path);
  }
}