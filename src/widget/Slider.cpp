#include "geeyoou/widget/Slider.hpp"

#include <algorithm>
#include <cmath>

#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"

namespace geeyoou {
namespace {
constexpr float kHandleRadius = 8.0f;
}

void Slider::setRange(double minValue, double maxValue) {
  min_ = minValue;
  max_ = maxValue;
  setValue(value_);  // re-clamp
  update();
}

void Slider::setValue(double v) {
  v = std::clamp(v, min_, max_);
  if (step_ > 0.0) {
    // Snap to the step grid measured from the minimum, not from zero: a range
    // of 5..45 with step 10 should offer 5/15/25/..., not 10/20/30.
    v = min_ + std::round((v - min_) / step_) * step_;
    v = std::clamp(v, min_, max_);
  }
  if (v == value_) return;
  value_ = v;
  update();
  valueChanged.emit(value_);
}

void Slider::setStep(double s) { step_ = s < 0.0 ? 0.0 : s; }

void Slider::setAccent(Color c) {
  accent_ = c;
  update();
}

void Slider::setTickCount(int n) {
  tickCount_ = std::max(0, n);
  update();
}

double Slider::normalised() const {
  if (max_ <= min_) return 0.0;
  return std::clamp((value_ - min_) / (max_ - min_), 0.0, 1.0);
}

Rect Slider::trackRect() const {
  const Rect r = localRect();
  const float h = 5.0f;
  // Inset by the handle radius so the handle never overhangs the widget (and
  // therefore never gets clipped by paintTree).
  return {kHandleRadius, r.center().y - h * 0.5f,
          std::max(0.0f, r.width() - kHandleRadius * 2.0f), h};
}

float Slider::handleCenterX() const {
  const Rect tr = trackRect();
  return tr.x() + tr.width() * float(normalised());
}

void Slider::setValueFromX(float x) {
  const Rect tr = trackRect();
  if (tr.width() <= 0.0f) return;
  const double n = std::clamp(double((x - tr.x()) / tr.width()), 0.0, 1.0);
  setValue(min_ + n * (max_ - min_));
}

void Slider::onPaint(Painter& p, const Rect&) {
  const Theme& t = Theme::current();
  const Rect r = localRect();
  const bool en = isEffectivelyEnabled();
  const Rect tr = trackRect();
  const float hx = handleCenterX();

  // A disabled slider fades towards the TRACK colour, not the background:
  // lerping to background left enough blue that an operator could still read a
  // locked setpoint as live. On an HMI, "am I allowed to touch this" has to be
  // answerable at a glance.
  const StyleProps& sp = style(styleState());
  const Color accent = sp.accentOr(accent_);
  Color fillColor = en ? accent : accent.lerp(t.track, 0.85f);

  p.fillRoundRect(tr, tr.height() * 0.5f, sp.backgroundOr(t.track));
  p.fillRoundRect({tr.x(), tr.y(), hx - tr.x(), tr.height()}, tr.height() * 0.5f,
                  fillColor);

  if (tickCount_ > 1) {
    for (int i = 0; i < tickCount_; ++i) {
      const float x = tr.x() + tr.width() * float(i) / float(tickCount_ - 1);
      p.strokeLine({x, tr.bottom() + 3.0f}, {x, tr.bottom() + 7.0f}, t.textDim, 1.0f);
    }
  }

  const Point c{hx, tr.center().y};
  const float hr = dragging_ ? kHandleRadius + 1.0f : kHandleRadius;
  p.fillCircle(c, hr, en ? t.text : t.textDisabled);
  p.strokeCircle(c, hr - 0.5f, en ? fillColor : t.textDisabled, 1.5f);

  if (hasFocus() && en) {
    p.strokeRoundRect(r.deflated(0.5f), 3.0f, t.focusRing, 1.0f);
  }
  (void)hovered_;
}

void Slider::onMouse(const MouseEvent& e) {
  switch (e.action) {
    case MouseAction::Enter: hovered_ = true; update(); e.accept(); break;
    case MouseAction::Leave: hovered_ = false; update(); e.accept(); break;
    case MouseAction::Press:
      if (e.button == MouseButton::Left) {
        dragging_ = true;
        setValueFromX(e.pos.x);  // click anywhere on the track jumps there
        update();
        e.accept();
      }
      break;
    case MouseAction::Move:
      // The window's press-grab keeps delivering moves to us even once the
      // cursor leaves the widget, which is what makes dragging past the end
      // behave (value pins to the limit instead of the drag dying).
      if (dragging_) {
        setValueFromX(e.pos.x);
        e.accept();
      }
      break;
    case MouseAction::Release:
      if (e.button == MouseButton::Left && dragging_) {
        dragging_ = false;
        update();
        e.accept();
      }
      break;
    default: break;
  }
}

void Slider::onKey(const KeyEvent& e) {
  if (!e.pressed) return;
  const double s = step_ > 0.0 ? step_ : (max_ - min_) / 100.0;
  switch (e.key) {
    case Key::Left:  case Key::Down: setValue(value_ - s); e.accept(); break;
    case Key::Right: case Key::Up:   setValue(value_ + s); e.accept(); break;
    case Key::PageDown: setValue(value_ - s * 10.0); e.accept(); break;
    case Key::PageUp:   setValue(value_ + s * 10.0); e.accept(); break;
    case Key::Home: setValue(min_); e.accept(); break;
    case Key::End:  setValue(max_); e.accept(); break;
    default: break;
  }
}

}  // namespace geeyoou
