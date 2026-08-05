#include "geeyoou/widget/CheckBox.hpp"

#include <algorithm>

#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"

namespace geeyoou {
namespace {
// The box onPaint() draws at full height, and the gap it leaves before the
// label.  Kept next to each other so the two stay in step.
constexpr float kBoxSide = 16.0f;
constexpr float kLabelGap = 9.0f;
constexpr float kRowHeight = 22.0f;
}  // namespace

void CheckBox::setText(std::string utf8) {
  text_ = std::move(utf8);
  update();
  invalidateSizeHint();
}

// Box + gap + label, and the width does NOT shrink.
//
// onPaint() draws the box at min(16, height) and the label immediately after
// it, with no ellipsis and no second line: there is nothing for a narrower
// CheckBox to do except cut its label in half, and a half-read "启用联锁" is
// worse than an overflow the diagnostics can report.  So min.width ==
// preferred.width, and a row that cannot fit says so through lastLayoutOverflow.
//
// The height does have slack: the box is drawn at min(16, h), so 16 is a real
// floor rather than an arbitrary one, and the extra six pixels of preferred
// height are the touch target.
SizeHint CheckBox::sizeHint() const {
  const float fontSize = style(styleState()).fontSizeOr(Theme::current().fontBody);
  const float labelW =
      text_.empty() ? 0.0f : kLabelGap + measureText(text_, fontSize).width;

  SizeHint h;
  h.preferred = Size{kBoxSide + labelW, kRowHeight};
  h.min = Size{kBoxSide + labelW, kBoxSide};
  return h;
}

void CheckBox::setChecked(bool on) {
  if (checked_ == on) return;
  checked_ = on;
  update();
  toggled.emit(checked_);
}

void CheckBox::onPaint(Painter& p, const Rect&) {
  const Theme& t = Theme::current();
  const Rect r = localRect();
  const bool on = isEffectivelyEnabled();

  const StyleProps& sp = style(styleState());
  const Color accent = sp.accentOr(t.accent);
  const float side = std::min(kBoxSide, r.height());
  const Rect box(0.0f, r.center().y - side * 0.5f, side, side);

  Color fill = checked_ ? accent : sp.backgroundOr(t.field);
  Color border = checked_ ? accent : sp.borderColorOr(t.panelBorder);
  if (!on) {
    fill = checked_ ? accent.lerp(t.background, 0.6f) : sp.backgroundOr(t.field);
    border = t.textDisabled;
  } else if (hovered_ && !checked_) {
    border = accent;
  }
  if (pressed_) fill = fill.lerp(t.background, 0.25f);

  p.fillRoundRect(box, 3.0f, fill);
  p.strokeRoundRect(box.deflated(0.5f), 3.0f, border, 1.0f);

  if (checked_) {
    // Two strokes rather than a glyph: a font-based check mark would depend on
    // the system font actually having U+2713, which is not guaranteed.
    const Color tick = on ? t.background : t.textDisabled;
    const Point a{box.x() + side * 0.24f, box.y() + side * 0.52f};
    const Point b{box.x() + side * 0.43f, box.y() + side * 0.71f};
    const Point c{box.x() + side * 0.77f, box.y() + side * 0.30f};
    p.strokeLine(a, b, tick, 2.0f);
    p.strokeLine(b, c, tick, 2.0f);
  }

  // The ring encloses the whole row (box + label) and stays INSIDE localRect:
  // paintTree clips every widget to its own bounds, so a ring drawn around the
  // box alone would have its left edge sliced off at x = 0.
  if (hasFocus() && on) {
    p.strokeRoundRect(r.deflated(0.5f), 3.0f, t.focusRing, 1.0f);
  }

  if (!text_.empty()) {
    p.drawText({side + kLabelGap, r.center().y}, text_, t.fontBody,
               on ? t.text : t.textDisabled, HAlign::Left, VAlign::Middle);
  }
}

void CheckBox::onMouse(const MouseEvent& e) {
  switch (e.action) {
    case MouseAction::Enter: hovered_ = true; update(); e.accept(); break;
    case MouseAction::Leave: hovered_ = false; pressed_ = false; update(); e.accept(); break;
    case MouseAction::Press:
      if (e.button == MouseButton::Left) { pressed_ = true; update(); e.accept(); }
      break;
    case MouseAction::Release:
      if (e.button == MouseButton::Left) {
        const bool was = pressed_;
        pressed_ = false;
        update();
        if (was && localRect().contains(e.pos)) toggle();
        e.accept();
      }
      break;
    default: break;
  }
}

void CheckBox::onKey(const KeyEvent& e) {
  if (e.pressed && e.key == Key::Space) {
    toggle();
    e.accept();
  }
}

}  // namespace geeyoou
