#include "render/SkinWindow.h"

#include "core/Log.h"

#include <shellscalingapi.h>
#include <windowsx.h>

#include <algorithm>

namespace deskskin::render {

using Microsoft::WRL::ComPtr;

namespace {
constexpr wchar_t kClassName[] = L"DeskSkin.SkinWindow";
}

// ---------------------------------------------------------------------------------------------
// Window class / lifetime
// ---------------------------------------------------------------------------------------------
bool SkinWindow::RegisterWindowClass(HINSTANCE hi) {
  static bool registered = false;
  if (registered) return true;
  WNDCLASSEXW wc{};
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = &SkinWindow::WndProc;
  wc.hInstance = hi;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = kClassName;
  // No background brush: with NOREDIRECTIONBITMAP there is nothing for GDI to paint into.
  if (!RegisterClassExW(&wc)) return false;
  registered = true;
  return true;
}

HRESULT SkinWindow::Create(const SkinWindowDesc& desc, ISkinContent* content) {
  if (m_hwnd) return S_OK;
  m_desc = desc;
  m_content = content;

  HRESULT hr = CreateInternal(m_host.Parent() != nullptr);
  if (FAILED(hr) && m_host.Parent()) {
    // Safety net: if composition on a child of the desktop window is refused on some system,
    // show the skin as a bottom-most top-level window instead of not showing it at all.
    core::Log::Write("wallpaper-layer window failed (0x{:08x}); retrying as top-level", static_cast<unsigned>(hr));
    hr = CreateInternal(false);
  }
  if (FAILED(hr)) core::Log::Write("skin window creation failed (0x{:08x})", static_cast<unsigned>(hr));
  return hr;
}

HRESULT SkinWindow::CreateInternal(bool useParent) {
  const SkinWindowDesc& desc = m_desc;
  HINSTANCE hi = GetModuleHandleW(nullptr);
  if (!RegisterWindowClass(hi)) return HRESULT_FROM_WIN32(GetLastError());

  const POINT screenPos{desc.x, desc.y};
  UINT dpiX = 96, dpiY = 96;
  if (HMONITOR mon = MonitorFromPoint(screenPos, MONITOR_DEFAULTTONEAREST))
    GetDpiForMonitor(mon, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
  m_dpi = dpiX;
  m_pxW = std::max(1, ToPx(desc.widthDip));
  m_pxH = std::max(1, ToPx(desc.heightDip));

  HWND parent = useParent ? m_host.Parent() : nullptr;
  const POINT pos = parent ? m_host.ScreenToParent(screenPos) : screenPos;

  const DWORD style = (parent ? WS_CHILD : WS_POPUP) | WS_VISIBLE;
  const DWORD exStyle = WS_EX_NOREDIRECTIONBITMAP | WS_EX_NOACTIVATE | (parent ? 0 : WS_EX_TOOLWINDOW);

  m_hwnd = CreateWindowExW(exStyle, kClassName, desc.title ? desc.title : L"DeskSkin", style, pos.x, pos.y,
                           m_pxW, m_pxH, parent, nullptr, hi, this);
  if (!m_hwnd) return HRESULT_FROM_WIN32(GetLastError());

  // Keep skins below sibling windows (matters for the Progman fallback layout, harmless otherwise).
  SetWindowPos(m_hwnd, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

  const HRESULT hr = CreateDeviceResources();
  if (FAILED(hr)) {
    Destroy();
    return hr;
  }

  m_onDesktop = useParent;
  m_dirty = FullRect();
  m_prevDirty = FullRect();  // both swap-chain buffers start undefined => first two frames are full
  InvalidateRect(m_hwnd, nullptr, FALSE);
  return S_OK;
}

void SkinWindow::Destroy() noexcept {
  if (m_hwnd) DestroyWindow(m_hwnd);  // WM_NCDESTROY releases GPU resources and nulls m_hwnd
  ReleaseDeviceResources();
  m_hwnd = nullptr;
}

// ---------------------------------------------------------------------------------------------
// GPU resources (per skin): swap chain + D2D context + DComp visual. Everything else is shared.
// ---------------------------------------------------------------------------------------------
HRESULT SkinWindow::CreateDeviceResources() {
  DXGI_SWAP_CHAIN_DESC1 sd{};
  sd.Width = static_cast<UINT>(m_pxW);
  sd.Height = static_cast<UINT>(m_pxH);
  sd.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
  sd.SampleDesc.Count = 1;
  sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  sd.BufferCount = 2;
  sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;  // supports dirty rects in Present1
  sd.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
  sd.Scaling = DXGI_SCALING_STRETCH;  // mandatory for composition swap chains

  HRESULT hr = m_gfx.DxgiFactory()->CreateSwapChainForComposition(m_gfx.D3D(), &sd, nullptr, &m_swap);
  if (FAILED(hr)) return hr;

  hr = m_gfx.D2DDevice()->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &m_ctx);
  if (FAILED(hr)) return hr;
  m_ctx->SetDpi(static_cast<float>(m_dpi), static_cast<float>(m_dpi));
  // ClearType needs an opaque target; on per-pixel-alpha surfaces it produces colour fringes.
  m_ctx->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);

  IDCompositionDevice* dcomp = m_gfx.Dcomp();
  hr = dcomp->CreateTargetForHwnd(m_hwnd, TRUE, &m_target);
  if (FAILED(hr)) return hr;
  hr = dcomp->CreateVisual(&m_visual);
  if (FAILED(hr)) return hr;
  hr = m_visual->SetContent(m_swap.Get());
  if (FAILED(hr)) return hr;
  hr = m_target->SetRoot(m_visual.Get());
  if (FAILED(hr)) return hr;
  return dcomp->Commit();
}

void SkinWindow::ReleaseDeviceResources() noexcept {
  const bool had = m_ctx || m_swap;
  m_target.Reset();
  m_visual.Reset();
  if (m_ctx) m_ctx->SetTarget(nullptr);
  m_ctx.Reset();
  m_swap.Reset();
  if (had && m_content) m_content->OnDeviceLost();
}

// ---------------------------------------------------------------------------------------------
// Invalidation + rendering
// ---------------------------------------------------------------------------------------------
void SkinWindow::Invalidate(const RECT& dip) noexcept {
  if (!m_hwnd) return;
  RECT px{ToPx(dip.left) - 1, ToPx(dip.top) - 1, ToPx(dip.right) + 1, ToPx(dip.bottom) + 1};
  const RECT full = FullRect();
  IntersectRect(&px, &px, &full);
  if (IsRectEmpty(&px)) return;
  UnionRect(&m_dirty, &m_dirty, &px);
  InvalidateRect(m_hwnd, nullptr, FALSE);  // WM_PAINT is coalesced by the OS
}

void SkinWindow::InvalidateAll() noexcept {
  if (!m_hwnd) return;
  m_dirty = FullRect();
  InvalidateRect(m_hwnd, nullptr, FALSE);
}

void SkinWindow::Render() {
  if (!m_ctx || !m_swap || !m_content) return;

  const RECT full = FullRect();
  RECT cur = m_dirty;
  m_dirty = RECT{0, 0, 0, 0};
  if (IsRectEmpty(&cur) && IsRectEmpty(&m_prevDirty)) return;  // spurious WM_PAINT

  RECT draw{};
  UnionRect(&draw, &cur, &m_prevDirty);  // see comment on m_prevDirty
  IntersectRect(&draw, &draw, &full);
  m_prevDirty = cur;
  if (IsRectEmpty(&draw)) return;

  const float scale = 96.0f / static_cast<float>(m_dpi);
  const D2D1_RECT_F clip = D2D1::RectF(draw.left * scale, draw.top * scale, draw.right * scale, draw.bottom * scale);

  HRESULT hr = S_OK;
  {
    // Flip-model back buffers rotate every Present, so the target bitmap is rebuilt per frame.
    ComPtr<IDXGISurface> surface;
    hr = m_swap->GetBuffer(0, IID_PPV_ARGS(&surface));
    if (FAILED(hr)) goto lost;

    const D2D1_BITMAP_PROPERTIES1 bp = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
        static_cast<float>(m_dpi), static_cast<float>(m_dpi));

    ComPtr<ID2D1Bitmap1> target;
    hr = m_ctx->CreateBitmapFromDxgiSurface(surface.Get(), &bp, &target);
    if (FAILED(hr)) goto lost;

    m_ctx->SetTarget(target.Get());
    m_ctx->BeginDraw();
    m_ctx->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_ALIASED);
    m_ctx->Clear(D2D1::ColorF(0.0f, 0.0f, 0.0f, 0.0f));  // transparent, clipped to the dirty rect
    m_content->Draw(m_ctx.Get(), clip);
    m_ctx->PopAxisAlignedClip();
    hr = m_ctx->EndDraw();
    m_ctx->SetTarget(nullptr);  // drop the buffer reference before Present
    if (FAILED(hr)) goto lost;
  }

  {
    DXGI_PRESENT_PARAMETERS pp{};
    pp.DirtyRectsCount = 1;
    pp.pDirtyRects = &draw;
    hr = m_swap->Present1(1, 0, &pp);
    if (FAILED(hr)) goto lost;
  }
  return;

lost:
  if (hr == D2DERR_RECREATE_TARGET || hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
    if (m_notify) PostMessageW(m_notify, m_notifyMsg, 0, 0);  // owner rebuilds GraphicsDevice + skins
  }
  m_dirty = full;  // retry whole surface next time we are asked to paint
  m_prevDirty = full;
}

// ---------------------------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------------------------
void SkinWindow::MoveTo(int screenX, int screenY) noexcept {
  if (!m_hwnd) return;
  m_desc.x = screenX;
  m_desc.y = screenY;
  const POINT p = m_host.Parent() ? m_host.ScreenToParent(POINT{screenX, screenY}) : POINT{screenX, screenY};
  SetWindowPos(m_hwnd, nullptr, p.x, p.y, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

HRESULT SkinWindow::Resize(int widthDip, int heightDip) {
  if (!m_hwnd || !m_swap) return E_UNEXPECTED;
  m_desc.widthDip = widthDip;
  m_desc.heightDip = heightDip;
  m_pxW = std::max(1, ToPx(widthDip));
  m_pxH = std::max(1, ToPx(heightDip));
  m_ctx->SetTarget(nullptr);  // no outstanding buffer references allowed during ResizeBuffers
  const HRESULT hr = m_swap->ResizeBuffers(0, static_cast<UINT>(m_pxW), static_cast<UINT>(m_pxH),
                                           DXGI_FORMAT_UNKNOWN, 0);
  if (FAILED(hr)) return hr;
  SetWindowPos(m_hwnd, nullptr, 0, 0, m_pxW, m_pxH, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
  m_prevDirty = FullRect();
  InvalidateAll();
  return S_OK;
}

// ---------------------------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------------------------
LRESULT CALLBACK SkinWindow::WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  SkinWindow* self = nullptr;
  if (m == WM_NCCREATE) {
    self = static_cast<SkinWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);
    SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
  } else {
    self = reinterpret_cast<SkinWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
  }
  return self ? self->OnMessage(h, m, w, l) : DefWindowProcW(h, m, w, l);
}

LRESULT SkinWindow::OnMessage(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_PAINT: {
      PAINTSTRUCT ps;
      BeginPaint(h, &ps);  // validates the update region; nothing is drawn through GDI
      Render();
      EndPaint(h, &ps);
      return 0;
    }
    case WM_ERASEBKGND:
      return 1;
    case WM_NCHITTEST:
      return m_clickThrough ? HTTRANSPARENT : HTCLIENT;
    case WM_MOUSEACTIVATE:
      return MA_NOACTIVATE;
    case WM_MOUSEMOVE:
      if (!m_trackingMouse) {
        TRACKMOUSEEVENT tme{sizeof(tme), TME_LEAVE, h, 0};
        m_trackingMouse = TrackMouseEvent(&tme) != FALSE;
      }
      [[fallthrough]];
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONUP:
      if (m_content)
        m_content->OnMouse(m, MulDiv(GET_X_LPARAM(l), 96, static_cast<int>(m_dpi)),
                           MulDiv(GET_Y_LPARAM(l), 96, static_cast<int>(m_dpi)));
      return 0;
    case WM_MOUSELEAVE:
      m_trackingMouse = false;
      if (m_content) m_content->OnMouse(WM_MOUSELEAVE, -1, -1);
      return 0;
    case WM_NCDESTROY:
      // Also reached when Explorer dies and takes our parent (and us) with it.
      ReleaseDeviceResources();
      SetWindowLongPtrW(h, GWLP_USERDATA, 0);
      m_hwnd = nullptr;
      return DefWindowProcW(h, m, w, l);
    default:
      return DefWindowProcW(h, m, w, l);
  }
}

}  // namespace deskskin::render
