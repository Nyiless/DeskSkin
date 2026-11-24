// Minimal declarative skin: reads an .ini via ini::Document and draws String and Bar meters.
// Image and Histogram meters (and measures/Lua) plug in here later; see docs/ARCHITECTURE.md.
#pragma once
#include <memory>
#include <string>
#include <vector>

#include "config/IniParser.h"
#include "core/Memory.h"
#include "render/GraphicsDevice.h"
#include "render/SkinWindow.h"

namespace deskskin::skin {

// Global time-token options (set by the dashboard): %TIME% / %HM% follow use24h, %DATE% follows showDate.
struct TimeFormat {
  bool use24h = true;
  bool showDate = true;
};
void SetTimeFormat(const TimeFormat& f);

enum class MeterKind : unsigned char { String, Bar };

struct Meter {
  MeterKind kind = MeterKind::String;
  D2D1_RECT_F rect{};
  D2D1_COLOR_F color{1, 1, 1, 1};
  D2D1_COLOR_F backColor{0, 0, 0, 0.35f};
  float value = 0.0f;       // Bar: 0..1
  bool dynamic = false;     // text contains %TOKENS% that change over time
  std::wstring templ;       // raw text with tokens
  std::wstring text;        // currently displayed text
  std::wstring action;      // LeftMouseUpAction, e.g. "Open:ms-settings:dateandtime"
  Microsoft::WRL::ComPtr<IDWriteTextFormat> format;  // device-independent, owned per meter
};

// All Meter objects of all skins come from one pool: reloading skins recycles slots instead of
// fragmenting the heap.
using MeterPool = core::ObjectPool<Meter, 32>;

class BasicSkin final : public render::ISkinContent {
 public:
  static std::unique_ptr<BasicSkin> Load(const std::filesystem::path& iniPath, render::GraphicsDevice& gfx,
                                         MeterPool& pool, std::vector<std::string>* warnings);

  render::SkinWindowDesc WindowDesc() const noexcept { return m_desc; }
  unsigned UpdateMs() const noexcept { return m_updateMs; }
  bool HasDynamicContent() const noexcept { return m_hasDynamic; }

  // Refreshes dynamic meters; returns true and the union of changed areas (DIPs) if anything changed.
  bool Tick(RECT* dirtyDip);

  void Draw(ID2D1DeviceContext* ctx, const D2D1_RECT_F& dirty) override;
  void OnMouse(UINT msg, int xDip, int yDip) override;
  void OnDeviceLost() override { m_brush.Reset(); }

 private:
  BasicSkin() = default;

  render::SkinWindowDesc m_desc{};
  std::wstring m_title;
  unsigned m_updateMs = 1000;
  bool m_hasDynamic = false;
  D2D1_COLOR_F m_background{0.08f, 0.1f, 0.14f, 0.8f};
  float m_cornerRadius = 12.0f;
  std::vector<MeterPool::Ptr> m_meters;
  Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brush;  // one brush, recoloured per draw call
};

}  // namespace deskskin::skin
