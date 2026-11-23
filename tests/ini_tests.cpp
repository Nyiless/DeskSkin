#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "config/IniParser.h"

using namespace deskskin::ini;
#define CHECK(c)                                          \
  do {                                                    \
    if (!(c)) {                                           \
      std::printf("FAIL line %d: %s\n", __LINE__, #c);    \
      std::exit(1);                                       \
    }                                                     \
  } while (0)

int main() {
  std::vector<Diagnostic> diags;
  auto d = Document::Parse(
      "\xEF\xBB\xBF; comment\r\n"
      "Orphan=1\r\n"
      "[Variables]\r\n"
      "Accent=10,20,30\r\n"
      "Pad=8\r\n"
      "Both=#Accent#,255\r\n"
      "[Meter A]\r\n"
      "Text = a;b=c  \r\n"
      "X=#Pad#\r\n"
      "Color=#Both#\r\n"
      "Color=#Both#,1\r\n"
      "Hex=#FF00FF\r\n"
      "Unknown=#Nope#\r\n"
      "Flag=Yes\r\n"
      "Ratio=0.25\r\n"
      "broken line\r\n"
      "[Unterminated\r\n"
      "[variables]\r\n"
      "Later=1\r\n"
      "[meter a]\r\n"
      "Extra=merged\r\n",
      {}, &diags);

  CHECK(d.SectionCount() == 3);  // "", Variables, Meter A (case-insensitive merge)
  CHECK(d.SectionName(0) == "");
  CHECK(d.GetInt("", "orphan") == 1);
  CHECK(d.GetString("METER A", "text") == "a;b=c");
  CHECK(d.GetInt("Meter A", "X") == 8);
  CHECK(d.GetString("Meter A", "Color") == "10,20,30,255,1");  // last duplicate wins
  CHECK(d.GetString("Meter A", "Hex") == "#FF00FF");
  CHECK(d.GetString("Meter A", "Unknown") == "#Nope#");
  CHECK(d.GetBool("Meter A", "Flag"));
  CHECK(d.GetDouble("Meter A", "Ratio") == 0.25);
  CHECK(d.GetString("Meter A", "Extra") == "merged");  // repeated section merged
  CHECK(d.GetInt("Variables", "Later") == 1);
  CHECK(diags.size() == 2);  // "broken line" + unterminated header
  CHECK(!d.Find("Meter A", "Missing"));

  int n = 0;
  d.ForEach("Meter A", [&](std::string_view, std::string_view) { ++n; });
  CHECK(n == 9);

  // Move safety: views must survive a move even for tiny documents.
  auto small = Document::Parse("[a]\nk=v\n");
  auto sv = small.GetString("a", "k");
  Document moved = std::move(small);
  CHECK(sv == "v" && moved.GetString("a", "k").data() == sv.data());

  // UTF-16LE through the file loader.
  const std::string u16 = std::string("\xFF\xFE", 2) + std::string("[\0s\0]\0\n\0k\0=\0\xE9\0", 14);
  const std::filesystem::path tmp = std::filesystem::temp_directory_path() / "deskskin_u16_test.ini";
  {
    FILE* f = tmp.empty() ? nullptr : std::fopen(tmp.string().c_str(), "wb");
    CHECK(f != nullptr);  // fail with a clear message instead of crashing
    std::fwrite(u16.data(), 1, u16.size(), f);
    std::fclose(f);
  }
  auto fd = Document::ParseFile(tmp);
  std::filesystem::remove(tmp);
  CHECK(fd && fd->GetString("s", "k") == "\xC3\xA9");

  // Self-referencing variables must terminate and stay bounded.
  auto loop = Document::Parse("[Variables]\nA=#A#x\n[s]\nv=#A#\n");
  CHECK(loop.GetString("s", "v").size() < 70000);

  std::puts("ini_tests: all passed");
  return 0;
}
