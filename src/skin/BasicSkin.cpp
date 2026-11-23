#include "skin/BasicSkin.h"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cwchar>

#include <shellapi.h>

namespace deskskin::skin {
namespace {

using ini::Document;

std::wstring Widen(std::string_view s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

bool IEq(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
      return false;
  return true;
}

// "R,G,B" or "R,G,B,A" (0-255) -> D2D colour (straight alpha; D2D brushes premultiply for us).
D2D1_COLOR_F ParseColor(std::string_view s, D2D1_COLOR_F def) {
  int v[4] = {255, 255, 255, 255};
  int count = 0;
  while (!s.empty() && count < 4) {
    while (!s.empty() && s.front() == ' ') s.remove_prefix(1);
    int x = 0;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), x);
    if (r.ec != std::errc{}) return def;
    v[count++] = std::clamp(x, 0, 255);
    s.remove_prefix(static_cast<std::size_t>(r.ptr - s.data()));
    while (!s.empty() && (s.front() == ',' || s.front() == ' ')) s.remove_prefix(1);
  }
  if (count < 3) return def;
  return D2D1::ColorF(v[0] / 255.0f, v[1] / 255.0f, v[2] / 255.0f, v[3] / 255.0f);
}

std::wstring ReplaceAll(std::wstring s, const std::wstring& from, const std::wstring& to) {
  for (std::size_t pos = 0; (pos = s.find(from, pos)) != std::wstring::npos; pos += to.size())
    s.replace(pos, from.size(), to);
  return s;
}

// Built-in text tokens for the demo. Real data sources arrive with the measure plug-ins.
std::wstring ResolveTokens(const std::wstring& templ) {
  if (templ.find(L'%') == std::wstring::npos) return templ;
  SYSTEMTIME t;
  GetLocalTime(&t);
  wchar_t hms[16], hm[8], date[16];
  swprintf(hms, 16, L"%02u:%02u:%02u", t.wHour, t.wMinute, t.wSecond);
  swprintf(hm, 8, L"%02u:%02u", t.wHour, t.wMinute);
  swprintf(date, 16, L"%04u-%02u-%02u", t.wYear, t.wMonth, t.wDay);
  std::wstring out = ReplaceAll(templ, L"%TIME%", hms);
  out = ReplaceAll(std::move(out), L"%HM%", hm);
  return ReplaceAll(std::move(out), L"%DATE%", date);
}

RECT ToDipRect(const D2D1_RECT_F& r) {
  return RECT{static_cast<LONG>(std::floor(r.left)), static_cast<LONG>(std::floor(r.top)),
              static_cast<LONG>(std::ceil(r.right)), static_cast<LONG>(std::ceil(r.bottom))};
}

}  // namespace

std::unique_ptr<BasicSkin> BasicSkin::Load(const std::filesystem::path& iniPath, render::GraphicsDevice& gfx,
                                           MeterPool& pool, std::vector<std::string>* warnings) {
  auto warn = [&](std::string m) {
    if (warnings) warnings->push_back(iniPath.filename().string() + ": " + std::move(m));
  };

  std::vector<ini::Diagnostic> diags;
  auto doc = Document::ParseFile(iniPath, {}, &diags);
  for (const auto& d : diags) warn("line " + std::to_string(d.line) + ": " + d.message);
  if (!doc) return nullptr;

  std::unique_ptr<BasicSkin> skin(new BasicSkin());
  skin->m_title = iniPath.stem().wstring();
  skin->m_desc.title = skin->m_title.c_str();
  skin->m_desc.x = static_cast<int>(doc->GetInt("Skin", "X", 100));
  skin->m_desc.y = static_cast<int>(doc->GetInt("Skin", "Y", 100));
  skin->m_desc.widthDip = static_cast<int>(std::clamp<std::int64_t>(doc->GetInt("Skin", "Width", 300), 8, 4096));
  skin->m_desc.heightDip = static_cast<int>(std::clamp<std::int64_t>(doc->GetInt("Skin", "Height", 120), 8, 4096));
  skin->m_updateMs = static_cast<unsigned>(std::clamp<std::int64_t>(doc->GetInt("Skin", "Update", 1000), 50, 3600000));
  skin->m_background = ParseColor(doc->GetString("Skin", "BackgroundColor"), skin->m_background);
  skin->m_cornerRadius = static_cast<float>(doc->GetDouble("Skin", "CornerRadius", 12.0));

  for (std::size_t i = 0; i < doc->SectionCount(); ++i) {
    const std::string_view name = doc->SectionName(i);
    if (name.empty() || IEq(name, "Skin") || IEq(name, "Variables")) continue;
    const auto kindStr = doc->Find(name, "Meter");
    if (!kindStr) {
      warn("[" + std::string(name) + "] has no Meter= key; ignored");
      continue;
    }

    MeterKind kind;
    if (IEq(*kindStr, "String")) {
      kind = MeterKind::String;
    } else if (IEq(*kindStr, "Bar")) {
      kind = MeterKind::Bar;
    } else {
      warn("[" + std::string(name) + "] meter type '" + std::string(*kindStr) + "' is not implemented yet");
      continue;
    }

    auto meter = pool.Make();
    meter->kind = kind;
    const float x = static_cast<float>(doc->GetDouble(name, "X", 0));
    const float y = static_cast<float>(doc->GetDouble(name, "Y", 0));
    const float w = static_cast<float>(doc->GetDouble(name, "W", kind == MeterKind::Bar ? 100 : 200));
    const float h = static_cast<float>(doc->GetDouble(name, "H", kind == MeterKind::Bar ? 8 : 24));
    meter->rect = D2D1::RectF(x, y, x + w, y + h);
    meter->color = ParseColor(doc->GetString(name, "FontColor", doc->GetString(name, "BarColor")), meter->color);
    meter->backColor = ParseColor(doc->GetString(name, "SolidColor"), meter->backColor);
    meter->action = Widen(doc->GetString(name, "LeftMouseUpAction"));
    meter->value = static_cast<float>(std::clamp(doc->GetDouble(name, "Value", 0.0), 0.0, 1.0));

    if (kind == MeterKind::String) {
      meter->templ = Widen(doc->GetString(name, "Text"));
      meter->text = ResolveTokens(meter->templ);
      meter->dynamic = meter->templ.find(L'%') != std::wstring::npos;
      skin->m_hasDynamic = skin->m_hasDynamic || meter->dynamic;

      const std::wstring face = Widen(doc->GetString(name, "FontFace", "Segoe UI"));
      const float size = static_cast<float>(doc->GetDouble(name, "FontSize", 14));
      const HRESULT hr = gfx.DWrite()->CreateTextFormat(face.c_str(), nullptr, DWRITE_FONT_WEIGHT_NORMAL,
                                                        DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL,
                                                        size, L"en-us", &meter->format);
      if (FAILED(hr)) {
        warn("[" + std::string(name) + "] could not create font");
        continue;
      }
      meter->format->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    }
    skin->m_meters.push_back(std::move(meter));
  }
  return skin;
}

bool BasicSkin::Tick(RECT* dirtyDip) {
  bool changed = false;
  RECT total{0, 0, 0, 0};
  for (auto& m : m_meters) {
    if (!m->dynamic) continue;
    std::wstring next = ResolveTokens(m->templ);
    if (next == m->text) continue;  // nothing visible changed => no redraw at all
    m->text = std::move(next);
    const RECT r = ToDipRect(m->rect);
    UnionRect(&total, &total, &r);
    changed = true;
  }
  if (changed && dirtyDip) *dirtyDip = total;
  return changed;
}

void BasicSkin::Draw(ID2D1DeviceContext* ctx, const D2D1_RECT_F& dirty) {
  if (!m_brush && FAILED(ctx->CreateSolidColorBrush(m_background, &m_brush))) return;

  auto intersects = [&](const D2D1_RECT_F& r) {
    return r.left < dirty.right && r.right > dirty.left && r.top < dirty.bottom && r.bottom > dirty.top;
  };

  const D2D1_RECT_F whole = D2D1::RectF(0, 0, static_cast<float>(m_desc.widthDip), static_cast<float>(m_desc.heightDip));
  if (intersects(whole)) {
    m_brush->SetColor(m_background);
    ctx->FillRoundedRectangle(D2D1::RoundedRect(whole, m_cornerRadius, m_cornerRadius), m_brush.Get());
  }

  for (const auto& m : m_meters) {
    if (!intersects(m->rect)) continue;  // cull: only meters touching the dirty rect are drawn
    if (m->kind == MeterKind::String) {
      m_brush->SetColor(m->color);
      ctx->DrawText(m->text.c_str(), static_cast<UINT32>(m->text.size()), m->format.Get(), m->rect,
                    m_brush.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
    } else {
      m_brush->SetColor(m->backColor);
      ctx->FillRectangle(m->rect, m_brush.Get());
      D2D1_RECT_F fill = m->rect;
      fill.right = fill.left + (fill.right - fill.left) * m->value;
      m_brush->SetColor(m->color);
      ctx->FillRectangle(fill, m_brush.Get());
    }
  }
}

}  // namespace deskskin::skin

namespace deskskin::skin {

// Supported actions: "Open:<file, folder, URL or ms-settings: URI>". Skins are code you chose to
// install, as with any widget engine: only install skins from sources you trust.
void BasicSkin::OnMouse(UINT msg, int xDip, int yDip) {
  if (msg != WM_LBUTTONUP) return;
  const float x = static_cast<float>(xDip), y = static_cast<float>(yDip);
  for (auto it = m_meters.rbegin(); it != m_meters.rend(); ++it) {
    const Meter& m = **it;
    if (m.action.empty() || x < m.rect.left || x >= m.rect.right || y < m.rect.top || y >= m.rect.bottom) continue;
    constexpr wchar_t kOpen[] = L"Open:";
    if (m.action.rfind(kOpen, 0) == 0)
      ShellExecuteW(nullptr, L"open", m.action.c_str() + 5, nullptr, nullptr, SW_SHOWNORMAL);
    return;
  }
}

}  // namespace deskskin::skin
