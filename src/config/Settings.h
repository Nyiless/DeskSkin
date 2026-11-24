// Persistent per-skin state (position, enabled, locked, click-through) in a small .ini file.
// Portable C++ (no Win32), so it is unit-tested on any OS. Writes are atomic (temp + rename).
#pragma once
#include <filesystem>
#include <map>
#include <string>

namespace deskskin::config {

struct SkinState {
  bool hasPosition = false;
  int x = 0;  // screen pixels (virtual-screen coordinates)
  int y = 0;
  bool enabled = true;
  bool locked = false;        // cannot be dragged
  bool clickThrough = false;  // ignores the mouse entirely
};

// Dashboard-wide options, stored in the reserved section [@App].
struct AppState {
  bool use24h = true;
  bool showDate = true;
  int accent = 0;             // index into the dashboard's accent palette
  bool performanceMode = false;
  bool autoHideOnIdle = false;
  std::string timeStyle;      // folder name of the preferred Time skin, e.g. "NeonClock"
};

class Settings {
 public:
  AppState& App() { return m_app; }
  const AppState& App() const { return m_app; }

  void SetPath(std::filesystem::path p) { m_path = std::move(p); }
  const std::filesystem::path& Path() const noexcept { return m_path; }

  bool Load();        // false if the file is missing/unreadable (state is left as-is)
  bool Save() const;  // false on I/O error

  SkinState& Skin(const std::string& key) { return m_skins[key]; }
  SkinState Get(const std::string& key) const {
    const auto it = m_skins.find(key);
    return it == m_skins.end() ? SkinState{} : it->second;
  }
  void ResetPositions() {
    for (auto& [k, s] : m_skins) s.hasPosition = false;
  }

 private:
  std::filesystem::path m_path;
  AppState m_app;
  std::map<std::string, SkinState> m_skins;  // key: "Folder/File.ini" relative to Skins\, '/' separators
};

}  // namespace deskskin::config
