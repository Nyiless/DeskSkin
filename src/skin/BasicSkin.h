// Declarative skin: reads an .ini via ini::Document and draws String, Bar, Histogram, Bars (audio),
// Hex and Calendar meters. Text can contain %TOKENS% (time/date built in, everything else from
// measures::Hub); Bar/Histogram meters can bind to a Hub source with Source=.
#pragma once
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "config/IniParser.h"
#include "core/Memory.h"
#include "render/GraphicsDevice.h"
#include "render/SkinWindow.h"

namespace deskskin::skin {

// Global time-token options (set by the dashboard): %TIME% / %HM% / %HOUR% follow use24h; date tokens follow showDate.
struct TimeFormat {
  bool use24h = true;
  bool showDate = true;
};
void SetTimeFormat(const TimeFormat& f);

// Click actions (LeftMouseUpAction=):
//   Open:<file|folder|url|ms-settings:...>      Run:<program>|<arguments>
//   Cmd:MediaPlayPause|MediaNext|MediaPrev|VolumeMute|VolumeUp|VolumeDown|EmptyRecycleBin|EditNotes|OpenFeed:<n>
//   Cmd:Search:<url prefix>   (asks for a query, then opens prefix + query)
bool RunAction(const std::wstring& action);
using PromptFn = bool (*)(const std::wstring& title, const std::wstring& label, std::wstring& value);
void SetPromptHandler(PromptFn fn);

using VarOverrides = std::vector<std::pair<std::string, std::string>>;

enum class MeterKind : unsigned char { String, Bar, Histogram, Bars, Hex, Calendar };

struct Meter {
  MeterKind kind = MeterKind::String;
  D2D1_RECT_F rect{};
  D2D1_COLOR_F color{1, 1, 1, 1};
  D2D1_COLOR_F backColor{0, 0, 0, 0.35f};
  D2D1_COLOR_F border{1, 1, 1, 0.18f};
  float value = 0.0f;  // Bar: 0..1
  float minV = 0.0f, maxV = 100.0f;
  bool dynamic = false;
  std::wstring templ;   // raw text with tokens
  std::wstring text;    // currently displayed text
  std::wstring action;  // LeftMouseUpAction
  std::string source;   // Hub source (Bar/Histogram)
  int bands = 32, style = 0, weekStart = 0;
  bool rainbow = false;
  unsigned version = 0;
  int lastDay = -1;
  Microsoft::WRL::ComPtr<IDWriteTextFormat> format;  // device-independent
};

// All Meter objects of all skins come from one pool: reloading skins recycles slots.
using MeterPool = core::ObjectPool<Meter, 32>;

class BasicSkin final : public render::ISkinContent {
 public:
  static std::unique_ptr<BasicSkin> Load(const std::filesystem::path& iniPath, render::GraphicsDevice& gfx,
                                         MeterPool& pool, std::vector<std::string>* warnings,
                                         const VarOverrides& overrides = {});
  static std::unique_ptr<BasicSkin> LoadFromText(std::string_view iniText, const std::wstring& title,
                                                 render::GraphicsDevice& gfx, MeterPool& pool,
                                                 std::vector<std::string>* warnings, const VarOverrides& overrides = {});

  render::SkinWindowDesc WindowDesc() const noexcept { return m_desc; }
  unsigned UpdateMs() const noexcept { return m_updateMs; }
  bool HasDynamicContent() const noexcept { return m_hasDynamic; }

  // Refreshes dynamic meters; returns true and the union of changed areas (DIPs) if anything changed.
  bool Tick(RECT* dirtyDip);

  void Draw(ID2D1RenderTarget* rt, const D2D1_RECT_F& dirty) override;
  void OnMouse(UINT msg, int xDip, int yDip) override;
  void OnDeviceLost() override { m_brush.Reset(); }

 private:
  BasicSkin() = default;
  static std::unique_ptr<BasicSkin> Build(const ini::Document& doc, const std::wstring& title, render::GraphicsDevice& gfx,
                                          MeterPool& pool, std::vector<std::string>* warnings);
  void DrawMeter(ID2D1RenderTarget* rt, const Meter& m);

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
