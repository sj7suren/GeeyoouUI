#include "geeyoou/widget/GroupBox.hpp"

#include <algorithm>

#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"

namespace geeyoou {
namespace {
// The frame contentRect() cuts out of the widget, named so sizeHint() can add
// back exactly what contentRect() takes away.
constexpr float kInsetX = 12.0f;
constexpr float kInsetBottom = 12.0f;
constexpr float kTopPlain = 12.0f;
constexpr float kTopTitled = 34.0f;
// Left indent of the title text plus a matching right margin.
constexpr float kTitlePad = 13.0f + 13.0f;
}  // namespace

void GroupBox::setTitle(std::string utf8) {
  title_ = std::move(utf8);
  update();
  invalidateSizeHint();
}

Rect GroupBox::contentRect() const {
  const float top = title_.empty() ? kTopPlain : kTopTitled;
  const Rect r = localRect();
  const float w = r.width() - 2.0f * kInsetX;
  const float h = r.height() - top - kInsetBottom;
  if (w <= 0.0f || h <= 0.0f) return {};
  return {kInsetX, top, w, h};
}

// The only one of the six that is a CONTAINER, so it is the only one whose hint
// can be more than its own decoration: if it owns a Layout, what it needs is
// what that layout needs, plus the frame.
//
// This is measure()'s first real consumer.  Note that it asks its OWN layout,
// not its children's -- a nested GroupBox answers for its own subtree when it
// is asked, and the recursion stops wherever the tree stops.
SizeHint GroupBox::sizeHint() const {
  const float top = title_.empty() ? kTopPlain : kTopTitled;
  const float frameW = 2.0f * kInsetX;
  const float frameH = top + kInsetBottom;

  SizeHint inner;
  if (const Layout* l = layout()) {
    inner = l->measure(*this);
  } else {
    // No layout: absolute positioning, and the natural size the base class
    // latched is the best statement anyone has made about how big this is.
    inner = Widget::sizeHint();
    inner.min = Size{0.0f, 0.0f};
    // The frame is added below, so what is folded in here is the CONTENT the
    // natural size was chosen to hold.
    inner.preferred = Size{(std::max)(0.0f, inner.preferred.width - frameW),
                           (std::max)(0.0f, inner.preferred.height - frameH)};
  }

  // A title that does not fit is a title that is drawn over the frame's corner,
  // so it is a floor on the width in its own right.
  const float titleW =
      title_.empty()
          ? 0.0f
          : measureText(title_, style(styleState()).fontSizeOr(Theme::current().fontBody))
                    .width +
                kTitlePad;

  SizeHint h;
  h.min = Size{(std::max)(inner.min.width + frameW, titleW), inner.min.height + frameH};
  h.preferred = Size{(std::max)(inner.preferred.width + frameW, titleW),
                     inner.preferred.height + frameH};
  h.preferred.width = (std::max)(h.preferred.width, h.min.width);
  h.preferred.height = (std::max)(h.preferred.height, h.min.height);
  return h;
}

void GroupBox::onPaint(Painter& p, const Rect&) {
  const Theme& t = Theme::current();
  const Rect r = localRect();
  const bool en = isEffectivelyEnabled();
  const StyleProps& sp = style(styleState());

  const float radius = sp.radiusOr(t.radius);
  const float bw = sp.borderWidthOr(1.0f);
  const Color border = sp.borderColorOr(t.panelBorder);
  const Color fill =
      sp.backgroundOr(en ? t.panel : t.panel.lerp(t.background, 0.45f));

  p.fillRoundRect(r, radius, fill);
  if (bw > 0.0f) p.strokeRoundRect(r.deflated(bw * 0.5f), radius, border, bw);

  if (!title_.empty()) {
    p.drawText({r.x() + 13.0f, r.y() + 10.0f}, title_, sp.fontSizeOr(t.fontBody),
               sp.colorOr(en ? t.text : t.textDisabled), HAlign::Left, VAlign::Top);
    p.strokeLine({r.x() + 12.0f, r.y() + 30.0f}, {r.right() - 12.0f, r.y() + 30.0f},
                 border, 1.0f);
  }
}

}  // namespace geeyoou
