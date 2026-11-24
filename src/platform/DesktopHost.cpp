#include "platform/DesktopHost.h"

namespace deskskin::platform {
namespace {

struct EnumState {
  HWND workerW = nullptr;
};

// Legacy layout: after Progman is asked to spawn the wallpaper WorkerW, it appears as a
// top-level sibling that FOLLOWS the top-level window hosting SHELLDLL_DefView (the icons).
BOOL CALLBACK EnumTopLevel(HWND top, LPARAM lp) {
  if (FindWindowExW(top, nullptr, L"SHELLDLL_DefView", nullptr)) {
    auto* st = reinterpret_cast<EnumState*>(lp);
    st->workerW = FindWindowExW(nullptr, top, L"WorkerW", nullptr);
    return FALSE;
  }
  return TRUE;
}

}  // namespace

bool DesktopHost::Attach() {
  m_parent = nullptr;
  m_layout = Layout::None;

  m_progman = FindWindowW(L"Progman", nullptr);
  if (!m_progman) return false;

  // Undocumented but long-stable message: makes Progman create the wallpaper WorkerW.
  DWORD_PTR ignored = 0;
  SendMessageTimeoutW(m_progman, 0x052C, 0xD, 0x1, SMTO_NORMAL, 1000, &ignored);

  // Windows 11 24H2+: the WorkerW lives inside Progman, beneath SHELLDLL_DefView.
  if (HWND w = FindWindowExW(m_progman, nullptr, L"WorkerW", nullptr)) {
    m_parent = w;
    m_layout = Layout::WorkerWInProgman;
    return true;
  }

  EnumState st;
  EnumWindows(EnumTopLevel, reinterpret_cast<LPARAM>(&st));
  if (st.workerW) {
    m_parent = st.workerW;
    m_layout = Layout::WorkerWTopLevel;
    return true;
  }

  m_parent = m_progman;
  m_layout = Layout::ProgmanFallback;
  return true;
}

POINT DesktopHost::ScreenToParent(POINT screen) const noexcept {
  if (m_parent) ScreenToClient(m_parent, &screen);
  return screen;
}

}  // namespace deskskin::platform
