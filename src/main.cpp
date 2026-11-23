// DeskSkin v0.2 - application shell: tray, skin discovery, dragging, persistence, autostart,
// live skin reload, Explorer/GPU recovery.
#include <objbase.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <cwchar>
#include <filesystem>
#include <memory>
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

constexpr UINT ID_REFRESH = 1001;
constexpr UINT ID_OPEN_FOLDER = 1002;
constexpr UINT ID_AUTOSTART = 1003;
constexpr UINT ID_RESET_ALL = 1004;
constexpr UINT ID_QUIT = 1005;
constexpr UINT ID_OPEN_UI = 1006;

constexpr wchar_t kManagerClass[] = L"DeskSkin.Manager";
constexpr UINT IDC_LIST = 3001, IDC_TOGGLE = 3002, IDC_LOCK = 3003, IDC_CLICK = 3004, IDC_EDIT = 3005,
               IDC_RESET = 3006, IDC_REFRESH = 3007, IDC_AUTOSTART = 3008, IDC_FOLDER = 3009;
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

class App final : public input::IDesktopMouseSink {
 public:
  bool Init(HINSTANCE hi, bool showUi);
  void Shutdown();
  bool HandleUiMessage(MSG* msg) { return m_ui && IsDialogMessageW(m_ui, msg); }  // Tab / Enter navigation
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

  // Settings window
  static LRESULT CALLBACK ManagerProc(HWND, UINT, WPARAM, LPARAM);
  void ShowManager();
  void RefreshManagerList();
  void OnManagerCommand(UINT id);

  HINSTANCE m_hInst = nullptr;
  HWND m_hwnd = nullptr;
  UINT m_taskbarCreated = 0;
  fs::path m_dataDir, m_skinsRoot;

  HWND m_ui = nullptr;
  HFONT m_uiFont = nullptr;

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
  skin::MeterPool m_meterPool;
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
  platform::autostart::SyncPath();

  const HRESULT hr = m_gfx.Initialize();
  if (FAILED(hr)) {
    core::Log::Write("graphics init failed: 0x{:08x}", static_cast<unsigned>(hr));
    MessageBoxW(nullptr, L"DeskSkin could not initialise Direct3D / Direct2D.", L"DeskSkin", MB_ICONERROR);
    return false;
  }

  AddTrayIcon();
  EnsureDefaultSkin();
  if (!m_router.Install(this)) core::Log::Write("mouse hook failed: widgets will not be draggable");
  // At logon Explorer may not have created the desktop yet: keep retrying instead of giving up.
  if (!TryAttachAndLoad()) SetTimer(m_hwnd, TIMER_ATTACH, 1000, nullptr);
  StartWatch();
  if (showUi) ShowManager();
  return true;
}

void App::Shutdown() {
  if (m_ui) DestroyWindow(m_ui);
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

  for (std::size_t i = 0; i < m_available.size(); ++i) {
    const auto& a = m_available[i];
    const config::SkinState st = m_settings.Get(a.key);
    if (!st.enabled) continue;

    std::vector<std::string> warnings;
    auto content = skin::BasicSkin::Load(a.path, m_gfx, m_meterPool, &warnings);
    for (const auto& w : warnings) core::Log::Write("skin warning: {}", w);
    if (!content) {
      core::Log::Write("skin failed to load: {}", a.key);
      continue;
    }

    render::SkinWindowDesc desc = content->WindowDesc();
    const POINT saved{st.x, st.y};
    if (st.hasPosition && PtInRect(&vs, saved)) {  // ignore positions from a monitor that is gone
      desc.x = st.x;
      desc.y = st.y;
    }

    auto window = std::make_unique<render::SkinWindow>(m_gfx, m_host);
    window->SetNotifyWindow(m_hwnd, WM_DEVICE_LOST);
    const HRESULT hr = window->Create(desc, content.get());
    core::Log::Write("skin '{}': create hr=0x{:08x} onWallpaperLayer={}", a.key, static_cast<unsigned>(hr),
                     hr >= 0 && window->IsOnWallpaperLayer());
    if (FAILED(hr)) continue;

    LoadedSkin ls;
    ls.content = std::move(content);
    ls.window = std::move(window);
    ls.avail = i;
    ls.locked = st.locked;
    ls.clickThrough = st.clickThrough;
    m_skins.push_back(std::move(ls));
  }
  UpdateTimer();
  RefreshManagerList();
  m_gfx.Trim();  // return load-time garbage to the OS: the idle footprint is measured after this
}

void App::UnloadSkins() {
  m_drag = {};
  m_skins.clear();
  m_gfx.Trim();
}

void App::Reload(bool resetGraphics) {
  KillTimer(m_hwnd, TIMER_TICK);
  UnloadSkins();
  if (resetGraphics && FAILED(m_gfx.Initialize())) return;
  if (!TryAttachAndLoad()) SetTimer(m_hwnd, TIMER_ATTACH, 1000, nullptr);
}

// Event-driven: no timer unless some skin has time-varying content, and then only at the fastest
// rate any skin asked for. A static skin costs zero wake-ups.
void App::UpdateTimer() {
  unsigned interval = 0;
  for (const auto& s : m_skins)
    if (s.content->HasDynamicContent())
      interval = interval ? std::min(interval, s.content->UpdateMs()) : s.content->UpdateMs();
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
    if (s.clickThrough || !s.window->IsAlive()) continue;
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
// Settings window: a plain Win32 window with a list box and buttons
// ---------------------------------------------------------------------------------------------
void App::RefreshManagerList() {
  if (!m_ui) return;
  HWND list = GetDlgItem(m_ui, IDC_LIST);
  const LRESULT sel = SendMessageW(list, LB_GETCURSEL, 0, 0);
  SendMessageW(list, WM_SETREDRAW, FALSE, 0);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);
  for (const auto& a : m_available) {
    const config::SkinState st = m_settings.Get(a.key);
    std::wstring line = st.enabled ? L"[On]    " : L"[Off]   ";
    line += a.display;
    if (st.locked) line += L"   (locked)";
    if (st.clickThrough) line += L"   (click-through)";
    SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
  }
  if (sel != LB_ERR && sel < static_cast<LRESULT>(m_available.size())) SendMessageW(list, LB_SETCURSEL, static_cast<WPARAM>(sel), 0);
  SendMessageW(list, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(list, nullptr, TRUE);
  CheckDlgButton(m_ui, IDC_AUTOSTART, platform::autostart::IsEnabled() ? BST_CHECKED : BST_UNCHECKED);
}

void App::ShowManager() {
  if (m_ui) {
    ShowWindow(m_ui, SW_SHOWNORMAL);
    SetForegroundWindow(m_ui);
    return;
  }
  static bool registered = false;
  if (!registered) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &App::ManagerProc;
    wc.hInstance = m_hInst;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(static_cast<UINT_PTR>(COLOR_BTNFACE + 1));
    wc.hIcon = LoadIconW(m_hInst, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = kManagerClass;
    if (!RegisterClassExW(&wc)) return;
    registered = true;
  }

  const UINT dpi = GetDpiForSystem();
  auto S = [dpi](int v) { return MulDiv(v, static_cast<int>(dpi), 96); };
  const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
  RECT rc{0, 0, S(440), S(310)};
  AdjustWindowRectExForDpi(&rc, style, FALSE, 0, dpi);
  m_ui = CreateWindowExW(0, kManagerClass, L"DeskSkin - Widgets", style, CW_USEDEFAULT, CW_USEDEFAULT,
                         rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, m_hInst, this);
  if (!m_ui) return;

  NONCLIENTMETRICSW ncm{sizeof(ncm)};
  SystemParametersInfoForDpi(SPI_GETNONCLIENTMETRICS, sizeof(ncm), &ncm, 0, dpi);
  m_uiFont = CreateFontIndirectW(&ncm.lfMessageFont);

  auto add = [&](DWORD ex, const wchar_t* cls, const wchar_t* text, DWORD st, int x, int y, int w, int h, UINT id) {
    HWND c = CreateWindowExW(ex, cls, text, WS_CHILD | WS_VISIBLE | st, S(x), S(y), S(w), S(h), m_ui,
                             reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), m_hInst, nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(m_uiFont), TRUE);
    return c;
  };
  const DWORD btn = WS_TABSTOP | BS_PUSHBUTTON;
  add(0, L"STATIC", L"Select a widget, then choose what to do with it:", 0, 12, 10, 416, 20, 0);
  add(WS_EX_CLIENTEDGE, L"LISTBOX", L"", WS_TABSTOP | WS_VSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT, 12, 34, 262, 204, IDC_LIST);
  add(0, L"BUTTON", L"Show / Hide", btn, 286, 34, 142, 28, IDC_TOGGLE);
  add(0, L"BUTTON", L"Lock position", btn, 286, 68, 142, 28, IDC_LOCK);
  add(0, L"BUTTON", L"Click-through", btn, 286, 102, 142, 28, IDC_CLICK);
  add(0, L"BUTTON", L"Edit skin file...", btn, 286, 136, 142, 28, IDC_EDIT);
  add(0, L"BUTTON", L"Reset position", btn, 286, 170, 142, 28, IDC_RESET);
  add(0, L"BUTTON", L"Refresh all", btn, 286, 210, 142, 28, IDC_REFRESH);
  add(0, L"BUTTON", L"Start with Windows", WS_TABSTOP | BS_AUTOCHECKBOX, 12, 250, 200, 22, IDC_AUTOSTART);
  add(0, L"BUTTON", L"Open skins folder", btn, 286, 246, 142, 28, IDC_FOLDER);
  add(0, L"STATIC", L"Tip: drag a widget on the desktop to move it; right-click it for options.", 0, 12, 284, 416, 20, 0);

  RefreshManagerList();
  ShowWindow(m_ui, SW_SHOWNORMAL);
  SetForegroundWindow(m_ui);
}

void App::OnManagerCommand(UINT id) {
  UINT action = ActCount;
  switch (id) {
    case IDC_AUTOSTART:
      platform::autostart::SetEnabled(IsDlgButtonChecked(m_ui, IDC_AUTOSTART) == BST_CHECKED);
      RefreshManagerList();
      return;
    case IDC_REFRESH:
      Reload(false);
      return;
    case IDC_FOLDER:
      ShellExecuteW(nullptr, L"open", m_skinsRoot.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
      return;
    case IDC_TOGGLE: action = ActEnabled; break;
    case IDC_LOCK: action = ActLocked; break;
    case IDC_CLICK: action = ActClickThrough; break;
    case IDC_EDIT: action = ActEdit; break;
    case IDC_RESET: action = ActResetPos; break;
    default: return;  // list notifications etc.
  }
  const LRESULT sel = SendMessageW(GetDlgItem(m_ui, IDC_LIST), LB_GETCURSEL, 0, 0);
  if (sel == LB_ERR || sel >= static_cast<LRESULT>(m_available.size())) {
    MessageBoxW(m_ui, L"Select a widget in the list first.", L"DeskSkin", MB_ICONINFORMATION);
    return;
  }
  HandleSkinAction(static_cast<std::size_t>(sel), action);
  RefreshManagerList();
}

LRESULT CALLBACK App::ManagerProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  App* self = nullptr;
  if (m == WM_NCCREATE) {
    self = static_cast<App*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
    SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<App*>(GetWindowLongPtrW(h, GWLP_USERDATA));
  }
  if (!self) return DefWindowProcW(h, m, w, l);
  switch (m) {
    case WM_COMMAND:
      self->OnManagerCommand(LOWORD(w));
      return 0;
    case WM_CLOSE:
      DestroyWindow(h);  // closing the window does NOT quit DeskSkin; it keeps running in the tray
      return 0;
    case WM_DESTROY:
      if (self->m_uiFont) DeleteObject(self->m_uiFont);
      self->m_uiFont = nullptr;
      self->m_ui = nullptr;
      return 0;
  }
  return DefWindowProcW(h, m, w, l);
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
    return 0;
  }
  switch (m) {
    case WM_TIMER:
      switch (w) {
        case TIMER_TICK:
          OnTick();
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
        if (app.HandleUiMessage(&msg)) continue;
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
