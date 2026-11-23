// Compact, allocation-frugal .ini parser (portable C++20, no Win32 dependency).
//
// Dialect (Rainmeter-compatible where it matters):
//   [Section]            case-insensitive names; repeated sections are merged
//   Key=Value            key/value trimmed; values may contain '=' and ';'
//   ; comment            only at the start of a line (so values like "a;b" survive)
//   #Var#                expanded from the [Variables] section (nested, depth-limited)
//   keys before any [Section] belong to the unnamed section ""
//   duplicate keys: last one wins
// Encodings: UTF-8 (with/without BOM), UTF-16 LE/BE (with BOM) -> normalised to UTF-8.
//
// Memory model: every string lives in ONE contiguous blob; entries are 20-byte records of
// offsets. A parsed Document therefore costs ~ (file size + 20 B/key) and views returned by
// accessors stay valid until the Document is destroyed (moving it is safe).
#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace deskskin::ini {

struct ParseOptions {
  bool expandVariables = true;
  std::size_t maxBytes = 4u << 20;  // reject absurd inputs
  int maxExpandDepth = 8;
};

struct Diagnostic {
  std::size_t line;  // 1-based; 0 = whole file
  std::string message;
};

class Document {
 public:
  Document() = default;
  Document(const Document&) = delete;
  Document& operator=(const Document&) = delete;
  Document(Document&&) noexcept = default;
  Document& operator=(Document&&) noexcept = default;

  static Document Parse(std::string_view utf8, const ParseOptions& opt = {},
                        std::vector<Diagnostic>* diags = nullptr);
  static std::optional<Document> ParseFile(const std::filesystem::path& path,
                                           const ParseOptions& opt = {},
                                           std::vector<Diagnostic>* diags = nullptr);

  // Sections are listed in order of first appearance (meters are drawn in file order).
  std::size_t SectionCount() const noexcept { return m_sections.size(); }
  std::string_view SectionName(std::size_t index) const noexcept;
  bool HasSection(std::string_view name) const noexcept;

  std::optional<std::string_view> Find(std::string_view section, std::string_view key) const noexcept;
  std::string_view GetString(std::string_view section, std::string_view key,
                             std::string_view def = {}) const noexcept;
  std::int64_t GetInt(std::string_view section, std::string_view key, std::int64_t def = 0) const noexcept;
  double GetDouble(std::string_view section, std::string_view key, double def = 0.0) const noexcept;
  bool GetBool(std::string_view section, std::string_view key, bool def = false) const noexcept;

  // fn(std::string_view key, std::string_view value) in file order. Duplicates are all visited.
  template <class F>
  void ForEach(std::string_view section, F&& fn) const {
    const std::uint32_t s = FindSection(section);
    if (s == kNone) return;
    for (std::uint32_t i = m_sections[s].begin; i < m_sections[s].end; ++i) {
      const Entry& e = m_entries[i];
      fn(View(e.keyOff, e.keyLen), View(e.valOff, e.valLen));
    }
  }

 private:
  static constexpr std::uint32_t kNone = 0xFFFFFFFFu;
  struct Entry {
    std::uint32_t section, keyOff, keyLen, valOff, valLen;
  };
  struct Section {
    std::uint32_t nameOff, nameLen, begin, end;
  };

  std::string_view View(std::uint32_t off, std::uint32_t len) const noexcept {
    return std::string_view(m_blob.data() + off, len);
  }
  std::uint32_t Intern(std::string_view s);
  std::uint32_t FindSection(std::string_view name) const noexcept;
  std::uint32_t FindOrAddSection(std::string_view name);
  std::optional<std::string_view> FindIn(std::uint32_t section, std::string_view key) const noexcept;
  void Finalize();
  void ExpandAll(const ParseOptions& opt);
  void Expand(std::string_view in, int depth, const ParseOptions& opt, std::string& out) const;

  std::vector<char> m_blob;  // vector (not string): no small-buffer optimisation => views survive moves
  std::vector<Entry> m_entries;
  std::vector<Section> m_sections;
  std::uint32_t m_varSection = kNone;
};

}  // namespace deskskin::ini
