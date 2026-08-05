#pragma once
#include <string>

#include "geeyoou/core/Signal.hpp"
#include "geeyoou/widget/Widget.hpp"

namespace geeyoou {

// Numeric setpoint entry -- the workhorse of every HMI parameter form.
//
// Text entry is restricted to digits, minus and the decimal point, driven off
// the platform-independent Key enum.  That is a deliberate scope choice, not an
// oversight: numeric-only entry needs no IME, so the v1 "no input method"
// boundary in docs/architecture.md section 4 holds while parameter forms still
// work for Chinese-locale operators.
class SpinBox : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(SpinBox, Widget)

  SpinBox() { setFocusPolicy(FocusPolicy::Tab); }

  void setRange(double minValue, double maxValue);
  void setValue(double v);
  double value() const { return value_; }

  void setStep(double s);
  void setDecimals(int d);
  void setSuffix(std::string utf8);  // e.g. " °C"

  Signal<double> valueChanged;

  SizeHint sizeHint() const override;

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;
  void onMouse(const MouseEvent& e) override;
  void onKey(const KeyEvent& e) override;
  void onFocusChanged(bool focused) override;

 private:
  enum class Zone { Field, Up, Down };

  Rect upRect() const;
  Rect downRect() const;
  Zone zoneAt(Point p) const;
  std::string displayText() const;
  void step(double multiplier);
  void beginEdit();
  void commitEdit();
  void cancelEdit();

  double min_ = 0.0;
  double max_ = 100.0;
  double value_ = 0.0;
  double step_ = 1.0;
  int decimals_ = 1;
  std::string suffix_;

  bool editing_ = false;
  std::string editBuffer_;
  Zone hoverZone_ = Zone::Field;
  bool hovered_ = false;
  Zone pressedZone_ = Zone::Field;
  bool pressed_ = false;
};

}  // namespace geeyoou
