#include "geeyoou/render/Canvas.hpp"

#include <blend2d/blend2d.h>

#include <cstring>

#include "geeyoou/render/Offscreen.hpp"

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

// ------------------------------------------------------------- offscreen ----
//
// Lives here rather than in a file of its own: see the header's note on why
// Painter.cpp and Canvas.cpp stay the only two TUs that spell BL* names.
//
// On the API spelling, the same warning as Painter.cpp applies -- the pinned
// commit is past the camelCase -> snake_case rename, so it is create_from_data
// and write_to_file, not createFromData / writeToFile.

bool OffscreenImage::resize(int width, int height, float dpr) {
  if (width <= 0 || height <= 0) {
    pixels_.clear();
    width_ = height_ = 0;
    dpr_ = 1.0f;
    return false;
  }
  // assign() rather than resize(): a reused image must come back zeroed, not
  // carrying the tail of the previous render.
  pixels_.assign(std::size_t(width) * std::size_t(height), 0u);
  width_ = width;
  height_ = height;
  dpr_ = dpr > 0.0f ? dpr : 1.0f;
  return true;
}

Surface OffscreenImage::surface() const {
  Surface s;
  if (!valid()) return s;
  s.pixels = const_cast<std::uint32_t*>(pixels_.data());
  s.width = width_;
  s.height = height_;
  s.stride = stride();
  s.dpr = dpr_;
  return s;
}

bool writePng(const OffscreenImage& img, const char* path) {
  if (!img.valid() || !path) return false;

  BLImage bl;
  // READ access + no destroy callback: Blend2D borrows our buffer for the
  // duration of the encode and never takes ownership of it.
  if (bl.create_from_data(img.width(), img.height(), BL_FORMAT_PRGB32,
                          const_cast<std::uint32_t*>(img.pixels()), img.stride(),
                          BL_DATA_ACCESS_READ) != BL_SUCCESS) {
    return false;
  }

  BLImageCodec codec;
  if (codec.find_by_name("PNG") != BL_SUCCESS) return false;
  return bl.write_to_file(path, codec) == BL_SUCCESS;
}

bool readPng(const char* path, OffscreenImage& out) {
  if (!path) return false;

  BLImage bl;
  if (bl.read_from_file(path) != BL_SUCCESS) return false;
  // A PNG without an alpha channel decodes to XRGB32; normalising here is what
  // lets a caller compare against a freshly rendered PRGB32 buffer word for
  // word instead of having to know what the encoder chose to write.
  if (bl.format() != BL_FORMAT_PRGB32 &&
      bl.convert(BL_FORMAT_PRGB32) != BL_SUCCESS) {
    return false;
  }

  BLImageData data;
  if (bl.get_data(&data) != BL_SUCCESS) return false;
  if (!out.resize(data.size.w, data.size.h)) return false;

  // Row by row: the decoded image's stride is Blend2D's business, ours is tight.
  const auto* src = static_cast<const std::uint8_t*>(data.pixel_data);
  const std::size_t rowBytes = std::size_t(data.size.w) * 4u;
  for (int y = 0; y < data.size.h; ++y) {
    std::memcpy(out.pixels() + std::size_t(y) * std::size_t(data.size.w),
                src + std::intptr_t(y) * data.stride, rowBytes);
  }
  return true;
}

}  // namespace geeyoou
