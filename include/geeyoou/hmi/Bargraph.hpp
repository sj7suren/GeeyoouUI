#pragma once
#include <string>

#include "geeyoou/core/Signal.hpp"
#include "geeyoou/widget/Widget.hpp"

namespace geeyoou {

// A filled bar indicator: a tank level, a fill percentage, any bounded analogue
// value that reads better as "how full" than as a needle.  The arc Gauge is its
// sibling; the two share their range/value/band API on purpose, so swapping one
// for the other is a one-line change.
//
// Vertical by default -- a level rises, which is the mental model for a tank or
// a hopper.  Horizontal is offered for a value that reads left-to-right.
//
// Bands work like Gauge's: at or above warn/alarm the FILLED portion is drawn
// in the warn/alarm colour, so a bar that has climbed into the alarm zone is
// red without the caller writing any test.  The band thresholds also leave a
// thin marker line on the track, so the operator sees where the limits are even
// when the value is nowhere near them.
class Bargraph : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(Bargraph, Widget)

  enum class Orientation : std::uint8_t { Vertical, Horizontal };

  void setOrientation(Orientation o);
  void setRange(double minValue, double maxValue);
  void setValue(double v);
  double value() const { return value_; }

  void setTitle(std::string utf8);
  void setUnit(std::string utf8);

  // At or above these values the fill turns warn / alarm coloured, and a marker
  // is drawn on the track.  Set above the range to disable a band.
  void setBands(double warnValue, double alarmValue);

  // Number of tick divisions along the track (0 = none).
  void setTicks(int divisions);

  Signal<double> valueChanged;

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;

 private:
  double normalised() const;
  double normalisedOf(double v) const;
  Color fillColor() const;

  Orientation orient_ = Orientation::Vertical;
  double min_ = 0.0;
  double max_ = 100.0;
  double value_ = 0.0;
  double warnAt_ = 1e300;
  double alarmAt_ = 1e300;
  int ticks_ = 4;
  std::string title_;
  std::string unit_;
};

}  // namespace geeyoou
