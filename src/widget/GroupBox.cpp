#include "geeyoou/widget/GroupBox.hpp"

#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"

namespace geeyoou {

void GroupBox::setTitle(std::string utf8) {
  title_ = std::move(utf8);
  update();
}

Rect GroupBox::contentRect() const {
  const float top = title_.empty() ? 12.0f : 34.0f;
  const Rect r = localRect();
  const float w = r.width() - 24.0f;
  const float h = r.height() - top - 12.0f;
  if (w <= 0.0f || h <= 0.0f) return {};
  return {12.0f, top, w, h};
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
