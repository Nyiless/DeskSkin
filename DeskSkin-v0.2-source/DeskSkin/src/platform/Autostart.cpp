#include "platform/Autostart.h"

#include <string>

#include "platform/DesktopHost.h"  // windows.h with the project's macros

namespace deskskin::platform::autostart {
namespace {

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kValueName[] = L"DeskSkin";

std::wstring Command() {
  wchar_t buf[1024];
  const DWORD n = GetModuleFileNameW(nullptr, buf, ARRAYSIZE(buf));
  return L"\"" + std::wstring(buf, n) + L"\"";
}

}  // namespace

bool IsEnabled() {
  HKEY key;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
  DWORD type = 0, size = 0;
  const LSTATUS r = RegQueryValueExW(key, kValueName, nullptr, &type, nullptr, &size);
  RegCloseKey(key);
  return r == ERROR_SUCCESS && type == REG_SZ;
}

bool SetEnabled(bool enabled) {
  HKEY key;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) != ERROR_SUCCESS) return false;
  LSTATUS r;
  if (enabled) {
    const std::wstring cmd = Command();
    r = RegSetValueExW(key, kValueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(cmd.c_str()),
                       static_cast<DWORD>((cmd.size() + 1) * sizeof(wchar_t)));
  } else {
    r = RegDeleteValueW(key, kValueName);
    if (r == ERROR_FILE_NOT_FOUND) r = ERROR_SUCCESS;
  }
  RegCloseKey(key);
  return r == ERROR_SUCCESS;
}

void SyncPath() {
  if (!IsEnabled()) return;
  HKEY key;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return;
  wchar_t buf[1024];
  DWORD size = sizeof(buf), type = 0;
  const LSTATUS r = RegQueryValueExW(key, kValueName, nullptr, &type, reinterpret_cast<BYTE*>(buf), &size);
  RegCloseKey(key);
  if (r != ERROR_SUCCESS || type != REG_SZ) return;
  const std::wstring current(buf, size / sizeof(wchar_t) > 0 ? size / sizeof(wchar_t) - 1 : 0);
  if (current != Command()) SetEnabled(true);
}

}  // namespace deskskin::platform::autostart
