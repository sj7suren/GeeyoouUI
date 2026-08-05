#include "geeyoou/widget/RadioButton.hpp"

#include <algorithm>

#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"

namespace geeyoou {
namespace {
// The dot onPaint() draws at full height, the gap it leaves before the label,
// and the touch target the row wants.  Deliberately the same three numbers as
// CheckBox: a radio and a check box stacked in one column that disagreed by a
// pixel would read as a rendering fault.
constexpr float kDotSide = 16.0f;
constexpr float kLabelGap = 9.0f;
constexpr float kRowHeight = 22.0f;
}  // namespace

void RadioButton::setText(std::string utf8) {
  text_ = std::move(utf8);
  update();
  invalidateSizeHint();
}

// Dot + gap + label, and the width does not shrink -- the CheckBox argument
// verbatim: there is nothing a narrower radio can do except cut its label in
// half, and half of "手动" is a different instruction, not a smaller one.
SizeHint RadioButton::sizeHint() const {
  const float fontSize = style(styleState()).fontSizeOr(Theme::current().fontBody);
  const float labelW =
      text_.empty() ? 0.0f : kLabelGap + measureText(text_, fontSize).width;

  SizeHint h;
  h.preferred = Size{kDotSide + labelW, kRowHeight};
  h.min = Size{kDotSide + labelW, kDotSide};
  return h;
}

void RadioButton::uncheckSiblings() {
  Widget* p = parent();
  if (!p) return;
  for (const auto& child : p->children()) {
    auto* other = dynamic_cast<RadioButton*>(child.get());
    if (!other || other == this || other->group_ != group_) continue;
    if (!other->checked_) continue;
    other->checked_ = false;
    other->update();
    other->toggled.emit(false);
  }
}

void RadioButton::setChecked(bool on) {
  if (checked_ == on) return;
  // Unchecking directly is a no-op: exclusivity means the only way out of the
  // checked state is another radio in the group taking it.  Allowing an empty
  // group would let an operator leave "运行模式" unset, which no HMI wants.
  if (!on) return;

  checked_ = true;
  uncheckSiblings();
  update();
  toggled.emit(true);
}

void RadioButton::onPaint(Painter& p, const Rect&) {
  const Theme& t = Theme::current();
  const Rect r = localRect();
  const bool on = isEffectivelyEnabled();

  const float side = std::min(kDotSide, r.height());
  const Point c{side * 0.5f, r.center().y};
  const float radius = side * 0.5f;

  const StyleProps& sp = style(styleState());
  const Color accent = sp.accentOr(t.accent);
  Color border = checked_ ? accent : sp.borderColorOr(t.panelBorder);
  if (!on) border = t.textDisabled;
  else if (hovered_ && !checked_) border = accent;

  p.fillCircle(c, radius, sp.backgroundOr(t.field));
  p.strokeCircle(c, radius - 0.5f, border, 1.0f);
  if (checked_) {
    p.fillCircle(c, radius * 0.48f, on ? accent : t.textDisabled);
  }

  // Encloses the whole row, and stays inside localRect -- paintTree clips each
  // widget to its bounds, so a ring around the dot alone would be cut at x = 0.
  if (hasFocus() && on) {
    p.strokeRoundRect(r.deflated(0.5f), 3.0f, t.focusRing, 1.0f);
  }

  if (!text_.empty()) {
    p.drawText({side + kLabelGap, r.center().y}, text_, t.fontBody,
               on ? t.text : t.textDisabled, HAlign::Left, VAlign::Middle);
  }
}

void RadioButton::onMouse(const MouseEvent& e) {
  switch (e.action) {
    case MouseAction::Enter: hovered_ = true; update(); e.accept(); break;
    case MouseAction::Leave: hovered_ = false; update(); e.accept(); break;
    case MouseAction::Release:
      if (e.button == MouseButton::Left && localRect().contains(e.pos)) {
        setChecked(true);
        e.accept();
      }
      break;
    case MouseAction::Press:
      if (e.button == MouseButton::Left) e.accept();
      break;
    default: break;
  }
}

void RadioButton::onKey(const KeyEvent& e) {
  if (e.pressed && e.key == Key::Space) {
    setChecked(true);
    e.accept();
  }
}

}  // namespace geeyoou
