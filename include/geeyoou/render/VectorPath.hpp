#pragma once
//
// An outline, built once and drawn many times.
//
// This exists for ONE reason: to make SVG path data a first-class icon source.
// Icon sets that matter (Lucide, Feather, Tabler, Material) are all authored on
// a 24x24 grid with 2-unit strokes -- which is precisely GeeyoouUI's own icon
// authoring grid -- so their `d="..."` attributes drop straight in.  What was
// missing was somewhere to put the parsed result.
//
// It is deliberately a dumb container, not a scene graph: no styling, no
// transform stack, no sub-path queries.  Everything a caller needs beyond
// "draw this outline" belongs in the Painter.
//
// Like Canvas, the Blend2D type is behind a pimpl so that consumers of
// GeeyoouUI are never forced to have Blend2D's headers on their include path.
// See docs/architecture.md section 3.5.
//
#include <memory>
#include <string>
#include <string_view>

#include "geeyoou/core/Types.hpp"

namespace geeyoou {

// Whether an outline is meant to be stroked or filled.  SVG icon sets split
// roughly along this line: Lucide/Feather/Tabler are stroke sets, Material and
// Bootstrap are fill sets, and getting it wrong renders a blob or a hairline.
enum class PathStyle : std::uint8_t { Stroke, Fill };

class VectorPath {
 public:
  VectorPath();
  ~VectorPath();
  VectorPath(const VectorPath&);
  VectorPath& operator=(const VectorPath&);
  VectorPath(VectorPath&&) noexcept;
  VectorPath& operator=(VectorPath&&) noexcept;

  bool empty() const;
  void clear();

  void moveTo(Point p);
  void lineTo(Point p);
  void quadTo(Point control, Point to);
  void cubicTo(Point c1, Point c2, Point to);
  void arcTo(Point center, float rx, float ry, float startDeg, float sweepDeg,
             bool forceMoveTo = false);
  void close();

  // Tight bounding box of the outline, ignoring stroke width.  Used to fit a
  // path whose author did not respect the nominal viewBox.
  Rect bounds() const;

  // --- SVG path data -------------------------------------------------------
  //
  // Parses the `d` attribute of an <svg> <path>.  Supported: M m L l H h V v
  // C c S s Q q T t A a Z z -- which is every command the mainstream icon sets
  // actually emit.
  //
  // Never throws.  On a malformed input it keeps whatever parsed cleanly and
  // reports the problem through `error`, for the same reason the style sheet
  // does: icon data is content, and bad content must not take the UI down.
  static VectorPath fromSvg(std::string_view pathData, std::string* error = nullptr);

  // Implementation detail, public only so Painter can reach it.  Opaque here.
  struct Impl;
  Impl& impl() { return *d_; }
  const Impl& impl() const { return *d_; }

 private:
  std::unique_ptr<Impl> d_;
};

}  // namespace geeyoou
