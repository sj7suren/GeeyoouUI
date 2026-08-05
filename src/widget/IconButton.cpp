#include "geeyoou/widget/IconButton.hpp"

#include <algorithm>

#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"

namespace geeyoou {

void IconButton::setIconScale(float f) {
  iconScale_ = std::clamp(f, 0.2f, 1.0f);
  update();
}

void IconButton::setCircular(bool on) {
  circular_ = on;
  update();
}

// Square, and sized off the BUTTON's height rather than its width.
//
// PushButton's width is "a label plus padding"; for a control that draws no
// label -- onPaint() above never touches text_ -- that comes out as a wide pill
// with a glyph adrift in the middle of it.  Taking the height for both axes
// keeps an icon button the same height as the push buttons beside it in a
// toolbar, which is the one dimension that has to agree, and gets the square
// for free.
SizeHint IconButton::sizeHint() const {
  const SizeHint base = PushButton::sizeHint();
  SizeHint h;
  h.preferred = Size{base.preferred.height, base.preferred.height};
  h.min = Size{base.min.height, base.min.height};
  return h;
}

void IconButton::onPaint(Painter& p, const Rect&) {
  const Theme& t = Theme::current();
  const Rect r = localRect();
  const Palette pal = palette();
  const float side = std::min(r.width(), r.height());
  const float radius = circular_ ? side * 0.5f : t.radius;

  if (pal.fill.alpha() > 0) p.fillRoundRect(r, radius, pal.fill);
  if (pal.border.alpha() > 0) {
    p.strokeRoundRect(r.deflated(0.5f), radius, pal.border, 1.0f);
  }
  if (hasFocus() && interactive()) {
    p.strokeRoundRect(r.deflated(2.5f), std::max(1.0f, radius - 2.0f), t.focusRing,
                      1.0f);
  }

  const float glyph = side * iconScale_;
  const Rect box(r.center().x - glyph * 0.5f, r.center().y - glyph * 0.5f, glyph,
                 glyph);

  if (isLoading()) {
    // The phase lives in PushButton and advances on the shared animation tick,
    // so this is the same spinner, not a second implementation of one.
    const float rad = glyph * 0.42f;
    p.strokeCircle(box.center(), rad, pal.label.withAlpha(70), 2.0f);
    p.strokeArc(box.center(), rad, spinPhase(), 90.0f, pal.label, 2.0f, true);
  } else {
    drawIcon(p, icon(), box, pal.label);
  }
}

}  // namespace geeyoou
