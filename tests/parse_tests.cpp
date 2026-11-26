#include <cmath>
#include <cstdio>
#include <cstdlib>
#include "measures/Parse.h"
using namespace deskskin::measures::parse;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); std::exit(1);} } while (0)
int main() {
  const char* wx = R"({"latitude":48.8,"current_units":{"temperature_2m":"°C"},"current":{"time":"2026-11-24T10:00","temperature_2m":8.4,"apparent_temperature":5.1,"weather_code":61,"wind_speed_10m":22.0,"precipitation":0.4},"daily":{"time":["2026-11-24","2026-11-25"],"temperature_2m_max":[10.1,null],"weather_code":[61,3]}})";
  auto cur = JsonObject(wx, "current");
  CHECK(!cur.empty() && *JsonNumber(cur, "temperature_2m") == 8.4);          // not the "°C" in current_units
  CHECK(*JsonNumber(cur, "weather_code") == 61 && !JsonNumber(cur, "nothing"));
  auto daily = JsonObject(wx, "daily");
  auto hi = JsonNumberArray(daily, "temperature_2m_max");
  CHECK(hi.size() == 2 && hi[0] == 10.1 && hi[1] == 0.0);
  auto days = JsonStringArray(daily, "time");
  CHECK(days.size() == 2 && WeekdayShort(days[0]) == "Tue" && WeekdayShort("2026-11-25") == "Wed");
  CHECK(*JsonString(R"({"city":"San \"Jose\"","x":1})", "city") == "San \"Jose\"");
  CHECK(std::string(WeatherCondition(2)) == "Partly cloudy" && std::string(WeatherCondition(1234)) == "Unknown");

  auto j1 = Jacket(5.0, 0, 5, 0, false);   CHECK(j1.needed && j1.why == "Feels like 5\xC2\xB0""C");
  auto j2 = Jacket(22.0, 0, 5, 1, false);  CHECK(!j2.needed);
  auto j3 = Jacket(20.0, 1.0, 5, 61, true); CHECK(j3.needed && j3.why == "Rain");
  auto j4 = Jacket(10.0, 0, 50, 0, true);  CHECK(j4.needed && j4.why.find("50\xC2\xB0""F") != std::string::npos);

  const char* rss = R"(<?xml version="1.0"?><rss><channel><title>My &amp; Feed</title><link>http://x</link>
    <item><title><![CDATA[Hello <b>world</b> & more]]></title><link>http://a/1</link></item>
    <item><title>Second &quot;one&quot; &#8211; ok</title><link>http://a/2</link></item></channel></rss>)";
  auto f = ParseFeed(rss, 5);
  CHECK(f.title == "My & Feed" && f.items.size() == 2);
  CHECK(f.items[0].title == "Hello <b>world</b> & more" && f.items[0].link == "http://a/1");
  CHECK(f.items[1].title == "Second \"one\" \xE2\x80\x93 ok");
  const char* atom = R"(<feed><title>A</title><entry><title type="html">T1</title><link rel="alternate" href="http://b/1?x=1&amp;y=2"/></entry><entry><title>T2</title><link href="http://b/2"/></entry></feed>)";
  auto fa = ParseFeed(atom, 1);
  CHECK(fa.title == "A" && fa.items.size() == 1 && fa.items[0].title == "T1" && fa.items[0].link == "http://b/1?x=1&y=2");
  CHECK(ParseFeed("<html>not a feed</html>", 5).items.empty());

  CHECK(FormatBytes(0) == "0 B" && FormatBytes(1536) == "1.5 KB" && FormatRate(3.5 * 1024 * 1024) == "3.5 MB/s");
  CHECK(UrlEncode("a b&c/\xC3\xA9") == "a%20b%26c%2F%C3%A9");

  // 1 kHz sine at 48 kHz must light the band containing 1 kHz far more than the 8 kHz band.
  const int N = 1024, B = 32; float s[N], out[B];
  for (int i = 0; i < N; ++i) s[i] = 0.8f * std::sin(2.0f * 3.14159265f * 1000.0f * i / 48000.0f);
  BandsFromSamples(s, N, 48000.0f, B, out);
  int peak = 0; for (int b = 1; b < B; ++b) if (out[b] > out[peak]) peak = b;
  const float f0 = 50.0f * std::pow(240.0f, (float)peak / B), f1 = 50.0f * std::pow(240.0f, (float)(peak + 1) / B);
  CHECK(f0 <= 1100.0f && f1 >= 900.0f);
  CHECK(out[peak] > 0.7f && out[B - 2] < 0.2f);
  float silent[N] = {0}; BandsFromSamples(silent, N, 48000.0f, B, out);
  for (int b = 0; b < B; ++b) CHECK(out[b] == 0.0f);
  std::puts("parse_tests: all passed");
}
