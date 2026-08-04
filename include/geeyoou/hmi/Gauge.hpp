#pragma once
#include <string>

#include "geeyoou/core/Signal.hpp"
#include "geeyoou/widget/Widget.hpp"

namespace geeyoou {

// Analogue arc gauge with a digital readout, warning/alarm bands and a needle.
class Gauge : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(Gauge, Widget)

  void setRange(double minValue, double maxValue);
  void setValue(double v);
  double value() const { return value_; }

  void setTitle(std::string utf8);
  void setUnit(std::string utf8);

  // Values at or above these fractions of the range are drawn in warn/alarm
  // colours.  Set to >1.0 to disable a band.
  void setBands(double warnValue, double alarmValue);

  // Degrees, 0 = 3 o'clock, clockwise.  The default 135 deg + 270 deg sweep is
  // the layout every process-instrument vendor uses.
  void setSweep(float startDeg, float sweepDeg);

  Signal<double> valueChanged;

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;

 private:
  double normalised() const;
  Color valueColor() const;

  double min_ = 0.0;
  double max_ = 100.0;
  double value_ = 0.0;
  double warnAt_ = 1e300;
  double alarmAt_ = 1e300;
  float startDeg_ = 135.0f;
  float sweepDeg_ = 270.0f;
  std::string title_;
  std::string unit_;
};

}  // namespace geeyoou
