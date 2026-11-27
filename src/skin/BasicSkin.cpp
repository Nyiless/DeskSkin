#include "skin/BasicSkin.h"

#include <shellapi.h>
#include <shlobj.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cwchar>

#include "measures/Hub.h"
#include "measures/Parse.h"

namespace deskskin::skin {
namespace {

using ini::Document;
using Microsoft::WRL::ComPtr;

TimeFormat g_timeFormat;
PromptFn g_prompt = nullptr;

std::wstring Widen(std::string_view s) {
  if (s.empty()) return {};
  const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<std::size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

std::string Narrow(const std::wstring& w) {
  if (w.empty()) return {};
  const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
  std::string s(static_cast<std::size_t>(n), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
  return s;
}

bool IEq(std::string_view a, std::string_view b) {
  if (a.size() != b.size()) return false;
  for (std::size_t i = 0; i < a.size(); ++i)
    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
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

D2D1_COLOR_F Hsv(float h, float s, float v, float a) {
  const float c = v * s, hp = std::fmod(h, 360.0f) / 60.0f;
  const float x = c * (1.0f - std::fabs(std::fmod(hp, 2.0f) - 1.0f));
  float r = 0, g = 0, b = 0;
  if (hp < 1) { r = c; g = x; } else if (hp < 2) { r = x; g = c; } else if (hp < 3) { g = c; b = x; }
  else if (hp < 4) { g = x; b = c; } else if (hp < 5) { r = x; b = c; } else { r = c; b = x; }
  const float m = v - c;
  return D2D1::ColorF(r + m, g + m, b + m, a);
}

RECT ToDipRect(const D2D1_RECT_F& r) {
  return RECT{static_cast<LONG>(std::floor(r.left)), static_cast<LONG>(std::floor(r.top)),
              static_cast<LONG>(std::ceil(r.right)), static_cast<LONG>(std::ceil(r.bottom))};
}

// ---------------------------------------------------------------------------------------------
// Text tokens
// ---------------------------------------------------------------------------------------------
bool TimeToken(const std::string& n, const SYSTEMTIME& t, std::wstring& out) {
  auto fmt = [&](const wchar_t* f) {
    wchar_t b[64] = {};
    return GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, 0, &t, f, b, 64, nullptr) ? std::wstring(b) : std::wstring();
  };
  wchar_t b[48];
  const bool d = g_timeFormat.showDate;
  unsigned h12 = t.wHour % 12;
  if (h12 == 0) h12 = 12;
  const wchar_t* ap = t.wHour < 12 ? L"AM" : L"PM";
  if (n == "TIME") {
    if (g_timeFormat.use24h) swprintf(b, 48, L"%02u:%02u:%02u", t.wHour, t.wMinute, t.wSecond);
    else swprintf(b, 48, L"%u:%02u:%02u %ls", h12, t.wMinute, t.wSecond, ap);
  } else if (n == "HM") {
    if (g_timeFormat.use24h) swprintf(b, 48, L"%02u:%02u", t.wHour, t.wMinute);
    else swprintf(b, 48, L"%u:%02u %ls", h12, t.wMinute, ap);
  } else if (n == "HOUR") {
    swprintf(b, 48, g_timeFormat.use24h ? L"%02u" : L"%u", g_timeFormat.use24h ? t.wHour : h12);
  } else if (n == "MIN") {
    swprintf(b, 48, L"%02u", t.wMinute);
  } else if (n == "SEC") {
    swprintf(b, 48, L"%02u", t.wSecond);
  } else if (n == "AMPM") {
    swprintf(b, 48, L"%ls", g_timeFormat.use24h ? L"" : ap);
  } else if (n == "DATE") {
    if (d) swprintf(b, 48, L"%04u-%02u-%02u", t.wYear, t.wMonth, t.wDay); else b[0] = 0;
  } else if (n == "DATELONG") { out = d ? fmt(L"dddd, MMMM d") : L""; return true;
  } else if (n == "WEEKDAY") { out = d ? fmt(L"dddd") : L""; return true;
  } else if (n == "WEEKDAY_SHORT") { out = d ? fmt(L"ddd") : L""; return true;
  } else if (n == "MONTH") { out = d ? fmt(L"MMMM") : L""; return true;
  } else if (n == "MONTH_SHORT") { out = d ? fmt(L"MMM") : L""; return true;
  } else if (n == "DAY") { out = d ? fmt(L"d") : L""; return true;
  } else if (n == "YEAR") { out = d ? fmt(L"yyyy") : L""; return true;
  } else {
    return false;
  }
  out = b;
  return true;
}

bool ValidTokenName(const std::wstring& n) {
  if (n.empty() || n.size() > 40) return false;
  for (wchar_t c : n)
    if (!((c >= L'0' && c <= L'9') || (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z') || c == L'_' || c == L':')) return false;
  return true;
}

std::wstring ResolveText(const std::wstring& templ) {
  if (templ.find(L'%') == std::wstring::npos) return templ;
  SYSTEMTIME now;
  GetLocalTime(&now);
  std::wstring out;
  std::size_t i = 0;
  while (i < templ.size()) {
    if (templ[i] != L'%') { out += templ[i++]; continue; }
    const std::size_t j = templ.find(L'%', i + 1);
    if (j == std::wstring::npos) { out.append(templ, i, std::wstring::npos); break; }
    const std::wstring name = templ.substr(i + 1, j - i - 1);
    std::wstring value;
    if (ValidTokenName(name)) {
      std::string n = Narrow(name);
      for (char& c : n) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
      if (TimeToken(n, now, value) || measures::Hub::Get().Text(n, value)) {
        out += value;
        i = j + 1;
        continue;
      }
    }
    out += L'%';  // not a token: literal '%', rescan from the next character
    ++i;
  }
  return out;
}

// ---------------------------------------------------------------------------------------------
// Drawing helpers
// ---------------------------------------------------------------------------------------------
ComPtr<IDWriteTextFormat> MakeFormat(IDWriteFactory* dw, const std::wstring& face, float size, std::string_view weight,
                                     int align, bool vcenter) {
  DWRITE_FONT_WEIGHT w = DWRITE_FONT_WEIGHT_NORMAL;
  if (IEq(weight, "Bold")) w = DWRITE_FONT_WEIGHT_BOLD;
  else if (IEq(weight, "SemiBold")) w = DWRITE_FONT_WEIGHT_SEMI_BOLD;
  else if (IEq(weight, "Light")) w = DWRITE_FONT_WEIGHT_LIGHT;
  ComPtr<IDWriteTextFormat> f;
  HRESULT hr = dw->CreateTextFormat(face.c_str(), nullptr, w, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &f);
  if (FAILED(hr)) hr = dw->CreateTextFormat(L"Segoe UI", nullptr, w, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, size, L"en-us", &f);
  if (FAILED(hr)) return nullptr;
  f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
  f->SetTextAlignment(align == 1 ? DWRITE_TEXT_ALIGNMENT_CENTER : (align == 2 ? DWRITE_TEXT_ALIGNMENT_TRAILING : DWRITE_TEXT_ALIGNMENT_LEADING));
  if (vcenter) f->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
  return f;
}

int DayOfWeek(int y, int m, int d) {  // 0 = Sunday
  static const int t[] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) y -= 1;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}

int DaysInMonth(int y, int m) {
  static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  const bool leap = (y % 4 == 0 && y % 100 != 0) || y % 400 == 0;
  return m == 2 && leap ? 29 : d[m - 1];
}

void SendKey(WORD vk) {
  INPUT in[2] = {};
  in[0].type = INPUT_KEYBOARD;
  in[0].ki.wVk = vk;
  in[1] = in[0];
  in[1].ki.dwFlags = KEYEVENTF_KEYUP;
  SendInput(2, in, sizeof(INPUT));
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Public helpers
// ---------------------------------------------------------------------------------------------
void SetTimeFormat(const TimeFormat& f) { g_timeFormat = f; }
void SetPromptHandler(PromptFn fn) { g_prompt = fn; }

bool RunAction(const std::wstring& a) {
  auto starts = [&](const wchar_t* p) { return a.rfind(p, 0) == 0; };
  if (starts(L"Open:")) {
    ShellExecuteW(nullptr, L"open", a.c_str() + 5, nullptr, nullptr, SW_SHOWNORMAL);
    return true;
  }
  if (starts(L"Run:")) {
    const std::wstring rest = a.substr(4);
    const std::size_t bar = rest.find(L'|');
    const std::wstring prog = rest.substr(0, bar);
    const std::wstring args = bar == std::wstring::npos ? L"" : rest.substr(bar + 1);
    ShellExecuteW(nullptr, L"open", prog.c_str(), args.empty() ? nullptr : args.c_str(), nullptr, SW_SHOWNORMAL);
    return true;
  }
  if (!starts(L"Cmd:")) return false;
  const std::wstring cmd = a.substr(4);
  if (cmd == L"MediaPlayPause") SendKey(VK_MEDIA_PLAY_PAUSE);
  else if (cmd == L"MediaNext") SendKey(VK_MEDIA_NEXT_TRACK);
  else if (cmd == L"MediaPrev") SendKey(VK_MEDIA_PREV_TRACK);
  else if (cmd == L"VolumeMute") SendKey(VK_VOLUME_MUTE);
  else if (cmd == L"VolumeUp") SendKey(VK_VOLUME_UP);
  else if (cmd == L"VolumeDown") SendKey(VK_VOLUME_DOWN);
  else if (cmd == L"EmptyRecycleBin") SHEmptyRecycleBinW(nullptr, nullptr, 0);  // Windows shows its own confirmation
  else if (cmd == L"EditNotes") {
    const std::wstring path = measures::Hub::Get().Config().notesPath;
    if (!path.empty()) {
      std::wstring ignored;
      measures::Hub::Get().Text("NOTE_1", ignored);  // creates the notes file if it does not exist yet
      const std::wstring arg = L"\"" + path + L"\"";
      ShellExecuteW(nullptr, L"open", L"notepad.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
    }
  } else if (cmd.rfind(L"OpenFeed:", 0) == 0) {
    const std::wstring link = measures::Hub::Get().FeedLink(_wtoi(cmd.c_str() + 9));
    if (!link.empty()) ShellExecuteW(nullptr, L"open", link.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
  } else if (cmd.rfind(L"Search:", 0) == 0) {
    std::wstring query;
    if (g_prompt && g_prompt(L"Search", L"What do you want to search for?", query) && !query.empty()) {
      const std::wstring url = cmd.substr(7) + Widen(measures::parse::UrlEncode(Narrow(query)));
      ShellExecuteW(nullptr, L"open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
  } else {
    return false;
  }
  return true;
}

// ---------------------------------------------------------------------------------------------
// Loading
// ---------------------------------------------------------------------------------------------
std::unique_ptr<BasicSkin> BasicSkin::Load(const std::filesystem::path& iniPath, render::GraphicsDevice& gfx,
                                           MeterPool& pool, std::vector<std::string>* warnings,
                                           const VarOverrides& overrides) {
  std::vector<ini::Diagnostic> diags;
  ini::ParseOptions opt;
  opt.overrides = overrides;
  auto doc = Document::ParseFile(iniPath, opt, &diags);
  for (const auto& d : diags)
    if (warnings) warnings->push_back(iniPath.filename().string() + ": line " + std::to_string(d.line) + ": " + d.message);
  if (!doc) return nullptr;
  return Build(*doc, iniPath.stem().wstring(), gfx, pool, warnings);
}

std::unique_ptr<BasicSkin> BasicSkin::LoadFromText(std::string_view text, const std::wstring& title, render::GraphicsDevice& gfx,
                                                   MeterPool& pool, std::vector<std::string>* warnings,
                                                   const VarOverrides& overrides) {
  std::vector<ini::Diagnostic> diags;
  ini::ParseOptions opt;
  opt.overrides = overrides;
  Document doc = Document::Parse(text, opt, &diags);
  for (const auto& d : diags)
    if (warnings) warnings->push_back("line " + std::to_string(d.line) + ": " + d.message);
  return Build(doc, title, gfx, pool, warnings);
}

std::unique_ptr<BasicSkin> BasicSkin::Build(const Document& doc, const std::wstring& title, render::GraphicsDevice& gfx,
                                            MeterPool& pool, std::vector<std::string>* warnings) {
  auto warn = [&](std::string m) {
    if (warnings) warnings->push_back(std::move(m));
  };
  std::unique_ptr<BasicSkin> skin(new BasicSkin());
  skin->m_title = title;
  skin->m_desc.title = skin->m_title.c_str();
  skin->m_desc.x = static_cast<int>(doc.GetInt("Skin", "X", 100));
  skin->m_desc.y = static_cast<int>(doc.GetInt("Skin", "Y", 100));
  skin->m_desc.widthDip = static_cast<int>(std::clamp<std::int64_t>(doc.GetInt("Skin", "Width", 300), 8, 4096));
  skin->m_desc.heightDip = static_cast<int>(std::clamp<std::int64_t>(doc.GetInt("Skin", "Height", 120), 8, 4096));
  skin->m_updateMs = static_cast<unsigned>(std::clamp<std::int64_t>(doc.GetInt("Skin", "Update", 1000), 30, 3600000));
  skin->m_background = ParseColor(doc.GetString("Skin", "BackgroundColor"), skin->m_background);
  skin->m_cornerRadius = static_cast<float>(doc.GetDouble("Skin", "CornerRadius", 12.0));

  for (std::size_t i = 0; i < doc.SectionCount(); ++i) {
    const std::string_view name = doc.SectionName(i);
    if (name.empty() || IEq(name, "Skin") || IEq(name, "Variables")) continue;
    const auto kindStr = doc.Find(name, "Meter");
    if (!kindStr) {
      warn("[" + std::string(name) + "] has no Meter= key; ignored");
      continue;
    }
    MeterKind kind;
    if (IEq(*kindStr, "String")) kind = MeterKind::String;
    else if (IEq(*kindStr, "Bar")) kind = MeterKind::Bar;
    else if (IEq(*kindStr, "Histogram")) kind = MeterKind::Histogram;
    else if (IEq(*kindStr, "Bars")) kind = MeterKind::Bars;
    else if (IEq(*kindStr, "Hex")) kind = MeterKind::Hex;
    else if (IEq(*kindStr, "Calendar")) kind = MeterKind::Calendar;
    else {
      warn("[" + std::string(name) + "] meter type '" + std::string(*kindStr) + "' is not implemented");
      continue;
    }

    auto meter = pool.Make();
    meter->kind = kind;
    const bool isBarLike = kind == MeterKind::Bar || kind == MeterKind::Histogram;
    const float x = static_cast<float>(doc.GetDouble(name, "X", 0));
    const float y = static_cast<float>(doc.GetDouble(name, "Y", 0));
    const float w = static_cast<float>(doc.GetDouble(name, "W", isBarLike ? 100 : 200));
    const float h = static_cast<float>(doc.GetDouble(name, "H", kind == MeterKind::Bar ? 8 : (isBarLike ? 40 : 24)));
    meter->rect = D2D1::RectF(x, y, x + w, y + h);
    meter->color = ParseColor(doc.GetString(name, "FontColor", doc.GetString(name, "BarColor")), meter->color);
    meter->backColor = ParseColor(doc.GetString(name, "SolidColor"), meter->backColor);
    meter->border = ParseColor(doc.GetString(name, "BorderColor"), meter->border);
    meter->action = Widen(doc.GetString(name, "LeftMouseUpAction"));
    meter->minV = static_cast<float>(doc.GetDouble(name, "Min", 0.0));
    meter->maxV = static_cast<float>(doc.GetDouble(name, "Max", 100.0));
    meter->value = static_cast<float>(std::clamp(doc.GetDouble(name, "Value", 0.0), 0.0, 1.0));
    meter->source = std::string(doc.GetString(name, "Source"));
    meter->bands = static_cast<int>(std::clamp<std::int64_t>(doc.GetInt(name, "Bands", 32), 4, 128));
    meter->rainbow = doc.GetBool(name, "Rainbow", false);
    meter->weekStart = static_cast<int>(doc.GetInt(name, "WeekStart", 0)) == 1 ? 1 : 0;
    const std::string_view style = doc.GetString(name, "Style", "Bars");
    meter->style = IEq(style, "Mirror") ? 1 : (IEq(style, "Bubbles") ? 2 : 0);

    const std::string_view alignStr = doc.GetString(name, "Align", "Left");
    const int align = IEq(alignStr, "Center") ? 1 : (IEq(alignStr, "Right") ? 2 : 0);
    if (kind == MeterKind::String || kind == MeterKind::Hex || kind == MeterKind::Calendar) {
      meter->templ = Widen(doc.GetString(name, "Text"));
      meter->text = ResolveText(meter->templ);
      const std::wstring face = Widen(doc.GetString(name, "FontFace", "Segoe UI"));
      const float size = static_cast<float>(doc.GetDouble(name, "FontSize", 14));
      const bool centered = kind != MeterKind::String;
      meter->format = MakeFormat(gfx.DWrite(), face, size, doc.GetString(name, "FontWeight"), centered ? 1 : align, centered);
      if (!meter->format) {
        warn("[" + std::string(name) + "] could not create font");
        continue;
      }
    }

    switch (kind) {
      case MeterKind::String:
      case MeterKind::Hex: meter->dynamic = meter->templ.find(L'%') != std::wstring::npos; break;
      case MeterKind::Bar: meter->dynamic = !meter->source.empty(); break;
      case MeterKind::Histogram:
        meter->dynamic = true;
        if (meter->source.empty()) { warn("[" + std::string(name) + "] Histogram needs Source="); continue; }
        break;
      case MeterKind::Bars:
      case MeterKind::Calendar: meter->dynamic = true; break;
    }
    if (kind == MeterKind::Bar && !meter->source.empty()) {  // show real data from the first frame
      double raw = 0;
      if (measures::Hub::Get().Value(meter->source, raw)) {
        const float span = meter->maxV - meter->minV;
        meter->value = span > 0 ? std::clamp((static_cast<float>(raw) - meter->minV) / span, 0.0f, 1.0f) : 0.0f;
      }
    }
    skin->m_hasDynamic = skin->m_hasDynamic || meter->dynamic;
    skin->m_meters.push_back(std::move(meter));
  }
  return skin;
}

// ---------------------------------------------------------------------------------------------
// Updating
// ---------------------------------------------------------------------------------------------
bool BasicSkin::Tick(RECT* dirtyDip) {
  auto& hub = measures::Hub::Get();
  SYSTEMTIME now;
  GetLocalTime(&now);
  bool changed = false;
  RECT total{0, 0, 0, 0};
  auto mark = [&](const Meter& m) {
    const RECT r = ToDipRect(m.rect);
    UnionRect(&total, &total, &r);
    changed = true;
  };
  std::vector<float> scratch;
  for (auto& mp : m_meters) {
    Meter& m = *mp;
    if (!m.dynamic) continue;
    switch (m.kind) {
      case MeterKind::String:
      case MeterKind::Hex: {
        std::wstring next = ResolveText(m.templ);
        if (next != m.text) { m.text = std::move(next); mark(m); }
        break;
      }
      case MeterKind::Bar: {
        double raw = 0;
        if (!hub.Value(m.source, raw)) break;
        const float span = m.maxV - m.minV;
        const float nv = span > 0 ? std::clamp((static_cast<float>(raw) - m.minV) / span, 0.0f, 1.0f) : 0.0f;
        if (std::fabs(nv - m.value) > 0.002f) { m.value = nv; mark(m); }
        break;
      }
      case MeterKind::Histogram: {
        double ignored = 0;
        hub.Value(m.source, ignored);
        const unsigned v = hub.Version(m.source);
        if (v != m.version) { m.version = v; mark(m); }
        break;
      }
      case MeterKind::Bars: {
        hub.Bands(m.bands, scratch);  // also (re)starts audio capture if it idled out
        const unsigned v = hub.BandsVersion();
        if (v != m.version) { m.version = v; mark(m); }
        break;
      }
      case MeterKind::Calendar:
        if (m.lastDay != now.wDay) { m.lastDay = now.wDay; mark(m); }
        break;
    }
  }
  if (changed && dirtyDip) *dirtyDip = total;
  return changed;
}

// ---------------------------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------------------------
void BasicSkin::Draw(ID2D1RenderTarget* rt, const D2D1_RECT_F& dirty) {
  if (!m_brush && FAILED(rt->CreateSolidColorBrush(m_background, &m_brush))) return;
  auto intersects = [&](const D2D1_RECT_F& r) {
    return r.left < dirty.right && r.right > dirty.left && r.top < dirty.bottom && r.bottom > dirty.top;
  };
  const D2D1_RECT_F whole = D2D1::RectF(0, 0, static_cast<float>(m_desc.widthDip), static_cast<float>(m_desc.heightDip));
  if (intersects(whole)) {
    m_brush->SetColor(m_background);
    rt->FillRoundedRectangle(D2D1::RoundedRect(whole, m_cornerRadius, m_cornerRadius), m_brush.Get());
  }
  for (const auto& m : m_meters)
    if (intersects(m->rect)) DrawMeter(rt, *m);  // cull: only meters touching the dirty rect are drawn
}

void BasicSkin::DrawMeter(ID2D1RenderTarget* rt, const Meter& m) {
  ID2D1SolidColorBrush* br = m_brush.Get();
  const float w = m.rect.right - m.rect.left, h = m.rect.bottom - m.rect.top;
  switch (m.kind) {
    case MeterKind::String:
      br->SetColor(m.color);
      rt->DrawText(m.text.c_str(), static_cast<UINT32>(m.text.size()), m.format.Get(), m.rect, br, D2D1_DRAW_TEXT_OPTIONS_NONE,
                   DWRITE_MEASURING_MODE_NATURAL);
      break;

    case MeterKind::Bar: {
      br->SetColor(m.backColor);
      rt->FillRectangle(m.rect, br);
      D2D1_RECT_F fill = m.rect;
      fill.right = fill.left + w * m.value;
      br->SetColor(m.color);
      rt->FillRectangle(fill, br);
      break;
    }

    case MeterKind::Histogram: {
      std::vector<float> hist;
      if (!measures::Hub::Get().History(m.source, hist) || hist.size() < 2) break;
      float lo = m.minV, hi = m.maxV;
      if (hi <= lo) { hi = std::max(1.0f, *std::max_element(hist.begin(), hist.end())); lo = 0; }  // Max=0: auto-scale
      ComPtr<ID2D1Factory> f;
      rt->GetFactory(&f);
      ComPtr<ID2D1PathGeometry> area, line;
      ComPtr<ID2D1GeometrySink> sa, sl;
      if (!f || FAILED(f->CreatePathGeometry(&area)) || FAILED(f->CreatePathGeometry(&line)) || FAILED(area->Open(&sa)) ||
          FAILED(line->Open(&sl)))
        break;
      const std::size_t n = hist.size();
      auto pt = [&](std::size_t i) {
        const float xx = m.rect.left + (static_cast<float>(i) + static_cast<float>(60 - std::min<std::size_t>(60, n))) * w / 59.0f;
        const float v = std::clamp((hist[i] - lo) / (hi - lo), 0.0f, 1.0f);
        return D2D1::Point2F(xx, m.rect.bottom - v * h);
      };
      sa->BeginFigure(D2D1::Point2F(pt(0).x, m.rect.bottom), D2D1_FIGURE_BEGIN_FILLED);
      sl->BeginFigure(pt(0), D2D1_FIGURE_BEGIN_HOLLOW);
      for (std::size_t i = 0; i < n; ++i) { sa->AddLine(pt(i)); sl->AddLine(pt(i)); }
      sa->AddLine(D2D1::Point2F(pt(n - 1).x, m.rect.bottom));
      sa->EndFigure(D2D1_FIGURE_END_CLOSED);
      sl->EndFigure(D2D1_FIGURE_END_OPEN);
      sa->Close();
      sl->Close();
      rt->PushAxisAlignedClip(m.rect, D2D1_ANTIALIAS_MODE_ALIASED);
      br->SetColor(D2D1::ColorF(m.color.r, m.color.g, m.color.b, 0.22f));
      rt->FillGeometry(area.Get(), br);
      br->SetColor(m.color);
      rt->DrawGeometry(line.Get(), br, 1.6f);
      rt->PopAxisAlignedClip();
      break;
    }

    case MeterKind::Bars: {
      std::vector<float> v;
      measures::Hub::Get().Bands(m.bands, v);
      const float slot = w / static_cast<float>(m.bands), bw = std::max(1.0f, slot * 0.72f);
      for (int i = 0; i < m.bands; ++i) {
        const float val = v[static_cast<std::size_t>(i)];
        br->SetColor(m.rainbow ? Hsv(300.0f * static_cast<float>(i) / static_cast<float>(m.bands) + 10.0f, 0.75f, 1.0f, m.color.a) : m.color);
        const float x = m.rect.left + slot * static_cast<float>(i) + (slot - bw) * 0.5f;
        if (m.style == 2) {  // bubbles
          const float maxR = std::min(slot * 0.5f, h * 0.5f);
          const float r = std::max(1.5f, val * maxR);
          rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x + bw * 0.5f, m.rect.top + h * 0.5f), r, r), br);
        } else if (m.style == 1) {  // mirrored around the centre line
          const float bh = std::max(1.0f, val * h * 0.5f), cy = m.rect.top + h * 0.5f;
          rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, cy - bh, x + bw, cy + bh), std::min(bw * 0.5f, 3.0f), std::min(bw * 0.5f, 3.0f)), br);
        } else {  // classic bars
          const float bh = std::max(2.0f, val * h);
          rt->FillRoundedRectangle(D2D1::RoundedRect(D2D1::RectF(x, m.rect.bottom - bh, x + bw, m.rect.bottom), std::min(bw * 0.5f, 3.0f), std::min(bw * 0.5f, 3.0f)), br);
        }
      }
      break;
    }

    case MeterKind::Hex: {
      ComPtr<ID2D1Factory> f;
      rt->GetFactory(&f);
      ComPtr<ID2D1PathGeometry> pg;
      ComPtr<ID2D1GeometrySink> sink;
      if (!f || FAILED(f->CreatePathGeometry(&pg)) || FAILED(pg->Open(&sink))) break;
      const float cx = (m.rect.left + m.rect.right) * 0.5f, cy = (m.rect.top + m.rect.bottom) * 0.5f;
      const float r = std::min(w * 0.5f, h * 0.5f / 0.866f);
      for (int i = 0; i < 6; ++i) {  // flat-top hexagon
        const float a = 60.0f * static_cast<float>(i) * 3.14159265f / 180.0f;
        const D2D1_POINT_2F p = D2D1::Point2F(cx + std::cos(a) * r, cy + std::sin(a) * r);
        if (i == 0) sink->BeginFigure(p, D2D1_FIGURE_BEGIN_FILLED); else sink->AddLine(p);
      }
      sink->EndFigure(D2D1_FIGURE_END_CLOSED);
      sink->Close();
      br->SetColor(m.backColor);
      rt->FillGeometry(pg.Get(), br);
      br->SetColor(m.border);
      rt->DrawGeometry(pg.Get(), br, 1.5f);
      br->SetColor(m.color);
      rt->DrawText(m.text.c_str(), static_cast<UINT32>(m.text.size()), m.format.Get(), m.rect, br, D2D1_DRAW_TEXT_OPTIONS_NONE,
                   DWRITE_MEASURING_MODE_NATURAL);
      break;
    }

    case MeterKind::Calendar: {
      SYSTEMTIME t;
      GetLocalTime(&t);
      const int days = DaysInMonth(t.wYear, t.wMonth);
      const int first = (DayOfWeek(t.wYear, t.wMonth, 1) - m.weekStart + 7) % 7;
      const float cw = w / 7.0f, ch = h / 7.0f;
      static const wchar_t* head[2][7] = {{L"S", L"M", L"T", L"W", L"T", L"F", L"S"}, {L"M", L"T", L"W", L"T", L"F", L"S", L"S"}};
      for (int c = 0; c < 7; ++c) {
        br->SetColor(D2D1::ColorF(m.color.r, m.color.g, m.color.b, m.color.a * 0.55f));
        rt->DrawText(head[m.weekStart][c], 1, m.format.Get(), D2D1::RectF(m.rect.left + cw * static_cast<float>(c), m.rect.top, m.rect.left + cw * static_cast<float>(c + 1), m.rect.top + ch),
                     br, D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
      }
      for (int d = 1; d <= days; ++d) {
        const int cell = first + d - 1, col = cell % 7, row = cell / 7 + 1;
        const D2D1_RECT_F cr = D2D1::RectF(m.rect.left + cw * static_cast<float>(col), m.rect.top + ch * static_cast<float>(row),
                                           m.rect.left + cw * static_cast<float>(col + 1), m.rect.top + ch * static_cast<float>(row + 1));
        if (d == t.wDay) {
          br->SetColor(m.backColor);
          const float r = std::min(cw, ch) * 0.5f - 1.0f;
          rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F((cr.left + cr.right) * 0.5f, (cr.top + cr.bottom) * 0.5f), r, r), br);
        }
        wchar_t num[4];
        const int len = swprintf(num, 4, L"%d", d);
        br->SetColor(m.color);
        rt->DrawText(num, static_cast<UINT32>(len), m.format.Get(), cr, br, D2D1_DRAW_TEXT_OPTIONS_NONE, DWRITE_MEASURING_MODE_NATURAL);
      }
      break;
    }
  }
}

void BasicSkin::OnMouse(UINT msg, int xDip, int yDip) {
  if (msg != WM_LBUTTONUP) return;
  const float x = static_cast<float>(xDip), y = static_cast<float>(yDip);
  for (auto it = m_meters.rbegin(); it != m_meters.rend(); ++it) {
    const Meter& m = **it;
    if (m.action.empty() || x < m.rect.left || x >= m.rect.right || y < m.rect.top || y >= m.rect.bottom) continue;
    RunAction(m.action);
    return;
  }
}

}  // namespace deskskin::skin
