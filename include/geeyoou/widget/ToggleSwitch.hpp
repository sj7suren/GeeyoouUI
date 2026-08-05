#pragma once
#include <string>

#include "geeyoou/core/Signal.hpp"
#include "geeyoou/widget/Widget.hpp"

namespace geeyoou {

// Pill-shaped on/off switch.
//
// Deliberately NOT animated.  A sliding knob needs a per-frame tick, and
// docs/architecture.md forbids widgets owning timers; more importantly, an
// operator toggling a pump wants unambiguous state now, not 150 ms of
// in-between.
class ToggleSwitch : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(ToggleSwitch, Widget)

  ToggleSwitch() { setFocusPolicy(FocusPolicy::Tab); }

  void setText(std::string utf8);
  void setChecked(bool on);
  bool isChecked() const { return checked_; }
  void toggle() { setChecked(!checked_); }

  // Colour used for the "on" state; defaults to the theme's ok green.
  void setOnColor(Color c);

  SizeHint sizeHint() const override;

  Signal<bool> toggled;

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;
  void onMouse(const MouseEvent& e) override;
  void onKey(const KeyEvent& e) override;

 private:
  std::string text_;
  Color onColor_ = Color::rgb(0x3E, 0xD1, 0x7A);
  bool checked_ = false;
  bool hovered_ = false;
};

}  // namespace geeyoou
