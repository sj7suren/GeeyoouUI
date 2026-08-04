#include "geeyoou/hmi/StatusLed.hpp"

#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"

namespace geeyoou {

void StatusLed::setState(State s) {
  if (state_ == s) return;
  state_ = s;
  blinkPhase_ = false;
  update();
}

void StatusLed::setCaption(std::string utf8) {
  caption_ = std::move(utf8);
  update();
}

void StatusLed::tick() {
  if (!blink_ || state_ != State::Alarm) return;
  blinkPhase_ = !blinkPhase_;
  // Repaint only the lamp, not the caption: the caption never changes, and on a
  // panel with 40 lamps blinking at 2 Hz the difference is real.
  const float d = localRect().height();
  update(Rect(0.0f, 0.0f, d, d));
}

Color StatusLed::stateColor() const {
  const Theme& t = Theme::current();
  switch (state_) {
    case State::Ok:    return t.ok;
    case State::Warn:  return t.warn;
    case State::Alarm: return t.alarm;
    case State::Off:
    default:           return t.track;
  }
}

void StatusLed::onPaint(Painter& p, const Rect&) {
  const Theme& t = Theme::current();
  const Rect r = localRect();
  const float d = r.height();
  const float radius = d * 0.32f;
  const Point c{d * 0.5f, r.center().y};

  Color lamp = stateColor();
  if (blink_ && state_ == State::Alarm && blinkPhase_) lamp = lamp.lerp(t.panel, 0.7f);

  // Halo first, then body, then a small specular highlight -- three cheap
  // circles read as a physical indicator lamp far better than one flat dot.
  if (state_ != State::Off) {
    p.fillCircle(c, radius * 1.9f, lamp.withAlpha(46));
    p.fillCircle(c, radius * 1.4f, lamp.withAlpha(70));
  }
  p.fillCircle(c, radius, lamp);
  p.strokeCircle(c, radius, t.background.lerp(lamp, 0.4f), 1.0f);
  if (state_ != State::Off) {
    p.fillCircle({c.x - radius * 0.3f, c.y - radius * 0.35f}, radius * 0.28f,
                 Color::rgba(255, 255, 255, 110));
  }

  if (!caption_.empty()) {
    p.drawText({d + 8.0f, r.center().y}, caption_, t.fontBody,
               state_ == State::Off ? t.textDim : t.text, HAlign::Left,
               VAlign::Middle);
  }
}

}  // namespace geeyoou
