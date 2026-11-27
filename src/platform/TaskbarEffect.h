// Experimental taskbar transparency/blur via the undocumented user32 SetWindowCompositionAttribute
// (the same call community tools use). Behaviour differs between Windows 11 builds; Restore() always
// returns the taskbar to the system look. Explorer re-applies its own look from time to time, so the
// owner re-calls Apply() on a slow timer while the effect is enabled (no timer when it is off).
#pragma once
#include "platform/DesktopHost.h"

namespace deskskin::platform {

enum class TaskbarStyle { Clear, Blur, Acrylic };

class TaskbarEffect {
 public:
  void Apply(TaskbarStyle style, unsigned tintAlpha /*0..255*/);
  void Restore();
};

}  // namespace deskskin::platform
