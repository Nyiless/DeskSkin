// The Widget Library page: search, category filters, favourites, installed markers, a live preview,
// install / enable / reset buttons and per-widget settings.
#include <algorithm>
#include <cmath>
#include <cwctype>

#include "ui/Dashboard.h"
#include "ui/DashboardIds.h"

namespace deskskin::ui {

using Microsoft::WRL::ComPtr;

namespace {

constexpr float kSidebarW = 240.0f;
D2D1_COLOR_F C(float r, float g, float b, float a = 1.0f) { return D2D1::ColorF(r, g, b, a); }
D2D1_RECT_F R(float x, float y, float w, float h) { return D2D1::RectF(x, y, x + w, y + h); }
const D2D1_COLOR_F kWhite = D2D1::ColorF(1, 1, 1, 1);

struct CatStyle { float r, g, b; int icon; };
const CatStyle kCat[7] = {{0.20f, 0.60f, 1.00f, 6},  {0.52f, 0.46f, 1.00f, 1}, {1.00f, 0.70f, 0.20f, 10}, {0.20f, 0.80f, 0.50f, 11},
                          {0.95f, 0.35f, 0.70f, 12}, {0.30f, 0.80f, 0.90f, 2},  {0.70f, 0.72f, 0.85f, 13}};
const wchar_t* const kCatLabels[10] = {L"All", L"Favorites", L"Installed", L"System Monitoring", L"Productivity",
                                       L"Weather", L"Music", L"Visualizers", L"Appearance", L"Utilities"};

std::wstring Lower(std::wstring s) {
  for (auto& c : s) c = static_cast<wchar_t>(std::towlower(c));
  return s;
}

}  // namespace

void Dashboard::AddHitClipped(const D2D1_RECT_F& r, const D2D1_RECT_F& clip, int id) {
  const D2D1_RECT_F i = D2D1::RectF(std::max(r.left, clip.left), std::max(r.top, clip.top), std::min(r.right, clip.right),
                                    std::min(r.bottom, clip.bottom));
  if (i.right > i.left && i.bottom > i.top) AddHit(i, id);
}

float Dashboard::Pill(float x, float y, const std::wstring& text, int id, int style) {
  const float h = style == 3 ? 34.0f : 40.0f, size = style == 3 ? 13.0f : 14.0f;
  const float w = TextW(text, size, style == 1) + (style == 3 ? 28.0f : 36.0f);
  const D2D1_RECT_F r = R(x, y, w, h);
  const bool hot = (m_hover == id);
  if (style == 1) {
    Fill(r, 12, Accent(hot ? 1.0f : 0.88f));
  } else if (style == 2) {
    Fill(r, 12, C(0.85f, 0.25f, 0.30f, hot ? 0.95f : 0.75f));
  } else {
    Fill(r, 12, C(1, 1, 1, hot ? 0.16f : 0.08f));
    Stroke(r, 12, hot ? Accent(0.8f) : C(1, 1, 1, 0.12f), 1.0f);
  }
  Txt(text, r, size, style == 1 || style == 2, kWhite, true);
  AddHit(r, id);
  return w;
}

void Dashboard::Star(float cx, float cy, float r, bool filled, D2D1_COLOR_F c) {
  ComPtr<ID2D1PathGeometry> pg;
  ComPtr<ID2D1GeometrySink> sink;
  if (FAILED(m_gfx.D2DFactory()->CreatePathGeometry(&pg)) || FAILED(pg->Open(&sink))) return;
  for (int i = 0; i < 10; ++i) {
    const float a = (36.0f * static_cast<float>(i) - 90.0f) * 3.14159265f / 180.0f, rad = (i % 2 == 0) ? r : r * 0.45f;
    const D2D1_POINT_2F p = D2D1::Point2F(cx + std::cos(a) * rad, cy + std::sin(a) * rad);
    if (i == 0) sink->BeginFigure(p, D2D1_FIGURE_BEGIN_FILLED); else sink->AddLine(p);
  }
  sink->EndFigure(D2D1_FIGURE_END_CLOSED);
  sink->Close();
  SetColor(c);
  if (filled) m_rt->FillGeometry(pg.Get(), m_brush.Get());
  else m_rt->DrawGeometry(pg.Get(), m_brush.Get(), 1.6f);
}

float Dashboard::WrapHeight(const std::wstring& s, float width, float size, bool semibold) {
  IDWriteTextFormat* f = Fmt(size, semibold, false);
  if (!f || s.empty()) return 0.0f;
  ComPtr<IDWriteTextLayout> layout;
  if (FAILED(m_gfx.DWrite()->CreateTextLayout(s.c_str(), static_cast<UINT32>(s.size()), f, width, 2000.0f, &layout))) return 0.0f;
  layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
  DWRITE_TEXT_METRICS tm{};
  layout->GetMetrics(&tm);
  return tm.height;
}

float Dashboard::TxtWrap(const std::wstring& s, const D2D1_RECT_F& r, float size, bool semibold, D2D1_COLOR_F c) {
  IDWriteTextFormat* f = Fmt(size, semibold, false);
  if (!f || s.empty()) return 0.0f;
  ComPtr<IDWriteTextLayout> layout;
  if (FAILED(m_gfx.DWrite()->CreateTextLayout(s.c_str(), static_cast<UINT32>(s.size()), f, r.right - r.left, 2000.0f, &layout))) return 0.0f;
  layout->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
  layout->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
  DWRITE_TEXT_METRICS tm{};
  layout->GetMetrics(&tm);
  SetColor(c);
  m_rt->DrawTextLayout(D2D1::Point2F(r.left, r.top), layout.Get(), m_brush.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
  return tm.height;
}

void Dashboard::DrawLibrary(const D2D1_SIZE_F& sz) {
  const std::vector<LibItem>& items = m_host.Library();
  const float x0 = kSidebarW + 36, W = sz.width - x0 - 36;
  PageTitle(L"Widget Library", L"Browse, install and configure widgets. Installing needs no download.", x0);

  // ---- search + actions
  float y = 106;
  const float sw = std::min(360.0f, std::max(180.0f, W - 300.0f));
  const D2D1_RECT_F sr = R(x0, y, sw, 40);
  Fill(sr, 12, C(1, 1, 1, m_searchFocus ? 0.14f : 0.08f));
  Stroke(sr, 12, m_searchFocus ? Accent(0.95f) : C(1, 1, 1, 0.14f), m_searchFocus ? 1.6f : 1.0f);
  Txt(m_libSearch.empty() ? L"Search widgets..." : m_libSearch, R(x0 + 14, y, sw - 28, 40), 15, false,
      m_libSearch.empty() ? C(1, 1, 1, 0.5f) : kWhite);
  if (m_searchFocus) {
    const float cx = x0 + 14 + TextW(m_libSearch, 15, false) + 1;
    SetColor(Accent(1.0f));
    m_rt->DrawLine(D2D1::Point2F(cx, y + 10), D2D1::Point2F(cx, y + 30), m_brush.Get(), 1.5f);
  }
  AddHit(sr, LibSearch);
  float bx = x0 + sw + 12;
  bx += Pill(bx, y, L"Import skin...", LibImport, 0) + 10;
  Pill(bx, y, L"Discover", LibDiscover, 0);

  // ---- category chips (wrap onto more lines when narrow)
  y = 156;
  float cx = x0;
  for (int i = 0; i < 10; ++i) {
    const float w = TextW(kCatLabels[i], 13, false) + 28;
    if (cx + w > x0 + W && cx > x0) { cx = x0; y += 40; }
    const D2D1_RECT_F r = R(cx, y, w, 32);
    const bool sel = (m_libCat == i), hot = (m_hover == LibCat + i);
    Fill(r, 16, sel ? Accent(0.9f) : C(1, 1, 1, hot ? 0.15f : 0.08f));
    if (!sel) Stroke(r, 16, C(1, 1, 1, 0.1f), 1.0f);
    Txt(kCatLabels[i], r, 13, sel, kWhite, true);
    AddHit(r, LibCat + i);
    cx += w + 8;
  }
  const float yb = y + 46;

  // ---- filter
  const std::wstring needle = Lower(m_libSearch);
  std::vector<int> shown;
  for (std::size_t i = 0; i < items.size(); ++i) {
    const LibItem& it = items[i];
    if (m_libCat == 1 && !it.favorite) continue;
    if (m_libCat == 2 && !it.installed) continue;
    if (m_libCat >= 3 && it.categoryIndex != m_libCat - 3) continue;
    if (!needle.empty() && Lower(it.name + L" " + it.category + L" " + it.description + L" " + it.source).find(needle) == std::wstring::npos) continue;
    shown.push_back(static_cast<int>(i));
  }
  bool selectedVisible = false;
  for (int i : shown) selectedVisible = selectedVisible || items[static_cast<std::size_t>(i)].id == m_selected;
  if (!selectedVisible && !shown.empty()) m_selected = items[static_cast<std::size_t>(shown.front())].id;

  const float bodyH = std::max(120.0f, sz.height - yb - 24);
  const float lw = std::min(380.0f, W * 0.44f);

  // ---- list panel
  const D2D1_RECT_F lp = R(x0, yb, lw, bodyH);
  Glass(lp, 18, false);
  const D2D1_RECT_F lclip = D2D1::RectF(lp.left + 4, lp.top + 4, lp.right - 4, lp.bottom - 4);
  m_rt->PushAxisAlignedClip(lclip, D2D1_ANTIALIAS_MODE_ALIASED);
  const float rowH = 70.0f, content = static_cast<float>(shown.size()) * rowH;
  m_libScroll = std::clamp(m_libScroll, 0.0f, std::max(0.0f, content - (bodyH - 16)));
  if (shown.empty()) Txt(L"No widgets match.", R(lp.left + 20, lp.top + 16, lw - 40, 30), 15, false, C(1, 1, 1, 0.7f));
  for (std::size_t k = 0; k < shown.size(); ++k) {
    const int idx = shown[k];
    const LibItem& it = items[static_cast<std::size_t>(idx)];
    const float ry = lp.top + 8 + static_cast<float>(k) * rowH - m_libScroll;
    if (ry + rowH < lp.top || ry > lp.bottom) continue;
    const D2D1_RECT_F row = R(lp.left + 8, ry, lw - 16, rowH - 6);
    const bool sel = (it.id == m_selected), hot = (m_hover == LibRow + idx);
    Fill(row, 14, sel ? Accent(0.28f) : C(1, 1, 1, hot ? 0.09f : 0.03f));
    if (sel) Stroke(row, 14, Accent(0.9f), 1.5f);
    const CatStyle& cs = kCat[std::clamp(it.categoryIndex, 0, 6)];
    const D2D1_RECT_F tile = R(row.left + 10, row.top + 10, 44, 44);
    Fill(tile, 12, C(cs.r, cs.g, cs.b, 0.28f));
    Icon(cs.icon, tile.left + 22, tile.top + 22, 22, C(cs.r + (1 - cs.r) * 0.5f, cs.g + (1 - cs.g) * 0.5f, cs.b + (1 - cs.b) * 0.5f, 1));
    Txt(it.name, R(row.left + 64, row.top + 8, lw - 150, 26), 16, true, kWhite);
    Txt(it.category, R(row.left + 64, row.top + 32, lw - 150, 22), 12, false, C(1, 1, 1, 0.65f));
    if (it.installed) {
      SetColor(C(0.35f, 0.85f, 0.5f, 1));
      m_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(row.right - 62, row.top + 21), 4.5f, 4.5f), m_brush.Get());
      Txt(it.enabled ? L"On" : L"Off", R(row.right - 54, row.top + 10, 30, 22), 11, false, C(1, 1, 1, 0.75f));
    }
    Star(row.right - 24, row.top + 21, 9, it.favorite, it.favorite ? C(1, 0.8f, 0.25f, 1) : C(1, 1, 1, 0.5f));
    AddHitClipped(row, lclip, LibRow + idx);
    AddHitClipped(R(row.right - 40, row.top + 4, 34, 34), lclip, LibStar + idx);
  }
  m_rt->PopAxisAlignedClip();

  // ---- detail panel
  const float dx = x0 + lw + 20, dw = x0 + W - dx;
  m_detailX = dx;
  if (dw < 240) return;
  const D2D1_RECT_F dp = R(dx, yb, dw, bodyH);
  Glass(dp, 18, false);
  const LibItem* sel = nullptr;
  for (const auto& it : items) if (it.id == m_selected) sel = &it;
  if (!sel) {
    Txt(L"Select a widget to see its preview and settings.", R(dx + 20, yb + 20, dw - 40, 30), 15, false, C(1, 1, 1, 0.7f));
    return;
  }
  const D2D1_RECT_F dclip = D2D1::RectF(dp.left + 4, dp.top + 4, dp.right - 4, dp.bottom - 4);
  const std::size_t hitMark = m_hits.size();
  m_rt->PushAxisAlignedClip(dclip, D2D1_ANTIALIAS_MODE_ALIASED);
  m_detailScroll = std::clamp(m_detailScroll, 0.0f, std::max(0.0f, m_detailMax));
  const float top = dp.top + 16 - m_detailScroll;
  float cy = top;

  // preview (live skin, drawn by the real widget engine)
  const D2D1_RECT_F pv = R(dx + 16, cy, dw - 32, 160);
  Fill(pv, 14, C(0.02f, 0.03f, 0.09f, 0.7f));
  Stroke(pv, 14, C(1, 1, 1, 0.1f), 1.0f);
  if (m_previewId != sel->id) {
    m_preview = (sel->feature || !sel->livePreview) ? nullptr : m_host.BuildPreview(sel->id);
    m_previewId = sel->id;
  }
  if (m_preview) {
    const render::SkinWindowDesc d = m_preview->WindowDesc();
    const float sw2 = static_cast<float>(d.widthDip), sh2 = static_cast<float>(d.heightDip);
    const float s = std::min({(pv.right - pv.left - 24) / sw2, (pv.bottom - pv.top - 24) / sh2, 1.5f});
    const float tx = pv.left + ((pv.right - pv.left) - sw2 * s) * 0.5f, ty = pv.top + ((pv.bottom - pv.top) - sh2 * s) * 0.5f;
    m_rt->PushAxisAlignedClip(pv, D2D1_ANTIALIAS_MODE_ALIASED);
    m_rt->SetTransform(D2D1::Matrix3x2F::Scale(s, s) * D2D1::Matrix3x2F::Translation(tx, ty));
    m_preview->Draw(m_rt.Get(), D2D1::RectF(0, 0, sw2, sh2));
    m_rt->SetTransform(D2D1::Matrix3x2F::Identity());
    m_rt->PopAxisAlignedClip();
  } else {
    Txt(sel->feature ? L"System feature: applies to the Windows taskbar. Install to try it."
                     : L"Live preview appears after you install it (it contacts an online service).",
        R(pv.left + 16, pv.top, pv.right - pv.left - 32, pv.bottom - pv.top), 14, false, C(1, 1, 1, 0.65f), true);
  }
  cy += 172;

  // name + favourite
  Txt(sel->name, R(dx + 16, cy, dw - 80, 34), 24, true, kWhite);
  {
    const D2D1_RECT_F fr = R(dx + dw - 16 - 38, cy - 2, 38, 38);
    Fill(fr, 12, C(1, 1, 1, m_hover == LibFavDetail ? 0.16f : 0.07f));
    Star((fr.left + fr.right) / 2, (fr.top + fr.bottom) / 2, 11, sel->favorite, sel->favorite ? C(1, 0.8f, 0.25f, 1) : C(1, 1, 1, 0.7f));
    AddHitClipped(fr, dclip, LibFavDetail);
  }
  cy += 44;

  // action row
  {
    float x = dx + 16;
    const float rowTop = cy;
    const float w1 = Pill(x, cy, sel->installed ? L"Uninstall" : (sel->feature ? L"Enable" : L"Install"), LibInstall, sel->installed ? 2 : 1);
    x += w1 + 14;
    if (sel->installed) {
      Txt(L"Enabled", R(x, rowTop, 70, 40), 14, false, kWhite);
      Toggle(R(x + 72, rowTop + 5, 56, 30), sel->enabled, LibEnable);
      x += 144;
      if (!sel->feature) Pill(x, rowTop, L"Reset position", LibReset, 0);
    }
  }
  cy += 54;

  // meta
  Txt(sel->category, R(dx + 16, cy, dw - 32, 20), 12, true, Accent(1.0f));
  cy += 22;
  cy += TxtWrap(sel->source, R(dx + 16, cy, dw - 32, 100), 12, false, C(1, 1, 1, 0.55f)) + 8;
  cy += TxtWrap(sel->description, R(dx + 16, cy, dw - 32, 200), 14, false, C(1, 1, 1, 0.9f)) + 8;
  if (!sel->needs.empty()) cy += TxtWrap(L"Needs: " + sel->needs, R(dx + 16, cy, dw - 32, 100), 12, false, Accent(0.95f)) + 8;
  cy += 6;

  // settings
  Txt(L"Settings", R(dx + 16, cy, 200, 28), 16, true, kWhite);
  cy += 34;
  if (sel->options.empty()) {
    Txt(L"This widget has no options.", R(dx + 16, cy, dw - 32, 24), 13, false, C(1, 1, 1, 0.6f));
    cy += 30;
  } else {
    float x = dx + 16;
    for (std::size_t k = 0; k < sel->options.size() && k < 10; ++k) {
      const LibOption& o = sel->options[k];
      std::wstring v = o.value.empty() ? L"(auto)" : o.value;
      if (v.size() > 26) v = v.substr(0, 25) + L"...";
      const std::wstring text = o.label + L": " + v;
      const float w = TextW(text, 13, false) + 28;
      if (x + w > dx + dw - 16 && x > dx + 16) { x = dx + 16; cy += 42; }
      Pill(x, cy, text, LibOpt + static_cast<int>(k), 3);
      x += w + 10;
    }
    cy += 46;
    TxtWrap(L"Click a setting to change it. Colors and background apply to this widget; city, units and feed address are shared by every widget that uses them.",
            R(dx + 16, cy, dw - 32, 60), 12, false, C(1, 1, 1, 0.5f));
    cy += 50;
  }
  m_detailMax = std::max(0.0f, (cy - top) + 16.0f - (bodyH - 32.0f));
  // clip every hit rect created inside the scrolling detail panel to the panel, so content scrolled out of view cannot be clicked
  for (std::size_t i = m_hits.size(); i-- > hitMark;) {
    D2D1_RECT_F& r = m_hits[i].r;
    r = D2D1::RectF(std::max(r.left, dclip.left), std::max(r.top, dclip.top), std::min(r.right, dclip.right), std::min(r.bottom, dclip.bottom));
    if (r.right <= r.left || r.bottom <= r.top) m_hits.erase(m_hits.begin() + static_cast<std::ptrdiff_t>(i));
  }
  m_rt->PopAxisAlignedClip();
}

}  // namespace deskskin::ui
