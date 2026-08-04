#pragma once
//
// A Surface that owns its pixels, plus PNG in/out.
//
// The point is that everything above platform/ can then be rendered WITHOUT a
// window: golden-image tests, screenshot export for a commissioning report, and
// print/preview all become the same code path the screen already uses -- bind a
// Surface to a Canvas and call paintTree.  See docs/architecture.md section
// 3.5b: the platform layer supplies pixels and nothing else, so anything else
// that can supply pixels is an equally valid target.
//
// The implementation lives in src/render/Canvas.cpp on purpose.  Painter.cpp and
// Canvas.cpp are the only two translation units in the library that touch BL*
// names, and PNG coding needs BLImageCodec; adding a third would erode the one
// property that made the Blend2D rename in section 3.6 a one-file problem.
//
#include <cstdint>
#include <vector>

#include "geeyoou/core/Surface.hpp"

namespace geeyoou {

// A top-down, 32-bit premultiplied BGRA buffer -- byte-identical to what a
// platform window hands up, so a widget cannot tell the difference.
class OffscreenImage {
 public:
  OffscreenImage() = default;
  // `width`/`height` are PHYSICAL pixels, matching Surface.  A logical 200x100
  // view at dpr 2 is therefore OffscreenImage(400, 200, 2.0f).
  OffscreenImage(int width, int height, float dpr = 1.0f) {
    resize(width, height, dpr);
  }

  // Reallocates and zero-fills.  Zeroing is not politeness: a golden test that
  // compared uninitialised padding would be flaky in a way that looks like a
  // rendering bug.
  bool resize(int width, int height, float dpr = 1.0f);

  bool valid() const { return width_ > 0 && height_ > 0 && !pixels_.empty(); }
  int width() const { return width_; }
  int height() const { return height_; }
  float dpr() const { return dpr_; }
  // Tight: no row padding, so stride is derivable but still stated explicitly
  // because Surface carries it.
  std::intptr_t stride() const { return std::intptr_t(width_) * 4; }

  // The writable view handed to Canvas::begin.  Const because a Surface is a
  // view, exactly like span::data() on a const span -- the constness the caller
  // cares about is the image's identity and geometry, not its pixels.
  Surface surface() const;

  const std::uint32_t* pixels() const { return pixels_.data(); }
  std::uint32_t* pixels() { return pixels_.data(); }

  // 0xAARRGGBB, premultiplied.  Out-of-range coordinates read as 0 rather than
  // trapping: a comparison loop that walks two differently-sized images should
  // report a size mismatch, not crash first.
  std::uint32_t pixel(int x, int y) const {
    if (x < 0 || y < 0 || x >= width_ || y >= height_) return 0;
    return pixels_[std::size_t(y) * std::size_t(width_) + std::size_t(x)];
  }

 private:
  std::vector<std::uint32_t> pixels_;
  int width_ = 0;
  int height_ = 0;
  float dpr_ = 1.0f;
};

// Writes `img` as a PNG.  Returns false on any codec or I/O error; nothing is
// thrown, because the caller (a test, an export button) always has a better
// recovery than a terminate.
bool writePng(const OffscreenImage& img, const char* path);

// Reads a PNG into `out`, converting whatever the file's format is to the
// premultiplied BGRA layout every Surface uses.  `out.dpr()` is left at 1.0:
// a PNG carries no device pixel ratio, and inventing one would be a lie.
bool readPng(const char* path, OffscreenImage& out);

}  // namespace geeyoou
