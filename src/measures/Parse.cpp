#include "measures/Parse.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <charconv>
#include <cmath>
#include <complex>

namespace deskskin::measures::parse {
namespace {

std::size_t FindKey(std::string_view s, std::string_view key) {
  std::string pat;
  pat.reserve(key.size() + 3);
  pat += '"';
  pat += key;
  pat += "\":";
  const std::size_t p = s.find(pat);
  return p == std::string_view::npos ? p : p + pat.size();
}

std::string_view SkipWs(std::string_view s) {
  while (!s.empty() && (s.front() == ' ' || s.front() == '\t' || s.front() == '\r' || s.front() == '\n')) s.remove_prefix(1);
  return s;
}

std::string_view Trim(std::string_view s) {
  s = SkipWs(s);
  while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r' || s.back() == '\n')) s.remove_suffix(1);
  return s;
}

std::optional<double> ParseNumberPrefix(std::string_view s) {
  s = SkipWs(s);
  double v = 0;
  const auto r = std::from_chars(s.data(), s.data() + s.size(), v);
  if (r.ec != std::errc{}) return std::nullopt;
  return v;
}

std::string StripCdata(std::string_view s) {
  s = Trim(s);
  constexpr std::string_view open = "<![CDATA[", close = "]]>";
  if (s.substr(0, open.size()) == open) {
    s.remove_prefix(open.size());
    const std::size_t e = s.rfind(close);
    if (e != std::string_view::npos) s = s.substr(0, e);
    return std::string(s);  // CDATA content is literal: no entity decoding
  }
  return DecodeEntities(s);
}

// text of the first <tag ...>text</tag> inside `chunk`
std::optional<std::string> TagText(std::string_view chunk, std::string_view tag) {
  std::string open = "<" + std::string(tag);
  std::size_t p = 0;
  while ((p = chunk.find(open, p)) != std::string_view::npos) {
    const std::size_t after = p + open.size();
    if (after < chunk.size() && (chunk[after] == '>' || chunk[after] == ' ' || chunk[after] == '/')) {
      const std::size_t gt = chunk.find('>', after);
      if (gt == std::string_view::npos) return std::nullopt;
      if (chunk[gt - 1] == '/') return std::nullopt;  // self-closing
      const std::string close = "</" + std::string(tag) + ">";
      const std::size_t e = chunk.find(close, gt + 1);
      if (e == std::string_view::npos) return std::nullopt;
      return StripCdata(chunk.substr(gt + 1, e - gt - 1));
    }
    p = after;
  }
  return std::nullopt;
}

std::string LinkOf(std::string_view chunk) {
  if (auto t = TagText(chunk, "link"); t && !t->empty()) return *t;  // RSS
  std::size_t p = 0;
  while ((p = chunk.find("<link", p)) != std::string_view::npos) {  // Atom: <link href="..."/>
    const std::size_t gt = chunk.find('>', p);
    if (gt == std::string_view::npos) break;
    const std::string_view tag = chunk.substr(p, gt - p);
    const std::size_t h = tag.find("href=\"");
    if (h != std::string_view::npos) {
      const std::size_t s = h + 6, e = tag.find('"', s);
      if (e != std::string_view::npos) return DecodeEntities(tag.substr(s, e - s));
    }
    p = gt;
  }
  return {};
}

}  // namespace

// ---------------------------------------------------------------------------------------------
std::optional<double> JsonNumber(std::string_view obj, std::string_view key) {
  const std::size_t p = FindKey(obj, key);
  if (p == std::string_view::npos) return std::nullopt;
  return ParseNumberPrefix(obj.substr(p));
}

std::optional<std::string> JsonString(std::string_view obj, std::string_view key) {
  const std::size_t p = FindKey(obj, key);
  if (p == std::string_view::npos) return std::nullopt;
  std::string_view s = SkipWs(obj.substr(p));
  if (s.empty() || s.front() != '"') return std::nullopt;
  std::string out;
  for (std::size_t i = 1; i < s.size(); ++i) {
    if (s[i] == '\\' && i + 1 < s.size()) { out += s[++i]; continue; }
    if (s[i] == '"') return out;
    out += s[i];
  }
  return std::nullopt;
}

std::string_view JsonObject(std::string_view json, std::string_view key) {
  const std::size_t p = FindKey(json, key);
  if (p == std::string_view::npos) return {};
  std::string_view s = SkipWs(json.substr(p));
  if (s.empty() || s.front() != '{') return {};
  int depth = 0;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] == '{') ++depth;
    else if (s[i] == '}' && --depth == 0) return s.substr(0, i + 1);
  }
  return {};
}

std::vector<double> JsonNumberArray(std::string_view obj, std::string_view key) {
  std::vector<double> out;
  const std::size_t p = FindKey(obj, key);
  if (p == std::string_view::npos) return out;
  std::string_view s = SkipWs(obj.substr(p));
  if (s.empty() || s.front() != '[') return out;
  const std::size_t e = s.find(']');
  s = s.substr(1, e == std::string_view::npos ? std::string_view::npos : e - 1);
  while (!s.empty()) {
    const std::size_t c = s.find(',');
    const std::string_view tok = Trim(s.substr(0, c));
    if (tok == "null") out.push_back(0.0);
    else if (auto v = ParseNumberPrefix(tok)) out.push_back(*v);
    if (c == std::string_view::npos) break;
    s.remove_prefix(c + 1);
  }
  return out;
}

std::vector<std::string> JsonStringArray(std::string_view obj, std::string_view key) {
  std::vector<std::string> out;
  const std::size_t p = FindKey(obj, key);
  if (p == std::string_view::npos) return out;
  std::string_view s = SkipWs(obj.substr(p));
  if (s.empty() || s.front() != '[') return out;
  const std::size_t e = s.find(']');
  s = s.substr(1, e == std::string_view::npos ? std::string_view::npos : e - 1);
  std::size_t i = 0;
  while ((i = s.find('"', i)) != std::string_view::npos) {
    const std::size_t j = s.find('"', i + 1);
    if (j == std::string_view::npos) break;
    out.emplace_back(s.substr(i + 1, j - i - 1));
    i = j + 1;
  }
  return out;
}

// ---------------------------------------------------------------------------------------------
const char* WeatherCondition(int c) {
  switch (c) {
    case 0: return "Clear sky";
    case 1: return "Mainly clear";
    case 2: return "Partly cloudy";
    case 3: return "Overcast";
    case 45: case 48: return "Fog";
    case 51: case 53: case 55: return "Drizzle";
    case 56: case 57: return "Freezing drizzle";
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: case 67: return "Freezing rain";
    case 71: return "Light snow";
    case 73: return "Snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grains";
    case 80: return "Light showers";
    case 81: return "Showers";
    case 82: return "Violent showers";
    case 85: case 86: return "Snow showers";
    case 95: return "Thunderstorm";
    case 96: case 99: return "Thunderstorm with hail";
    default: return "Unknown";
  }
}

const char* WeatherGlyph(int c) {
  if (c == 0 || c == 1) return "\xE2\x98\x80";                    // sun
  if (c == 2) return "\xE2\x9B\x85";                              // sun behind cloud
  if (c == 3) return "\xE2\x98\x81";                              // cloud
  if (c == 45 || c == 48) return "\xE2\x89\x8B";                  // fog (triple tilde)
  if ((c >= 51 && c <= 67) || (c >= 80 && c <= 82)) return "\xE2\x98\x82";  // umbrella
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return "\xE2\x9D\x84";    // snowflake
  if (c >= 95) return "\xE2\x9A\xA1";                             // lightning
  return "\xE2\x98\x81";
}

std::string WeekdayShort(std::string_view d) {
  static const char* names[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  if (d.size() < 10) return "--";
  int y = 0, m = 0, day = 0;
  std::from_chars(d.data(), d.data() + 4, y);
  std::from_chars(d.data() + 5, d.data() + 7, m);
  std::from_chars(d.data() + 8, d.data() + 10, day);
  if (m < 1 || m > 12) return "--";
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  return names[(y + y / 4 - y / 100 + y / 400 + t[m - 1] + day) % 7];
}

JacketAdvice Jacket(double feelsC, double precipMm, double windKmh, int code, bool f) {
  JacketAdvice a;
  auto temp = [&](double c) {
    char b[24];
    std::snprintf(b, sizeof b, f ? "%.0f\xC2\xB0""F" : "%.0f\xC2\xB0""C", f ? c * 9.0 / 5.0 + 32.0 : c);
    return std::string(b);
  };
  std::string why;
  auto add = [&](const std::string& s) { why += (why.empty() ? "" : ", ") + s; };
  const bool rain = precipMm >= 0.2 || (code >= 51 && code <= 67) || (code >= 80 && code <= 82);
  const bool snow = (code >= 71 && code <= 77) || code == 85 || code == 86;
  if (feelsC < 14.0) { a.needed = true; add("feels like " + temp(feelsC)); }
  if (rain) { a.needed = true; add("rain"); }
  if (snow) { a.needed = true; add("snow"); }
  if (windKmh > 35.0) { a.needed = true; add("windy"); }
  if (code >= 95) { a.needed = true; add("storms"); }
  if (!a.needed) why = "feels like " + temp(feelsC) + ", no rain expected";
  if (!why.empty()) why[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(why[0])));
  a.why = why;
  return a;
}

// ---------------------------------------------------------------------------------------------
std::string DecodeEntities(std::string_view s) {
  std::string out;
  out.reserve(s.size());
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '&') { out += s[i]; continue; }
    const std::size_t e = s.find(';', i);
    if (e == std::string_view::npos || e - i > 10) { out += s[i]; continue; }
    const std::string_view ent = s.substr(i + 1, e - i - 1);
    if (ent == "amp") out += '&';
    else if (ent == "lt") out += '<';
    else if (ent == "gt") out += '>';
    else if (ent == "quot") out += '"';
    else if (ent == "apos") out += '\'';
    else if (!ent.empty() && ent[0] == '#') {
      unsigned cp = 0;
      const bool hex = ent.size() > 1 && (ent[1] == 'x' || ent[1] == 'X');
      const auto r = hex ? std::from_chars(ent.data() + 2, ent.data() + ent.size(), cp, 16)
                         : std::from_chars(ent.data() + 1, ent.data() + ent.size(), cp, 10);
      if (r.ec != std::errc{} || cp == 0) { out += s[i]; continue; }
      if (cp < 0x80) out += static_cast<char>(cp);
      else if (cp < 0x800) { out += static_cast<char>(0xC0 | (cp >> 6)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
      else if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
      else { out += static_cast<char>(0xF0 | (cp >> 18)); out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F)); out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F)); out += static_cast<char>(0x80 | (cp & 0x3F)); }
    } else { out += s[i]; continue; }
    i = e;
  }
  return out;
}

Feed ParseFeed(std::string_view xml, std::size_t maxItems) {
  Feed feed;
  auto nextItem = [&](std::size_t from) -> std::size_t {
    const std::size_t a = xml.find("<item", from), b = xml.find("<entry", from);
    return std::min(a, b);
  };
  std::size_t first = nextItem(0);
  if (first == std::string_view::npos) return feed;
  if (auto t = TagText(xml.substr(0, first), "title")) feed.title = *t;
  std::size_t p = first;
  while (p != std::string_view::npos && feed.items.size() < maxItems) {
    const bool isItem = xml.substr(p, 5) == "<item";
    const std::string_view close = isItem ? "</item>" : "</entry>";
    const std::size_t e = xml.find(close, p);
    if (e == std::string_view::npos) break;
    const std::string_view chunk = xml.substr(p, e - p);
    FeedItem it;
    if (auto t = TagText(chunk, "title")) it.title = *t;
    it.link = LinkOf(chunk);
    for (char& c : it.title) if (c == '\n' || c == '\r' || c == '\t') c = ' ';
    if (!it.title.empty()) feed.items.push_back(std::move(it));
    p = nextItem(e + close.size());
  }
  return feed;
}

// ---------------------------------------------------------------------------------------------
std::string FormatBytes(double b) {
  static const char* u[] = {"B", "KB", "MB", "GB", "TB"};
  int i = 0;
  while (b >= 1024.0 && i < 4) { b /= 1024.0; ++i; }
  char buf[32];
  std::snprintf(buf, sizeof buf, i == 0 ? "%.0f %s" : "%.1f %s", b, u[i]);
  return buf;
}

std::string FormatRate(double bps) { return FormatBytes(bps) + "/s"; }

std::string UrlEncode(std::string_view s) {
  static const char* hex = "0123456789ABCDEF";
  std::string out;
  for (unsigned char c : s) {
    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out += static_cast<char>(c);
    else { out += '%'; out += hex[c >> 4]; out += hex[c & 15]; }
  }
  return out;
}

// ---------------------------------------------------------------------------------------------
namespace {
void Fft(std::vector<std::complex<float>>& a) {
  const std::size_t n = a.size();
  for (std::size_t i = 1, j = 0; i < n; ++i) {  // bit reversal
    std::size_t bit = n >> 1;
    for (; j & bit; bit >>= 1) j ^= bit;
    j ^= bit;
    if (i < j) std::swap(a[i], a[j]);
  }
  for (std::size_t len = 2; len <= n; len <<= 1) {
    const float ang = -2.0f * 3.14159265358979f / static_cast<float>(len);
    const std::complex<float> wl(std::cos(ang), std::sin(ang));
    for (std::size_t i = 0; i < n; i += len) {
      std::complex<float> w(1.0f, 0.0f);
      for (std::size_t k = 0; k < len / 2; ++k) {
        const auto u = a[i + k], v = a[i + k + len / 2] * w;
        a[i + k] = u + v;
        a[i + k + len / 2] = u - v;
        w *= wl;
      }
    }
  }
}
}  // namespace

void BandsFromSamples(const float* x, std::size_t n, float rate, int bands, float* out) {
  for (int b = 0; b < bands; ++b) out[b] = 0.0f;
  if (n < 16 || (n & (n - 1)) != 0 || bands <= 0) return;
  std::vector<std::complex<float>> buf(n);
  for (std::size_t i = 0; i < n; ++i) {
    const float hann = 0.5f - 0.5f * std::cos(2.0f * 3.14159265358979f * static_cast<float>(i) / static_cast<float>(n - 1));
    buf[i] = {x[i] * hann, 0.0f};
  }
  Fft(buf);
  const float binHz = rate / static_cast<float>(n);
  const float lo = 50.0f, hi = std::min(12000.0f, rate * 0.5f);
  for (int b = 0; b < bands; ++b) {
    const float f0 = lo * std::pow(hi / lo, static_cast<float>(b) / static_cast<float>(bands));
    const float f1 = lo * std::pow(hi / lo, static_cast<float>(b + 1) / static_cast<float>(bands));
    std::size_t k0 = static_cast<std::size_t>(f0 / binHz), k1 = static_cast<std::size_t>(f1 / binHz);
    if (k1 <= k0) k1 = k0 + 1;
    k1 = std::min(k1, n / 2);
    float peak = 0.0f;
    for (std::size_t k = k0; k < k1; ++k) peak = std::max(peak, std::abs(buf[k]));
    const float amp = peak * 4.0f / static_cast<float>(n);  // ~1.0 for a full-scale sine (Hann gain 0.5)
    const float db = 20.0f * std::log10(std::max(amp, 1e-5f));
    out[b] = std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
  }
}

}  // namespace deskskin::measures::parse
