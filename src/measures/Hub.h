// The Hub serves every live value a skin can show: %CPU%, %DISK:C%, %NET_DOWN%, %WEATHER_TEMP%,
// %MEDIA_TITLE%, %FEED_1% ... It is entirely demand-driven:
//   * a measure is sampled only when some skin asks for it, and at most once per its own interval,
//     so ten skins showing %CPU% cost one sample per second;
//   * network sources (weather, feeds) fetch on a short-lived worker thread and are re-fetched
//     only while a skin keeps asking; failures become readable text ("Weather unavailable (offline)");
//   * audio capture starts on the first request and stops by itself when nobody is asking.
// All methods are called from the UI thread.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace deskskin::measures {

struct HubConfig {
  std::string city;            // empty = locate by IP address
  std::string units = "metric";  // or "imperial"
  std::string feedUrl;
  std::wstring notesPath;      // sticky-notes text file
};

class Hub {
 public:
  static Hub& Get();

  void SetConfig(const HubConfig& cfg);
  const HubConfig& Config() const;

  // Token without the surrounding '%', e.g. "CPU", "DISK_FREE:C". False if unknown.
  bool Text(std::string_view token, std::wstring& out);
  // Raw numeric source: CPU/RAM/DISK:x are percent (0..100), NET_DOWN/NET_UP are bytes per second.
  bool Value(std::string_view source, double& out);
  bool History(std::string_view source, std::vector<float>& out);  // oldest -> newest, raw values
  unsigned Version(std::string_view source);                       // changes whenever new data arrived

  bool Bands(int count, std::vector<float>& out);  // audio spectrum 0..1; starts capture if needed
  unsigned BandsVersion();

  std::wstring FeedLink(int index1Based);  // link of the n-th feed item, "" if none
  void Shutdown();

 private:
  Hub();
  ~Hub();
  struct Impl;
  std::unique_ptr<Impl> m;
};

}  // namespace deskskin::measures
