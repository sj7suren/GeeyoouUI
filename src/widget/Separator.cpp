#include "geeyoou/widget/Separator.hpp"

#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"

namespace geeyoou {

void Separator::setOrientation(Orientation o) {
  orientation_ = o;
  update();
  invalidateSizeHint();  // the thickness swapped axes
}

// The only widget in the library whose hint has a real MAXIMUM.  A rule is its
// stroke and nothing else, so a box that grew it to forty pixels would be
// handing thirty-nine pixels to something that cannot draw in them -- and,
// worse, would take those pixels off a neighbour that could.  Across its axis
// it is happy with anything at all.
SizeHint Separator::sizeHint() const {
  // The same expression onPaint() uses, so a sheet that thickens the rule also
  // widens the slot the layout reserves for it.
  const float thickness = style(styleState()).borderWidthOr(1.0f);

  SizeHint h;
  if (orientation_ == Orientation::Horizontal) {
    h.min = Size{0.0f, thickness};
    h.preferred = Size{0.0f, thickness};
    h.max = Size{kUnbounded, thickness};
  } else {
    h.min = Size{thickness, 0.0f};
    h.preferred = Size{thickness, 0.0f};
    h.max = Size{thickness, kUnbounded};
  }
  return h;
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
