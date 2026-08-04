#include "geeyoou/render/Painter.hpp"

#include <blend2d/blend2d.h>

#include <cmath>
#include <string>
#include <unordered_map>
#include <vector>

#include "render/VectorPathImpl.hpp"

// NOTE ON THE BLEND2D API SPELLING
// --------------------------------
// Blend2D publishes no release tags -- there is only a rolling master -- and the
// commit pinned in cmake/Dependencies.cmake sits after the project's global
// camelCase -> snake_case rename (fillRect -> fill_rect, and so on).  Almost
// every tutorial and Stack Overflow answer online still shows the old spelling.
// This file is the ONLY place in GeeyoouUI that touches those names, which is
// exactly why the Painter facade exists.

namespace geeyoou {
namespace {

constexpr double kDeg2Rad = 3.14159265358979323846 / 180.0;

inline BLRgba32 toBL(Color c) { return BLRgba32(c.argb()); }

// ---------------------------------------------------------------------------
// Font registry.
//
// Process-global on purpose: a font face is an mmap'd file plus shaping tables,
// and there is no reason to hold more than one per process.  Fonts are cached
// by rounded pixel size so the per-frame path never constructs a BLFont (which
// would allocate) -- see docs/architecture.md section 1, rule 2.
// ---------------------------------------------------------------------------
class FontRegistry {
 public:
  static FontRegistry& instance() {
    static FontRegistry reg;
    return reg;
  }

  // Returns nullptr if no usable system font was found.
  const BLFont* fontForSize(float pixelSize) {
    if (!faceLoaded_) return nullptr;
    const int key = static_cast<int>(pixelSize * 4.0f + 0.5f);  // 0.25px buckets
    auto it = cache_.find(key);
    if (it != cache_.end()) return &it->second;

    BLFont font;
    if (font.create_from_face(face_, float(key) * 0.25f) != BL_SUCCESS) return nullptr;
    return &cache_.emplace(key, std::move(font)).first->second;
  }

 private:
  FontRegistry() { loadSystemFace(); }

  void loadSystemFace() {
    // Ordered by preference.  Microsoft YaHei is a .ttc collection so it needs
    // the two-step BLFontData path; the plain .ttf entries are fallbacks for
    // stripped-down Windows installs.
    struct Candidate {
      const char* path;
      bool collection;
    };
    static const Candidate kCandidates[] = {
        {"C:\\Windows\\Fonts\\msyh.ttc", true},
        {"C:\\Windows\\Fonts\\simhei.ttf", false},
        {"C:\\Windows\\Fonts\\deng.ttf", false},
        {"C:\\Windows\\Fonts\\segoeui.ttf", false},
        {"C:\\Windows\\Fonts\\arial.ttf", false},
    };

    for (const Candidate& c : kCandidates) {
      if (c.collection) {
        BLFontData data;
        if (data.create_from_file(c.path) != BL_SUCCESS) continue;
        if (face_.create_from_data(data, 0) == BL_SUCCESS) {
          faceLoaded_ = true;
          return;
        }
      } else {
        if (face_.create_from_file(c.path) == BL_SUCCESS) {
          faceLoaded_ = true;
          return;
        }
      }
    }
  }

  BLFontFace face_;
  bool faceLoaded_ = false;
  std::unordered_map<int, BLFont> cache_;
};

}  // namespace

Painter::Painter(BLContext* ctx, float dpr) : ctx_(ctx), dpr_(dpr) {}

// ------------------------------------------------------------------ state ---
void Painter::save() { ctx_->save(); }
void Painter::restore() { ctx_->restore(); }
void Painter::translate(float dx, float dy) { ctx_->translate(dx, dy); }
void Painter::scale(float factor) { ctx_->scale(factor); }

void Painter::clip(const Rect& r) {
  ctx_->clip_to_rect(BLRect(r.x(), r.y(), r.width(), r.height()));
}

// ----------------------------------------------------------------- shapes ---
void Painter::fillRect(const Rect& r, Color c) {
  if (r.isEmpty()) return;
  ctx_->fill_rect(BLRect(r.x(), r.y(), r.width(), r.height()), toBL(c));
}

void Painter::strokeRect(const Rect& r, Color c, float lineWidth) {
  if (r.isEmpty()) return;
  ctx_->set_stroke_width(lineWidth);
  ctx_->stroke_rect(BLRect(r.x(), r.y(), r.width(), r.height()), toBL(c));
}

void Painter::fillRoundRect(const Rect& r, float radius, Color c) {
  if (r.isEmpty()) return;
  ctx_->fill_round_rect(BLRoundRect(r.x(), r.y(), r.width(), r.height(), radius),
                        toBL(c));
}

void Painter::strokeRoundRect(const Rect& r, float radius, Color c,
                              float lineWidth) {
  if (r.isEmpty()) return;
  ctx_->set_stroke_width(lineWidth);
  ctx_->stroke_round_rect(BLRoundRect(r.x(), r.y(), r.width(), r.height(), radius),
                          toBL(c));
}

void Painter::fillCircle(Point center, float radius, Color c) {
  if (radius <= 0.0f) return;
  ctx_->fill_circle(BLCircle(center.x, center.y, radius), toBL(c));
}

void Painter::strokeCircle(Point center, float radius, Color c, float lineWidth) {
  if (radius <= 0.0f) return;
  ctx_->set_stroke_width(lineWidth);
  ctx_->stroke_circle(BLCircle(center.x, center.y, radius), toBL(c));
}

void Painter::fillTriangle(Point a, Point b, Point c, Color color) {
  ctx_->fill_triangle(BLTriangle(a.x, a.y, b.x, b.y, c.x, c.y), toBL(color));
}

void Painter::strokeLine(Point a, Point b, Color c, float lineWidth) {
  ctx_->set_stroke_width(lineWidth);
  ctx_->stroke_line(BLLine(a.x, a.y, b.x, b.y), toBL(c));
}

void Painter::strokeArc(Point center, float radius, float startDeg, float sweepDeg,
                        Color c, float lineWidth, bool roundCap) {
  if (radius <= 0.0f || sweepDeg == 0.0f) return;
  BLPath path;
  path.arc_to(center.x, center.y, radius, radius, startDeg * kDeg2Rad,
              sweepDeg * kDeg2Rad, true);
  ctx_->set_stroke_width(lineWidth);
  ctx_->set_stroke_caps(roundCap ? BL_STROKE_CAP_ROUND : BL_STROKE_CAP_BUTT);
  ctx_->stroke_path(path, toBL(c));
  ctx_->set_stroke_caps(BL_STROKE_CAP_BUTT);
}

void Painter::fillArcRing(Point center, float outerRadius, float innerRadius,
                          float startDeg, float sweepDeg, Color c) {
  if (outerRadius <= 0.0f || sweepDeg == 0.0f) return;
  if (innerRadius < 0.0f) innerRadius = 0.0f;

  const double a0 = startDeg * kDeg2Rad;
  const double sweep = sweepDeg * kDeg2Rad;

  BLPath path;
  // Outer edge, forwards.
  path.arc_to(center.x, center.y, outerRadius, outerRadius, a0, sweep, true);
  // Inner edge, backwards -- traversing it in reverse keeps the ring a single
  // non-self-intersecting contour, so the default non-zero fill rule works.
  path.arc_to(center.x, center.y, innerRadius, innerRadius, a0 + sweep, -sweep,
              false);
  path.close();
  ctx_->fill_path(path, toBL(c));
}

void Painter::strokePolyline(const Point* pts, std::size_t count, Color c,
                             float lineWidth) {
  if (!pts || count < 2) return;
  BLPath path;
  path.move_to(pts[0].x, pts[0].y);
  for (std::size_t i = 1; i < count; ++i) path.line_to(pts[i].x, pts[i].y);
  ctx_->set_stroke_width(lineWidth);
  ctx_->set_stroke_join(BL_STROKE_JOIN_ROUND);
  ctx_->stroke_path(path, toBL(c));
}

// ---------------------------------------------------------------- outlines ---
void Painter::fillPath(const VectorPath& path, Color c) {
  if (path.empty()) return;
  ctx_->fill_path(path.impl().path, toBL(c));
}

void Painter::strokePath(const VectorPath& path, Color c, float lineWidth,
                         bool roundCaps) {
  if (path.empty()) return;
  ctx_->set_stroke_width(lineWidth);
  if (roundCaps) {
    // Icon sets are authored assuming round caps and joins; butt caps make a
    // Lucide glyph look chipped at every stroke end.
    ctx_->set_stroke_caps(BL_STROKE_CAP_ROUND);
    ctx_->set_stroke_join(BL_STROKE_JOIN_ROUND);
  }
  ctx_->stroke_path(path.impl().path, toBL(c));
  if (roundCaps) ctx_->set_stroke_caps(BL_STROKE_CAP_BUTT);
}

// ------------------------------------------------------------------- text ---
Size measureText(std::string_view utf8, float pixelSize) {
  const BLFont* font = FontRegistry::instance().fontForSize(pixelSize);
  if (!font) return {};
  const BLFontMetrics fm = font->metrics();
  const float lineHeight = float(fm.ascent + fm.descent);
  if (utf8.empty()) return Size(0.0f, lineHeight);

  BLGlyphBuffer gb;
  if (gb.set_utf8_text(utf8.data(), utf8.size()) != BL_SUCCESS) return {};

  BLTextMetrics tm;
  if (font->get_text_metrics(gb, tm) != BL_SUCCESS) return {};

  return Size(float(tm.advance.x), lineHeight);
}

float fontLineHeight(float pixelSize) {
  const BLFont* font = FontRegistry::instance().fontForSize(pixelSize);
  if (!font) return pixelSize;
  const BLFontMetrics fm = font->metrics();
  return float(fm.ascent + fm.descent);
}

Size Painter::measureText(std::string_view utf8, float pixelSize) {
  return geeyoou::measureText(utf8, pixelSize);
}

void Painter::drawText(Point pos, std::string_view utf8, float pixelSize, Color c,
                       HAlign h, VAlign v) {
  const BLFont* font = FontRegistry::instance().fontForSize(pixelSize);
  if (!font || utf8.empty()) return;

  const BLFontMetrics fm = font->metrics();
  float x = pos.x;
  float y = pos.y;

  if (h != HAlign::Left) {
    const float w = measureText(utf8, pixelSize).width;
    x -= (h == HAlign::Center) ? w * 0.5f : w;
  }

  // Blend2D positions text by its baseline; convert from the caller's anchor.
  switch (v) {
    case VAlign::Top:      y += fm.ascent; break;
    case VAlign::Middle:   y += (fm.ascent - fm.descent) * 0.5f; break;
    case VAlign::Bottom:   y -= fm.descent; break;
    case VAlign::Baseline: break;
  }

  ctx_->fill_utf8_text(BLPoint(x, y), *font, utf8.data(), utf8.size(), toBL(c));
}

}  // namespace geeyoou
