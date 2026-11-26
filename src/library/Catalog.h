// The built-in Widget Library: every entry is a real, working DeskSkin skin (or system feature)
// compiled into the program, so "Install" needs no download. Generated from tools/library_spec.py.
#pragma once
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace deskskin::library {

enum class Category { SystemMonitoring, Productivity, Weather, Music, Visualizers, Appearance, Utilities };

// A user-editable option. `choices` is "Label=Value|Label=Value" (empty = free text). Keys starting with
// "shared." are shared between widgets (e.g. the weather city); every other key is passed to the skin as
// the ini variable #Key# (or read directly for features).
struct OptionDef {
  const char* key;
  const char* label;
  const char* choices;
  const char* def;  // default choice LABEL, or default text
  bool shared;
};

struct WidgetDef {
  const char* id;
  const char* name;
  Category category;
  const char* description;
  const char* author;
  const char* source;    // attribution / inspiration
  const char* needs;     // what it needs to be useful ("" = nothing)
  bool feature;          // true = system feature (no skin file)
  const char* ini;       // skin text (nullptr for features)
  std::vector<OptionDef> options;
};

const std::vector<WidgetDef>& All();
const WidgetDef* Find(std::string_view id);
const char* CategoryName(Category c);

std::vector<std::pair<std::string, std::string>> Choices(const OptionDef& o);  // label -> value
// Effective value of an option: `stored` (label for choices, text otherwise) or the default, resolved to the value.
std::string ResolveValue(const OptionDef& o, const std::string& stored);
// Label to show for an option given its stored value.
std::string DisplayValue(const OptionDef& o, const std::string& stored);
// Next choice label after the one currently stored (wraps around).
std::string NextChoice(const OptionDef& o, const std::string& stored);

}  // namespace deskskin::library
