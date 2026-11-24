// Locates the window that sits BEHIND the desktop icons so skins can be parented to it.
// Because skins are children of the wallpaper layer, they are not top-level windows:
// Win+D (Show Desktop) and "peek" never hide them.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

namespace deskskin::platform {

class DesktopHost {
 public:
  enum class Layout {
    None,
    WorkerWTopLevel,  // Windows 10 / 11 <= 23H2: sibling WorkerW behind the DefView host
    WorkerWInProgman, // Windows 11 24H2+: WorkerW is a child of Progman, below SHELLDLL_DefView
    ProgmanFallback,  // last resort: child of Progman, pushed to the bottom of its z-order
  };

  // Idempotent. Call again after Explorer restarts ("TaskbarCreated"): the old parent, and
  // therefore every skin window, is destroyed with it.
  bool Attach();

  HWND Parent() const noexcept { return m_parent; }
  Layout GetLayout() const noexcept { return m_layout; }

  // Skins are positioned in screen pixels; children need parent-client coordinates.
  POINT ScreenToParent(POINT screen) const noexcept;

 private:
  HWND m_progman = nullptr;
  HWND m_parent = nullptr;
  Layout m_layout = Layout::None;
};

}  // namespace deskskin::platform
