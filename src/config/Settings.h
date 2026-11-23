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

class Settings {
 public:
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
  std::map<std::string, SkinState> m_skins;  // key: "Folder/File.ini" relative to Skins\, '/' separators
};

}  // namespace deskskin::config
