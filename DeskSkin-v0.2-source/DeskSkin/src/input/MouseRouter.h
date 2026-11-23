// Windows behind the desktop icon layer never receive mouse messages (SHELLDLL_DefView covers the
// whole screen), so interaction is delivered by a low-level mouse hook that is consulted only for
// clicks and for moves while a press is in progress. The sink decides whether to swallow an event.
//
// Rules the sink must follow (the hook runs inside the input path of the whole system):
//   * return quickly - Windows silently removes a hook that takes ~300 ms;
//   * NEVER swallow WM_MOUSEMOVE (that would freeze the cursor);
//   * do heavy or blocking work (menus, ShellExecute, file I/O) by posting a message instead.
#pragma once
#include "platform/DesktopHost.h"

namespace deskskin::input {

class IDesktopMouseSink {
 public:
  virtual ~IDesktopMouseSink() = default;
  // `pt` is in screen pixels. Return true to swallow the event.
  virtual bool OnDesktopMouse(UINT msg, POINT pt) = 0;
};

class MouseRouter {
 public:
  MouseRouter() = default;
  MouseRouter(const MouseRouter&) = delete;
  MouseRouter& operator=(const MouseRouter&) = delete;
  ~MouseRouter() { Uninstall(); }

  bool Install(IDesktopMouseSink* sink);
  void Uninstall() noexcept;

 private:
  static LRESULT CALLBACK Proc(int code, WPARAM wp, LPARAM lp);
  HHOOK m_hook = nullptr;
};

}  // namespace deskskin::input
