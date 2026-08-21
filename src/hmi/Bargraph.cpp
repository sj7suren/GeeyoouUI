#include "geeyoou/hmi/Bargraph.hpp"

#include <algorithm>
#include <cstdio>

#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"

namespace geeyoou {
namespace {
constexpr float kPad = 8.0f;
constexpr float kTitleH = 20.0f;
constexpr float kReadoutH = 26.0f;
constexpr float kTrackW = 26.0f;   // thickness of the bar
}  // namespace

void Bargraph::setOrientation(Orientation o) {
  orient_ = o;
  update();
}

void Bargraph::setRange(double minValue, double maxValue) {
  min_ = minValue;
  max_ = maxValue;
  update();
}

void Bargraph::setValue(double v) {
  v = std::clamp(v, min_, max_);
  if (v == value_) return;
  value_ = v;
  update();
  // Tail emit: the door is the last statement, so a slot that destroys this
  // widget leaves nothing behind to touch a freed `this` (section 11.4).
  valueChanged.emit(v);
}

void Bargraph::setTitle(std::string utf8) {
  title_ = std::move(utf8);
  update();
}

void Bargraph::setUnit(std::string utf8) {
  unit_ = std::move(utf8);
  update();
}

void Bargraph::setBands(double warnValue, double alarmValue) {
  warnAt_ = warnValue;
  alarmAt_ = alarmValue;
  update();
}

void Bargraph::setTicks(int divisions) {
  ticks_ = std::max(0, divisions);
  update();
}

double Bargraph::normalisedOf(double v) const {
  if (max_ <= min_) return 0.0;
  return std::clamp((v - min_) / (max_ - min_), 0.0, 1.0);
}

double Bargraph::normalised() const { return normalisedOf(value_); }

Color Bargraph::fillColor() const {
  const Theme& t = Theme::current();
  if (value_ >= alarmAt_) return t.alarm;
  if (value_ >= warnAt_) return t.warn;
  return t.accent;
}

void Bargraph::onPaint(Painter& p, const Rect&) {
  const Theme& t = Theme::current();
  const Rect r = localRect();

  // Panel.
  p.fillRoundRect(r, t.radius, t.panel);
  p.strokeRoundRect(r.deflated(0.5f), t.radius, t.panelBorder, 1.0f);

  // Title (top) and readout (bottom) reserve rows; the track fills the middle.
  if (!title_.empty()) {
    p.drawText({r.x() + kPad, r.y() + kPad}, title_, t.fontSmall, t.textDim,
               HAlign::Left, VAlign::Top);
  }

  char buf[48];
  std::snprintf(buf, sizeof(buf), "%.1f", value_);
  const std::string readout =
      unit_.empty() ? std::string(buf) : (std::string(buf) + " " + unit_);
  p.drawText({r.center().x, r.bottom() - kReadoutH * 0.5f}, readout, 15.0f,
             fillColor(), HAlign::Center, VAlign::Middle);

  // The track rectangle, between the title and the readout.
  const float top = r.y() + kTitleH + kPad;
  const float bottom = r.bottom() - kReadoutH;
  Rect track;
  if (orient_ == Orientation::Vertical) {
    track = {r.center().x - kTrackW * 0.5f, top, kTrackW,
             std::max(0.0f, bottom - top)};
  } else {
    const float cy = (top + bottom) * 0.5f;
    track = {r.x() + kPad, cy - kTrackW * 0.5f,
             std::max(0.0f, r.width() - 2.0f * kPad), kTrackW};
  }
  if (track.width() <= 0.0f || track.height() <= 0.0f) return;

  p.fillRoundRect(track, 4.0f, t.track);

  // The filled portion grows from the "zero" end.
  const float f = float(normalised());
  Rect fill;
  if (orient_ == Orientation::Vertical) {
    const float h = track.height() * f;
    fill = {track.x(), track.bottom() - h, track.width(), h};
  } else {
    fill = {track.x(), track.y(), track.width() * f, track.height()};
  }
  if (fill.width() > 0.0f && fill.height() > 0.0f) {
    p.fillRoundRect(fill, 4.0f, fillColor());
  }

  // Band markers: a thin line across the track at each threshold.
  auto marker = [&](double v, Color c) {
    if (v > max_ || v < min_) return;
    const float fr = float(normalisedOf(v));
    if (orient_ == Orientation::Vertical) {
      const float y = track.bottom() - track.height() * fr;
      p.strokeLine({track.x(), y}, {track.right(), y}, c, 1.0f);
    } else {
      const float x = track.x() + track.width() * fr;
      p.strokeLine({x, track.y()}, {x, track.bottom()}, c, 1.0f);
    }
  };
  marker(warnAt_, t.warn);
  marker(alarmAt_, t.alarm);

  // Ticks along the outer edge of the track.
  for (int i = 1; i < ticks_; ++i) {
    const float fr = float(i) / float(ticks_);
    if (orient_ == Orientation::Vertical) {
      const float y = track.bottom() - track.height() * fr;
      p.strokeLine({track.x() - 4.0f, y}, {track.x() - 1.0f, y}, t.panelBorder,
                   1.0f);
    } else {
      const float x = track.x() + track.width() * fr;
      p.strokeLine({x, track.bottom() + 1.0f}, {x, track.bottom() + 4.0f},
                   t.panelBorder, 1.0f);
    }
  }
}

}  // namespace geeyoou
