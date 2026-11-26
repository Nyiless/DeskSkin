// Pure, portable helpers behind the live data sources (no Win32): JSON/RSS/Atom extraction,
// weather-code text, number formatting, jacket advice and the FFT used by the audio visualizers.
#pragma once
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace deskskin::measures::parse {

// --- minimal JSON access (enough for Open-Meteo / ipwho.is responses; not a general parser) ---
std::optional<double> JsonNumber(std::string_view obj, std::string_view key);
std::optional<std::string> JsonString(std::string_view obj, std::string_view key);
std::string_view JsonObject(std::string_view json, std::string_view key);  // "{...}" or empty
std::vector<double> JsonNumberArray(std::string_view obj, std::string_view key);
std::vector<std::string> JsonStringArray(std::string_view obj, std::string_view key);

// --- weather ---
const char* WeatherCondition(int wmoCode);  // "Partly cloudy"
const char* WeatherGlyph(int wmoCode);      // UTF-8 symbol (Segoe UI Symbol): sun, cloud, umbrella, snow, bolt...
std::string WeekdayShort(std::string_view isoDate);  // "2026-11-24" -> "Tue"

struct JacketAdvice {
  bool needed = false;
  std::string why;
};
JacketAdvice Jacket(double feelsLikeC, double precipMm, double windKmh, int wmoCode, bool fahrenheit);

// --- feeds ---
struct FeedItem {
  std::string title, link;
};
struct Feed {
  std::string title;
  std::vector<FeedItem> items;
};
Feed ParseFeed(std::string_view xml, std::size_t maxItems);
std::string DecodeEntities(std::string_view s);

// --- formatting ---
std::string FormatBytes(double bytes);      // "1.2 GB"
std::string FormatRate(double bytesPerSec);  // "3.4 MB/s"
std::string UrlEncode(std::string_view utf8);

// --- audio ---
// `samples` holds n mono samples (n must be a power of two). Writes `bands` values in 0..1,
// log-spaced between ~50 Hz and ~12 kHz.
void BandsFromSamples(const float* samples, std::size_t n, float sampleRate, int bands, float* out);

}  // namespace deskskin::measures::parse
