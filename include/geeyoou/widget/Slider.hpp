#pragma once
#include "geeyoou/core/Signal.hpp"
#include "geeyoou/widget/Widget.hpp"

namespace geeyoou {

// Horizontal setpoint slider.
//
// Vertical orientation is intentionally absent rather than half-implemented --
// see docs/architecture.md section 4 for the v1 scope rule.
class Slider : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(Slider, Widget)

  Slider() { setFocusPolicy(FocusPolicy::Tab); }

  void setRange(double minValue, double maxValue);
  void setValue(double v);
  double value() const { return value_; }
  double minimum() const { return min_; }
  double maximum() const { return max_; }

  // Keyboard increment; also the quantum values snap to when dragging.
  // Zero means continuous.
  void setStep(double s);

  void setAccent(Color c);
  void setTickCount(int n);  // 0 = no ticks

  SizeHint sizeHint() const override;

  Signal<double> valueChanged;

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;
  void onMouse(const MouseEvent& e) override;
  void onKey(const KeyEvent& e) override;

 private:
  double normalised() const;
  void setValueFromX(float x);
  Rect trackRect() const;
  float handleCenterX() const;

  double min_ = 0.0;
  double max_ = 100.0;
  double value_ = 0.0;
  double step_ = 1.0;
  int tickCount_ = 0;
  Color accent_ = Color::rgb(0x2F, 0xA8, 0xFF);
  bool hovered_ = false;
  bool dragging_ = false;
};

}  // namespace geeyoou
