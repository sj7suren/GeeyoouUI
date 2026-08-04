#pragma once
#include <string>

#include "geeyoou/widget/Widget.hpp"

namespace geeyoou {

// Indicator lamp with a text caption -- the most-used element on any HMI panel.
class StatusLed : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(StatusLed, Widget)

  enum class State { Off, Ok, Warn, Alarm };

  void setState(State s);
  State state() const { return state_; }

  void setCaption(std::string utf8);

  // When enabled, an Alarm-state lamp pulses.  The pulse is driven by
  // tick(), not by an internal timer: the application already has a UI tick,
  // and a widget that owns a timer is a widget that keeps a screen awake
  // forever.  See docs/architecture.md section 1, rule 2.
  void setBlinkOnAlarm(bool on) { blink_ = on; }
  void tick();

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;

 private:
  Color stateColor() const;

  State state_ = State::Off;
  std::string caption_;
  bool blink_ = false;
  bool blinkPhase_ = false;
};

}  // namespace geeyoou
