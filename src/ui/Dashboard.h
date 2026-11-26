// The DeskSkin dashboard: a custom-drawn (Direct2D) Windows 11-style window with a fixed sidebar,
// large widget cards and toggles. It owns no application logic: it reads a DashboardModel and
// reports user intent through IDashboardHost::OnCommand.
#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "render/GraphicsDevice.h"
#include "skin/BasicSkin.h"

namespace deskskin::ui {

struct DashboardModel {
  bool timeAvailable = false, timeOn = false;
  bool actionsAvailable = false, actionsOn = false;
  bool startWithWindows = false;
  bool use24h = true, showDate = true;
  bool performance = false, autoHide = false;
  int accent = 0;
  std::wstring timeStyle;  // label of the active Time style, e.g. "Neon"
  std::wstring tip;
};

// ---- Widget Library view-model ----
struct LibOption {
  std::string key;
  std::wstring label, value;  // value is what to show (already resolved to a label)
};
struct LibItem {
  std::string id;
  std::wstring name, category, description, author, source, needs;
  int categoryIndex = 0;
  bool installed = false, enabled = false, favorite = false, feature = false;
  bool livePreview = true;  // false: needs an online service, so no preview until it is installed
  std::vector<LibOption> options;
};
enum class LibCmd { Install, Uninstall, ToggleEnabled, ToggleFavorite, Option, ResetPosition, Import, Discover };

enum class Cmd {
  TimeOn, ActionsOn, StartWithWindows, Use24h, ShowDate, CycleTimeStyle, Performance, AutoHide, Accent,
  TimeMenu, ActionsMenu, EditActions, CustomizeLayout, RefreshAll, OpenFolder, ResetPositions
};

class IDashboardHost {
 public:
  virtual ~IDashboardHost() = default;
  virtual DashboardModel GetModel() = 0;
  // `value` is the requested new state for toggles (0/1), the palette index for Accent, else 0.
  virtual void OnCommand(Cmd cmd, int value) = 0;
  virtual const std::vector<LibItem>& Library() = 0;
  virtual void OnLibraryCommand(LibCmd cmd, const std::string& id, const std::string& key) = 0;
  virtual std::unique_ptr<skin::BasicSkin> BuildPreview(const std::string& id) = 0;
};

class Dashboard {
 public:
  Dashboard(render::GraphicsDevice& gfx, IDashboardHost& host, HINSTANCE hi) : m_gfx(gfx), m_host(host), m_hi(hi) {}
  ~Dashboard() { Close(); }
  Dashboard(const Dashboard&) = delete;
  Dashboard& operator=(const Dashboard&) = delete;

  void Show();                // create, or bring to front
  void Close();
  void Refresh();             // model changed: repaint
  void ResetResources();      // GPU device was rebuilt: drop cached device objects
  void InvalidatePreview() { m_previewId.clear(); m_preview.reset(); }
  bool IsOpen() const { return m_hwnd != nullptr; }
  HWND Hwnd() const { return m_hwnd; }

 private:
  struct Hit { D2D1_RECT_F r; int id; };

  static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT OnMessage(HWND, UINT, WPARAM, LPARAM);
  bool EnsureTarget();
  void Paint();
  int HitAt(float x, float y) const;
  void OnClick(int id);

  // drawing helpers (DIP coordinates)
  void SetColor(D2D1_COLOR_F c) { m_brush->SetColor(c); }
  void Fill(const D2D1_RECT_F& r, float rad, D2D1_COLOR_F c);
  void Stroke(const D2D1_RECT_F& r, float rad, D2D1_COLOR_F c, float w);
  void Glass(const D2D1_RECT_F& r, float rad, bool active);
  void Txt(const std::wstring& s, const D2D1_RECT_F& r, float size, bool semibold, D2D1_COLOR_F c, bool center = false);
  float TextW(const std::wstring& s, float size, bool semibold);
  IDWriteTextFormat* Fmt(float size, bool semibold, bool center);
  void AddHit(const D2D1_RECT_F& r, int id) { m_hits.push_back({r, id}); }
  D2D1_COLOR_F Accent(float a = 1.0f) const;

  void Toggle(const D2D1_RECT_F& r, bool on, int id);
  float Chip(float x, float y, int icon, const std::wstring& label, int id);  // returns width
  void IconTile(const D2D1_RECT_F& r);
  void Icon(int kind, float cx, float cy, float size, D2D1_COLOR_F c);
  void Gear(float cx, float cy, float r, D2D1_COLOR_F c);
  void GearButton(const D2D1_RECT_F& r, int id);
  void Chevron(float cx, float cy, float s, bool down, D2D1_COLOR_F c);

  void DrawBackground(const D2D1_SIZE_F& sz);
  void DrawSidebar(const D2D1_SIZE_F& sz, const DashboardModel& m);
  void PageTitle(const wchar_t* title, const wchar_t* subtitle, float x);
  void DrawWidgets(const D2D1_SIZE_F& sz, const DashboardModel& m);
  void DrawSettings(const D2D1_SIZE_F& sz);
  void DrawAppearance(const D2D1_SIZE_F& sz, const DashboardModel& m);
  void DrawAbout(const D2D1_SIZE_F& sz);
  void DrawLibrary(const D2D1_SIZE_F& sz);
  float Pill(float x, float y, const std::wstring& text, int id, int style);  // style 0 normal, 1 primary, 2 danger, 3 option
  float TxtWrap(const std::wstring& s, const D2D1_RECT_F& r, float size, bool semibold, D2D1_COLOR_F c);
  float WrapHeight(const std::wstring& s, float width, float size, bool semibold);
  void Star(float cx, float cy, float r, bool filled, D2D1_COLOR_F c);
  void AddHitClipped(const D2D1_RECT_F& r, const D2D1_RECT_F& clip, int id);

  render::GraphicsDevice& m_gfx;
  IDashboardHost& m_host;
  HINSTANCE m_hi;
  HWND m_hwnd = nullptr;
  UINT m_dpi = 96;
  bool m_tracking = false;

  Microsoft::WRL::ComPtr<ID2D1HwndRenderTarget> m_rt;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brush;
  std::map<int, Microsoft::WRL::ComPtr<IDWriteTextFormat>> m_fmts;

  std::vector<Hit> m_hits;  // rebuilt on every paint, so hit-testing always matches what is drawn
  int m_hover = -1, m_down = -1;
  int m_page = 0;
  bool m_advOpen = true;
  int m_accent = 0;

  // library page state
  int m_libCat = 0;
  std::wstring m_libSearch;
  bool m_searchFocus = false;
  float m_libScroll = 0, m_detailScroll = 0, m_detailMax = 0, m_detailX = 100000;
  std::string m_selected;
  std::unique_ptr<skin::BasicSkin> m_preview;
  std::string m_previewId;
};

}  // namespace deskskin::ui
