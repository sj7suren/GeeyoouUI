#pragma once
//
// Drawing facade over Blend2D.
//
// Deliberately a facade and not a full wrapper: it exposes the primitives HMI
// widgets actually need, plus raw() as an escape hatch.  The rule is that
// GeeyoouUI's own widgets only ever use the facade (so the backend stays
// replaceable); user code may reach for raw() and accept the coupling.
// See docs/architecture.md section 3.5.
//
// BLContext is forward-declared so that consumers of GeeyoouUI are not forced
// to have Blend2D's headers on their include path.
//
#include <cstddef>
#include <string_view>

#include "geeyoou/core/Types.hpp"
#include "geeyoou/render/VectorPath.hpp"

class BLContext;

namespace geeyoou {

enum class HAlign { Left, Center, Right };
enum class VAlign { Top, Middle, Bottom, Baseline };

// Text measurement WITHOUT a canvas.
//
// Font metrics are a property of the font, not of whatever surface you happen
// to be drawing on -- and text controls must measure during mouse handling
// (to turn a click x into a caret index) where no Painter exists.  Making this
// a free function is what stops LineEdit from having to cache a stale layout
// from the last paint.
Size measureText(std::string_view utf8, float pixelSize);
// Ascent + descent of the font at `pixelSize`; useful for vertical centring.
float fontLineHeight(float pixelSize);

class Painter {
 public:
  // `dpr` is already baked into the context transform; Painter keeps it only so
  // widgets can snap to physical pixels when they want crisp 1px lines.
  Painter(BLContext* ctx, float dpr);

  // --- state ---------------------------------------------------------------
  void save();
  void restore();
  void translate(float dx, float dy);
  // Uniform scale about the current origin.  Note that this scales STROKE
  // WIDTHS too, so a caller drawing an outline at a known device weight must
  // divide it back out -- see IconCanvas::path().
  void scale(float factor);
  void clip(const Rect& r);

  // --- shapes --------------------------------------------------------------
  void fillRect(const Rect& r, Color c);
  void strokeRect(const Rect& r, Color c, float lineWidth = 1.0f);
  void fillRoundRect(const Rect& r, float radius, Color c);
  void strokeRoundRect(const Rect& r, float radius, Color c, float lineWidth = 1.0f);
  void fillCircle(Point center, float radius, Color c);
  void strokeCircle(Point center, float radius, Color c, float lineWidth = 1.0f);
  void fillTriangle(Point a, Point b, Point c, Color color);
  void strokeLine(Point a, Point b, Color c, float lineWidth = 1.0f);

  // Angles in DEGREES, 0 = 3 o'clock, positive = clockwise (screen convention,
  // y grows downwards).  Chosen over radians because gauge scales are always
  // specified in degrees by instrument vendors.
  void strokeArc(Point center, float radius, float startDeg, float sweepDeg,
                 Color c, float lineWidth = 1.0f, bool roundCap = false);

  // Filled ring segment -- the workhorse of every analogue gauge.
  void fillArcRing(Point center, float outerRadius, float innerRadius,
                   float startDeg, float sweepDeg, Color c);

  // Polyline through `count` points.  This is THE hot path for trend charts, so
  // it takes a raw span and builds a single path rather than N line segments.
  void strokePolyline(const Point* pts, std::size_t count, Color c,
                      float lineWidth = 1.0f);

  // --- outlines ------------------------------------------------------------
  //
  // A VectorPath is built once and drawn many times -- unlike every primitive
  // above, which constructs its geometry per call.  That is what makes an
  // SVG-sourced icon CHEAPER to draw than a hand-coded one.
  void fillPath(const VectorPath& path, Color c);
  void strokePath(const VectorPath& path, Color c, float lineWidth = 1.0f,
                  bool roundCaps = true);

  // --- text ----------------------------------------------------------------
  void drawText(Point pos, std::string_view utf8, float pixelSize, Color c,
                HAlign h = HAlign::Left, VAlign v = VAlign::Top);
  Size measureText(std::string_view utf8, float pixelSize);

  // --- escape hatch --------------------------------------------------------
  BLContext& raw() { return *ctx_; }
  float devicePixelRatio() const { return dpr_; }

 private:
  BLContext* ctx_;
  float dpr_;
};

}  // namespace geeyoou
