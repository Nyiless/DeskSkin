#include <winsock2.h>
#include <windows.h>
#include <ws2ipdef.h>
#include <iphlpapi.h>
#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <thread>

#include "measures/Audio.h"
#include "measures/Http.h"
#include "measures/Hub.h"
#include "measures/Parse.h"

namespace deskskin::measures {
namespace {

long long NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

std::wstring Widen(std::string_view s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

std::string Narrow(const std::wstring& w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(static_cast<std::size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

std::wstring Fmt(const char* f, double v) {
  char b[48];
  std::snprintf(b, sizeof b, f, v);
  return Widen(b);
}

struct Series {
  double value = 0.0;
  unsigned version = 0;
  long long lastMs = -1000000;
  std::vector<float> history;
  void Push(double v) {
    value = v;
    ++version;
    history.push_back(static_cast<float>(v));
    if (history.size() > 60) history.erase(history.begin());
  }
};

// ---- network-backed data, shared with detached worker threads ----
struct WeatherData {
  bool ok = false;
  std::string status = "Loading weather...";
  std::string city;
  double tempC = 0, feelsC = 0, windKmh = 0, precipMm = 0, hiC = 0, loC = 0;
  int humidity = 0, code = 0;
  struct Day { std::string name; int code = 0; double hiC = 0, loC = 0; } days[3];
};
struct FeedData {
  bool ok = false;
  std::string status = "Loading feed...";
  parse::Feed feed;
};
struct NetState {
  std::mutex mutex;
  WeatherData weather;
  FeedData feed;
  bool weatherBusy = false, feedBusy = false;
  double lat = 0, lon = 0;
  std::string locKey, locName;
  bool haveLoc = false;
  unsigned weatherVersion = 0, feedVersion = 0;
};

void FetchWeather(std::shared_ptr<NetState> st, HubConfig cfg) {
  std::string body, err;
  auto fail = [&](const std::string& why) {
    std::lock_guard<std::mutex> lock(st->mutex);
    if (!st->weather.ok) st->weather.status = "Weather unavailable (" + why + ")";
    else st->weather.status = "OK";  // keep showing the last good data
    st->weatherBusy = false;
    ++st->weatherVersion;
  };
  double lat = 0, lon = 0;
  std::string name;
  bool have = false;
  {
    std::lock_guard<std::mutex> lock(st->mutex);
    if (st->haveLoc && st->locKey == cfg.city) { lat = st->lat; lon = st->lon; name = st->locName; have = true; }
  }
  if (!have) {
    if (!cfg.city.empty()) {
      if (!HttpGet(L"https://geocoding-api.open-meteo.com/v1/search?count=1&name=" + Widen(parse::UrlEncode(cfg.city)), body, err))
        return fail(err);
      auto res = parse::JsonNumber(body, "latitude"), res2 = parse::JsonNumber(body, "longitude");
      if (!res || !res2) return fail("city not found");
      lat = *res; lon = *res2;
      name = parse::JsonString(body, "name").value_or(cfg.city);
    } else {
      if (!HttpGet(L"https://ipwho.is/", body, err)) return fail(err);
      auto a = parse::JsonNumber(body, "latitude"), b = parse::JsonNumber(body, "longitude");
      if (!a || !b) return fail("could not locate you");
      lat = *a; lon = *b;
      name = parse::JsonString(body, "city").value_or("Your location");
    }
    std::lock_guard<std::mutex> lock(st->mutex);
    st->lat = lat; st->lon = lon; st->locName = name; st->locKey = cfg.city; st->haveLoc = true;
  }
  char url[512];
  std::snprintf(url, sizeof url,
                "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f&timezone=auto&forecast_days=4"
                "&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,wind_speed_10m,precipitation"
                "&daily=weather_code,temperature_2m_max,temperature_2m_min",
                lat, lon);
  if (!HttpGet(Widen(url), body, err)) return fail(err);
  const auto cur = parse::JsonObject(body, "current");
  const auto daily = parse::JsonObject(body, "daily");
  const auto t = parse::JsonNumber(cur, "temperature_2m");
  if (!t) return fail("unexpected response");

  WeatherData w;
  w.ok = true;
  w.status = "OK";
  w.city = name;
  w.tempC = *t;
  w.feelsC = parse::JsonNumber(cur, "apparent_temperature").value_or(*t);
  w.humidity = static_cast<int>(parse::JsonNumber(cur, "relative_humidity_2m").value_or(0));
  w.code = static_cast<int>(parse::JsonNumber(cur, "weather_code").value_or(0));
  w.windKmh = parse::JsonNumber(cur, "wind_speed_10m").value_or(0);
  w.precipMm = parse::JsonNumber(cur, "precipitation").value_or(0);
  const auto hi = parse::JsonNumberArray(daily, "temperature_2m_max"), lo = parse::JsonNumberArray(daily, "temperature_2m_min");
  const auto codes = parse::JsonNumberArray(daily, "weather_code");
  const auto dates = parse::JsonStringArray(daily, "time");
  if (!hi.empty()) w.hiC = hi[0];
  if (!lo.empty()) w.loC = lo[0];
  for (int i = 0; i < 3; ++i) {
    const std::size_t k = static_cast<std::size_t>(i) + 1;
    if (k < dates.size()) w.days[i].name = parse::WeekdayShort(dates[k]);
    if (k < codes.size()) w.days[i].code = static_cast<int>(codes[k]);
    if (k < hi.size()) w.days[i].hiC = hi[k];
    if (k < lo.size()) w.days[i].loC = lo[k];
  }
  std::lock_guard<std::mutex> lock(st->mutex);
  st->weather = w;
  st->weatherBusy = false;
  ++st->weatherVersion;
}

void FetchFeed(std::shared_ptr<NetState> st, HubConfig cfg) {
  std::string body, err;
  bool ok = HttpGet(Widen(cfg.feedUrl), body, err);
  parse::Feed feed;
  if (ok) {
    feed = parse::ParseFeed(body, 8);
    if (feed.items.empty()) { ok = false; err = "not an RSS/Atom feed"; }
  }
  std::lock_guard<std::mutex> lock(st->mutex);
  if (ok) {
    st->feed.ok = true;
    st->feed.status = "OK";
    st->feed.feed = std::move(feed);
  } else if (!st->feed.ok) {
    st->feed.status = "Feed unavailable (" + err + ")";
  }
  st->feedBusy = false;
  ++st->feedVersion;
}

std::string Upper(std::string_view s) {
  std::string r(s);
  for (char& c : r) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return r;
}

char DriveLetter(std::string_view arg) {
  for (char c : arg) if (std::isalpha(static_cast<unsigned char>(c))) return static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return 'C';
}

}  // namespace

// ---------------------------------------------------------------------------------------------
struct Hub::Impl {
  HubConfig cfg;
  std::shared_ptr<NetState> net = std::make_shared<NetState>();
  AudioCapture audio;
  unsigned lastBandsVersion = 0;
  long long lastWeatherStart = -10000000, lastFeedStart = -10000000;

  Series cpu, ram, netDown, netUp;
  ULONGLONG prevIdle = 0, prevKernel = 0, prevUser = 0;
  bool haveCpuPrev = false;
  ULONGLONG prevIn = 0, prevOut = 0;
  long long prevNetMs = 0;
  bool haveNetPrev = false;
  MEMORYSTATUSEX mem{};

  struct Disk { long long ms = -1000000; double usedPct = 0; ULONGLONG total = 0, free = 0; bool ok = false; };
  std::map<char, Disk> disks;
  long long recycleMs = -1000000;
  long long recycleItems = 0, recycleSize = 0;
  long long mediaMs = -1000000;
  std::wstring mediaTitle, mediaArtist, mediaStatus = L"Not running";
  unsigned mediaVersion = 0;
  std::map<DWORD, bool> spotifyPid;
  long long notesMs = -1000000;
  FILETIME notesStamp{};
  std::vector<std::wstring> notes;

  // ----- samplers -----
  void SampleCpu() {
    const long long now = NowMs();
    if (now - cpu.lastMs < 900) return;
    FILETIME i, k, u;
    if (!GetSystemTimes(&i, &k, &u)) return;
    auto to64 = [](const FILETIME& f) { return (static_cast<ULONGLONG>(f.dwHighDateTime) << 32) | f.dwLowDateTime; };
    const ULONGLONG idle = to64(i), kern = to64(k), user = to64(u);
    if (haveCpuPrev) {
      const ULONGLONG total = (kern - prevKernel) + (user - prevUser), idleD = idle - prevIdle;
      if (total > 0) cpu.Push(std::clamp(100.0 * static_cast<double>(total - idleD) / static_cast<double>(total), 0.0, 100.0));
    }
    prevIdle = idle; prevKernel = kern; prevUser = user; haveCpuPrev = true;
    cpu.lastMs = now;
  }

  void SampleRam() {
    const long long now = NowMs();
    if (now - ram.lastMs < 900) return;
    mem.dwLength = sizeof(mem);
    if (GlobalMemoryStatusEx(&mem)) ram.Push(mem.dwMemoryLoad);
    ram.lastMs = now;
  }

  void SampleNet() {
    const long long now = NowMs();
    if (now - netDown.lastMs < 900) return;
    PMIB_IF_TABLE2 table = nullptr;
    if (GetIfTable2(&table) != NO_ERROR || !table) return;
    ULONGLONG in = 0, out = 0;
    for (ULONG r = 0; r < table->NumEntries; ++r) {
      const MIB_IF_ROW2& row = table->Table[r];
      if (row.OperStatus == IfOperStatusUp && row.InterfaceAndOperStatusFlags.HardwareInterface &&
          row.Type != IF_TYPE_SOFTWARE_LOOPBACK) {
        in += row.InOctets;
        out += row.OutOctets;
      }
    }
    FreeMibTable(table);
    if (haveNetPrev && now > prevNetMs) {
      const double secs = static_cast<double>(now - prevNetMs) / 1000.0;
      netDown.Push(in >= prevIn ? static_cast<double>(in - prevIn) / secs : 0.0);
      netUp.Push(out >= prevOut ? static_cast<double>(out - prevOut) / secs : 0.0);
    }
    prevIn = in; prevOut = out; prevNetMs = now; haveNetPrev = true;
    netDown.lastMs = netUp.lastMs = now;
  }

  Disk& SampleDisk(char letter) {
    Disk& d = disks[letter];
    const long long now = NowMs();
    if (now - d.ms < 10000) return d;
    d.ms = now;
    const wchar_t root[4] = {static_cast<wchar_t>(letter), L':', L'\\', 0};
    ULARGE_INTEGER avail{}, total{}, freeB{};
    d.ok = GetDiskFreeSpaceExW(root, &avail, &total, &freeB) && total.QuadPart > 0;
    if (d.ok) {
      d.total = total.QuadPart;
      d.free = freeB.QuadPart;
      d.usedPct = 100.0 * static_cast<double>(d.total - d.free) / static_cast<double>(d.total);
    }
    return d;
  }

  void SampleRecycle() {
    const long long now = NowMs();
    if (now - recycleMs < 4000) return;
    recycleMs = now;
    SHQUERYRBINFO info{};
    info.cbSize = sizeof(info);
    if (SUCCEEDED(SHQueryRecycleBinW(nullptr, &info))) {
      recycleItems = info.i64NumItems;
      recycleSize = info.i64Size;
    }
  }

  bool IsSpotify(DWORD pid) {
    auto it = spotifyPid.find(pid);
    if (it != spotifyPid.end()) return it->second;
    bool yes = false;
    if (HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)) {
      wchar_t path[MAX_PATH * 2];
      DWORD n = ARRAYSIZE(path);
      if (QueryFullProcessImageNameW(h, 0, path, &n)) {
        std::wstring p(path, n);
        const std::size_t s = p.find_last_of(L"\\/");
        std::wstring exe = s == std::wstring::npos ? p : p.substr(s + 1);
        for (auto& c : exe) c = static_cast<wchar_t>(towlower(c));
        yes = (exe == L"spotify.exe");
      }
      CloseHandle(h);
    }
    return spotifyPid[pid] = yes;
  }

  struct FindCtx { Impl* self; std::wstring title; bool found = false; };
  static BOOL CALLBACK EnumSpotify(HWND h, LPARAM lp) {
    auto* ctx = reinterpret_cast<FindCtx*>(lp);
    wchar_t cls[64] = {};
    GetClassNameW(h, cls, ARRAYSIZE(cls));
    if (wcscmp(cls, L"Chrome_WidgetWin_1") != 0) return TRUE;
    wchar_t title[512] = {};
    if (GetWindowTextW(h, title, ARRAYSIZE(title)) <= 0) return TRUE;
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (!ctx->self->IsSpotify(pid)) return TRUE;
    ctx->title = title;
    ctx->found = true;
    return FALSE;
  }

  // Spotify's desktop window title is "Artist - Song" while playing and "Spotify ..." otherwise.
  // It needs no account, token or API credentials; any player's media keys are handled elsewhere.
  void SampleMedia() {
    const long long now = NowMs();
    if (now - mediaMs < 900) return;
    mediaMs = now;
    FindCtx ctx{this, {}, false};
    EnumWindows(&Impl::EnumSpotify, reinterpret_cast<LPARAM>(&ctx));
    std::wstring title, artist, status;
    if (!ctx.found) {
      status = L"Not running";
    } else if (ctx.title.rfind(L"Spotify", 0) == 0) {
      status = L"Paused";
    } else {
      status = L"Playing";
      const std::size_t dash = ctx.title.find(L" - ");
      if (dash != std::wstring::npos) { artist = ctx.title.substr(0, dash); title = ctx.title.substr(dash + 3); }
      else title = ctx.title;
    }
    if (status == L"Paused") { title = mediaTitle; artist = mediaArtist; }  // keep the last song visible while paused
    if (title != mediaTitle || artist != mediaArtist || status != mediaStatus) ++mediaVersion;
    mediaTitle = title; mediaArtist = artist; mediaStatus = status;
  }

  void SampleNotes() {
    const long long now = NowMs();
    if (now - notesMs < 1500) return;
    notesMs = now;
    if (cfg.notesPath.empty()) return;
    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(cfg.notesPath.c_str(), GetFileExInfoStandard, &fad)) {
      CreateDirectoryW(cfg.notesPath.substr(0, cfg.notesPath.find_last_of(L"\\/")).c_str(), nullptr);
      std::ofstream(std::filesystem::path(cfg.notesPath), std::ios::binary) << "Click this note to edit it.\r\nOne line per bullet.\r\n";
      GetFileAttributesExW(cfg.notesPath.c_str(), GetFileExInfoStandard, &fad);
    }
    if (CompareFileTime(&fad.ftLastWriteTime, &notesStamp) == 0 && !notes.empty()) return;
    notesStamp = fad.ftLastWriteTime;
    notes.clear();
    std::ifstream in{std::filesystem::path(cfg.notesPath), std::ios::binary};
    std::string line;
    while (std::getline(in, line) && notes.size() < 6) {
      while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
      if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF) line.erase(0, 3);  // UTF-8 BOM
      if (!line.empty()) notes.push_back(Widen(line));
    }
  }

  void MaybeStartWeather() {
    std::lock_guard<std::mutex> lock(net->mutex);
    const long long now = NowMs();
    const long long period = net->weather.ok ? 15 * 60 * 1000 : 60 * 1000;  // retry quickly while failing
    if (net->weatherBusy || now - lastWeatherStart < period) return;
    net->weatherBusy = true;
    lastWeatherStart = now;
    std::thread(FetchWeather, net, cfg).detach();
  }

  void MaybeStartFeed() {
    if (cfg.feedUrl.empty()) return;
    std::lock_guard<std::mutex> lock(net->mutex);
    const long long now = NowMs();
    const long long period = net->feed.ok ? 30 * 60 * 1000 : 60 * 1000;
    if (net->feedBusy || now - lastFeedStart < period) return;
    net->feedBusy = true;
    lastFeedStart = now;
    std::thread(FetchFeed, net, cfg).detach();
  }
};

// ---------------------------------------------------------------------------------------------
Hub::Hub() : m(std::make_unique<Impl>()) {}
Hub::~Hub() = default;

Hub& Hub::Get() {
  static Hub hub;
  return hub;
}

void Hub::SetConfig(const HubConfig& cfg) {
  const bool locChanged = cfg.city != m->cfg.city;
  const bool feedChanged = cfg.feedUrl != m->cfg.feedUrl;
  m->cfg = cfg;
  std::lock_guard<std::mutex> lock(m->net->mutex);
  if (locChanged) { m->lastWeatherStart = -10000000; m->net->weather.ok = false; m->net->weather.status = "Loading weather..."; m->net->haveLoc = false; }
  if (feedChanged) { m->lastFeedStart = -10000000; m->net->feed = FeedData{}; }
  m->notesMs = -1000000;
  m->notes.clear();
}

const HubConfig& Hub::Config() const { return m->cfg; }

void Hub::Shutdown() {}

bool Hub::Value(std::string_view s, double& out) {
  const std::string k = Upper(s);
  if (k == "CPU") { m->SampleCpu(); out = m->cpu.value; return true; }
  if (k == "RAM") { m->SampleRam(); out = m->ram.value; return true; }
  if (k == "NET_DOWN" || k == "NET_UP") { m->SampleNet(); out = k == "NET_DOWN" ? m->netDown.value : m->netUp.value; return true; }
  if (k.rfind("DISK:", 0) == 0) {
    auto& d = m->SampleDisk(DriveLetter(k.substr(5)));
    out = d.usedPct;
    return d.ok;
  }
  return false;
}

bool Hub::History(std::string_view s, std::vector<float>& out) {
  const std::string k = Upper(s);
  double ignored;
  if (!Value(k, ignored)) return false;
  if (k == "CPU") out = m->cpu.history;
  else if (k == "RAM") out = m->ram.history;
  else if (k == "NET_DOWN") out = m->netDown.history;
  else if (k == "NET_UP") out = m->netUp.history;
  else return false;
  return true;
}

unsigned Hub::Version(std::string_view s) {
  const std::string k = Upper(s);
  if (k == "CPU") return m->cpu.version;
  if (k == "RAM") return m->ram.version;
  if (k == "NET_DOWN") return m->netDown.version;
  if (k == "NET_UP") return m->netUp.version;
  return 0;
}

bool Hub::Bands(int count, std::vector<float>& out) {
  m->audio.Request();
  float b[AudioCapture::kBands];
  m->audio.Snapshot(b);
  count = std::clamp(count, 1, 256);
  out.assign(static_cast<std::size_t>(count), 0.0f);
  for (int i = 0; i < count; ++i) {
    const int a = i * AudioCapture::kBands / count;
    const int e = std::max(a + 1, (i + 1) * AudioCapture::kBands / count);
    float mx = 0.0f;
    for (int k = a; k < std::min(e, AudioCapture::kBands); ++k) mx = std::max(mx, b[k]);
    out[static_cast<std::size_t>(i)] = mx;
  }
  return true;
}

unsigned Hub::BandsVersion() { return m->audio.Version(); }

std::wstring Hub::FeedLink(int n) {
  std::lock_guard<std::mutex> lock(m->net->mutex);
  const auto& items = m->net->feed.feed.items;
  if (n < 1 || static_cast<std::size_t>(n) > items.size()) return {};
  return Widen(items[static_cast<std::size_t>(n) - 1].link);
}

// ---------------------------------------------------------------------------------------------
bool Hub::Text(std::string_view token, std::wstring& out) {
  std::string name = Upper(token), arg;
  if (const std::size_t c = name.find(':'); c != std::string::npos) {
    arg = name.substr(c + 1);
    name = name.substr(0, c);
  }
  const bool imperial = m->cfg.units == "imperial";

  if (name == "CPU") { m->SampleCpu(); out = Fmt("%.0f", m->cpu.value); return true; }
  if (name == "RAM" || name == "RAM_USED" || name == "RAM_TOTAL" || name == "RAM_FREE") {
    m->SampleRam();
    if (name == "RAM") out = Fmt("%.0f", m->ram.value);
    else if (name == "RAM_TOTAL") out = Widen(parse::FormatBytes(static_cast<double>(m->mem.ullTotalPhys)));
    else if (name == "RAM_FREE") out = Widen(parse::FormatBytes(static_cast<double>(m->mem.ullAvailPhys)));
    else out = Widen(parse::FormatBytes(static_cast<double>(m->mem.ullTotalPhys - m->mem.ullAvailPhys)));
    return true;
  }
  if (name == "DISK" || name == "DISK_FREE" || name == "DISK_USED" || name == "DISK_TOTAL") {
    auto& d = m->SampleDisk(DriveLetter(arg));
    if (!d.ok) { out = L"n/a"; return true; }
    if (name == "DISK") out = Fmt("%.0f", d.usedPct);
    else if (name == "DISK_FREE") out = Widen(parse::FormatBytes(static_cast<double>(d.free)));
    else if (name == "DISK_USED") out = Widen(parse::FormatBytes(static_cast<double>(d.total - d.free)));
    else out = Widen(parse::FormatBytes(static_cast<double>(d.total)));
    return true;
  }
  if (name == "NET_DOWN" || name == "NET_UP") {
    m->SampleNet();
    out = Widen(parse::FormatRate(name == "NET_DOWN" ? m->netDown.value : m->netUp.value));
    return true;
  }
  if (name == "RECYCLE_COUNT" || name == "RECYCLE_SIZE") {
    m->SampleRecycle();
    out = name == "RECYCLE_COUNT" ? Widen(std::to_string(m->recycleItems)) : Widen(parse::FormatBytes(static_cast<double>(m->recycleSize)));
    return true;
  }
  if (name.rfind("MEDIA_", 0) == 0) {
    m->SampleMedia();
    if (name == "MEDIA_STATUS") out = m->mediaStatus;
    else if (name == "MEDIA_TITLE") out = m->mediaStatus == L"Not running" ? L"Spotify isn't running" : (m->mediaTitle.empty() ? L"Nothing playing" : m->mediaTitle);
    else if (name == "MEDIA_ARTIST") out = m->mediaArtist;
    else if (name == "MEDIA_LINE") out = m->mediaStatus == L"Not running" ? L"Open Spotify to see what's playing" : (m->mediaTitle.empty() ? L"Nothing playing" : (m->mediaArtist.empty() ? m->mediaTitle : m->mediaArtist + L" \x2014 " + m->mediaTitle));
    else return false;
    return true;
  }
  if (name.rfind("NOTE_", 0) == 0) {
    m->SampleNotes();
    const std::size_t i = static_cast<std::size_t>(std::atoi(name.c_str() + 5));
    out = (i >= 1 && i <= m->notes.size()) ? m->notes[i - 1] : L"";
    return true;
  }
  if (name == "USER" || name == "HOST") {
    wchar_t buf[256];
    DWORD n = ARRAYSIZE(buf);
    out = (name == "USER" ? GetUserNameW(buf, &n) : GetComputerNameW(buf, &n)) ? std::wstring(buf) : L"";
    return true;
  }
  if (name == "GREETING") {
    SYSTEMTIME t;
    GetLocalTime(&t);
    out = t.wHour < 12 ? L"Good morning" : (t.wHour < 18 ? L"Good afternoon" : L"Good evening");
    return true;
  }
  if (name == "UPTIME") {
    const ULONGLONG s = GetTickCount64() / 1000;
    wchar_t b[48];
    if (s >= 86400) swprintf(b, 48, L"%llud %lluh %llum", s / 86400, (s / 3600) % 24, (s / 60) % 60);
    else swprintf(b, 48, L"%lluh %llum", s / 3600, (s / 60) % 60);
    out = b;
    return true;
  }
  if (name == "AUDIO_STATUS") {
    m->audio.Request();
    out = m->audio.Status();
    return true;
  }

  // ---- weather (Open-Meteo, no API key) ----
  if (name.rfind("WEATHER_", 0) == 0 || name.rfind("JACKET", 0) == 0) {
    m->MaybeStartWeather();
    WeatherData w;
    {
      std::lock_guard<std::mutex> lock(m->net->mutex);
      w = m->net->weather;
    }
    auto temp = [&](double c) { return Fmt("%.0f\xC2\xB0", imperial ? c * 9.0 / 5.0 + 32.0 : c); };
    if (name == "WEATHER_STATUS") { out = w.ok ? L"" : Widen(w.status); return true; }
    if (name == "JACKET") {
      out = w.ok ? (parse::Jacket(w.feelsC, w.precipMm, w.windKmh, w.code, imperial).needed ? L"Yes, take a jacket" : L"No jacket needed") : Widen(w.status);
      return true;
    }
    if (name == "JACKET_WHY") { out = w.ok ? Widen(parse::Jacket(w.feelsC, w.precipMm, w.windKmh, w.code, imperial).why) : L""; return true; }
    if (!w.ok) { out = (name == "WEATHER_COND") ? Widen(w.status) : L"--"; return true; }
    if (name == "WEATHER_TEMP") out = temp(w.tempC);
    else if (name == "WEATHER_FEELS") out = temp(w.feelsC);
    else if (name == "WEATHER_HI") out = temp(w.hiC);
    else if (name == "WEATHER_LO") out = temp(w.loC);
    else if (name == "WEATHER_COND") out = Widen(parse::WeatherCondition(w.code));
    else if (name == "WEATHER_ICON") out = Widen(parse::WeatherGlyph(w.code));
    else if (name == "WEATHER_CITY") out = Widen(w.city);
    else if (name == "WEATHER_HUMIDITY") out = Fmt("%.0f%%", w.humidity);
    else if (name == "WEATHER_WIND") out = imperial ? Fmt("%.0f mph", w.windKmh * 0.621371) : Fmt("%.0f km/h", w.windKmh);
    else if (name.rfind("WEATHER_D", 0) == 0 && name.size() > 11 && name[10] == '_') {  // WEATHER_D1_NAME etc.
      const int d = name[9] - '1';
      if (d < 0 || d > 2) return false;
      const std::string what = name.substr(11);
      if (what == "NAME") out = Widen(w.days[d].name);
      else if (what == "ICON") out = Widen(parse::WeatherGlyph(w.days[d].code));
      else if (what == "HI") out = temp(w.days[d].hiC);
      else if (what == "LO") out = temp(w.days[d].loC);
      else return false;
    } else return false;
    return true;
  }

  // ---- RSS / Atom ----
  if (name == "FEED_NAME" || name == "FEED_STATUS" || name.rfind("FEED_", 0) == 0) {
    if (m->cfg.feedUrl.empty()) { out = name == "FEED_1" ? L"No feed set. Open Widget Library > Feeds > Feed URL." : L""; return true; }
    m->MaybeStartFeed();
    std::lock_guard<std::mutex> lock(m->net->mutex);
    const FeedData& f = m->net->feed;
    if (name == "FEED_NAME") out = f.ok ? Widen(f.feed.title) : L"Feed";
    else if (name == "FEED_STATUS") out = f.ok ? L"" : Widen(f.status);
    else {
      const int i = std::atoi(name.c_str() + 5);
      if (i < 1) return false;
      if (!f.ok) out = i == 1 ? Widen(f.status) : L"";
      else out = static_cast<std::size_t>(i) <= f.feed.items.size() ? Widen(f.feed.items[static_cast<std::size_t>(i) - 1].title) : L"";
    }
    return true;
  }
  return false;
}

}  // namespace deskskin::measures
