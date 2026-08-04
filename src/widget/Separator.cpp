#include "geeyoou/widget/Separator.hpp"

#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"

namespace geeyoou {

void Separator::setOrientation(Orientation o) {
  orientation_ = o;
  update();
}

void Separator::setColor(Color c) {
  color_ = c;
  colorSet_ = true;
  update();
}

void Separator::onPaint(Painter& p, const Rect&) {
  const Theme& t = Theme::current();
  const Rect r = localRect();
  const StyleProps& sp = style(styleState());
  // An explicit setColor() still wins over the sheet: code that named a colour
  // for this one instance is more specific than any selector could be.
  const Color c = colorSet_ ? color_ : sp.colorOr(t.panelBorder);
  const float w = sp.borderWidthOr(1.0f);

  if (orientation_ == Orientation::Horizontal) {
    const float y = r.center().y;
    p.strokeLine({r.x(), y}, {r.right(), y}, c, w);
  } else {
    const float x = r.center().x;
    p.strokeLine({x, r.y()}, {x, r.bottom()}, c, w);
  }
}

}  // namespace geeyoou
