#include "geeyoou/hmi/Gauge.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"

namespace geeyoou {
namespace {
constexpr double kDeg2Rad = 3.14159265358979323846 / 180.0;
}

void Gauge::setRange(double minValue, double maxValue) {
  min_ = minValue;
  max_ = maxValue;
  update();
}

void Gauge::setValue(double v) {
  v = std::clamp(v, min_, max_);
  if (v == value_) return;
  value_ = v;
  valueChanged.emit(v);
  update();
}

void Gauge::setTitle(std::string utf8) {
  title_ = std::move(utf8);
  update();
}

void Gauge::setUnit(std::string utf8) {
  unit_ = std::move(utf8);
  update();
}

void Gauge::setBands(double warnValue, double alarmValue) {
  warnAt_ = warnValue;
  alarmAt_ = alarmValue;
  update();
}

void Gauge::setSweep(float startDeg, float sweepDeg) {
  startDeg_ = startDeg;
  sweepDeg_ = sweepDeg;
  update();
}

double Gauge::normalised() const {
  if (max_ <= min_) return 0.0;
  return std::clamp((value_ - min_) / (max_ - min_), 0.0, 1.0);
}

Color Gauge::valueColor() const {
  const Theme& t = Theme::current();
  if (value_ >= alarmAt_) return t.alarm;
  if (value_ >= warnAt_) return t.warn;
  return t.accent;
}

void Gauge::onPaint(Painter& p, const Rect&) {
  const Theme& t = Theme::current();
  const Rect r = localRect();

  p.fillRoundRect(r, t.radius, t.panel);
  p.strokeRoundRect(r.deflated(0.5f), t.radius, t.panelBorder, 1.0f);

  const Point c{r.center().x, r.center().y + r.height() * 0.06f};
  const float outer = std::min(r.width(), r.height()) * 0.40f;
  const float thickness = outer * 0.24f;
  const float inner = outer - thickness;

  // 1. Track.
  p.fillArcRing(c, outer, inner, startDeg_, sweepDeg_, t.track);

  // 2. Warn / alarm bands drawn on the track, so the operator can see where the
  //    limits are even when the value is nowhere near them.
  const auto bandStart = [&](double v) {
    return startDeg_ + sweepDeg_ * float(std::clamp((v - min_) / (max_ - min_), 0.0, 1.0));
  };
  if (warnAt_ < max_) {
    const float a0 = bandStart(warnAt_);
    const float a1 = (alarmAt_ < max_) ? bandStart(alarmAt_) : startDeg_ + sweepDeg_;
    p.fillArcRing(c, outer, outer - thickness * 0.30f, a0, a1 - a0, t.warn.withAlpha(150));
  }
  if (alarmAt_ < max_) {
    const float a0 = bandStart(alarmAt_);
    const float a1 = startDeg_ + sweepDeg_;
    p.fillArcRing(c, outer, outer - thickness * 0.30f, a0, a1 - a0, t.alarm.withAlpha(170));
  }

  // 3. Value arc.
  const float sweep = sweepDeg_ * float(normalised());
  if (sweep > 0.01f) {
    p.fillArcRing(c, outer, inner, startDeg_, sweep, valueColor());
  }

  // 4. Major ticks.
  const int kTicks = 11;
  for (int i = 0; i < kTicks; ++i) {
    const float a = (startDeg_ + sweepDeg_ * float(i) / float(kTicks - 1)) *
                    float(kDeg2Rad);
    const float ca = std::cos(a);
    const float sa = std::sin(a);
    const float r0 = inner - 3.0f;
    const float r1 = inner - 9.0f;
    p.strokeLine({c.x + ca * r0, c.y + sa * r0}, {c.x + ca * r1, c.y + sa * r1},
                 t.textDim, 1.0f);
  }

  // 5. Needle: a triangle plus a hub, which stays legible at small sizes where
  //    a thin line would alias into nothing.
  {
    const float a = (startDeg_ + sweep) * float(kDeg2Rad);
    const float ca = std::cos(a);
    const float sa = std::sin(a);
    const float len = inner - 4.0f;
    const float halfBase = std::max(2.0f, outer * 0.055f);
    const Point tip{c.x + ca * len, c.y + sa * len};
    const Point b1{c.x - sa * halfBase, c.y + ca * halfBase};
    const Point b2{c.x + sa * halfBase, c.y - ca * halfBase};
    p.fillTriangle(tip, b1, b2, t.text);
    p.fillCircle(c, halfBase * 1.6f, t.panelBorder);
    p.fillCircle(c, halfBase * 0.9f, t.text);
  }

  // 6. Digital readout -- the number is what the operator actually reads; the
  //    arc is for peripheral vision.
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%.1f", value_);
  const float readoutY = c.y + outer * 0.55f;
  p.drawText({c.x, readoutY}, buf, t.fontLarge, valueColor(), HAlign::Center,
             VAlign::Top);
  if (!unit_.empty()) {
    p.drawText({c.x, readoutY + t.fontLarge + 3.0f}, unit_, t.fontSmall, t.textDim,
               HAlign::Center, VAlign::Top);
  }
  if (!title_.empty()) {
    p.drawText({r.center().x, r.y() + 10.0f}, title_, t.fontBody, t.textDim,
               HAlign::Center, VAlign::Top);
  }
}

}  // namespace geeyoou
