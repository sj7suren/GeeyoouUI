#include "geeyoou/render/Canvas.hpp"

#include <blend2d/blend2d.h>

namespace geeyoou {

struct Canvas::Impl {
  BLImage image;
  BLContext ctx;
  bool open = false;

  // Cached identity of whatever the image is currently bound to, so a repaint
  // of an unchanged window does not rebuild the image object every frame.
  void* boundPixels = nullptr;
  int boundW = 0;
  int boundH = 0;
  std::intptr_t boundStride = 0;
  float dpr = 1.0f;
};

Canvas::Canvas() : d_(std::make_unique<Impl>()) {}

Canvas::~Canvas() {
  if (d_ && d_->open) d_->ctx.end();
}

bool Canvas::begin(const Surface& s, const Rect& dirtyPhysical) {
  if (!s.valid()) return false;
  if (d_->open) end();

  if (s.pixels != d_->boundPixels || s.width != d_->boundW ||
      s.height != d_->boundH || s.stride != d_->boundStride) {
    // createFromData does NOT copy: Blend2D rasterises straight into the
    // platform's buffer, which is the whole point of passing a raw Surface up.
    if (d_->image.create_from_data(s.width, s.height, BL_FORMAT_PRGB32, s.pixels,
                                   s.stride) != BL_SUCCESS) {
      return false;
    }
    d_->boundPixels = s.pixels;
    d_->boundW = s.width;
    d_->boundH = s.height;
    d_->boundStride = s.stride;
  }

  if (d_->ctx.begin(d_->image) != BL_SUCCESS) return false;
  d_->open = true;

  // Clip in PHYSICAL pixels first, THEN apply the dpr transform.  Doing it in
  // this order makes the clip match the platform's damage rectangle exactly,
  // with no rounding slop at the edges.
  if (!dirtyPhysical.isEmpty()) {
    d_->ctx.clip_to_rect(BLRect(dirtyPhysical.x(), dirtyPhysical.y(),
                                dirtyPhysical.width(), dirtyPhysical.height()));
  }
  d_->ctx.scale(s.dpr);  // the one and only place DPI enters the output path
  d_->dpr = s.dpr;
  return true;
}

Painter Canvas::painter() { return Painter(&d_->ctx, d_->dpr); }

void Canvas::end() {
  if (!d_->open) return;
  d_->ctx.end();
  d_->open = false;
}

}  // namespace geeyoou
