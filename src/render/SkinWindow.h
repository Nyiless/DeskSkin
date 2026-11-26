// One skin = one borderless, per-pixel-alpha window parented to the wallpaper layer.
//
// Rendering path: Direct2D device context -> DXGI flip-model swap chain (premultiplied alpha)
// -> DirectComposition visual. WS_EX_NOREDIRECTIONBITMAP means Windows allocates NO GDI
// redirection surface for the window; the only per-skin pixel memory is the 2 swap-chain buffers.
//
// Rendering is strictly event-driven: nothing is drawn unless Invalidate() was called. The
// message loop blocks in GetMessage, so an idle engine uses 0% CPU.
#pragma once
#include "platform/DesktopHost.h"
#include "render/GraphicsDevice.h"

namespace deskskin::render {

class ISkinContent {
 public:
  virtual ~ISkinContent() = default;
  // `dirty` is in DIPs. The host has already clipped to it and cleared it to transparent, so
  // implementations should only draw what intersects `dirty`.
  virtual void Draw(ID2D1RenderTarget* rt, const D2D1_RECT_F& dirty) = 0;
  virtual void OnMouse(UINT /*msg*/, int /*xDip*/, int /*yDip*/) {}
  // The GPU device went away; drop any device-dependent resources (brushes, bitmaps...).
  virtual void OnDeviceLost() {}
};

struct SkinWindowDesc {
  int x = 100;  // screen pixels (virtual-screen coordinates)
  int y = 100;
  int widthDip = 300;
  int heightDip = 120;
  const wchar_t* title = L"DeskSkin";
};

class SkinWindow {
 public:
  SkinWindow(GraphicsDevice& gfx, const platform::DesktopHost& host) : m_gfx(gfx), m_host(host) {}
  ~SkinWindow() { Destroy(); }
  SkinWindow(const SkinWindow&) = delete;
  SkinWindow& operator=(const SkinWindow&) = delete;

  HRESULT Create(const SkinWindowDesc& desc, ISkinContent* content);
  // Used after Explorer restarts: the old parent (and so this window) is gone.
  HRESULT Recreate() { Destroy(); return Create(m_desc, m_content); }
  void Destroy() noexcept;

  // Marks a region (DIPs) dirty and schedules exactly one WM_PAINT. Coalescing is done by the
  // OS: N calls before the message is dispatched produce one Render().
  void Invalidate(const RECT& dipRect) noexcept;
  void InvalidateAll() noexcept;

  void MoveTo(int screenX, int screenY) noexcept;
  HRESULT Resize(int widthDip, int heightDip);
  void SetClickThrough(bool on) noexcept { m_clickThrough = on; }

  // Receives `msg` (posted) when the GPU device is lost so the owner can rebuild everything.
  void SetNotifyWindow(HWND hwnd, UINT msg) noexcept { m_notify = hwnd; m_notifyMsg = msg; }

  void SetVisible(bool v) noexcept {
    if (m_hwnd) ShowWindow(m_hwnd, v ? SW_SHOWNOACTIVATE : SW_HIDE);
  }
  bool IsVisible() const noexcept { return m_hwnd && IsWindowVisible(m_hwnd); }
  bool IsAlive() const noexcept { return m_hwnd != nullptr; }
  bool IsOnWallpaperLayer() const noexcept { return m_onDesktop; }
  RECT ScreenRect() const noexcept {
    RECT r{0, 0, 0, 0};
    if (m_hwnd) GetWindowRect(m_hwnd, &r);
    return r;
  }
  POINT Position() const noexcept { return POINT{m_desc.x, m_desc.y}; }
  UINT Dpi() const noexcept { return m_dpi; }
  HWND Hwnd() const noexcept { return m_hwnd; }

 private:
  static bool RegisterWindowClass(HINSTANCE hi);
  static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
  LRESULT OnMessage(HWND, UINT, WPARAM, LPARAM);

  HRESULT CreateInternal(bool useParent);
  HRESULT CreateDeviceResources();
  void ReleaseDeviceResources() noexcept;
  void Render();
  RECT FullRect() const noexcept { return RECT{0, 0, m_pxW, m_pxH}; }
  int ToPx(int dip) const noexcept { return MulDiv(dip, static_cast<int>(m_dpi), 96); }

  GraphicsDevice& m_gfx;
  const platform::DesktopHost& m_host;
  SkinWindowDesc m_desc{};
  ISkinContent* m_content = nullptr;

  HWND m_hwnd = nullptr;
  HWND m_notify = nullptr;
  UINT m_notifyMsg = 0;
  UINT m_dpi = 96;
  int m_pxW = 1, m_pxH = 1;
  bool m_clickThrough = false;
  bool m_trackingMouse = false;
  bool m_onDesktop = false;

  Microsoft::WRL::ComPtr<IDXGISwapChain1> m_swap;
  Microsoft::WRL::ComPtr<ID2D1DeviceContext> m_ctx;
  Microsoft::WRL::ComPtr<IDCompositionTarget> m_target;
  Microsoft::WRL::ComPtr<IDCompositionVisual> m_visual;

  // Dirty tracking (pixels). The swap chain has two buffers that alternate, so a buffer being
  // redrawn is one frame stale: each frame must repaint dirty(now) UNION dirty(previous).
  RECT m_dirty{0, 0, 0, 0};
  RECT m_prevDirty{0, 0, 0, 0};
};

}  // namespace deskskin::render
