// Tiny file logger (DeskSkin.log next to the settings). Essential for diagnosing desktop-layer
// problems on machines we cannot see. Not thread-safe: call from the UI thread only.
#pragma once
#include <filesystem>
#include <format>
#include <fstream>
#include <utility>

namespace deskskin::core {

class Log {
 public:
  static void Open(const std::filesystem::path& path) { State().out.open(path, std::ios::trunc); }
  template <class... Args>
  static void Write(std::format_string<Args...> fmt, Args&&... args) {
    auto& o = State().out;
    if (!o.is_open()) return;
    o << std::format(fmt, std::forward<Args>(args)...) << '\n';
    o.flush();
  }

 private:
  struct Impl {
    std::ofstream out;
  };
  static Impl& State() {
    static Impl s;
    return s;
  }
};

}  // namespace deskskin::core
