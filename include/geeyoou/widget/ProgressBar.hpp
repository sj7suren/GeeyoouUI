#pragma once
#include <string>

#include "geeyoou/widget/Widget.hpp"

namespace geeyoou {

// Read-only fill indicator: batch progress, tank level, buffer occupancy.
class ProgressBar : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(ProgressBar, Widget)

  void setRange(double minValue, double maxValue);
  void setValue(double v);
  double value() const { return value_; }

  void setBarColor(Color c);
  void setTextVisible(bool on);
  // Optional text override; empty means "show the percentage".
  void setText(std::string utf8);

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;

 private:
  double min_ = 0.0;
  double max_ = 100.0;
  double value_ = 0.0;
  Color bar_ = Color::rgb(0x2F, 0xA8, 0xFF);
  bool textVisible_ = true;
  std::string text_;
};

}  // namespace geeyoou
