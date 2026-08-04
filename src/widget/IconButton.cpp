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
