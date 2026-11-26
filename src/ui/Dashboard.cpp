#include "ui/Dashboard.h"
#include "ui/DashboardIds.h"

#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>

namespace deskskin::ui {

using Microsoft::WRL::ComPtr;

namespace {

constexpr wchar_t kClass[] = L"DeskSkin.Dashboard";
constexpr float kSidebarW = 240.0f;
constexpr float kMinW = 900.0f, kMinH = 700.0f;


struct Rgb { float r, g, b; };
constexpr Rgb kAccents[4] = {{0.18f, 0.49f, 1.00f}, {0.55f, 0.42f, 1.00f}, {0.00f, 0.78f, 0.74f}, {0.94f, 0.35f, 0.67f}};
const wchar_t* const kAccentNames[4] = {L"Electric blue", L"Indigo", L"Teal", L"Rose"};

D2D1_COLOR_F C(float r, float g, float b, float a = 1.0f) { return D2D1::ColorF(r, g, b, a); }
D2D1_RECT_F R(float x, float y, float w, float h) { return D2D1::RectF(x, y, x + w, y + h); }
bool Contains(const D2D1_RECT_F& r, float x, float y) { return x >= r.left && x < r.right && y >= r.top && y < r.bottom; }
const D2D1_COLOR_F kWhite = D2D1::ColorF(1, 1, 1, 1);

}  // namespace

// ---------------------------------------------------------------------------------------------
// Window lifetime
// ---------------------------------------------------------------------------------------------
void Dashboard::Show() {
  if (m_hwnd) {
    ShowWindow(m_hwnd, IsIconic(m_hwnd) ? SW_RESTORE : SW_SHOW);
    SetForegroundWindow(m_hwnd);
    return;
  }
  static bool registered = false;
  if (!registered) {
    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &Dashboard::WndProc;
    wc.hInstance = m_hi;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(m_hi, MAKEINTRESOURCEW(1));
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = kClass;  // no background brush: every pixel is painted by Direct2D
    if (!RegisterClassExW(&wc)) return;
    registered = true;
  }
  const UINT dpi = GetDpiForSystem();
  const DWORD style = WS_OVERLAPPEDWINDOW;
  RECT rc{0, 0, MulDiv(1060, static_cast<int>(dpi), 96), MulDiv(780, static_cast<int>(dpi), 96)};
  AdjustWindowRectExForDpi(&rc, style, FALSE, 0, dpi);
  m_hwnd = CreateWindowExW(0, kClass, L"DeskSkin", style, CW_USEDEFAULT, CW_USEDEFAULT, rc.right - rc.left,
                           rc.bottom - rc.top, nullptr, nullptr, m_hi, this);
  if (!m_hwnd) return;

  // Dark title bar tinted to the navy background (Windows 11; harmlessly ignored on older builds).
  const BOOL dark = TRUE;
  DwmSetWindowAttribute(m_hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof(dark));
  const COLORREF caption = RGB(11, 15, 38);
  DwmSetWindowAttribute(m_hwnd, 35 /*DWMWA_CAPTION_COLOR*/, &caption, sizeof(caption));
  const COLORREF border = RGB(40, 52, 110);
  DwmSetWindowAttribute(m_hwnd, 34 /*DWMWA_BORDER_COLOR*/, &border, sizeof(border));

  SetTimer(m_hwnd, 1, 250, nullptr);  // drives live previews (only does work while the Library page is open)
  ShowWindow(m_hwnd, SW_SHOWNORMAL);
  UpdateWindow(m_hwnd);
  SetForegroundWindow(m_hwnd);
}

void Dashboard::Close() {
  if (m_hwnd) DestroyWindow(m_hwnd);
  m_hwnd = nullptr;
  m_rt.Reset();
  m_brush.Reset();
}

void Dashboard::Refresh() {
  if (m_hwnd) InvalidateRect(m_hwnd, nullptr, FALSE);
}

void Dashboard::ResetResources() {
  m_preview.reset();
  m_previewId.clear();
  m_rt.Reset();
  m_brush.Reset();
  m_fmts.clear();
  Refresh();
}

bool Dashboard::EnsureTarget() {
  if (m_rt) return true;
  RECT rc;
  GetClientRect(m_hwnd, &rc);
  m_dpi = GetDpiForWindow(m_hwnd);
  HRESULT hr = m_gfx.D2DFactory()->CreateHwndRenderTarget(
      D2D1::RenderTargetProperties(),
      D2D1::HwndRenderTargetProperties(m_hwnd, D2D1::SizeU(static_cast<UINT>(std::max<LONG>(1, rc.right)),
                                                           static_cast<UINT>(std::max<LONG>(1, rc.bottom)))),
      &m_rt);
  if (FAILED(hr)) return false;
  m_rt->SetDpi(static_cast<float>(m_dpi), static_cast<float>(m_dpi));
  hr = m_rt->CreateSolidColorBrush(C(1, 1, 1), &m_brush);
  return SUCCEEDED(hr);
}

// ---------------------------------------------------------------------------------------------
// Messages
// ---------------------------------------------------------------------------------------------
LRESULT CALLBACK Dashboard::WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  Dashboard* self = nullptr;
  if (m == WM_NCCREATE) {
    self = static_cast<Dashboard*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
    SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    self->m_hwnd = h;
  } else {
    self = reinterpret_cast<Dashboard*>(GetWindowLongPtrW(h, GWLP_USERDATA));
  }
  return self ? self->OnMessage(h, m, w, l) : DefWindowProcW(h, m, w, l);
}

int Dashboard::HitAt(float x, float y) const {
  for (auto it = m_hits.rbegin(); it != m_hits.rend(); ++it)  // later-drawn items are on top
    if (Contains(it->r, x, y)) return it->id;
  return -1;
}

LRESULT Dashboard::OnMessage(HWND h, UINT m, WPARAM w, LPARAM l) {
  const float scale = 96.0f / static_cast<float>(m_dpi ? m_dpi : 96);
  switch (m) {
    case WM_PAINT: {
      PAINTSTRUCT ps;
      BeginPaint(h, &ps);
      Paint();
      EndPaint(h, &ps);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_SIZE:
      if (m_rt && w != SIZE_MINIMIZED)
        m_rt->Resize(D2D1::SizeU(LOWORD(l), HIWORD(l)));
      InvalidateRect(h, nullptr, FALSE);
      return 0;
    case WM_DPICHANGED: {
      m_dpi = HIWORD(w);
      if (m_rt) m_rt->SetDpi(static_cast<float>(m_dpi), static_cast<float>(m_dpi));
      const RECT* r = reinterpret_cast<const RECT*>(l);
      SetWindowPos(h, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
      return 0;
    }
    case WM_GETMINMAXINFO: {
      auto* mm = reinterpret_cast<MINMAXINFO*>(l);
      const UINT dpi = GetDpiForWindow(h);
      RECT rc{0, 0, MulDiv(static_cast<int>(kMinW), static_cast<int>(dpi), 96), MulDiv(static_cast<int>(kMinH), static_cast<int>(dpi), 96)};
      AdjustWindowRectExForDpi(&rc, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi);
      mm->ptMinTrackSize.x = rc.right - rc.left;
      mm->ptMinTrackSize.y = rc.bottom - rc.top;
      return 0;
    }
    case WM_TIMER:
      if (w == 1 && m_page == 1 && m_preview) {
        RECT r;
        if (m_preview->Tick(&r)) Refresh();
      }
      return 0;
    case WM_CHAR:
      if (m_page == 1 && m_searchFocus) {
        if (w == 8) { if (!m_libSearch.empty()) m_libSearch.pop_back(); }
        else if (w == 27) { m_libSearch.clear(); m_searchFocus = false; }
        else if (w >= 32 && w != 127 && m_libSearch.size() < 60) m_libSearch.push_back(static_cast<wchar_t>(w));
        m_libScroll = 0;
        Refresh();
      }
      return 0;
    case WM_MOUSEWHEEL: {
      if (m_page != 1) return 0;
      POINT pt{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
      ScreenToClient(h, &pt);
      const float delta = -static_cast<float>(GET_WHEEL_DELTA_WPARAM(w)) / 120.0f * 60.0f;
      const float mx = pt.x * scale;
      if (mx < m_detailX) m_libScroll += delta; else m_detailScroll += delta;
      m_libScroll = std::max(0.0f, m_libScroll);
      m_detailScroll = std::clamp(m_detailScroll, 0.0f, std::max(0.0f, m_detailMax));
      Refresh();
      return 0;
    }
    case WM_MOUSEMOVE: {
      if (!m_tracking) {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, h, 0};
        m_tracking = TrackMouseEvent(&tme) != FALSE;
      }
      const int id = HitAt(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale);
      if (id != m_hover) {
        m_hover = id;
        InvalidateRect(h, nullptr, FALSE);
      }
      return 0;
    }
    case WM_MOUSELEAVE:
      m_tracking = false;
      if (m_hover != -1) {
        m_hover = -1;
        InvalidateRect(h, nullptr, FALSE);
      }
      return 0;
    case WM_SETCURSOR:
      if (LOWORD(l) == HTCLIENT && m_hover >= 0) {
        SetCursor(LoadCursorW(nullptr, IDC_HAND));
        return TRUE;
      }
      break;
    case WM_LBUTTONDOWN:
      m_down = HitAt(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale);
      SetCapture(h);
      return 0;
    case WM_LBUTTONUP: {
      ReleaseCapture();
      const int id = HitAt(GET_X_LPARAM(l) * scale, GET_Y_LPARAM(l) * scale);
      const int down = m_down;
      m_down = -1;
      if (id != LibSearch) m_searchFocus = false;
      if (id >= 0 && id == down) OnClick(id);
      Refresh();
      return 0;
    }
    case WM_DESTROY:
      KillTimer(h, 1);
      m_preview.reset();
      m_previewId.clear();
      m_rt.Reset();
      m_brush.Reset();
      m_hwnd = nullptr;  // closing the dashboard never quits DeskSkin
      return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

void Dashboard::OnClick(int id) {
  const DashboardModel m = m_host.GetModel();
  auto flip = [](bool b) { return b ? 0 : 1; };
  if (id >= NavBase && id < NavBase + kNavCount) { m_page = id - NavBase; Refresh(); return; }
  {
    const std::vector<LibItem>& items = m_host.Library();
    auto at = [&](int base) -> const LibItem* {
      const std::size_t k = static_cast<std::size_t>(id - base);
      return k < items.size() ? &items[k] : nullptr;
    };
    if (id == LibSearch) { m_searchFocus = true; Refresh(); return; }
    if (id >= LibCat && id < LibCat + 10) { m_libCat = id - LibCat; m_libScroll = 0; Refresh(); return; }
    if (id >= LibRow && id < LibRow + 100) { if (auto* it = at(LibRow)) { m_selected = it->id; m_detailScroll = 0; } Refresh(); return; }
    if (id >= LibStar && id < LibStar + 100) { if (auto* it = at(LibStar)) m_host.OnLibraryCommand(LibCmd::ToggleFavorite, it->id, ""); Refresh(); return; }
    const LibItem* sel = nullptr;
    for (const auto& it : items) if (it.id == m_selected) sel = &it;
    if (id >= LibOpt && id < LibOpt + 10 && sel) {
      const std::size_t k = static_cast<std::size_t>(id - LibOpt);
      if (k < sel->options.size()) m_host.OnLibraryCommand(LibCmd::Option, sel->id, sel->options[k].key);
      Refresh();
      return;
    }
    switch (id) {
      case LibImport: m_host.OnLibraryCommand(LibCmd::Import, "", ""); Refresh(); return;
      case LibDiscover: m_host.OnLibraryCommand(LibCmd::Discover, "", ""); return;
      case LibInstall: if (sel) m_host.OnLibraryCommand(sel->installed ? LibCmd::Uninstall : LibCmd::Install, sel->id, ""); Refresh(); return;
      case LibEnable: if (sel) m_host.OnLibraryCommand(LibCmd::ToggleEnabled, sel->id, ""); Refresh(); return;
      case LibReset: if (sel) m_host.OnLibraryCommand(LibCmd::ResetPosition, sel->id, ""); Refresh(); return;
      case LibFavDetail: if (sel) m_host.OnLibraryCommand(LibCmd::ToggleFavorite, sel->id, ""); Refresh(); return;
      default: break;
    }
  }
  if (id >= AccentBase && id < AccentBase + 4) { m_accent = id - AccentBase; m_host.OnCommand(Cmd::Accent, m_accent); Refresh(); return; }
  switch (id) {
    case SideAuto: m_host.OnCommand(Cmd::StartWithWindows, flip(m.startWithWindows)); break;
    case TimeToggle: m_host.OnCommand(Cmd::TimeOn, flip(m.timeOn)); break;
    case TimeGear: m_host.OnCommand(Cmd::TimeMenu, 0); break;
    case ChipFormat: m_host.OnCommand(Cmd::Use24h, flip(m.use24h)); break;
    case ChipDate: m_host.OnCommand(Cmd::ShowDate, flip(m.showDate)); break;
    case ChipStyle: m_host.OnCommand(Cmd::CycleTimeStyle, 0); break;
    case ActToggle: m_host.OnCommand(Cmd::ActionsOn, flip(m.actionsOn)); break;
    case ActGear: m_host.OnCommand(Cmd::ActionsMenu, 0); break;
    case ChipEdit: m_host.OnCommand(Cmd::EditActions, 0); break;
    case ChipLayout: m_host.OnCommand(Cmd::CustomizeLayout, 0); break;
    case AdvHeader: m_advOpen = !m_advOpen; break;
    case PerfToggle: m_host.OnCommand(Cmd::Performance, flip(m.performance)); break;
    case IdleToggle: m_host.OnCommand(Cmd::AutoHide, flip(m.autoHide)); break;
    case SetRefresh: m_host.OnCommand(Cmd::RefreshAll, 0); break;
    case SetFolder: m_host.OnCommand(Cmd::OpenFolder, 0); break;
    case SetReset: m_host.OnCommand(Cmd::ResetPositions, 0); break;
  }
  Refresh();
}

// ---------------------------------------------------------------------------------------------
// Drawing primitives
// ---------------------------------------------------------------------------------------------
D2D1_COLOR_F Dashboard::Accent(float a) const {
  const Rgb& c = kAccents[std::clamp(m_accent, 0, 3)];
  return C(c.r, c.g, c.b, a);
}

void Dashboard::Fill(const D2D1_RECT_F& r, float rad, D2D1_COLOR_F c) {
  SetColor(c);
  m_rt->FillRoundedRectangle(D2D1::RoundedRect(r, rad, rad), m_brush.Get());
}

void Dashboard::Stroke(const D2D1_RECT_F& r, float rad, D2D1_COLOR_F c, float w) {
  SetColor(c);
  const float i = w * 0.5f;
  m_rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(r.left + i, r.top + i, r.right - i, r.bottom - i), rad, rad),
                             m_brush.Get(), w);
}

// "Glass": soft layered shadow + translucent fill + hairline highlight border.
void Dashboard::Glass(const D2D1_RECT_F& r, float rad, bool active) {
  for (int i = 3; i >= 1; --i) {
    const float e = static_cast<float>(i) * 3.0f;
    Fill(D2D1::RectF(r.left - e, r.top - e + 6, r.right + e, r.bottom + e + 6), rad + e, C(0, 0, 0.05f, 0.07f));
  }
  Fill(r, rad, C(1, 1, 1, 0.075f));
  Fill(D2D1::RectF(r.left, r.top, r.right, r.top + (r.bottom - r.top) * 0.5f), rad, C(1, 1, 1, 0.025f));  // top sheen
  if (active) {
    Stroke(r, rad, Accent(0.95f), 2.0f);
  } else {
    Stroke(r, rad, C(1, 1, 1, 0.14f), 1.0f);
  }
}

IDWriteTextFormat* Dashboard::Fmt(float size, bool semibold, bool center) {
  const int key = static_cast<int>(size * 10.0f) * 4 + (semibold ? 2 : 0) + (center ? 1 : 0);
  auto it = m_fmts.find(key);
  if (it != m_fmts.end()) return it->second.Get();
  ComPtr<IDWriteTextFormat> f;
  if (FAILED(m_gfx.DWrite()->CreateTextFormat(L"Segoe UI", nullptr,
                                              semibold ? DWRITE_FONT_WEIGHT_SEMI_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                                              DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &f)))
    return nullptr;
  f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
  f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  f->SetTextAlignment(center ? DWRITE_TEXT_ALIGNMENT_CENTER : DWRITE_TEXT_ALIGNMENT_LEADING);
  m_fmts[key] = f;
  return f.Get();
}

void Dashboard::Txt(const std::wstring& s, const D2D1_RECT_F& r, float size, bool semibold, D2D1_COLOR_F c, bool center) {
  IDWriteTextFormat* f = Fmt(size, semibold, center);
  if (!f || s.empty()) return;
  SetColor(c);
  m_rt->DrawText(s.c_str(), static_cast<UINT32>(s.size()), f, r, m_brush.Get(), D2D1_DRAW_TEXT_OPTIONS_CLIP);
}

float Dashboard::TextW(const std::wstring& s, float size, bool semibold) {
  IDWriteTextFormat* f = Fmt(size, semibold, false);
  if (!f || s.empty()) return 0.0f;
  ComPtr<IDWriteTextLayout> layout;
  if (FAILED(m_gfx.DWrite()->CreateTextLayout(s.c_str(), static_cast<UINT32>(s.size()), f, 4000.0f, 100.0f, &layout)))
    return 0.0f;
  DWRITE_TEXT_METRICS tm{};
  layout->GetMetrics(&tm);
  return tm.widthIncludingTrailingWhitespace;
}

void Dashboard::Chevron(float cx, float cy, float s, bool down, D2D1_COLOR_F c) {
  SetColor(c);
  if (down) {
    m_rt->DrawLine(D2D1::Point2F(cx - s, cy - s * 0.5f), D2D1::Point2F(cx, cy + s * 0.5f), m_brush.Get(), 1.8f);
    m_rt->DrawLine(D2D1::Point2F(cx, cy + s * 0.5f), D2D1::Point2F(cx + s, cy - s * 0.5f), m_brush.Get(), 1.8f);
  } else {
    m_rt->DrawLine(D2D1::Point2F(cx - s * 0.5f, cy - s), D2D1::Point2F(cx + s * 0.5f, cy), m_brush.Get(), 1.8f);
    m_rt->DrawLine(D2D1::Point2F(cx + s * 0.5f, cy), D2D1::Point2F(cx - s * 0.5f, cy + s), m_brush.Get(), 1.8f);
  }
}

void Dashboard::Gear(float cx, float cy, float r, D2D1_COLOR_F c) {
  SetColor(c);
  for (int i = 0; i < 8; ++i) {
    m_rt->SetTransform(D2D1::Matrix3x2F::Rotation(static_cast<float>(i) * 45.0f, D2D1::Point2F(cx, cy)));
    m_rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - r * 0.16f, cy - r, cx + r * 0.16f, cy - r * 0.55f), 1.0f, 1.0f),
                               m_brush.Get());
  }
  m_rt->SetTransform(D2D1::Matrix3x2F::Identity());
  m_rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r * 0.62f, r * 0.62f), m_brush.Get(), r * 0.3f);
  m_rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), r * 0.22f, r * 0.22f), m_brush.Get(), r * 0.12f);
}

void Dashboard::GearButton(const D2D1_RECT_F& r, int id) {
  const bool hot = (m_hover == id);
  Fill(r, 12, C(1, 1, 1, hot ? 0.16f : 0.07f));
  Stroke(r, 12, C(1, 1, 1, 0.16f), 1.0f);
  Gear((r.left + r.right) / 2, (r.top + r.bottom) / 2, 11.0f, kWhite);
  AddHit(r, id);
}

// Icon kinds: 0 clock, 1 calendar, 2 palette, 3 pencil, 4 grid, 5 info, 6 monitor, 7 moon, 8 bolt, 9 windows
void Dashboard::Icon(int kind, float cx, float cy, float s, D2D1_COLOR_F c) {
  SetColor(c);
  const float w = std::max(1.4f, s * 0.09f);
  switch (kind) {
    case 0:
      m_rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), s * 0.5f, s * 0.5f), m_brush.Get(), w);
      m_rt->DrawLine(D2D1::Point2F(cx, cy), D2D1::Point2F(cx, cy - s * 0.3f), m_brush.Get(), w);
      m_rt->DrawLine(D2D1::Point2F(cx, cy), D2D1::Point2F(cx + s * 0.22f, cy + s * 0.12f), m_brush.Get(), w);
      break;
    case 1:
      m_rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - s * 0.5f, cy - s * 0.42f, cx + s * 0.5f, cy + s * 0.5f), 3, 3), m_brush.Get(), w);
      m_rt->DrawLine(D2D1::Point2F(cx - s * 0.5f, cy - s * 0.12f), D2D1::Point2F(cx + s * 0.5f, cy - s * 0.12f), m_brush.Get(), w);
      m_rt->DrawLine(D2D1::Point2F(cx - s * 0.25f, cy - s * 0.55f), D2D1::Point2F(cx - s * 0.25f, cy - s * 0.3f), m_brush.Get(), w);
      m_rt->DrawLine(D2D1::Point2F(cx + s * 0.25f, cy - s * 0.55f), D2D1::Point2F(cx + s * 0.25f, cy - s * 0.3f), m_brush.Get(), w);
      break;
    case 2:
      m_rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), s * 0.5f, s * 0.46f), m_brush.Get(), w);
      for (int i = 0; i < 3; ++i) {
        const float a = (200.0f + 50.0f * static_cast<float>(i)) * 3.14159265f / 180.0f;
        m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + std::cos(a) * s * 0.26f, cy + std::sin(a) * s * 0.24f), s * 0.07f, s * 0.07f), m_brush.Get());
      }
      m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + s * 0.22f, cy + s * 0.2f), s * 0.07f, s * 0.07f), m_brush.Get());
      break;
    case 3:
      m_rt->DrawLine(D2D1::Point2F(cx - s * 0.4f, cy + s * 0.4f), D2D1::Point2F(cx + s * 0.38f, cy - s * 0.38f), m_brush.Get(), w * 2.2f);
      m_rt->DrawLine(D2D1::Point2F(cx - s * 0.5f, cy + s * 0.5f), D2D1::Point2F(cx + s * 0.1f, cy + s * 0.5f), m_brush.Get(), w);
      break;
    case 4:
      for (int i = 0; i < 4; ++i) {
        const float x = cx + ((i % 2) ? s * 0.06f : -s * 0.5f), y = cy + ((i / 2) ? s * 0.06f : -s * 0.5f);
        m_rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, y, x + s * 0.44f, y + s * 0.44f), 2.5f, 2.5f), m_brush.Get(), w);
      }
      break;
    case 5:
      m_rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), s * 0.5f, s * 0.5f), m_brush.Get(), w);
      m_rt->DrawLine(D2D1::Point2F(cx, cy - s * 0.02f), D2D1::Point2F(cx, cy + s * 0.26f), m_brush.Get(), w * 1.3f);
      m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy - s * 0.24f), w * 0.85f, w * 0.85f), m_brush.Get());
      break;
    case 6:
      m_rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - s * 0.5f, cy - s * 0.42f, cx + s * 0.5f, cy + s * 0.2f), 3, 3), m_brush.Get(), w);
      m_rt->DrawLine(D2D1::Point2F(cx, cy + s * 0.2f), D2D1::Point2F(cx, cy + s * 0.42f), m_brush.Get(), w);
      m_rt->DrawLine(D2D1::Point2F(cx - s * 0.22f, cy + s * 0.44f), D2D1::Point2F(cx + s * 0.22f, cy + s * 0.44f), m_brush.Get(), w);
      break;
    case 7: {
      ComPtr<ID2D1EllipseGeometry> a, b;
      ComPtr<ID2D1PathGeometry> pg;
      ComPtr<ID2D1GeometrySink> sink;
      ID2D1Factory* f = m_gfx.D2DFactory();
      if (SUCCEEDED(f->CreateEllipseGeometry(D2D1::Ellipse(D2D1::Point2F(cx, cy), s * 0.48f, s * 0.48f), &a)) &&
          SUCCEEDED(f->CreateEllipseGeometry(D2D1::Ellipse(D2D1::Point2F(cx + s * 0.28f, cy - s * 0.2f), s * 0.42f, s * 0.42f), &b)) &&
          SUCCEEDED(f->CreatePathGeometry(&pg)) && SUCCEEDED(pg->Open(&sink))) {
        a->CombineWithGeometry(b.Get(), D2D1_COMBINE_MODE_EXCLUDE, nullptr, sink.Get());
        sink->Close();
        m_rt->FillGeometry(pg.Get(), m_brush.Get());
      }
      break;
    }
    case 8: {
      ComPtr<ID2D1PathGeometry> pg;
      ComPtr<ID2D1GeometrySink> sink;
      if (SUCCEEDED(m_gfx.D2DFactory()->CreatePathGeometry(&pg)) && SUCCEEDED(pg->Open(&sink))) {
        const D2D1_POINT_2F p[] = {{cx + s * 0.12f, cy - s * 0.5f}, {cx - s * 0.32f, cy + s * 0.06f}, {cx - s * 0.02f, cy + s * 0.06f},
                                   {cx - s * 0.12f, cy + s * 0.5f}, {cx + s * 0.34f, cy - s * 0.1f}, {cx + s * 0.04f, cy - s * 0.1f}};
        sink->BeginFigure(p[0], D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLines(p + 1, 5);
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        sink->Close();
        m_rt->FillGeometry(pg.Get(), m_brush.Get());
      }
      break;
    }
    case 10:  // cloud
      m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - s * 0.2f, cy + s * 0.05f), s * 0.28f, s * 0.28f), m_brush.Get());
      m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + s * 0.08f, cy - s * 0.12f), s * 0.34f, s * 0.34f), m_brush.Get());
      m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx + s * 0.3f, cy + s * 0.08f), s * 0.24f, s * 0.24f), m_brush.Get());
      m_rt->FillRectangle(D2D1::RectF(cx - s * 0.2f, cy + s * 0.05f, cx + s * 0.3f, cy + s * 0.32f), m_brush.Get());
      break;
    case 11:  // music note
      m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx - s * 0.18f, cy + s * 0.3f), s * 0.2f, s * 0.15f), m_brush.Get());
      m_rt->DrawLine(D2D1::Point2F(cx - s * 0.0f, cy + s * 0.28f), D2D1::Point2F(cx - s * 0.0f, cy - s * 0.42f), m_brush.Get(), w * 1.4f);
      m_rt->DrawLine(D2D1::Point2F(cx, cy - s * 0.42f), D2D1::Point2F(cx + s * 0.3f, cy - s * 0.2f), m_brush.Get(), w * 1.4f);
      break;
    case 12:  // equalizer
      for (int i = 0; i < 4; ++i) {
        const float hgt = s * (0.35f + 0.18f * static_cast<float>((i * 2) % 4));
        const float x = cx - s * 0.46f + static_cast<float>(i) * s * 0.26f;
        m_rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, cy + s * 0.45f - hgt, x + s * 0.17f, cy + s * 0.45f), 2, 2), m_brush.Get());
      }
      break;
    case 13: Gear(cx, cy, s * 0.5f, c); break;
    case 14:  // layers
      for (int i = 0; i < 3; ++i) {
        const float y = cy - s * 0.38f + static_cast<float>(i) * s * 0.34f;
        m_rt->DrawRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(cx - s * 0.5f, y, cx + s * 0.5f, y + s * 0.24f), 4, 4), m_brush.Get(), w);
      }
      break;
    case 9:
      for (int i = 0; i < 4; ++i) {
        const float x = cx + ((i % 2) ? s * 0.04f : -s * 0.5f), y = cy + ((i / 2) ? s * 0.04f : -s * 0.5f);
        m_rt->FillRectangle(D2D1::RectF(x, y, x + s * 0.46f, y + s * 0.46f), m_brush.Get());
      }
      break;
  }
}

void Dashboard::IconTile(const D2D1_RECT_F& r) {
  Fill(r, 18, C(0, 0, 0.1f, 0.28f));
  Stroke(r, 18, C(1, 1, 1, 0.08f), 1.0f);
}

void Dashboard::Toggle(const D2D1_RECT_F& r, bool on, int id) {
  const float h = r.bottom - r.top, w = r.right - r.left;
  const bool hot = (m_hover == id);
  if (on) {
    Fill(D2D1::RectF(r.left - 3, r.top - 3, r.right + 3, r.bottom + 3), h / 2 + 3, Accent(0.18f));  // glow
    Fill(r, h / 2, Accent(1.0f));
  } else {
    Fill(r, h / 2, C(1, 1, 1, hot ? 0.24f : 0.16f));
  }
  const float kr = h / 2 - 4.0f;
  const float kx = on ? r.right - h / 2 : r.left + h / 2;
  SetColor(C(0, 0, 0, 0.25f));
  m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kx, r.top + h / 2 + 1.5f), kr, kr), m_brush.Get());
  SetColor(on ? kWhite : C(0.85f, 0.88f, 0.95f, 1));
  m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(kx, r.top + h / 2), kr, kr), m_brush.Get());
  (void)w;
  AddHit(r, id);
}

float Dashboard::Chip(float x, float y, int icon, const std::wstring& label, int id) {
  const float h = 40.0f, tw = TextW(label, 15.0f, false);
  const float w = 14 + 20 + 10 + tw + 12 + 12 + 14;
  const D2D1_RECT_F r = R(x, y, w, h);
  const bool hot = (m_hover == id);
  Fill(r, 12, C(1, 1, 1, hot ? 0.16f : 0.08f));
  Stroke(r, 12, hot ? Accent(0.8f) : C(1, 1, 1, 0.12f), 1.0f);
  Icon(icon, x + 14 + 10, y + h / 2, 18, kWhite);
  Txt(label, R(x + 14 + 20 + 10, y, tw + 8, h), 15, false, kWhite);
  Chevron(x + w - 18, y + h / 2, 4.5f, true, C(1, 1, 1, 0.8f));
  AddHit(r, id);
  return w;
}

// ---------------------------------------------------------------------------------------------
// Frame
// ---------------------------------------------------------------------------------------------
void Dashboard::Paint() {
  if (!m_hwnd || !EnsureTarget()) return;
  m_accent = std::clamp(m_accent, 0, 3);
  const DashboardModel m = m_host.GetModel();
  m_accent = std::clamp(m.accent, 0, 3);

  m_hits.clear();
  m_rt->BeginDraw();
  const D2D1_SIZE_F sz = m_rt->GetSize();  // DIPs
  DrawBackground(sz);
  DrawSidebar(sz, m);
  switch (m_page) {
    case 0: DrawWidgets(sz, m); break;
    case 1: DrawLibrary(sz); break;
    case 2: DrawSettings(sz); break;
    case 3: DrawAppearance(sz, m); break;
    default: DrawAbout(sz); break;
  }
  const HRESULT hr = m_rt->EndDraw();
  if (hr == D2DERR_RECREATE_TARGET) {
    m_preview.reset();
    m_previewId.clear();
    m_rt.Reset();
    m_brush.Reset();
    Refresh();
  }
}

void Dashboard::DrawBackground(const D2D1_SIZE_F& sz) {
  ComPtr<ID2D1GradientStopCollection> stops;
  const D2D1_GRADIENT_STOP gs[2] = {{0.0f, C(0.04f, 0.06f, 0.16f)}, {1.0f, C(0.11f, 0.09f, 0.27f)}};
  if (SUCCEEDED(m_rt->CreateGradientStopCollection(gs, 2, &stops))) {
    ComPtr<ID2D1LinearGradientBrush> lb;
    if (SUCCEEDED(m_rt->CreateLinearGradientBrush(
            D2D1::LinearGradientBrushProperties(D2D1::Point2F(0, 0), D2D1::Point2F(sz.width, sz.height)), stops.Get(), &lb)))
      m_rt->FillRectangle(D2D1::RectF(0, 0, sz.width, sz.height), lb.Get());
  }
  // two soft glows (indigo top-right, blue bottom-left) give the "glass" something to refract
  auto glow = [&](float cx, float cy, float rad, D2D1_COLOR_F c) {
    const D2D1_GRADIENT_STOP g[2] = {{0.0f, c}, {1.0f, C(c.r, c.g, c.b, 0.0f)}};
    ComPtr<ID2D1GradientStopCollection> sc;
    ComPtr<ID2D1RadialGradientBrush> rb;
    if (SUCCEEDED(m_rt->CreateGradientStopCollection(g, 2, &sc)) &&
        SUCCEEDED(m_rt->CreateRadialGradientBrush(D2D1::RadialGradientBrushProperties(D2D1::Point2F(cx, cy), D2D1::Point2F(0, 0), rad, rad),
                                                  sc.Get(), &rb)))
      m_rt->FillRectangle(D2D1::RectF(cx - rad, cy - rad, cx + rad, cy + rad), rb.Get());
  };
  glow(sz.width * 0.85f, sz.height * 0.1f, 380, C(0.42f, 0.30f, 0.90f, 0.35f));
  glow(sz.width * 0.35f, sz.height * 0.95f, 420, C(0.10f, 0.35f, 0.85f, 0.22f));
}

void Dashboard::DrawSidebar(const D2D1_SIZE_F& sz, const DashboardModel& m) {
  m_rt->FillRectangle(D2D1::RectF(0, 0, kSidebarW, sz.height), (SetColor(C(0.02f, 0.03f, 0.10f, 0.45f)), m_brush.Get()));
  SetColor(C(1, 1, 1, 0.08f));
  m_rt->FillRectangle(D2D1::RectF(kSidebarW - 1, 0, kSidebarW, sz.height), m_brush.Get());

  // logo: hexagon with a "layers" glyph
  {
    const float cx = 62, cy = 62, r = 28;
    ComPtr<ID2D1PathGeometry> pg;
    ComPtr<ID2D1GeometrySink> sink;
    if (SUCCEEDED(m_gfx.D2DFactory()->CreatePathGeometry(&pg)) && SUCCEEDED(pg->Open(&sink))) {
      for (int i = 0; i < 6; ++i) {
        const float a = (60.0f * static_cast<float>(i) - 90.0f) * 3.14159265f / 180.0f;
        const D2D1_POINT_2F p = D2D1::Point2F(cx + std::cos(a) * r, cy + std::sin(a) * r);
        if (i == 0) sink->BeginFigure(p, D2D1_FIGURE_BEGIN_FILLED); else sink->AddLine(p);
      }
      sink->EndFigure(D2D1_FIGURE_END_CLOSED);
      sink->Close();
      SetColor(Accent(1.0f));
      m_rt->FillGeometry(pg.Get(), m_brush.Get());
    }
    SetColor(kWhite);
    m_rt->DrawLine(D2D1::Point2F(cx - 13, cy - 2), D2D1::Point2F(cx, cy - 10), m_brush.Get(), 2.5f);
    m_rt->DrawLine(D2D1::Point2F(cx, cy - 10), D2D1::Point2F(cx + 13, cy - 2), m_brush.Get(), 2.5f);
    m_rt->DrawLine(D2D1::Point2F(cx + 13, cy - 2), D2D1::Point2F(cx, cy + 6), m_brush.Get(), 2.5f);
    m_rt->DrawLine(D2D1::Point2F(cx, cy + 6), D2D1::Point2F(cx - 13, cy - 2), m_brush.Get(), 2.5f);
    m_rt->DrawLine(D2D1::Point2F(cx - 13, cy + 6), D2D1::Point2F(cx, cy + 14), m_brush.Get(), 2.5f);
    m_rt->DrawLine(D2D1::Point2F(cx, cy + 14), D2D1::Point2F(cx + 13, cy + 6), m_brush.Get(), 2.5f);
  }
  Txt(L"DeskSkin", R(100, 38, 130, 28), 24, true, kWhite);
  Txt(L"Customize your desktop", R(100, 64, 136, 20), 12, false, C(1, 1, 1, 0.75f));

  const wchar_t* names[kNavCount] = {L"Widgets", L"Widget Library", L"Settings", L"Appearance", L"About"};
  const int icons[kNavCount] = {4, 14, -1, 2, 5};
  for (int i = 0; i < kNavCount; ++i) {
    const D2D1_RECT_F r = R(18, 128.0f + 62.0f * static_cast<float>(i), kSidebarW - 36, 52);
    const bool sel = (m_page == i), hot = (m_hover == NavBase + i);
    if (sel) {
      Fill(r, 14, Accent(0.85f));
      Stroke(r, 14, C(1, 1, 1, 0.35f), 1.0f);
    } else if (hot) {
      Fill(r, 14, C(1, 1, 1, 0.07f));
    }
    const float cx = r.left + 32, cy = (r.top + r.bottom) / 2;
    if (icons[i] < 0) Gear(cx, cy, 12, kWhite); else Icon(icons[i], cx, cy, 24, kWhite);
    Txt(names[i], R(r.left + 62, r.top, 130, 52), 17, false, kWhite);
    AddHit(r, NavBase + i);
  }

  // bottom: Start with Windows
  SetColor(C(1, 1, 1, 0.1f));
  m_rt->FillRectangle(D2D1::RectF(20, sz.height - 128, kSidebarW - 20, sz.height - 127), m_brush.Get());
  Icon(9, 44, sz.height - 90, 26, Accent(1.0f));
  Txt(L"Start with Windows", R(68, sz.height - 108, 160, 36), 15, false, kWhite);
  Toggle(R(kSidebarW - 88, sz.height - 66, 56, 30), m.startWithWindows, SideAuto);
}

void Dashboard::PageTitle(const wchar_t* title, const wchar_t* subtitle, float x) {
  Txt(title, R(x, 24, 500, 48), 34, true, kWhite);
  Txt(subtitle, R(x, 72, 700, 28), 17, false, C(1, 1, 1, 0.78f));
}

void Dashboard::DrawWidgets(const D2D1_SIZE_F& sz, const DashboardModel& m) {
  const float x0 = kSidebarW + 36, W = sz.width - x0 - 36;
  const float extra = std::max(0.0f, sz.height - kMinH);
  PageTitle(L"Widgets", L"Toggle and customize your desktop widgets.", x0);

  // ---- Time card
  float y = 124;
  {
    const float h = 150 + extra * 0.5f;
    const D2D1_RECT_F card = R(x0, y, W, h);
    Glass(card, 22, m.timeOn);
    const D2D1_RECT_F tile = R(x0 + 24, y + (h - 92) / 2, 92, 92);
    IconTile(tile);
    Icon(0, tile.left + 46, tile.top + 46, 54, kWhite);
    Txt(L"Time", R(x0 + 144, y + 18, 300, 34), 25, true, kWhite);
    Txt(m.timeAvailable ? L"Show the time on your desktop." : L"No time skin found in the Skins folder.",
        R(x0 + 144, y + 52, 500, 26), 15, false, C(1, 1, 1, 0.78f));
    float cx = x0 + 144;
    const float cy = y + h - 60;
    cx += Chip(cx, cy, 0, m.use24h ? L"24-hour" : L"12-hour", ChipFormat) + 12;
    cx += Chip(cx, cy, 1, m.showDate ? L"Date: On" : L"Date: Off", ChipDate) + 12;
    Chip(cx, cy, 2, L"Style: " + m.timeStyle, ChipStyle);
    GearButton(R(x0 + W - 24 - 44, y + 18, 44, 44), TimeGear);
    Toggle(R(x0 + W - 24 - 76, cy, 76, 40), m.timeOn, TimeToggle);
    y += h + 20;
  }
  // ---- Quick Actions card
  {
    const float h = 130 + extra * 0.5f;
    const D2D1_RECT_F card = R(x0, y, W, h);
    Glass(card, 22, false);
    const D2D1_RECT_F tile = R(x0 + 24, y + (h - 92) / 2, 92, 92);
    IconTile(tile);
    Icon(8, tile.left + 46, tile.top + 46, 54, kWhite);
    Txt(L"Quick Actions", R(x0 + 144, y + 14, 400, 34), 25, true, kWhite);
    Txt(m.actionsAvailable ? L"Access useful actions with one click." : L"No Quick Actions skin found in the Skins folder.",
        R(x0 + 144, y + 48, 500, 26), 15, false, C(1, 1, 1, 0.78f));
    float cx = x0 + 144;
    const float cy = y + h - 56;
    cx += Chip(cx, cy, 3, L"Edit Actions", ChipEdit) + 12;
    Chip(cx, cy, 4, L"Customize Layout", ChipLayout);
    GearButton(R(x0 + W - 24 - 44, y + 14, 44, 44), ActGear);
    Toggle(R(x0 + W - 24 - 76, cy, 76, 40), m.actionsOn, ActToggle);
    y += h + 24;
  }
  // ---- Advanced options (collapsible)
  {
    const D2D1_RECT_F hdr = R(x0, y, 260, 36);
    SetColor(C(1, 1, 1, 0.1f));
    m_rt->FillRectangle(D2D1::RectF(x0, y - 10, x0 + W, y - 9), m_brush.Get());
    Txt(L"Advanced Options", R(x0 + 4, y, 220, 36), 19, false, kWhite);
    Chevron(x0 + 4 + TextW(L"Advanced Options", 19, false) + 20, y + 18, 5.5f, m_advOpen, C(1, 1, 1, 0.85f));
    AddHit(hdr, AdvHeader);
    y += 46;
    if (m_advOpen) {
      const float cw = (W - 20) / 2, h = 82;
      for (int i = 0; i < 2; ++i) {
        const float x = x0 + static_cast<float>(i) * (cw + 20);
        Glass(R(x, y, cw, h), 18, false);
        Icon(i == 0 ? 6 : 7, x + 40, y + h / 2, 30, kWhite);
        Txt(i == 0 ? L"Performance Mode" : L"Auto-Hide on Idle", R(x + 76, y + 14, cw - 170, 28), 17, true, kWhite);
        Txt(i == 0 ? L"Use fewer resources" : L"Hide widgets when inactive", R(x + 76, y + 42, cw - 170, 24), 14, false, C(1, 1, 1, 0.75f));
        Toggle(R(x + cw - 24 - 60, y + (h - 32) / 2, 60, 32), i == 0 ? m.performance : m.autoHide, i == 0 ? PerfToggle : IdleToggle);
      }
      y += h + 16;
    }
  }
  // ---- Tip bar
  {
    const D2D1_RECT_F bar = R(x0, y, W, 50);
    Glass(bar, 16, false);
    Icon(5, x0 + 32, y + 25, 24, C(1, 1, 1, 0.9f));
    Txt(m.tip, R(x0 + 60, y, W - 80, 50), 15, false, C(1, 1, 1, 0.9f));
  }
}

void Dashboard::DrawSettings(const D2D1_SIZE_F& sz) {
  const float x0 = kSidebarW + 36, W = sz.width - x0 - 36;
  PageTitle(L"Settings", L"Maintenance and housekeeping for your widgets.", x0);
  struct Row { const wchar_t* title; const wchar_t* desc; int id; int icon; };
  const Row rows[3] = {
      {L"Refresh all widgets", L"Reload every skin from disk. Saved skin files also reload on their own.", SetRefresh, 8},
      {L"Open skins folder", L"Add, remove or edit skins (Skins\\<Name>\\<Name>.ini).", SetFolder, 4},
      {L"Reset all positions", L"Put every widget back at its default position.", SetReset, 0}};
  float y = 124;
  for (const Row& r : rows) {
    const D2D1_RECT_F rc = R(x0, y, W, 86);
    Glass(rc, 20, m_hover == r.id);
    IconTile(R(x0 + 18, y + 13, 60, 60));
    Icon(r.icon, x0 + 48, y + 43, 30, kWhite);
    Txt(r.title, R(x0 + 96, y + 14, W - 180, 28), 19, true, kWhite);
    Txt(r.desc, R(x0 + 96, y + 44, W - 180, 26), 14, false, C(1, 1, 1, 0.75f));
    Chevron(x0 + W - 36, y + 43, 7, false, C(1, 1, 1, 0.8f));
    AddHit(rc, r.id);
    y += 102;
  }
  Txt(L"Your widget positions and options are saved automatically.", R(x0, y + 6, W, 28), 14, false, C(1, 1, 1, 0.65f));
}

void Dashboard::DrawAppearance(const D2D1_SIZE_F& sz, const DashboardModel& m) {
  const float x0 = kSidebarW + 36, W = sz.width - x0 - 36;
  PageTitle(L"Appearance", L"Choose the accent color used by this window.", x0);
  Glass(R(x0, 124, W, 190), 22, false);
  Txt(L"Accent color", R(x0 + 28, 142, 300, 32), 21, true, kWhite);
  for (int i = 0; i < 4; ++i) {
    const float cx = x0 + 28 + 54 + static_cast<float>(i) * 140, cy = 232;
    const Rgb& c = kAccents[i];
    SetColor(C(c.r, c.g, c.b, 1));
    m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 28, 28), m_brush.Get());
    if (m.accent == i) {
      SetColor(kWhite);
      m_rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 35, 35), m_brush.Get(), 2.5f);
    } else if (m_hover == AccentBase + i) {
      SetColor(C(1, 1, 1, 0.5f));
      m_rt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(cx, cy), 34, 34), m_brush.Get(), 1.5f);
    }
    Txt(kAccentNames[i], R(cx - 60, cy + 40, 120, 24), 13, false, C(1, 1, 1, 0.85f), true);
    AddHit(R(cx - 36, cy - 36, 72, 72), AccentBase + i);
  }
}

void Dashboard::DrawAbout(const D2D1_SIZE_F& sz) {
  const float x0 = kSidebarW + 36, W = sz.width - x0 - 36;
  PageTitle(L"About", L"A lightweight, native desktop widget engine.", x0);
  Glass(R(x0, 124, W, 250), 22, false);
  Txt(L"DeskSkin 0.3", R(x0 + 28, 144, 400, 34), 24, true, kWhite);
  const wchar_t* lines[5] = {L"Native C++20 with Win32, Direct2D, DirectWrite and DirectComposition.",
                             L"Widgets are plain .ini skins in the Skins folder; saving one reloads it.",
                             L"Drag a widget to move it. Right-click it for lock, click-through and more.",
                             L"Settings are stored in settings.ini next to the program.",
                             L"If something looks wrong, DeskSkin.log (same folder) explains what happened."};
  for (int i = 0; i < 5; ++i) Txt(lines[i], R(x0 + 28, 194 + 36.0f * static_cast<float>(i), W - 56, 30), 15, false, C(1, 1, 1, 0.85f));
}

}  // namespace deskskin::ui
