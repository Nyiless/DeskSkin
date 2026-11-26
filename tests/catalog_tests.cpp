#include <cstdio>
#include <set>
#include "config/IniParser.h"
#include "library/Catalog.h"
using namespace deskskin;
int main() {
  int bad = 0, n = 0;
  const std::set<std::string> kinds = {"String","Bar","Histogram","Bars","Hex","Calendar"};
  for (const auto& w : library::All()) {
    if (w.feature) continue;
    ++n;
    // overrides built from default options, exactly like the app does
    ini::ParseOptions opt;
    for (const auto& o : w.options) if (!o.shared) opt.overrides.emplace_back(o.key, library::ResolveValue(o, ""));
    std::vector<ini::Diagnostic> d;
    auto doc = ini::Document::Parse(w.ini, opt, &d);
    if (!d.empty()) { std::printf("%s: %zu parse diagnostics\n", w.id, d.size()); ++bad; }
    const double W = doc.GetDouble("Skin","Width",0), H = doc.GetDouble("Skin","Height",0);
    if (W < 8 || H < 8) { std::printf("%s: bad size\n", w.id); ++bad; }
    for (size_t i = 0; i < doc.SectionCount(); ++i) {
      auto s = doc.SectionName(i);
      if (s == "Skin" || s == "Variables") continue;
      auto k = doc.Find(s, "Meter");
      if (!k || !kinds.count(std::string(*k))) { std::printf("%s [%.*s]: bad Meter\n", w.id, (int)s.size(), s.data()); ++bad; continue; }
      double x = doc.GetDouble(s,"X"), y = doc.GetDouble(s,"Y"), mw = doc.GetDouble(s,"W",100), mh = doc.GetDouble(s,"H",24);
      if (x < 0 || y < 0 || x + mw > W + 0.01 || y + mh > H + 0.01) { std::printf("%s [%.*s]: outside skin %.1f,%.1f %.1fx%.1f in %.0fx%.0f\n", w.id, (int)s.size(), s.data(), x,y,mw,mh,W,H); ++bad; }
      for (auto key : {"Text","Source","LeftMouseUpAction"}) { auto v = doc.Find(s,key); if (v && v->find('#') != std::string_view::npos) { std::printf("%s [%.*s]: unexpanded # in %s: %.*s\n", w.id,(int)s.size(),s.data(),key,(int)v->size(),v->data()); ++bad; } }
    }
  }
  // every id unique, every option has a default that resolves, every skin contains the standard variables
  std::set<std::string> ids;
  for (const auto& w : library::All()) {
    if (!ids.insert(w.id).second) { std::printf("duplicate id %s\n", w.id); ++bad; }
    for (const auto& o : w.options) {
      const auto ch = library::Choices(o);
      if (!ch.empty()) {
        bool found = false;
        for (const auto& c : ch) found = found || c.first == o.def;
        if (!found) { std::printf("%s option %s: default '%s' is not one of its choices\n", w.id, o.key, o.def); ++bad; }
      }
    }
    if (!w.feature && std::string(w.ini).find("#Accent#") == std::string::npos) { std::printf("%s ignores the accent option\n", w.id); ++bad; }
  }
  if (n < 20) { std::printf("catalog too small: %d\n", n); ++bad; }
  std::printf("catalog_tests: %d skins checked, %d problems\n", n, bad);
  return bad;
}
