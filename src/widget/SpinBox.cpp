#include "geeyoou/widget/SpinBox.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"

namespace geeyoou {
namespace {
constexpr float kButtonWidth = 22.0f;
// onPaint() right-aligns the value at (right - kButtonWidth - 8), so 8 is the
// real gap between the text and the stepper; the left one matches it.
constexpr float kTextGap = 8.0f;
constexpr float kPadLeft = 8.0f;
constexpr float kPadYComfortable = 8.0f;
constexpr float kPadYTight = 3.0f;
}  // namespace

void SpinBox::setRange(double minValue, double maxValue) {
  min_ = minValue;
  max_ = maxValue;
  setValue(value_);  // re-clamp
  update();
  invalidateSizeHint();
}

void SpinBox::setValue(double v) {
  v = std::clamp(v, min_, max_);
  if (v == value_) return;
  value_ = v;
  update();
  valueChanged.emit(value_);
}

void SpinBox::setStep(double s) { step_ = (s > 0.0) ? s : 1.0; }

void SpinBox::setDecimals(int d) {
  decimals_ = std::clamp(d, 0, 6);
  update();
  invalidateSizeHint();
}

void SpinBox::setSuffix(std::string utf8) {
  suffix_ = std::move(utf8);
  update();
  invalidateSizeHint();
}

// Sized for the WIDEST value the range can produce, not for the value it
// happens to be showing.
//
// This is the whole reason a SpinBox needs a hint of its own.  A field measured
// from its current text is 42px wide at 9.9 and 58px at -100.0, so a form full
// of them re-flows every time a pump changes speed -- on a screen an operator
// is reading numbers off.  Measuring both ends of the range instead makes the
// width a property of the CONFIGURATION, which only a commissioning engineer
// changes, and setRange/setDecimals/setSuffix are exactly the three calls that
// re-measure it.
SizeHint SpinBox::sizeHint() const {
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.*f", decimals_, min_);
  float textW = measureText(buf, Theme::current().fontBody).width;
  std::snprintf(buf, sizeof(buf), "%.*f", decimals_, max_);
  textW = std::max(textW, measureText(buf, Theme::current().fontBody).width);
  if (!suffix_.empty()) {
    textW += measureText(suffix_, Theme::current().fontBody).width;
  }

  const float line = fontLineHeight(Theme::current().fontBody);
  const float width = kPadLeft + textW + kTextGap + kButtonWidth;
  SizeHint h;
  h.preferred = Size{width, line + 2.0f * kPadYComfortable};
  // The stepper buttons and the digits are the widget; there is nothing left to
  // give up horizontally, so the minimum width is the preferred one.
  h.min = Size{width, line + 2.0f * kPadYTight};
  return h;
}

// ------------------------------------------------------------------ layout --
Rect SpinBox::upRect() const {
  const Rect r = localRect();
  return {r.right() - kButtonWidth, r.y(), kButtonWidth, r.height() * 0.5f};
}

Rect SpinBox::downRect() const {
  const Rect r = localRect();
  return {r.right() - kButtonWidth, r.y() + r.height() * 0.5f, kButtonWidth,
          r.height() * 0.5f};
}

SpinBox::Zone SpinBox::zoneAt(Point p) const {
  if (upRect().contains(p)) return Zone::Up;
  if (downRect().contains(p)) return Zone::Down;
  return Zone::Field;
}

std::string SpinBox::displayText() const {
  if (editing_) return editBuffer_;
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.*f", decimals_, value_);
  return std::string(buf) + suffix_;
}

// ------------------------------------------------------------------- edit ---
void SpinBox::step(double multiplier) {
  if (editing_) commitEdit();
  setValue(value_ + step_ * multiplier);
}

void SpinBox::beginEdit() {
  if (editing_) return;
  editing_ = true;
  // Starts EMPTY rather than pre-filled with the current value.  On an HMI the
  // operator is setting a new setpoint, not amending a string -- and with no
  // caret-positioning support, a pre-filled buffer you can only backspace
  // through would be worse than retyping.
  editBuffer_.clear();
  update();
}

void SpinBox::commitEdit() {
  if (!editing_) return;
  editing_ = false;
  if (!editBuffer_.empty()) {
    char* end = nullptr;
    const double parsed = std::strtod(editBuffer_.c_str(), &end);
    if (end != editBuffer_.c_str()) setValue(parsed);
  }
  editBuffer_.clear();
  update();
}

void SpinBox::cancelEdit() {
  if (!editing_) return;
  editing_ = false;
  editBuffer_.clear();
  update();
}

void SpinBox::onFocusChanged(bool focused) {
  // Losing focus commits: tabbing away from a half-typed setpoint should apply
  // it, not silently discard it.  Escape is the explicit discard.
  if (!focused) commitEdit();
  update();
}

// ------------------------------------------------------------------ paint ---
void SpinBox::onPaint(Painter& p, const Rect&) {
  const Theme& t = Theme::current();
  const Rect r = localRect();
  const bool en = isEffectivelyEnabled();

  p.fillRoundRect(r, t.radius, en ? t.field : t.field.lerp(t.background, 0.5f));
  p.strokeRoundRect(r.deflated(0.5f), t.radius,
                    (hasFocus() && en) ? t.focusRing : t.panelBorder, 1.0f);

  // --- value / edit buffer ---
  const std::string txt = displayText();
  const float textRight = r.right() - kButtonWidth - 8.0f;
  const Color textColor = en ? (editing_ ? t.accent : t.text) : t.textDisabled;
  if (!txt.empty()) {
    p.drawText({textRight, r.center().y}, txt, t.fontBody, textColor,
               HAlign::Right, VAlign::Middle);
  }
  if (editing_) {
    const float w = p.measureText(txt, t.fontBody).width;
    const float cx = textRight - w - 2.0f;
    p.strokeLine({cx, r.center().y - 7.0f}, {cx, r.center().y + 7.0f}, t.accent,
                 1.0f);
  }

  // --- stepper buttons ---
  const Rect up = upRect();
  const Rect down = downRect();
  p.strokeLine({up.x(), r.y() + 3.0f}, {up.x(), r.bottom() - 3.0f}, t.panelBorder,
               1.0f);

  const auto drawArrow = [&](const Rect& box, bool pointUp) {
    const bool active = en && hovered_ && hoverZone_ == (pointUp ? Zone::Up : Zone::Down);
    const bool down_ = en && pressed_ && pressedZone_ == (pointUp ? Zone::Up : Zone::Down);
    if (down_) p.fillRect(box.deflated(1.0f), t.accent.withAlpha(60));
    else if (active) p.fillRect(box.deflated(1.0f), t.panelBorder.withAlpha(90));

    const Color c = en ? (active || down_ ? t.text : t.textDim) : t.textDisabled;
    const Point ctr = box.center();
    const float w = 4.0f, h = 3.0f;
    if (pointUp) {
      p.fillTriangle({ctr.x, ctr.y - h}, {ctr.x - w, ctr.y + h}, {ctr.x + w, ctr.y + h}, c);
    } else {
      p.fillTriangle({ctr.x, ctr.y + h}, {ctr.x - w, ctr.y - h}, {ctr.x + w, ctr.y - h}, c);
    }
  };
  drawArrow(up, true);
  drawArrow(down, false);
}

// ------------------------------------------------------------------ input ---
void SpinBox::onMouse(const MouseEvent& e) {
  switch (e.action) {
    case MouseAction::Enter:
      hovered_ = true;
      hoverZone_ = zoneAt(e.pos);
      update();
      e.accept();
      break;
    case MouseAction::Leave:
      hovered_ = false;
      pressed_ = false;
      update();
      e.accept();
      break;
    case MouseAction::Move: {
      const Zone z = zoneAt(e.pos);
      if (z != hoverZone_) {
        hoverZone_ = z;
        update();
      }
      e.accept();
      break;
    }
    case MouseAction::Press:
      if (e.button == MouseButton::Left) {
        pressed_ = true;
        pressedZone_ = zoneAt(e.pos);
        if (pressedZone_ == Zone::Up) step(+1.0);
        else if (pressedZone_ == Zone::Down) step(-1.0);
        update();
        e.accept();
      }
      break;
    case MouseAction::Release:
      if (e.button == MouseButton::Left) {
        pressed_ = false;
        update();
        e.accept();
      }
      break;
    case MouseAction::Wheel:
      if (hasFocus()) {  // only when focused, so scrolling a panel cannot
        step(e.wheelDelta > 0 ? +1.0 : -1.0);  // silently change a setpoint
        e.accept();
      }
      break;
    default:
      break;
  }
}

void SpinBox::onKey(const KeyEvent& e) {
  if (!e.pressed) return;

  int digit = 0;
  if (keyToDigit(e.key, digit)) {
    beginEdit();
    if (editBuffer_.size() < 16) editBuffer_ += char('0' + digit);
    update();
    e.accept();
    return;
  }

  switch (e.key) {
    case Key::Up:       step(+1.0);  e.accept(); break;
    case Key::Down:     step(-1.0);  e.accept(); break;
    case Key::PageUp:   step(+10.0); e.accept(); break;
    case Key::PageDown: step(-10.0); e.accept(); break;
    case Key::Home: if (!editing_) { setValue(min_); e.accept(); } break;
    case Key::End:  if (!editing_) { setValue(max_); e.accept(); } break;

    case Key::Minus:
      beginEdit();
      // Only meaningful as a leading sign; mid-buffer it would not parse.
      if (editBuffer_.empty()) editBuffer_ += '-';
      update();
      e.accept();
      break;

    case Key::Period:
      beginEdit();
      if (editBuffer_.find('.') == std::string::npos) {
        if (editBuffer_.empty()) editBuffer_ += '0';
        editBuffer_ += '.';
      }
      update();
      e.accept();
      break;

    case Key::Backspace:
      if (editing_ && !editBuffer_.empty()) {
        editBuffer_.pop_back();
        update();
      }
      e.accept();
      break;

    case Key::Enter:  commitEdit(); e.accept(); break;
    case Key::Escape: cancelEdit(); e.accept(); break;
    default: break;
  }
}

}  // namespace geeyoou
