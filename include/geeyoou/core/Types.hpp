#pragma once
//
// Geometry and colour primitives.
//
// IMPORTANT: every coordinate in this file -- and in every layer above
// platform/ -- is in LOGICAL pixels.  DPI scaling is applied exactly twice, at
// the platform boundary (input) and inside Painter (output).  Nothing in
// between is allowed to know about the device pixel ratio.
// See docs/architecture.md section 3.1.
//
#include <algorithm>
#include <cstdint>

namespace geeyoou {

struct Point {
  float x = 0.0f;
  float y = 0.0f;

  constexpr Point() = default;
  constexpr Point(float x_, float y_) : x(x_), y(y_) {}

  constexpr Point operator+(const Point& o) const { return {x + o.x, y + o.y}; }
  constexpr Point operator-(const Point& o) const { return {x - o.x, y - o.y}; }
};

struct Size {
  float width = 0.0f;
  float height = 0.0f;

  constexpr Size() = default;
  constexpr Size(float w, float h) : width(w), height(h) {}

  constexpr bool isEmpty() const { return width <= 0.0f || height <= 0.0f; }
};

// Axis-aligned rectangle stored as origin + extent.
class Rect {
 public:
  constexpr Rect() = default;
  constexpr Rect(float x, float y, float w, float h) : x_(x), y_(y), w_(w), h_(h) {}
  constexpr Rect(const Point& origin, const Size& size)
      : x_(origin.x), y_(origin.y), w_(size.width), h_(size.height) {}

  constexpr float x() const { return x_; }
  constexpr float y() const { return y_; }
  constexpr float width() const { return w_; }
  constexpr float height() const { return h_; }
  constexpr float left() const { return x_; }
  constexpr float top() const { return y_; }
  constexpr float right() const { return x_ + w_; }
  constexpr float bottom() const { return y_ + h_; }

  constexpr Point origin() const { return {x_, y_}; }
  constexpr Size size() const { return {w_, h_}; }
  constexpr Point center() const { return {x_ + w_ * 0.5f, y_ + h_ * 0.5f}; }

  constexpr bool isEmpty() const { return w_ <= 0.0f || h_ <= 0.0f; }

  constexpr bool contains(const Point& p) const {
    return p.x >= x_ && p.x < right() && p.y >= y_ && p.y < bottom();
  }

  constexpr bool intersects(const Rect& o) const {
    return !isEmpty() && !o.isEmpty() && x_ < o.right() && o.x_ < right() &&
           y_ < o.bottom() && o.y_ < bottom();
  }

  constexpr Rect translated(float dx, float dy) const {
    return {x_ + dx, y_ + dy, w_, h_};
  }

  // Shrink (positive amount) or grow (negative) on all four sides.
  constexpr Rect deflated(float amount) const {
    return {x_ + amount, y_ + amount, w_ - amount * 2.0f, h_ - amount * 2.0f};
  }

  Rect intersected(const Rect& o) const {
    const float l = std::max(x_, o.x_);
    const float t = std::max(y_, o.y_);
    const float r = std::min(right(), o.right());
    const float b = std::min(bottom(), o.bottom());
    if (r <= l || b <= t) return {};
    return {l, t, r - l, b - t};
  }

  // Bounding box of both rectangles.  An empty rect is the identity element,
  // which is what makes dirty-region accumulation start from Rect{}.
  Rect united(const Rect& o) const {
    if (isEmpty()) return o;
    if (o.isEmpty()) return *this;
    const float l = std::min(x_, o.x_);
    const float t = std::min(y_, o.y_);
    const float r = std::max(right(), o.right());
    const float b = std::max(bottom(), o.bottom());
    return {l, t, r - l, b - t};
  }

 private:
  float x_ = 0.0f;
  float y_ = 0.0f;
  float w_ = 0.0f;
  float h_ = 0.0f;
};

// Packed 0xAARRGGBB, matching Blend2D's BLRgba32 layout so the conversion in
// Painter is a plain reinterpretation rather than a shuffle.
class Color {
 public:
  constexpr Color() = default;
  constexpr explicit Color(std::uint32_t argb) : argb_(argb) {}

  static constexpr Color rgb(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    return rgba(r, g, b, 255);
  }
  static constexpr Color rgba(std::uint8_t r, std::uint8_t g, std::uint8_t b,
                              std::uint8_t a) {
    return Color((std::uint32_t(a) << 24) | (std::uint32_t(r) << 16) |
                 (std::uint32_t(g) << 8) | std::uint32_t(b));
  }

  constexpr std::uint32_t argb() const { return argb_; }
  constexpr std::uint8_t alpha() const { return std::uint8_t(argb_ >> 24); }
  constexpr std::uint8_t red() const { return std::uint8_t(argb_ >> 16); }
  constexpr std::uint8_t green() const { return std::uint8_t(argb_ >> 8); }
  constexpr std::uint8_t blue() const { return std::uint8_t(argb_); }

  constexpr Color withAlpha(std::uint8_t a) const {
    return Color((argb_ & 0x00FFFFFFu) | (std::uint32_t(a) << 24));
  }

  // Linear blend towards `other`; t in [0,1].
  Color lerp(const Color& other, float t) const;

 private:
  std::uint32_t argb_ = 0xFF000000u;
};

}  // namespace geeyoou
