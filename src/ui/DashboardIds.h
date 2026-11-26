// Hit-test ids shared by the dashboard's translation units.
#pragma once

namespace deskskin::ui {

enum DashId : int {
  NavBase = 100,  // 100..104: Widgets, Library, Settings, Appearance, About
  SideAuto = 110,
  TimeToggle = 120, TimeGear, ChipFormat, ChipDate, ChipStyle,
  ActToggle = 130, ActGear, ChipEdit, ChipLayout,
  AdvHeader = 140, PerfToggle, IdleToggle,
  SetRefresh = 150, SetFolder, SetReset,
  AccentBase = 160,  // 160..163
  LibSearch = 200, LibImport, LibDiscover,
  LibCat = 210,      // 210..219
  LibInstall = 230, LibEnable, LibReset, LibFavDetail,
  LibOpt = 240,      // 240..249
  LibRow = 300,      // 300..399
  LibStar = 400      // 400..499
};

constexpr int kNavCount = 5;

}  // namespace deskskin::ui
