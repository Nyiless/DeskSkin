#include "library/Catalog.h"

namespace deskskin::library {

const WidgetDef* Find(std::string_view id) {
  for (const auto& w : All())
    if (id == w.id) return &w;
  return nullptr;
}

const char* CategoryName(Category c) {
  switch (c) {
    case Category::SystemMonitoring: return "System Monitoring";
    case Category::Productivity: return "Productivity";
    case Category::Weather: return "Weather";
    case Category::Music: return "Music";
    case Category::Visualizers: return "Visualizers";
    case Category::Appearance: return "Appearance";
    case Category::Utilities: return "Utilities";
  }
  return "";
}

std::vector<std::pair<std::string, std::string>> Choices(const OptionDef& o) {
  std::vector<std::pair<std::string, std::string>> out;
  std::string_view s = o.choices ? o.choices : "";
  while (!s.empty()) {
    const std::size_t bar = s.find('|');
    const std::string_view item = s.substr(0, bar);
    const std::size_t eq = item.find('=');
    if (eq == std::string_view::npos) out.emplace_back(std::string(item), std::string(item));
    else out.emplace_back(std::string(item.substr(0, eq)), std::string(item.substr(eq + 1)));
    if (bar == std::string_view::npos) break;
    s.remove_prefix(bar + 1);
  }
  return out;
}

std::string DisplayValue(const OptionDef& o, const std::string& stored) {
  const std::string v = stored.empty() ? std::string(o.def) : stored;
  return v;
}

std::string ResolveValue(const OptionDef& o, const std::string& stored) {
  const std::string v = DisplayValue(o, stored);
  const auto ch = Choices(o);
  if (ch.empty()) return v;
  for (const auto& [label, value] : ch)
    if (label == v) return value;
  return ch.front().second;
}

std::string NextChoice(const OptionDef& o, const std::string& stored) {
  const auto ch = Choices(o);
  if (ch.empty()) return stored;
  const std::string cur = DisplayValue(o, stored);
  for (std::size_t i = 0; i < ch.size(); ++i)
    if (ch[i].first == cur) return ch[(i + 1) % ch.size()].first;
  return ch.front().first;
}

}  // namespace deskskin::library
