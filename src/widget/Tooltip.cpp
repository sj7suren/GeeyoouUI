#include "geeyoou/widget/Tooltip.hpp"

#include <algorithm>

#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"

namespace geeyoou {

Rect paintTooltipBubble(Painter& p, const Rect& bounds, Point anchor,
                        const std::string& text) {
  if (text.empty()) return {};

  const Theme& t = Theme::current();
  const Size ts = p.measureText(text, t.fontSmall);
  const float padX = 8.0f, padY = 5.0f;
  const float bw = ts.width + 2.0f * padX;
  const float bh = ts.height + 2.0f * padY;

  // Offset from the anchor so the pointer does not sit on the text; flip to the
  // other side when the default placement would overhang the bounds.
  float bx = anchor.x + 14.0f;
  float by = anchor.y + 20.0f;
  if (bx + bw > bounds.right()) bx = anchor.x - bw - 4.0f;
  if (by + bh > bounds.bottom()) by = anchor.y - bh - 6.0f;
  bx = std::clamp(bx, bounds.x(), std::max(bounds.x(), bounds.right() - bw));
  by = std::clamp(by, bounds.y(), std::max(bounds.y(), bounds.bottom() - bh));

  const Rect bubble{bx, by, bw, bh};
  p.fillRoundRect(bubble, 4.0f, t.panel.lerp(t.text, 0.06f));
  p.strokeRoundRect(bubble, 4.0f, t.panelBorder, 1.0f);
  p.drawText({bx + padX, by + bh * 0.5f}, text, t.fontSmall, t.text,
             HAlign::Left, VAlign::Middle);
  return bubble;
}

}  // namespace geeyoou
