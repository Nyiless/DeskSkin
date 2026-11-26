#include <cstdio>
#include <cstdlib>
#include "config/Settings.h"
using namespace deskskin::config;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); std::exit(1);} } while (0)
int main() {
  const std::filesystem::path p = std::filesystem::temp_directory_path() / "dskin_settings_test" / "settings.ini";
  std::filesystem::remove_all(p.parent_path());
  Settings a; a.SetPath(p);
  CHECK(!a.Load());                                   // missing file is not an error for the app
  auto& d = a.Skin("Demo/Demo.ini");
  d.hasPosition = true; d.x = -1920; d.y = 40; d.locked = true;
  a.Skin("Clock/Clock.ini").enabled = false;
  a.Skin("Bad[Name]/x.ini").x = 1;                    // unrepresentable key: must be skipped, not corrupt the file
  a.App().use24h = false; a.App().showDate = false; a.App().accent = 2; a.App().autoHideOnIdle = true; a.App().timeStyle = "NeonClock";
  a.Options("astro-weather")["Accent"] = "1,2,3,255"; a.Options("illustro-disk1")["Drive"] = "D";
  a.Shared()["shared.city"] = "Paris"; a.Shared()["shared.feedUrl"] = "https://example.com/rss?a=1;b=2";
  a.Favorites() = {"astro-weather", "rainify"};
  CHECK(a.Save());
  Settings b; b.SetPath(p);
  CHECK(b.Load());
  auto s = b.Get("Demo/Demo.ini");
  CHECK(s.hasPosition && s.x == -1920 && s.y == 40 && s.locked && s.enabled && !s.clickThrough);
  CHECK(!b.Get("Clock/Clock.ini").enabled);
  CHECK(b.OptionsOf("astro-weather").at("Accent") == "1,2,3,255" && b.OptionsOf("illustro-disk1").at("Drive") == "D");
  CHECK(b.Shared().at("shared.city") == "Paris" && b.Shared().at("shared.feedUrl") == "https://example.com/rss?a=1;b=2");
  CHECK(b.Favorites().size() == 2 && b.Favorites().count("rainify") == 1 && b.OptionsOf("none").empty());
  CHECK(!b.Get("Unknown/x.ini").hasPosition && b.Get("Unknown/x.ini").enabled);
  CHECK(!b.App().use24h && !b.App().showDate && b.App().accent == 2 && b.App().autoHideOnIdle && !b.App().performanceMode && b.App().timeStyle == "NeonClock");
  b.ResetPositions(); CHECK(b.Save());
  Settings c; c.SetPath(p); CHECK(c.Load()); CHECK(!c.Get("Demo/Demo.ini").hasPosition && c.Get("Demo/Demo.ini").locked);
  CHECK(!std::filesystem::exists(p.string() + ".tmp"));
  std::puts("settings_tests: all passed");
}
