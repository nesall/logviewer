#include "ui/plotpanel.h"

#include <utility>

#include "3rdparty/nlohmann/json.hpp"

namespace ui {

  PlotPanel::PlotPanel(std::string title)
    : title_(std::move(title))
  {
  }

  nlohmann::json PlotPanel::saveState() const
  {
    return nlohmann::json{ {"title", title_} };
  }

  void PlotPanel::loadState(const nlohmann::json & state)
  {
    if (state.contains("title") && state["title"].is_string()) {
      title_ = state["title"].get<std::string>();
    }
  }

} // namespace ui