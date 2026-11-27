#include "input/MouseRouter.h"

namespace deskskin::input {
namespace {
IDesktopMouseSink* g_sink = nullptr;  // the hook procedure has no user-data parameter
}

bool MouseRouter::Install(IDesktopMouseSink* sink) {
  Uninstall();
  g_sink = sink;
  m_hook = SetWindowsHookExW(WH_MOUSE_LL, &MouseRouter::Proc, GetModuleHandleW(nullptr), 0);
  if (!m_hook) g_sink = nullptr;
  return m_hook != nullptr;
}

void MouseRouter::Uninstall() noexcept {
  if (m_hook) UnhookWindowsHookEx(m_hook);
  m_hook = nullptr;
  g_sink = nullptr;
}

LRESULT CALLBACK MouseRouter::Proc(int code, WPARAM wp, LPARAM lp) {
  if (code == HC_ACTION && g_sink) {
    switch (wp) {
      case WM_MOUSEMOVE:
      case WM_LBUTTONDOWN:
      case WM_LBUTTONUP:
      case WM_RBUTTONDOWN:
      case WM_RBUTTONUP: {
        const auto* ms = reinterpret_cast<const MSLLHOOKSTRUCT*>(lp);
        if (g_sink->OnDesktopMouse(static_cast<UINT>(wp), ms->pt) && wp != WM_MOUSEMOVE) return 1;
        break;
      }
      default:
        break;
    }
  }
  return CallNextHookEx(nullptr, code, wp, lp);
}

}  // namespace deskskin::input
