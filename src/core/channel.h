#pragma once

#include <string>
#include <vector>

namespace core {

  // A single named data series from a log, aligned 1:1 by row index with
  // every other Channel in the same LogSession.
  class Channel {
  public:
    Channel() = default;
    Channel(std::string name, std::string unit);

    const std::string &name() const { return name_; }
    const std::string &unit() const { return unit_; }

    const std::vector<double> &values() const { return values_; }
    std::vector<double> &values() { return values_; }

    // False if any sample was a token we couldn't parse as a number or a
    // recognized boolean-ish value (Yes/No, On/Off, High/Low,
    // Active/Inactive). Non-numeric channels are still stored (bad samples
    // become NaN) but the UI should treat them as suspect / hide by
    // default rather than plot them as-is.
    bool isNumeric() const { return isNumeric_; }
    void setIsNumeric(bool value) { isNumeric_ = value; }

    // True if every parsed sample in this channel came from a recognized
    // boolean-ish token (never a genuine numeric token). Distinct from
    // isNumeric(): a boolean channel IS numeric (stored as 1.0/0.0), but
    // this flags it as semantically digital/status rather than
    // continuous -- e.g. for StatusPanel's auto-detected default set.
    bool isBoolean() const { return isBoolean_; }
    void setIsBoolean(bool value) { isBoolean_ = value; }

    bool isCustom() const { return isCustom_; }
    void setIsCustom(bool value) { isCustom_ = value; }

    const std::string &formula() const { return formula_; }
    void setFormula(std::string formula) { formula_ = std::move(formula); }

  private:
    std::string name_;
    std::string unit_;
    std::vector<double> values_;
    bool isNumeric_ = true;
    bool isBoolean_ = false;
    bool isCustom_ = false;
    std::string formula_; // Stores original formula text e.g. "[MAP] - 101.3"
  };

} // namespace core