// DeskSkin v0.2 - application shell: tray, skin discovery, dragging, persistence, autostart,
// live skin reload, Explorer/GPU recovery.
#include <windows.h>  // must come before commdlg.h and the other Windows headers

#include <commdlg.h>
#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <cwctype>
#include <map>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "config/Settings.h"
#include "core/Log.h"
#include "input/MouseRouter.h"
#include "platform/Autostart.h"
#include "platform/DesktopHost.h"
#include "render/GraphicsDevice.h"
#include "render/SkinWindow.h"
#include "skin/BasicSkin.h"
#include "skin/DefaultSkin.h"
#include "ui/Dashboard.h"
#include "ui/PromptDialog.h"
#include "library/Catalog.h"
#include "measures/Hub.h"
#include "platform/TaskbarEffect.h"

namespace fs = std::filesystem;
using namespace deskskin;

namespace {

constexpr wchar_t kControlClass[] = L"DeskSkin.Control";
constexpr wchar_t kMutexName[] = L"Local\\DeskSkin.SingleInstance";

constexpr UINT WM_TRAY = WM_APP + 1;
constexpr UINT WM_DEVICE_LOST = WM_APP + 2;
constexpr UINT WM_SKIN_DRAGGED = WM_APP + 3;  // wParam = loaded-skin index
constexpr UINT WM_SKIN_CLICK = WM_APP + 4;    // wParam = index, lParam = MAKELPARAM(xDip, yDip)
constexpr UINT WM_SKIN_MENU = WM_APP + 5;     // wParam = index
constexpr UINT WM_SKINS_CHANGED = WM_APP + 6;
constexpr UINT WM_SHOW_UI = WM_APP + 7;  // posted by a second launch: bring the settings window forward

constexpr UINT_PTR TIMER_TICK = 1;
constexpr UINT_PTR TIMER_ATTACH = 2;
constexpr UINT_PTR TIMER_RELOAD = 3;
constexpr UINT_PTR TIMER_DEVLOST = 4;
constexpr UINT_PTR TIMER_IDLE = 5;
constexpr UINT_PTR TIMER_TASKBAR = 6;

constexpr UINT ID_REFRESH = 1001;
constexpr UINT ID_OPEN_FOLDER = 1002;
constexpr UINT ID_AUTOSTART = 1003;
constexpr UINT ID_RESET_ALL = 1004;
constexpr UINT ID_QUIT = 1005;
constexpr UINT ID_OPEN_UI = 1006;
constexpr UINT ID_SKIN_BASE = 2000;  // + availableIndex * 8 + action
enum SkinAction : UINT { ActEnabled, ActLocked, ActClickThrough, ActEdit, ActResetPos, ActCount = 8 };

std::string Utf8(const std::wstring& w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(static_cast<std::size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

bool DirWritable(const fs::path& dir) {
  std::error_code ec;
  fs::create_directories(dir, ec);
  const fs::path probe = dir / L".deskskin_write_test";
  HANDLE h = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
  if (h == INVALID_HANDLE_VALUE) return false;
  CloseHandle(h);
  return true;
}

// Portable by default (everything next to the exe); falls back to %APPDATA%\DeskSkin when the
// exe lives somewhere read-only such as Program Files.
fs::path ChooseDataDir(const fs::path& exeDir) {
  if (DirWritable(exeDir)) return exeDir;
  wchar_t buf[MAX_PATH];
  const DWORD n = GetEnvironmentVariableW(L"APPDATA", buf, MAX_PATH);
  fs::path p = (n > 0 && n < MAX_PATH) ? fs::path(buf) / L"DeskSkin" : exeDir;
  std::error_code ec;
  fs::create_directories(p, ec);
  return p;
}

// True when the point is over the desktop surface (not another app, not the taskbar).
bool IsDesktopSurface(POINT pt) {
  HWND h = WindowFromPoint(pt);
  if (!h) return false;
  HWND root = GetAncestor(h, GA_ROOT);
  wchar_t cls[32] = {};
  GetClassNameW(root ? root : h, cls, ARRAYSIZE(cls));
  return wcscmp(cls, L"Progman") == 0 || wcscmp(cls, L"WorkerW") == 0;
}

std::wstring Widen(std::string_view s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

bool PromptThunk(const std::wstring& title, const std::wstring& label, std::wstring& value) {
  return ui::PromptText(nullptr, title, label, value);
}

// Library widgets live in Skins\<id>\<id>.ini
const library::WidgetDef* WidgetForKey(const std::string& key) {
  const std::size_t slash = key.find('/');
  if (slash == std::string::npos) return nullptr;
  const std::string folder = key.substr(0, slash);
  if (key.substr(slash + 1) != folder + ".ini") return nullptr;
  const library::WidgetDef* d = library::Find(folder);
  return d && !d->feature ? d : nullptr;
}

// First position (preferring `desired`) where a w x h widget fits inside the work area without covering another widget.
POINT FindFreeSpot(int w, int h, POINT desired, const std::vector<RECT>& taken, const RECT& area) {
  auto fits = [&](int x, int y) {
    const RECT r{x, y, x + w, y + h};
    if (r.left < area.left || r.top < area.top || r.right > area.right || r.bottom > area.bottom) return false;
    for (const RECT& t : taken) {
      RECT i;
      if (IntersectRect(&i, &r, &t)) return false;
    }
    return true;
  };
  if (fits(desired.x, desired.y)) return desired;
  for (int y = area.top + 24; y + h <= area.bottom; y += 24)
    for (int x = area.left + 24; x + w <= area.right; x += 24)
      if (fits(x, y)) return POINT{x, y};
  return desired;  // screen is full: stack it where it wanted to be
}

struct AvailableSkin {
  std::string key;      // "Folder/File.ini"
  fs::path path;
  std::wstring display;
};

struct LoadedSkin {
  // Declaration order matters: the window is destroyed BEFORE the content it draws.
  std::unique_ptr<skin::BasicSkin> content;
  std::unique_ptr<render::SkinWindow> window;
  std::size_t avail = 0;
  bool locked = false;
  bool clickThrough = false;
};

class App final : public input::IDesktopMouseSink, public ui::IDashboardHost {
 public:
  bool Init(HINSTANCE hi, bool showUi);
  void Shutdown();
  ui::DashboardModel GetModel() override;
  void OnCommand(ui::Cmd cmd, int value) override;
  const std::vector<ui::LibItem>& Library() override;
  void OnLibraryCommand(ui::LibCmd cmd, const std::string& id, const std::string& key) override;
  std::unique_ptr<skin::BasicSkin> BuildPreview(const std::string& id) override;
  bool OnDesktopMouse(UINT msg, POINT pt) override;  // runs inside the mouse hook: keep it fast

 private:
  static LRESULT CALLBACK ControlProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT OnMessage(HWND, UINT, WPARAM, LPARAM);

  bool TryAttachAndLoad();
  void EnsureDefaultSkin();
  void ScanSkins();
  void LoadSkins();
  void UnloadSkins();
  void Reload(bool resetGraphics);
  void UpdateTimer();
  void OnTick();
  int HitTest(POINT pt) const;

  void StartWatch();
  void RearmWatch();
  void StopWatch();
  static VOID CALLBACK WatchCallback(PVOID ctx, BOOLEAN);

  void AddTrayIcon();
  void ShowTrayMenu();
  void ShowSkinMenu(std::size_t loadedIndex);
  void AppendSkinItems(HMENU menu, std::size_t availIndex);
  void HandleSkinAction(std::size_t availIndex, UINT action);
  void SaveSkinPosition(std::size_t loadedIndex);
  void Popup(HMENU menu);

  // Dashboard window + the toggles behind it
  void ShowManager();
  void RefreshManagerList();
  std::vector<std::size_t> GroupIndices(bool timeGroup) const;
  std::size_t ActiveTimeSkin() const;
  std::wstring FolderOf(std::size_t availIndex) const;
  void SetGroupEnabled(bool timeGroup, bool on);
  void CycleTimeStyle();
  void ApplyTimeFormat();
  void ShowGroupMenu(bool timeGroup);
  void UpdateIdleTimer();
  void OnIdleTick();
  void SetSkinsVisible(bool visible);

  // Widget Library
  void RebuildLibrary();
  skin::VarOverrides BuildOverrides(const library::WidgetDef& def) const;
  void ApplyHubConfig();
  void ApplyTaskbar();
  void ImportSkin();
  bool IsInstalled(const library::WidgetDef& def) const;

  HINSTANCE m_hInst = nullptr;
  HWND m_hwnd = nullptr;
  UINT m_taskbarCreated = 0;
  fs::path m_dataDir, m_skinsRoot;

  std::wstring m_tip = L"Tip: Customize each widget using the settings button.";
  bool m_idleHidden = false;
  bool m_autostart = false;
  std::vector<ui::LibItem> m_lib;
  bool m_libDirty = true;
  platform::TaskbarEffect m_taskbar;
  bool m_taskbarOn = false;

  HANDLE m_watch = INVALID_HANDLE_VALUE;
  HANDLE m_watchWait = nullptr;

  struct Drag {
    bool pressed = false, moving = false, rightDown = false;
    std::size_t skin = 0;
    POINT start{}, grab{};
  } m_drag;

  // Member order = construction order = reverse destruction order.
  render::GraphicsDevice m_gfx;
  platform::DesktopHost m_host;
  config::Settings m_settings;
  skin::MeterPool m_meterPool;            // before m_dash: the dashboard's preview skin returns meters to it
  std::unique_ptr<ui::Dashboard> m_dash;  // after m_gfx: destroyed before the GPU device
  std::vector<AvailableSkin> m_available;
  std::vector<LoadedSkin> m_skins;
  input::MouseRouter m_router;
};

// ---------------------------------------------------------------------------------------------
// Startup / shutdown
// ---------------------------------------------------------------------------------------------
bool App::Init(HINSTANCE hi, bool showUi) {
  m_hInst = hi;

  wchar_t exe[MAX_PATH];
  GetModuleFileNameW(nullptr, exe, MAX_PATH);
  m_dataDir = ChooseDataDir(fs::path(exe).parent_path());
  m_skinsRoot = m_dataDir / L"Skins";
  core::Log::Open(m_dataDir / L"DeskSkin.log");
  core::Log::Write("DeskSkin 0.2 starting. data dir: {}", Utf8(m_dataDir.wstring()));

  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = &App::ControlProc;
  wc.hInstance = hi;
  wc.lpszClassName = kControlClass;
  if (!RegisterClassExW(&wc)) return false;

  // Hidden *top-level* window (not message-only): only those receive the broadcast
  // "TaskbarCreated" message that says Explorer restarted.
  m_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kControlClass, L"DeskSkin", WS_POPUP, 0, 0, 0, 0, nullptr, nullptr, hi, this);
  if (!m_hwnd) return false;
  m_taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");

  m_settings.SetPath(m_dataDir / L"settings.ini");
  core::Log::Write("settings loaded: {}", m_settings.Load());
  ApplyHubConfig();
  skin::SetPromptHandler(&PromptThunk);
  skin::SetTimeFormat({m_settings.App().use24h, m_settings.App().showDate});
  platform::autostart::SyncPath();

  const HRESULT hr = m_gfx.Initialize();
  if (FAILED(hr)) {
    core::Log::Write("graphics init failed: 0x{:08x}", static_cast<unsigned>(hr));
    MessageBoxW(nullptr, L"DeskSkin could not initialise Direct3D / Direct2D.", L"DeskSkin", MB_ICONERROR);
    return false;
  }

  m_dash = std::make_unique<ui::Dashboard>(m_gfx, *this, m_hInst);
  m_autostart = platform::autostart::IsEnabled();
  AddTrayIcon();
  EnsureDefaultSkin();
  if (!m_router.Install(this)) core::Log::Write("mouse hook failed: widgets will not be draggable");
  // At logon Explorer may not have created the desktop yet: keep retrying instead of giving up.
  if (!TryAttachAndLoad()) SetTimer(m_hwnd, TIMER_ATTACH, 1000, nullptr);
  StartWatch();
  ApplyTaskbar();
  UpdateIdleTimer();
  if (showUi) ShowManager();
  return true;
}

void App::Shutdown() {
  if (m_taskbarOn) m_taskbar.Restore();
  measures::Hub::Get().Shutdown();
  if (m_dash) m_dash->Close();
  m_router.Uninstall();
  StopWatch();
  UnloadSkins();
}

bool App::TryAttachAndLoad() {
  if (!m_host.Attach()) {
    core::Log::Write("desktop (Progman) not available yet");
    return false;
  }
  core::Log::Write("attached to desktop, layout={}, parent={}", static_cast<int>(m_host.GetLayout()),
                   reinterpret_cast<std::uintptr_t>(m_host.Parent()));
  LoadSkins();
  return true;
}

void App::EnsureDefaultSkin() {
  std::error_code ec;
  bool any = false;
  if (fs::exists(m_skinsRoot, ec))
    for (const auto& e : fs::recursive_directory_iterator(m_skinsRoot, ec))
      if (e.is_regular_file(ec) && e.path().extension() == L".ini") { any = true; break; }
  if (any) return;
  fs::create_directories(m_skinsRoot / L"Demo", ec);
  HANDLE f = CreateFileW((m_skinsRoot / L"Demo" / L"Demo.ini").c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) return;
  DWORD written = 0;
  WriteFile(f, skin::kDefaultSkinIni, static_cast<DWORD>(sizeof(skin::kDefaultSkinIni) - 1), &written, nullptr);
  CloseHandle(f);
  core::Log::Write("wrote default demo skin");
}

// ---------------------------------------------------------------------------------------------
// Skin management
// ---------------------------------------------------------------------------------------------
void App::ScanSkins() {
  m_available.clear();
  std::error_code ec;
  if (!fs::exists(m_skinsRoot, ec)) return;
  // Layout: Skins\<SkinName>\<anything>.ini - each .ini file is one skin (Rainmeter-style).
  for (const auto& dir : fs::directory_iterator(m_skinsRoot, ec)) {
    if (!dir.is_directory(ec)) continue;
    for (const auto& file : fs::directory_iterator(dir.path(), ec)) {
      if (!file.is_regular_file(ec) || file.path().extension() != L".ini") continue;
      AvailableSkin a;
      a.path = file.path();
      a.key = Utf8(fs::relative(a.path, m_skinsRoot, ec).generic_wstring());
      a.display = a.path.stem().wstring();
      m_available.push_back(std::move(a));
    }
  }
  std::sort(m_available.begin(), m_available.end(), [](const auto& l, const auto& r) { return l.key < r.key; });
}

void App::LoadSkins() {
  ScanSkins();
  const RECT vs{GetSystemMetrics(SM_XVIRTUALSCREEN), GetSystemMetrics(SM_YVIRTUALSCREEN),
                GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN),
                GetSystemMetrics(SM_YVIRTUALSCREEN) + GetSystemMetrics(SM_CYVIRTUALSCREEN)};
  RECT work{};
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &work, 0);
  const int sysDpi = static_cast<int>(GetDpiForSystem());
  std::vector<RECT> taken;
  bool positionsChanged = false;

  for (std::size_t i = 0; i < m_available.size(); ++i) {
    const auto& a = m_available[i];
    const config::SkinState st = m_settings.Get(a.key);
    if (!st.enabled) continue;

    // Library widgets receive their appearance / configuration options as ini variable overrides.
    skin::VarOverrides overrides;
    if (const library::WidgetDef* def = WidgetForKey(a.key)) overrides = BuildOverrides(*def);

    std::vector<std::string> warnings;
    auto content = skin::BasicSkin::Load(a.path, m_gfx, m_meterPool, &warnings, overrides);
    for (const auto& w : warnings) core::Log::Write("skin warning: {}", w);
    if (!content) {
      core::Log::Write("skin failed to load: {}", a.key);
      continue;
    }

    render::SkinWindowDesc desc = content->WindowDesc();
    const POINT saved{st.x, st.y};
    const int wpx = MulDiv(desc.widthDip, sysDpi, 96), hpx = MulDiv(desc.heightDip, sysDpi, 96);
    if (st.hasPosition && PtInRect(&vs, saved)) {  // ignore positions from a monitor that is gone
      desc.x = st.x;
      desc.y = st.y;
    } else {  // first run of this widget: put it somewhere free and remember it
      const POINT p = FindFreeSpot(wpx, hpx, POINT{desc.x, desc.y}, taken, work);
      desc.x = p.x;
      desc.y = p.y;
      auto& ms = m_settings.Skin(a.key);
      ms.hasPosition = true;
      ms.x = p.x;
      ms.y = p.y;
      positionsChanged = true;
    }

    auto window = std::make_unique<render::SkinWindow>(m_gfx, m_host);
    window->SetNotifyWindow(m_hwnd, WM_DEVICE_LOST);
    const HRESULT hr = window->Create(desc, content.get());
    core::Log::Write("skin '{}': create hr=0x{:08x} onWallpaperLayer={}", a.key, static_cast<unsigned>(hr),
                     hr >= 0 && window->IsOnWallpaperLayer());
    if (FAILED(hr)) continue;
    taken.push_back(RECT{desc.x, desc.y, desc.x + wpx, desc.y + hpx});

    LoadedSkin ls;
    ls.content = std::move(content);
    ls.window = std::move(window);
    ls.avail = i;
    ls.locked = st.locked;
    ls.clickThrough = st.clickThrough;
    m_skins.push_back(std::move(ls));
  }
  if (positionsChanged) m_settings.Save();
  UpdateTimer();
  RefreshManagerList();
  m_gfx.Trim();  // return load-time garbage to the OS: the idle footprint is measured after this
}

void App::UnloadSkins() {
  m_drag = {};
  m_idleHidden = false;
  m_skins.clear();
  m_gfx.Trim();
}

void App::Reload(bool resetGraphics) {
  KillTimer(m_hwnd, TIMER_TICK);
  UnloadSkins();
  if (resetGraphics) {
    if (FAILED(m_gfx.Initialize())) return;
    if (m_dash) m_dash->ResetResources();
  }
  if (!TryAttachAndLoad()) SetTimer(m_hwnd, TIMER_ATTACH, 1000, nullptr);
}

// Event-driven: no timer unless some skin has time-varying content, and then only at the fastest
// rate any skin asked for. A static skin costs zero wake-ups.
void App::UpdateTimer() {
  unsigned interval = 0;
  for (const auto& s : m_skins)
    if (s.content->HasDynamicContent())
      interval = interval ? std::min(interval, s.content->UpdateMs()) : s.content->UpdateMs();
  if (interval && m_settings.App().performanceMode && interval < 1000) interval = 1000;  // Performance Mode
  if (interval)
    SetTimer(m_hwnd, TIMER_TICK, interval, nullptr);
  else
    KillTimer(m_hwnd, TIMER_TICK);
}

void App::OnTick() {
  for (auto& s : m_skins) {
    RECT dirty;
    if (s.content->Tick(&dirty)) s.window->Invalidate(dirty);
  }
}

// ---------------------------------------------------------------------------------------------
// Mouse: hit-testing, dragging, click and context-menu dispatch
// ---------------------------------------------------------------------------------------------
int App::HitTest(POINT pt) const {
  for (std::size_t i = m_skins.size(); i-- > 0;) {
    const auto& s = m_skins[i];
    if (s.clickThrough || !s.window->IsAlive() || !s.window->IsVisible()) continue;
    const RECT r = s.window->ScreenRect();
    if (PtInRect(&r, pt)) return static_cast<int>(i);
  }
  return -1;
}

bool App::OnDesktopMouse(UINT msg, POINT pt) {
  switch (msg) {
    case WM_LBUTTONDOWN: {
      const int i = HitTest(pt);
      if (i < 0 || !IsDesktopSurface(pt)) return false;
      const RECT r = m_skins[static_cast<std::size_t>(i)].window->ScreenRect();
      m_drag = {};
      m_drag.pressed = true;
      m_drag.skin = static_cast<std::size_t>(i);
      m_drag.start = pt;
      m_drag.grab = POINT{pt.x - r.left, pt.y - r.top};
      return true;
    }
    case WM_MOUSEMOVE: {
      if (!m_drag.pressed || m_drag.skin >= m_skins.size()) return false;
      if (!m_drag.moving && (std::abs(pt.x - m_drag.start.x) >= 4 || std::abs(pt.y - m_drag.start.y) >= 4))
        m_drag.moving = true;
      auto& s = m_skins[m_drag.skin];
      if (m_drag.moving && !s.locked) {
        const RECT r = s.window->ScreenRect();
        const int w = r.right - r.left, h = r.bottom - r.top;
        const int vx = GetSystemMetrics(SM_XVIRTUALSCREEN), vy = GetSystemMetrics(SM_YVIRTUALSCREEN);
        const int vw = GetSystemMetrics(SM_CXVIRTUALSCREEN), vh = GetSystemMetrics(SM_CYVIRTUALSCREEN);
        const int x = std::clamp(static_cast<int>(pt.x - m_drag.grab.x), vx - w + 40, vx + vw - 40);
        const int y = std::clamp(static_cast<int>(pt.y - m_drag.grab.y), vy - h + 40, vy + vh - 40);
        s.window->MoveTo(x, y);
      }
      return false;  // never swallow moves
    }
    case WM_LBUTTONUP: {
      if (!m_drag.pressed) return false;
      const Drag d = m_drag;
      m_drag = {};
      if (d.skin >= m_skins.size()) return true;
      const auto& s = m_skins[d.skin];
      if (d.moving) {
        if (!s.locked) PostMessageW(m_hwnd, WM_SKIN_DRAGGED, d.skin, 0);
      } else {
        const RECT r = s.window->ScreenRect();
        const int dpi = static_cast<int>(s.window->Dpi());
        PostMessageW(m_hwnd, WM_SKIN_CLICK, d.skin,
                     MAKELPARAM(MulDiv(pt.x - r.left, 96, dpi), MulDiv(pt.y - r.top, 96, dpi)));
      }
      return true;
    }
    case WM_RBUTTONDOWN: {
      const int i = HitTest(pt);
      if (i < 0 || !IsDesktopSurface(pt)) return false;
      m_drag.rightDown = true;  // swallow the matching button-up too so the system never sees half a click
      return true;
    }
    case WM_RBUTTONUP: {
      if (!m_drag.rightDown) return false;
      m_drag.rightDown = false;
      const int i = HitTest(pt);
      if (i >= 0) PostMessageW(m_hwnd, WM_SKIN_MENU, static_cast<WPARAM>(i), 0);
      return true;
    }
  }
  return false;
}

void App::SaveSkinPosition(std::size_t loadedIndex) {
  if (loadedIndex >= m_skins.size()) return;
  const auto& s = m_skins[loadedIndex];
  const RECT r = s.window->ScreenRect();  // authoritative: where the window actually is
  auto& st = m_settings.Skin(m_available[s.avail].key);
  st.hasPosition = true;
  st.x = r.left;
  st.y = r.top;
  m_settings.Save();
}

// ---------------------------------------------------------------------------------------------
// Live reload: one kernel change-notification handle on Skins\ - zero polling, zero idle cost
// ---------------------------------------------------------------------------------------------
VOID CALLBACK App::WatchCallback(PVOID ctx, BOOLEAN) {
  PostMessageW(static_cast<App*>(ctx)->m_hwnd, WM_SKINS_CHANGED, 0, 0);  // thread-pool thread: post only
}

void App::StartWatch() {
  StopWatch();
  std::error_code ec;
  if (!fs::exists(m_skinsRoot, ec)) return;
  m_watch = FindFirstChangeNotificationW(m_skinsRoot.c_str(), TRUE,
                                         FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_FILE_NAME |
                                             FILE_NOTIFY_CHANGE_DIR_NAME);
  if (m_watch == INVALID_HANDLE_VALUE) return;
  RegisterWaitForSingleObject(&m_watchWait, m_watch, &App::WatchCallback, this, INFINITE,
                              WT_EXECUTEONLYONCE | WT_EXECUTEINWAITTHREAD);
}

void App::RearmWatch() {
  if (m_watch == INVALID_HANDLE_VALUE) return StartWatch();
  if (m_watchWait) UnregisterWaitEx(m_watchWait, INVALID_HANDLE_VALUE);
  m_watchWait = nullptr;
  FindNextChangeNotification(m_watch);
  RegisterWaitForSingleObject(&m_watchWait, m_watch, &App::WatchCallback, this, INFINITE,
                              WT_EXECUTEONLYONCE | WT_EXECUTEINWAITTHREAD);
}

void App::StopWatch() {
  if (m_watchWait) UnregisterWaitEx(m_watchWait, INVALID_HANDLE_VALUE);
  m_watchWait = nullptr;
  if (m_watch != INVALID_HANDLE_VALUE) FindCloseChangeNotification(m_watch);
  m_watch = INVALID_HANDLE_VALUE;
}

// ---------------------------------------------------------------------------------------------
// Tray + menus
// ---------------------------------------------------------------------------------------------
void App::AddTrayIcon() {
  NOTIFYICONDATAW nid{sizeof(nid)};
  nid.hWnd = m_hwnd;
  nid.uID = 1;
  nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  nid.uCallbackMessage = WM_TRAY;
  nid.hIcon = LoadIconW(m_hInst, MAKEINTRESOURCEW(1));
  if (!nid.hIcon) nid.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
  wcscpy_s(nid.szTip, L"DeskSkin");
  Shell_NotifyIconW(NIM_ADD, &nid);
}

void App::Popup(HMENU menu) {
  POINT pt;
  GetCursorPos(&pt);
  SetForegroundWindow(m_hwnd);  // required, or the menu will not dismiss when clicking elsewhere
  TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, m_hwnd, nullptr);
  PostMessageW(m_hwnd, WM_NULL, 0, 0);
  DestroyMenu(menu);  // also destroys attached submenus
}

void App::AppendSkinItems(HMENU menu, std::size_t i) {
  const config::SkinState st = m_settings.Get(m_available[i].key);
  const UINT base = ID_SKIN_BASE + static_cast<UINT>(i) * ActCount;
  AppendMenuW(menu, MF_STRING | (st.enabled ? MF_CHECKED : 0), base + ActEnabled, L"Enabled");
  AppendMenuW(menu, MF_STRING | (st.locked ? MF_CHECKED : 0), base + ActLocked, L"Lock position");
  AppendMenuW(menu, MF_STRING | (st.clickThrough ? MF_CHECKED : 0), base + ActClickThrough,
              L"Click-through (undo from tray menu)");
  AppendMenuW(menu, MF_STRING, base + ActEdit, L"Edit skin file...");
  AppendMenuW(menu, MF_STRING, base + ActResetPos, L"Reset position");
}

void App::ShowTrayMenu() {
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING, ID_OPEN_UI, L"Open DeskSkin window...");
  AppendMenuW(menu, MF_STRING, ID_REFRESH, L"Refresh all skins");

  HMENU skins = CreatePopupMenu();
  if (m_available.empty()) AppendMenuW(skins, MF_STRING | MF_GRAYED, 0, L"(no skins found)");
  for (std::size_t i = 0; i < m_available.size(); ++i) {
    HMENU sub = CreatePopupMenu();
    AppendSkinItems(sub, i);
    AppendMenuW(skins, MF_POPUP | (m_settings.Get(m_available[i].key).enabled ? MF_CHECKED : 0),
                reinterpret_cast<UINT_PTR>(sub), m_available[i].display.c_str());
  }
  AppendMenuW(menu, MF_POPUP, reinterpret_cast<UINT_PTR>(skins), L"Skins");
  AppendMenuW(menu, MF_STRING, ID_RESET_ALL, L"Reset all positions");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING | (platform::autostart::IsEnabled() ? MF_CHECKED : 0), ID_AUTOSTART,
              L"Start with Windows");
  AppendMenuW(menu, MF_STRING, ID_OPEN_FOLDER, L"Open skins folder");
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, ID_QUIT, L"Exit");
  Popup(menu);
}

void App::ShowSkinMenu(std::size_t loadedIndex) {
  if (loadedIndex >= m_skins.size()) return;
  HMENU menu = CreatePopupMenu();
  AppendSkinItems(menu, m_skins[loadedIndex].avail);
  AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(menu, MF_STRING, ID_REFRESH, L"Refresh all skins");
  Popup(menu);
}

void App::HandleSkinAction(std::size_t i, UINT action) {
  if (i >= m_available.size()) return;
  const std::string key = m_available[i].key;
  auto& st = m_settings.Skin(key);
  switch (action) {
    case ActEnabled:
      st.enabled = !st.enabled;
      m_settings.Save();
      Reload(false);
      return;
    case ActLocked:
    case ActClickThrough:
      if (action == ActLocked) st.locked = !st.locked; else st.clickThrough = !st.clickThrough;
      for (auto& s : m_skins)
        if (m_available[s.avail].key == key) { s.locked = st.locked; s.clickThrough = st.clickThrough; }
      m_settings.Save();
      return;
    case ActEdit: {
      const std::wstring arg = L"\"" + m_available[i].path.wstring() + L"\"";
      ShellExecuteW(nullptr, L"open", L"notepad.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
      return;
    }
    case ActResetPos:
      st.hasPosition = false;
      m_settings.Save();
      Reload(false);
      return;
  }
}

// ---------------------------------------------------------------------------------------------
// Dashboard host: the toggles on the dashboard act directly on skins and settings
// ---------------------------------------------------------------------------------------------
const wchar_t* const kTimeSkins[] = {L"Demo", L"MinimalClock", L"NeonClock", L"DateCard", L"illustro-clock", L"elegance-clock"};
const wchar_t* const kActionSkins[] = {L"QuickLaunch"};

std::wstring StyleLabel(const std::wstring& folder) {
  if (folder == L"MinimalClock") return L"Minimal";
  if (folder == L"NeonClock") return L"Neon";
  if (folder == L"DateCard") return L"Card";
  if (folder == L"Demo") return L"Classic";
  if (folder == L"illustro-clock") return L"Illustro";
  if (folder == L"elegance-clock") return L"Elegance";
  return folder;
}

void App::ShowManager() {
  if (m_dash) m_dash->Show();
}

void App::RefreshManagerList() {
  m_libDirty = true;
  if (m_dash) m_dash->Refresh();
}

std::wstring App::FolderOf(std::size_t i) const {
  return i < m_available.size() ? m_available[i].path.parent_path().filename().wstring() : std::wstring();
}

std::vector<std::size_t> App::GroupIndices(bool timeGroup) const {
  std::vector<std::size_t> out;
  for (std::size_t i = 0; i < m_available.size(); ++i) {
    const std::wstring folder = FolderOf(i);
    if (timeGroup) {
      for (const wchar_t* n : kTimeSkins) if (_wcsicmp(folder.c_str(), n) == 0) out.push_back(i);
    } else {
      for (const wchar_t* n : kActionSkins) if (_wcsicmp(folder.c_str(), n) == 0) out.push_back(i);
    }
  }
  return out;
}

// The Time skin currently shown, else the remembered style, else the first one available.
std::size_t App::ActiveTimeSkin() const {
  const auto g = GroupIndices(true);
  if (g.empty()) return static_cast<std::size_t>(-1);
  for (std::size_t i : g)
    if (m_settings.Get(m_available[i].key).enabled) return i;
  for (std::size_t i : g)
    if (Utf8(FolderOf(i)) == m_settings.App().timeStyle) return i;
  return g.front();
}

ui::DashboardModel App::GetModel() {
  ui::DashboardModel m;
  const auto t = GroupIndices(true), a = GroupIndices(false);
  m.timeAvailable = !t.empty();
  m.actionsAvailable = !a.empty();
  for (std::size_t i : t) if (m_settings.Get(m_available[i].key).enabled) m.timeOn = true;
  for (std::size_t i : a) if (m_settings.Get(m_available[i].key).enabled) m.actionsOn = true;
  const std::size_t ti = ActiveTimeSkin();
  m.timeStyle = ti == static_cast<std::size_t>(-1) ? L"-" : StyleLabel(FolderOf(ti));
  m.startWithWindows = m_autostart;
  const config::AppState& st = m_settings.App();
  m.use24h = st.use24h;
  m.showDate = st.showDate;
  m.performance = st.performanceMode;
  m.autoHide = st.autoHideOnIdle;
  m.accent = st.accent;
  m.tip = m_tip;
  return m;
}

void App::SetGroupEnabled(bool timeGroup, bool on) {
  const auto g = GroupIndices(timeGroup);
  if (g.empty()) return;
  const std::size_t target = timeGroup ? ActiveTimeSkin() : g.front();
  for (std::size_t i : g)
    m_settings.Skin(m_available[i].key).enabled = on && (!timeGroup || i == target);
  if (timeGroup && on) m_settings.App().timeStyle = Utf8(FolderOf(target));
  m_settings.Save();
  Reload(false);
}

void App::CycleTimeStyle() {
  const auto g = GroupIndices(true);
  if (g.empty()) return;
  const std::size_t cur = ActiveTimeSkin();
  std::size_t pos = 0;
  for (std::size_t k = 0; k < g.size(); ++k) if (g[k] == cur) pos = k;
  const std::size_t next = g[(pos + 1) % g.size()];
  m_settings.App().timeStyle = Utf8(FolderOf(next));
  bool wasOn = false;
  for (std::size_t i : g) wasOn = wasOn || m_settings.Get(m_available[i].key).enabled;
  if (wasOn) {
    for (std::size_t i : g) m_settings.Skin(m_available[i].key).enabled = (i == next);
    m_settings.Save();
    Reload(false);
  } else {
    m_settings.Save();
  }
}

void App::ApplyTimeFormat() {
  skin::SetTimeFormat({m_settings.App().use24h, m_settings.App().showDate});
  m_settings.Save();
  OnTick();  // dynamic meters re-resolve their text and repaint only if it changed
}

void App::ShowGroupMenu(bool timeGroup) {
  const std::size_t i = timeGroup ? ActiveTimeSkin() : (GroupIndices(false).empty() ? static_cast<std::size_t>(-1) : GroupIndices(false).front());
  if (i >= m_available.size()) return;
  const config::SkinState st = m_settings.Get(m_available[i].key);
  const UINT base = ID_SKIN_BASE + static_cast<UINT>(i) * ActCount;
  HMENU menu = CreatePopupMenu();
  AppendMenuW(menu, MF_STRING | (st.locked ? MF_CHECKED : 0), base + ActLocked, L"Lock position");
  AppendMenuW(menu, MF_STRING | (st.clickThrough ? MF_CHECKED : 0), base + ActClickThrough, L"Click-through");
  AppendMenuW(menu, MF_STRING, base + ActResetPos, L"Reset position");
  AppendMenuW(menu, MF_STRING, base + ActEdit, L"Edit skin file...");
  Popup(menu);
}

void App::OnCommand(ui::Cmd cmd, int value) {
  using ui::Cmd;
  config::AppState& st = m_settings.App();
  switch (cmd) {
    case Cmd::TimeOn: SetGroupEnabled(true, value != 0); break;
    case Cmd::ActionsOn: SetGroupEnabled(false, value != 0); break;
    case Cmd::StartWithWindows:
      platform::autostart::SetEnabled(value != 0);
      m_autostart = platform::autostart::IsEnabled();
      break;
    case Cmd::Use24h: st.use24h = value != 0; ApplyTimeFormat(); break;
    case Cmd::ShowDate: st.showDate = value != 0; ApplyTimeFormat(); break;
    case Cmd::CycleTimeStyle: CycleTimeStyle(); break;
    case Cmd::Performance:
      st.performanceMode = value != 0;
      m_settings.Save();
      UpdateTimer();
      break;
    case Cmd::AutoHide:
      st.autoHideOnIdle = value != 0;
      m_settings.Save();
      UpdateIdleTimer();
      break;
    case Cmd::Accent: st.accent = value; m_settings.Save(); break;
    case Cmd::TimeMenu: ShowGroupMenu(true); break;
    case Cmd::ActionsMenu: ShowGroupMenu(false); break;
    case Cmd::EditActions: {
      const auto a = GroupIndices(false);
      if (a.empty()) m_tip = L"No Quick Actions skin found in the Skins folder.";
      else HandleSkinAction(a.front(), ActEdit);
      break;
    }
    case Cmd::CustomizeLayout:
      for (const auto& av : m_available) m_settings.Skin(av.key).locked = false;
      for (auto& s : m_skins) s.locked = false;
      m_settings.Save();
      m_tip = L"Layout mode: drag any widget on the desktop to move it. Positions are saved automatically.";
      break;
    case Cmd::RefreshAll: Reload(false); break;
    case Cmd::OpenFolder: ShellExecuteW(nullptr, L"open", m_skinsRoot.c_str(), nullptr, nullptr, SW_SHOWNORMAL); break;
    case Cmd::ResetPositions:
      m_settings.ResetPositions();
      m_settings.Save();
      Reload(false);
      break;
  }
  RefreshManagerList();
}

// Auto-Hide on Idle: while enabled, a 5-second timer checks the last user input; no input for
// 5 minutes hides every widget, and the next input shows them again. Disabled = no timer at all.
void App::UpdateIdleTimer() {
  if (m_settings.App().autoHideOnIdle) {
    SetTimer(m_hwnd, TIMER_IDLE, 5000, nullptr);
  } else {
    KillTimer(m_hwnd, TIMER_IDLE);
    if (m_idleHidden) SetSkinsVisible(true);
  }
}

void App::OnIdleTick() {
  LASTINPUTINFO li{sizeof(li)};
  if (!GetLastInputInfo(&li)) return;
  const bool shouldHide = (GetTickCount() - li.dwTime) > 5u * 60u * 1000u;
  if (shouldHide != m_idleHidden) SetSkinsVisible(!shouldHide);
}

void App::SetSkinsVisible(bool visible) {
  m_idleHidden = !visible;
  for (auto& s : m_skins) s.window->SetVisible(visible);
}

// ---------------------------------------------------------------------------------------------
// Widget Library: install / enable / configure
// ---------------------------------------------------------------------------------------------
bool App::IsInstalled(const library::WidgetDef& def) const {
  if (def.feature) {
    const auto o = m_settings.OptionsOf(def.id);
    const auto it = o.find("installed");
    return it != o.end() && it->second == "1";
  }
  std::error_code ec;
  const std::wstring id = Widen(def.id);
  return fs::exists(m_skinsRoot / id / (id + L".ini"), ec);
}

skin::VarOverrides App::BuildOverrides(const library::WidgetDef& def) const {
  skin::VarOverrides out;
  const auto stored = m_settings.OptionsOf(def.id);
  for (const auto& o : def.options) {
    if (o.shared) continue;
    const auto it = stored.find(o.key);
    std::string value = library::ResolveValue(o, it == stored.end() ? std::string() : it->second);
    if (std::string_view(o.key).rfind("Drive", 0) == 0) {  // a drive letter: "d", "D:" -> "D"
      char letter = 'C';
      for (char c : value) if (std::isalpha(static_cast<unsigned char>(c))) { letter = static_cast<char>(std::toupper(static_cast<unsigned char>(c))); break; }
      value.assign(1, letter);
    }
    out.emplace_back(o.key, std::move(value));
  }
  return out;
}

void App::ApplyHubConfig() {
  measures::HubConfig cfg;
  const auto& sh = m_settings.Shared();
  auto get = [&](const char* key, const char* def) {
    const auto it = sh.find(key);
    return it == sh.end() ? std::string(def) : it->second;
  };
  cfg.city = get("shared.city", "");
  cfg.units = get("shared.units", "Metric") == "Imperial" ? "imperial" : "metric";
  cfg.feedUrl = get("shared.feedUrl", "https://feeds.bbci.co.uk/news/rss.xml");
  cfg.notesPath = (m_dataDir / L"notes.txt").wstring();
  measures::Hub::Get().SetConfig(cfg);
}

void App::ApplyTaskbar() {
  const library::WidgetDef* def = library::Find("translucent-taskbar");
  const auto o = m_settings.OptionsOf("translucent-taskbar");
  auto flag = [&](const char* k) { const auto it = o.find(k); return it != o.end() && it->second == "1"; };
  const bool on = def && flag("installed") && flag("enabled");
  if (!on) {
    if (m_taskbarOn) {
      m_taskbar.Restore();
      m_taskbarOn = false;
      KillTimer(m_hwnd, TIMER_TASKBAR);
    }
    return;
  }
  std::string style = "blur", opacity = "50";
  for (const auto& od : def->options) {
    const auto it = o.find(od.key);
    const std::string v = library::ResolveValue(od, it == o.end() ? std::string() : it->second);
    if (std::string_view(od.key) == "style") style = v; else if (std::string_view(od.key) == "opacity") opacity = v;
  }
  m_taskbar.Apply(style == "clear" ? platform::TaskbarStyle::Clear : (style == "acrylic" ? platform::TaskbarStyle::Acrylic : platform::TaskbarStyle::Blur),
                  static_cast<unsigned>(std::clamp(std::atoi(opacity.c_str()), 0, 255)));
  if (!m_taskbarOn) {
    m_taskbarOn = true;
    SetTimer(m_hwnd, TIMER_TASKBAR, 2000, nullptr);  // only runs while the effect is on
  }
}

void App::RebuildLibrary() {
  m_lib.clear();
  const auto& fav = m_settings.Favorites();
  for (const library::WidgetDef& w : library::All()) {
    ui::LibItem it;
    it.id = w.id;
    it.name = Widen(w.name);
    it.category = Widen(library::CategoryName(w.category));
    it.categoryIndex = static_cast<int>(w.category);
    it.description = Widen(w.description);
    it.author = Widen(w.author);
    it.source = Widen(w.source);
    it.needs = Widen(w.needs);
    it.feature = w.feature;
    it.favorite = fav.count(w.id) != 0;
    it.installed = IsInstalled(w);
    if (w.feature) {
      const auto o = m_settings.OptionsOf(w.id);
      const auto e = o.find("enabled");
      it.enabled = it.installed && e != o.end() && e->second == "1";
    } else {
      it.enabled = it.installed && m_settings.Get(std::string(w.id) + "/" + w.id + ".ini").enabled;
    }
    it.livePreview = it.installed || std::string_view(w.needs).find("Internet") == std::string_view::npos;
    const std::map<std::string, std::string> own = m_settings.OptionsOf(w.id);
    for (const auto& od : w.options) {
      const std::map<std::string, std::string>& store = od.shared ? m_settings.Shared() : own;
      const auto f = store.find(od.key);
      const bool present = f != store.end();
      const bool isChoice = !std::string_view(od.choices).empty();
      const std::string shown = isChoice ? library::DisplayValue(od, present ? f->second : std::string())
                                         : (present ? f->second : std::string(od.def));
      it.options.push_back({od.key, Widen(od.label), Widen(shown)});
    }
    m_lib.push_back(std::move(it));
  }
}

const std::vector<ui::LibItem>& App::Library() {
  if (m_libDirty) {
    RebuildLibrary();
    m_libDirty = false;
  }
  return m_lib;
}

std::unique_ptr<skin::BasicSkin> App::BuildPreview(const std::string& id) {
  const library::WidgetDef* def = library::Find(id);
  if (!def || def->feature || !def->ini) return nullptr;
  std::vector<std::string> warnings;
  return skin::BasicSkin::LoadFromText(def->ini, Widen(def->name), m_gfx, m_meterPool, &warnings, BuildOverrides(*def));
}

void App::OnLibraryCommand(ui::LibCmd cmd, const std::string& id, const std::string& key) {
  using ui::LibCmd;
  const library::WidgetDef* def = id.empty() ? nullptr : library::Find(id);
  const std::string skinKey = def ? std::string(def->id) + "/" + def->id + ".ini" : std::string();
  switch (cmd) {
    case LibCmd::Install: {
      if (!def) break;
      if (def->feature) {
        auto& o = m_settings.Options(id);
        o["installed"] = "1";
        o["enabled"] = "1";
        m_settings.Save();
        ApplyTaskbar();
        break;
      }
      const fs::path dir = m_skinsRoot / Widen(def->id);
      std::error_code ec;
      fs::create_directories(dir, ec);
      const fs::path file = dir / (Widen(def->id) + L".ini");
      if (!fs::exists(file, ec)) {  // never overwrite a file the user may have edited
        HANDLE f = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (f == INVALID_HANDLE_VALUE) {
          MessageBoxW(m_dash ? m_dash->Hwnd() : nullptr, L"Could not write the skin file. Is the DeskSkin folder writable?", L"DeskSkin", MB_ICONERROR);
          break;
        }
        DWORD written = 0;
        const std::string_view text = def->ini;
        const std::string withBom = "\xEF\xBB\xBF" + std::string(text);  // so every editor reads it as UTF-8
        WriteFile(f, withBom.data(), static_cast<DWORD>(withBom.size()), &written, nullptr);
        CloseHandle(f);
      }
      m_settings.Skin(skinKey).enabled = true;
      m_settings.Save();
      core::Log::Write("installed widget {}", id);
      Reload(false);
      break;
    }
    case LibCmd::Uninstall: {
      if (!def) break;
      if (def->feature) {
        auto& o = m_settings.Options(id);
        o["installed"] = "0";
        o["enabled"] = "0";
        m_settings.Save();
        ApplyTaskbar();
        break;
      }
      std::error_code ec;
      fs::remove_all(m_skinsRoot / Widen(def->id), ec);
      m_settings.Skin(skinKey) = config::SkinState{};
      m_settings.Save();
      core::Log::Write("uninstalled widget {}", id);
      Reload(false);
      break;
    }
    case LibCmd::ToggleEnabled: {
      if (!def) break;
      if (def->feature) {
        auto& o = m_settings.Options(id);
        o["enabled"] = o["enabled"] == "1" ? "0" : "1";
        m_settings.Save();
        ApplyTaskbar();
      } else {
        auto& st = m_settings.Skin(skinKey);
        st.enabled = !st.enabled;
        m_settings.Save();
        Reload(false);
      }
      break;
    }
    case LibCmd::ToggleFavorite: {
      auto& fav = m_settings.Favorites();
      if (!fav.erase(id)) fav.insert(id);
      m_settings.Save();
      break;
    }
    case LibCmd::ResetPosition: {
      if (!def || def->feature) break;
      m_settings.Skin(skinKey).hasPosition = false;
      m_settings.Save();
      Reload(false);
      break;
    }
    case LibCmd::Option: {
      if (!def) break;
      const library::OptionDef* od = nullptr;
      for (const auto& o : def->options) if (key == o.key) od = &o;
      if (!od) break;
      auto& store = od->shared ? m_settings.Shared() : m_settings.Options(id);
      const auto it = store.find(key);
      const bool edited = it != store.end();
      const std::string cur = edited ? it->second : std::string();
      std::string next;
      if (std::string_view(od->choices).empty()) {
        std::wstring v = Widen(edited ? cur : std::string(od->def));
        if (!ui::PromptText(m_dash ? m_dash->Hwnd() : nullptr, Widen(od->label),
                            L"Enter a value (leave empty for none / automatic):", v))
          break;
        next = Utf8(v);
      } else {
        next = library::NextChoice(*od, cur);
      }
      store[key] = next;
      m_settings.Save();
      if (od->shared) ApplyHubConfig();
      else if (def->feature) ApplyTaskbar();
      else Reload(false);  // the skin re-reads its variables
      break;
    }
    case LibCmd::Import: ImportSkin(); break;
    case LibCmd::Discover:
      ShellExecuteW(nullptr, L"open", L"https://visualskins.com/", nullptr, nullptr, SW_SHOWNORMAL);
      break;
  }
  if (m_dash) m_dash->InvalidatePreview();
  RefreshManagerList();
}

// Imports a DeskSkin-format skin: a single .ini, or a .zip / .dskin archive containing skin folders.
// (Rainmeter .rmskin packages are a different format and are not supported.)
void App::ImportSkin() {
  HWND owner = m_dash ? m_dash->Hwnd() : nullptr;
  wchar_t file[MAX_PATH] = {};
  OPENFILENAMEW ofn{sizeof(ofn)};
  ofn.hwndOwner = owner;
  ofn.lpstrFilter = L"DeskSkin skins (*.zip;*.dskin;*.ini)\0*.zip;*.dskin;*.ini\0All files\0*.*\0";
  ofn.lpstrFile = file;
  ofn.nMaxFile = MAX_PATH;
  ofn.lpstrTitle = L"Import a DeskSkin skin";
  ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;
  if (!GetOpenFileNameW(&ofn)) return;

  if (MessageBoxW(owner,
                  L"Skins can open programs, files and web pages when you click them.\nOnly import skins from people you trust.\n\nContinue?",
                  L"Import skin", MB_YESNO | MB_ICONWARNING) != IDYES)
    return;

  const fs::path src(file);
  std::wstring ext = src.extension().wstring();
  for (auto& c : ext) c = static_cast<wchar_t>(towlower(c));
  std::error_code ec;
  int imported = 0;
  auto hasSkinSection = [](const fs::path& ini) {
    auto doc = ini::Document::ParseFile(ini);
    return doc && doc->HasSection("Skin");
  };
  auto uniqueDest = [&](const std::wstring& name) {
    fs::path d = m_skinsRoot / name;
    for (int n = 2; fs::exists(d, ec); ++n) d = m_skinsRoot / (name + L"-" + std::to_wstring(n));
    return d;
  };

  if (ext == L".ini") {
    if (!hasSkinSection(src)) {
      MessageBoxW(owner, L"That file is not a DeskSkin skin (it has no [Skin] section).", L"Import skin", MB_ICONINFORMATION);
      return;
    }
    const fs::path dest = uniqueDest(src.stem().wstring());
    fs::create_directories(dest, ec);
    fs::copy_file(src, dest / src.filename(), fs::copy_options::overwrite_existing, ec);
    imported = ec ? 0 : 1;
  } else {
    // Windows 10 (1803+) and 11 ship bsdtar as tar.exe, which also reads .zip and refuses paths that escape the folder.
    const fs::path tmp = m_dataDir / L"import_tmp";
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp, ec);
    std::wstring cmdline = L"tar.exe -xf \"" + src.wstring() + L"\" -C \"" + tmp.wstring() + L"\"";
    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    DWORD exitCode = 1;
    if (CreateProcessW(nullptr, cmdline.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
      if (WaitForSingleObject(pi.hProcess, 60000) == WAIT_OBJECT_0) GetExitCodeProcess(pi.hProcess, &exitCode);
      else TerminateProcess(pi.hProcess, 1);
      CloseHandle(pi.hThread);
      CloseHandle(pi.hProcess);
    }
    if (exitCode != 0) {
      fs::remove_all(tmp, ec);
      MessageBoxW(owner, L"Could not unpack that archive. (It must be a .zip; this needs the tar.exe that comes with Windows 10/11.)",
                  L"Import skin", MB_ICONERROR);
      return;
    }
    for (const auto& entry : fs::directory_iterator(tmp, ec)) {
      if (!entry.is_directory(ec)) continue;
      bool ok = false;
      for (const auto& f : fs::recursive_directory_iterator(entry.path(), ec))
        if (f.is_regular_file(ec) && f.path().extension() == L".ini" && hasSkinSection(f.path())) { ok = true; break; }
      if (!ok) continue;
      const fs::path dest = uniqueDest(entry.path().filename().wstring());
      fs::rename(entry.path(), dest, ec);
      if (ec) {
        ec.clear();
        fs::copy(entry.path(), dest, fs::copy_options::recursive, ec);
      }
      if (!ec) ++imported;
    }
    fs::remove_all(tmp, ec);
  }

  if (imported > 0) {
    const std::wstring msg = std::to_wstring(imported) + L" skin(s) imported. They appear in the tray menu under Skins.";
    MessageBoxW(owner, msg.c_str(), L"Import skin", MB_ICONINFORMATION);
    Reload(false);
  } else {
    MessageBoxW(owner, L"No DeskSkin skins were found in that file.\n(Rainmeter .rmskin packages use a different format and are not supported.)",
                L"Import skin", MB_ICONINFORMATION);
  }
}

// ---------------------------------------------------------------------------------------------
// Control window
// ---------------------------------------------------------------------------------------------
LRESULT CALLBACK App::ControlProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  App* self = nullptr;
  if (m == WM_NCCREATE) {
    self = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
    SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<App*>(GetWindowLongPtrW(h, GWLP_USERDATA));
  }
  return self ? self->OnMessage(h, m, w, l) : DefWindowProcW(h, m, w, l);
}

LRESULT App::OnMessage(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m != 0 && m == m_taskbarCreated) {  // Explorer restarted: our parent and every skin window are gone
    core::Log::Write("Explorer restarted; rebuilding");
    UnloadSkins();
    AddTrayIcon();
    if (!TryAttachAndLoad()) SetTimer(h, TIMER_ATTACH, 1000, nullptr);
    ApplyTaskbar();
    return 0;
  }
  switch (m) {
    case WM_TIMER:
      switch (w) {
        case TIMER_TICK:
          OnTick();
          break;
        case TIMER_IDLE:
          OnIdleTick();
          break;
        case TIMER_TASKBAR:
          ApplyTaskbar();  // Explorer repaints its own look now and then; keep the effect applied
          break;
        case TIMER_ATTACH:
          if (TryAttachAndLoad()) KillTimer(h, TIMER_ATTACH);
          break;
        case TIMER_RELOAD:
          if (m_drag.pressed) break;  // try again on the next tick
          KillTimer(h, TIMER_RELOAD);
          core::Log::Write("skins changed on disk; reloading");
          Reload(false);
          RearmWatch();
          break;
        case TIMER_DEVLOST:
          KillTimer(h, TIMER_DEVLOST);
          core::Log::Write("GPU device lost; rebuilding");
          Reload(true);
          break;
      }
      return 0;
    case WM_SKINS_CHANGED:
      SetTimer(h, TIMER_RELOAD, 500, nullptr);  // debounce editors that save in several steps
      return 0;
    case WM_DEVICE_LOST:
      SetTimer(h, TIMER_DEVLOST, 200, nullptr);  // coalesces one message per skin into one rebuild
      return 0;
    case WM_SKIN_DRAGGED:
      SaveSkinPosition(w);
      return 0;
    case WM_SKIN_CLICK:
      if (w < m_skins.size()) m_skins[w].content->OnMouse(WM_LBUTTONUP, LOWORD(l), HIWORD(l));
      return 0;
    case WM_SKIN_MENU:
      ShowSkinMenu(w);
      return 0;
    case WM_TRAY:
      if (LOWORD(l) == WM_LBUTTONUP) ShowManager();
      else if (LOWORD(l) == WM_RBUTTONUP || LOWORD(l) == WM_CONTEXTMENU) ShowTrayMenu();
      return 0;
    case WM_SHOW_UI:
      ShowManager();
      return 0;
    case WM_COMMAND: {
      const UINT id = LOWORD(w);
      if (id >= ID_SKIN_BASE) {
        HandleSkinAction((id - ID_SKIN_BASE) / ActCount, (id - ID_SKIN_BASE) % ActCount);
        return 0;
      }
      switch (id) {
        case ID_OPEN_UI:
          ShowManager();
          break;
        case ID_REFRESH:
          Reload(false);
          break;
        case ID_RESET_ALL:
          m_settings.ResetPositions();
          m_settings.Save();
          Reload(false);
          break;
        case ID_AUTOSTART:
          platform::autostart::SetEnabled(!platform::autostart::IsEnabled());
          break;
        case ID_OPEN_FOLDER:
          ShellExecuteW(nullptr, L"open", m_skinsRoot.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
          break;
        case ID_QUIT:
          DestroyWindow(h);
          break;
      }
      return 0;
    }
    case WM_DESTROY: {
      KillTimer(h, TIMER_TICK);
      NOTIFYICONDATAW nid{sizeof(nid)};
      nid.hWnd = h;
      nid.uID = 1;
      Shell_NotifyIconW(NIM_DELETE, &nid);  // must happen while the window still exists
      PostQuitMessage(0);
      return 0;
    }
    default:
      return DefWindowProcW(h, m, w, l);
  }
}

}  // namespace

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR cmdLine, int) {
  // One instance only: a second double-click must not stack a second set of widgets.
  HANDLE mutex = CreateMutexW(nullptr, TRUE, kMutexName);
  if (mutex && GetLastError() == ERROR_ALREADY_EXISTS) {
    if (HWND other = FindWindowW(kControlClass, nullptr)) PostMessageW(other, WM_SHOW_UI, 0, 0);  // show the existing instance
    CloseHandle(mutex);
    return 0;
  }

  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);  // no-op if the manifest already did
  CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

  int exitCode = 0;
  {
    App app;
    // Started by the Windows Run entry => stay in the tray; started by a double-click => show the window.
    const bool background = cmdLine && wcsstr(cmdLine, L"--background") != nullptr;
    if (app.Init(hi, !background)) {
      MSG msg;
      while (GetMessageW(&msg, nullptr, 0, 0) > 0) {  // blocks: 0% CPU while nothing happens
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
      exitCode = static_cast<int>(msg.wParam);
      app.Shutdown();
    } else {
      exitCode = 1;
    }
  }
  CoUninitialize();
  if (mutex) CloseHandle(mutex);
  return exitCode;
}
