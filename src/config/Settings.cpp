#include "config/Settings.h"

#include <fstream>

#include "config/IniParser.h"

namespace deskskin::config {

bool Settings::Load() {
  ini::ParseOptions opt;
  opt.expandVariables = false;  // keys are file paths; '#' has no special meaning here
  auto doc = ini::Document::ParseFile(m_path, opt);
  if (!doc) return false;
  m_skins.clear();
  if (doc->HasSection("@App")) {
    m_app.use24h = doc->GetBool("@App", "Use24h", true);
    m_app.showDate = doc->GetBool("@App", "ShowDate", true);
    m_app.accent = static_cast<int>(doc->GetInt("@App", "Accent", 0));
    m_app.performanceMode = doc->GetBool("@App", "PerformanceMode", false);
    m_app.autoHideOnIdle = doc->GetBool("@App", "AutoHideOnIdle", false);
    m_app.timeStyle = std::string(doc->GetString("@App", "TimeStyle"));
  }
  for (std::size_t i = 0; i < doc->SectionCount(); ++i) {
    const std::string_view name = doc->SectionName(i);
    if (name.empty() || name.front() == '@') continue;
    SkinState s;
    s.hasPosition = doc->Find(name, "X").has_value() && doc->Find(name, "Y").has_value();
    if (s.hasPosition) {
      s.x = static_cast<int>(doc->GetInt(name, "X", 0));
      s.y = static_cast<int>(doc->GetInt(name, "Y", 0));
    }
    s.enabled = doc->GetBool(name, "Enabled", true);
    s.locked = doc->GetBool(name, "Locked", false);
    s.clickThrough = doc->GetBool(name, "ClickThrough", false);
    m_skins[std::string(name)] = s;
  }
  return true;
}

bool Settings::Save() const {
  if (m_path.empty()) return false;
  std::error_code ec;
  if (m_path.has_parent_path()) std::filesystem::create_directories(m_path.parent_path(), ec);

  std::filesystem::path tmp = m_path;
  tmp += ".tmp";
  {
    std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out << "; DeskSkin settings - written automatically. Safe to edit while DeskSkin is closed.\n\n";
    out << "[@App]\nUse24h=" << (m_app.use24h ? 1 : 0) << "\nShowDate=" << (m_app.showDate ? 1 : 0)
        << "\nAccent=" << m_app.accent << "\nPerformanceMode=" << (m_app.performanceMode ? 1 : 0)
        << "\nAutoHideOnIdle=" << (m_app.autoHideOnIdle ? 1 : 0) << "\nTimeStyle=" << m_app.timeStyle << "\n\n";
    for (const auto& [key, s] : m_skins) {
      if (key.find('[') != std::string::npos || key.find(']') != std::string::npos) continue;  // unrepresentable
      out << '[' << key << "]\n";
      if (s.hasPosition) out << "X=" << s.x << "\nY=" << s.y << '\n';
      out << "Enabled=" << (s.enabled ? 1 : 0) << "\nLocked=" << (s.locked ? 1 : 0)
          << "\nClickThrough=" << (s.clickThrough ? 1 : 0) << "\n\n";
    }
    out.flush();
    if (!out) return false;
  }
  std::filesystem::rename(tmp, m_path, ec);  // replaces the destination atomically on Windows and POSIX
  if (ec) {
    std::filesystem::remove(tmp, ec);
    return false;
  }
  return true;
}

}  // namespace deskskin::config
