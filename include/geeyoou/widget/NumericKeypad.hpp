#pragma once
//
// An on-screen numeric keypad -- the widget a TOUCHSCREEN HMI cannot do without.
//
// Industrial panels are frequently touch-only, with no physical keyboard.
// Entering a set point then means tapping digits on the glass, so a keypad is
// not a nicety here, it is the difference between a screen an operator can use
// and one they cannot.
//
// This is a standalone input widget: a readout line over a grid of keys.  Feed
// it an initial value, read the result off `value()`, or wire `committed` to
// the Enter key.  For the common "tap a field, a keypad pops up, OK writes it
// back" flow, use numericInput() below -- it drops this widget into a modal
// Dialog and hands you the answer.
//
#include <string>

#include "geeyoou/core/Signal.hpp"
#include "geeyoou/widget/PushButton.hpp"
#include "geeyoou/widget/Widget.hpp"

namespace geeyoou {

class Window;

class NumericKeypad : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(NumericKeypad, Widget)

  NumericKeypad();

  void setValue(double v);
  double value() const;             // parses the current entry; 0 if empty
  const std::string& text() const { return entry_; }
  void setText(std::string utf8);
  void clear();

  // Whether the ± sign key and the decimal point are shown.  A count entry
  // wants neither; a set point wants both (the default).
  void setAllowSign(bool on);
  void setAllowDecimal(bool on);

  // A caption shown above the readout (e.g. the tag being edited).
  void setPrompt(std::string utf8);
  // A unit shown after the readout (e.g. "°C").
  void setUnit(std::string utf8);

  Signal<double> valueChanged;  // on every edit
  Signal<double> committed;     // on the Enter key

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;
  void onGeometryChanged() override;

 private:
  void buildKeys();
  void layoutKeys();
  void press(const std::string& key);
  void changed();

  struct Key {
    PushButton* widget = nullptr;
    std::string label;
    int row = 0;
    int col = 0;
  };

  std::string entry_;
  std::string prompt_;
  std::string unit_;
  bool allowSign_ = true;
  bool allowDecimal_ = true;
  std::vector<Key> keys_;
};

// The touchscreen set-point flow: pops a modal keypad on `w`, seeded with
// `initial`, and calls onAccept with the entered value if the operator confirms.
// `prompt` labels what is being edited; `unit` trails the readout.
void numericInput(Window* w, std::string prompt, double initial,
                  std::function<void(double)> onAccept, std::string unit = {});

}  // namespace geeyoou
