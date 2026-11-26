// "Start with Windows" via HKCU\...\Run (per-user, no admin rights, trivially reversible).
#pragma once

namespace deskskin::platform::autostart {

bool IsEnabled();
bool SetEnabled(bool enabled);
// If autostart is on but the exe was moved, repoint the registry entry at the current path.
void SyncPath();

}  // namespace deskskin::platform::autostart
