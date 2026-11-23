#include "config/IniParser.h"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <limits>

namespace deskskin::ini {
namespace {

constexpr std::size_t kMaxExpandedValue = 64 * 1024;

constexpr bool IsSpace(char c) noexcept {
  return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
}
constexpr char Lower(char c) noexcept { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; }

std::string_view Trim(std::string_view s) noexcept {
  while (!s.empty() && IsSpace(s.front())) s.remove_prefix(1);
  while (!s.empty() && IsSpace(s.back())) s.remove_suffix(1);
  return s;
}

bool IEquals(std::string_view a, std::string_view b) noexcept {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (Lower(a[i]) != Lower(b[i])) return false;
  return true;
}

bool IsVarName(std::string_view n) noexcept {
  if (n.empty()) return false;
  for (char c : n) {
    const bool ok = (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                    c == '_' || c == '.';
    if (!ok) return false;
  }
  return true;
}

void AppendUtf8(std::string& out, std::uint32_t cp) {
  if (cp < 0x80) {
    out.push_back(static_cast<char>(cp));
  } else if (cp < 0x800) {
    out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else if (cp < 0x10000) {
    out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  } else {
    out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
    out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
  }
}

std::string Utf16ToUtf8(const char* data, std::size_t bytes, bool bigEndian) {
  std::string out;
  out.reserve(bytes / 2);
  auto unit = [&](std::size_t i) -> std::uint32_t {
    const auto a = static_cast<unsigned char>(data[i]);
    const auto b = static_cast<unsigned char>(data[i + 1]);
    return bigEndian ? (a << 8 | b) : (b << 8 | a);
  };
  for (std::size_t i = 0; i + 1 < bytes; i += 2) {
    std::uint32_t u = unit(i);
    if (u >= 0xD800 && u <= 0xDBFF && i + 3 < bytes) {
      const std::uint32_t lo = unit(i + 2);
      if (lo >= 0xDC00 && lo <= 0xDFFF) {
        u = 0x10000 + ((u - 0xD800) << 10) + (lo - 0xDC00);
        i += 2;
      }
    }
    AppendUtf8(out, u);
  }
  return out;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Storage helpers
// ---------------------------------------------------------------------------------------------
std::uint32_t Document::Intern(std::string_view s) {
  const std::size_t off = m_blob.size();
  if (off + s.size() >= std::numeric_limits<std::uint32_t>::max()) return kNone;
  m_blob.insert(m_blob.end(), s.begin(), s.end());
  return static_cast<std::uint32_t>(off);
}

std::uint32_t Document::FindSection(std::string_view name) const noexcept {
  for (std::uint32_t i = 0; i < m_sections.size(); ++i)
    if (IEquals(View(m_sections[i].nameOff, m_sections[i].nameLen), name)) return i;
  return kNone;
}

std::uint32_t Document::FindOrAddSection(std::string_view name) {
  if (const auto s = FindSection(name); s != kNone) return s;
  const std::uint32_t off = Intern(name);
  m_sections.push_back({off, static_cast<std::uint32_t>(name.size()), 0, 0});
  return static_cast<std::uint32_t>(m_sections.size() - 1);
}

std::optional<std::string_view> Document::FindIn(std::uint32_t section, std::string_view key) const noexcept {
  if (section == kNone) return std::nullopt;
  const Section& s = m_sections[section];
  for (std::uint32_t i = s.end; i > s.begin; --i) {  // backwards: last duplicate wins
    const Entry& e = m_entries[i - 1];
    if (IEquals(View(e.keyOff, e.keyLen), key)) return View(e.valOff, e.valLen);
  }
  return std::nullopt;
}

// ---------------------------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------------------------
Document Document::Parse(std::string_view text, const ParseOptions& opt, std::vector<Diagnostic>* diags) {
  Document doc;
  auto report = [&](std::size_t line, std::string msg) {
    if (diags) diags->push_back({line, std::move(msg)});
  };

  if (text.size() > opt.maxBytes) {
    report(0, "file exceeds ParseOptions::maxBytes; ignored");
    return doc;
  }
  if (text.size() >= 3 && static_cast<unsigned char>(text[0]) == 0xEF &&
      static_cast<unsigned char>(text[1]) == 0xBB && static_cast<unsigned char>(text[2]) == 0xBF)
    text.remove_prefix(3);

  doc.m_blob.reserve(text.size());  // keys+values never exceed the source size (before expansion)

  std::uint32_t current = kNone;
  std::size_t lineNo = 0;
  while (!text.empty()) {
    const std::size_t nl = text.find('\n');
    const std::string_view raw = text.substr(0, nl);
    text = (nl == std::string_view::npos) ? std::string_view{} : text.substr(nl + 1);
    ++lineNo;

    const std::string_view line = Trim(raw);
    if (line.empty() || line.front() == ';') continue;

    if (line.front() == '[') {
      const std::size_t close = line.find(']');
      if (close == std::string_view::npos) {
        report(lineNo, "unterminated section header");
        continue;
      }
      const std::string_view name = Trim(line.substr(1, close - 1));
      if (name.empty()) {
        report(lineNo, "empty section name");
        continue;
      }
      current = doc.FindOrAddSection(name);
      continue;
    }

    const std::size_t eq = line.find('=');
    if (eq == std::string_view::npos) {
      report(lineNo, "expected key=value");
      continue;
    }
    const std::string_view key = Trim(line.substr(0, eq));
    const std::string_view val = Trim(line.substr(eq + 1));
    if (key.empty()) {
      report(lineNo, "empty key");
      continue;
    }
    if (current == kNone) current = doc.FindOrAddSection("");

    const std::uint32_t ko = doc.Intern(key);
    const std::uint32_t vo = doc.Intern(val);
    if (ko == kNone || vo == kNone) {
      report(lineNo, "document too large");
      break;
    }
    doc.m_entries.push_back({current, ko, static_cast<std::uint32_t>(key.size()), vo,
                             static_cast<std::uint32_t>(val.size())});
  }

  doc.Finalize();
  if (opt.expandVariables) doc.ExpandAll(opt);

  doc.m_blob.shrink_to_fit();
  doc.m_entries.shrink_to_fit();
  doc.m_sections.shrink_to_fit();
  return doc;
}

void Document::Finalize() {
  // Group entries by section while preserving file order inside each section.
  std::stable_sort(m_entries.begin(), m_entries.end(),
                   [](const Entry& a, const Entry& b) { return a.section < b.section; });
  for (auto& s : m_sections) s.begin = s.end = 0;
  std::size_t i = 0;
  const std::size_t n = m_entries.size();
  while (i < n) {
    const std::uint32_t sec = m_entries[i].section;
    std::size_t j = i;
    while (j < n && m_entries[j].section == sec) ++j;
    m_sections[sec].begin = static_cast<std::uint32_t>(i);
    m_sections[sec].end = static_cast<std::uint32_t>(j);
    i = j;
  }
}

// ---------------------------------------------------------------------------------------------
// #Variable# expansion
// ---------------------------------------------------------------------------------------------
void Document::Expand(std::string_view in, int depth, const ParseOptions& opt, std::string& out) const {
  std::size_t i = 0;
  while (i < in.size() && out.size() < kMaxExpandedValue) {
    const char c = in[i];
    if (c != '#') {
      out.push_back(c);
      ++i;
      continue;
    }
    const std::size_t close = in.find('#', i + 1);
    if (close == std::string_view::npos) {
      out.append(in.substr(i));
      return;
    }
    const std::string_view name = in.substr(i + 1, close - i - 1);
    if (IsVarName(name)) {
      if (auto v = FindIn(m_varSection, name)) {
        if (depth < opt.maxExpandDepth)
          Expand(*v, depth + 1, opt, out);
        else
          out.append(*v);
        i = close + 1;
        continue;
      }
    }
    out.push_back('#');  // not a variable: literal '#', rescan from the next character
    ++i;
  }
}

void Document::ExpandAll(const ParseOptions& opt) {
  m_varSection = FindSection("Variables");
  if (m_varSection == kNone) return;
  std::string scratch;
  for (std::size_t i = 0; i < m_entries.size(); ++i) {
    const std::string_view v = View(m_entries[i].valOff, m_entries[i].valLen);
    if (v.find('#') == std::string_view::npos) continue;
    scratch.clear();
    Expand(v, 0, opt, scratch);  // reads blob; writes only to scratch
    if (scratch == v) continue;
    const std::uint32_t off = Intern(scratch);  // may reallocate blob: no views held across this call
    if (off == kNone) continue;
    m_entries[i].valOff = off;
    m_entries[i].valLen = static_cast<std::uint32_t>(scratch.size());
  }
}

// ---------------------------------------------------------------------------------------------
// File loading
// ---------------------------------------------------------------------------------------------
std::optional<Document> Document::ParseFile(const std::filesystem::path& path, const ParseOptions& opt,
                                            std::vector<Diagnostic>* diags) {
  std::ifstream f(path, std::ios::binary | std::ios::ate);
  if (!f) {
    if (diags) diags->push_back({0, "cannot open " + path.string()});
    return std::nullopt;
  }
  const std::streamsize size = f.tellg();
  if (size < 0 || static_cast<std::size_t>(size) > opt.maxBytes) {
    if (diags) diags->push_back({0, "file too large or unreadable"});
    return std::nullopt;
  }
  std::string bytes(static_cast<std::size_t>(size), '\0');
  f.seekg(0);
  f.read(bytes.data(), size);

  auto b = [&](std::size_t i) { return static_cast<unsigned char>(bytes[i]); };
  if (bytes.size() >= 2 && b(0) == 0xFF && b(1) == 0xFE) {
    const std::string u8 = Utf16ToUtf8(bytes.data() + 2, bytes.size() - 2, false);
    return Parse(u8, opt, diags);
  }
  if (bytes.size() >= 2 && b(0) == 0xFE && b(1) == 0xFF) {
    const std::string u8 = Utf16ToUtf8(bytes.data() + 2, bytes.size() - 2, true);
    return Parse(u8, opt, diags);
  }
  return Parse(bytes, opt, diags);
}

// ---------------------------------------------------------------------------------------------
// Accessors
// ---------------------------------------------------------------------------------------------
std::string_view Document::SectionName(std::size_t i) const noexcept {
  return i < m_sections.size() ? View(m_sections[i].nameOff, m_sections[i].nameLen) : std::string_view{};
}

bool Document::HasSection(std::string_view name) const noexcept { return FindSection(name) != kNone; }

std::optional<std::string_view> Document::Find(std::string_view section, std::string_view key) const noexcept {
  return FindIn(FindSection(section), key);
}

std::string_view Document::GetString(std::string_view s, std::string_view k, std::string_view def) const noexcept {
  const auto v = Find(s, k);
  return v ? *v : def;
}

std::int64_t Document::GetInt(std::string_view s, std::string_view k, std::int64_t def) const noexcept {
  const auto v = Find(s, k);
  if (!v) return def;
  std::int64_t out{};
  const auto r = std::from_chars(v->data(), v->data() + v->size(), out);
  return r.ec == std::errc{} ? out : def;
}

double Document::GetDouble(std::string_view s, std::string_view k, double def) const noexcept {
  const auto v = Find(s, k);
  if (!v) return def;
  double out{};
  const auto r = std::from_chars(v->data(), v->data() + v->size(), out);
  return r.ec == std::errc{} ? out : def;
}

bool Document::GetBool(std::string_view s, std::string_view k, bool def) const noexcept {
  const auto v = Find(s, k);
  if (!v) return def;
  if (IEquals(*v, "1") || IEquals(*v, "true") || IEquals(*v, "yes") || IEquals(*v, "on")) return true;
  if (IEquals(*v, "0") || IEquals(*v, "false") || IEquals(*v, "no") || IEquals(*v, "off")) return false;
  return def;
}

}  // namespace deskskin::ini
