//
// NumericKeypad: the value logic the readout and numericInput both rely on.
// (The digit-append behaviour rides on button clicks and is exercised live in
// the showcase; here we pin the parse/format round-trip that everything reads.)
//
#include "framework/Test.hpp"
#include "geeyoou/widget/NumericKeypad.hpp"

using geeyoou::NumericKeypad;

GEEYOOU_TEST(keypad, text_parses_to_value) {
  NumericKeypad k;
  k.setText("42.5");
  CHECK_NEAR(k.value(), 42.5, 0.001);
  k.setText("-7");
  CHECK_NEAR(k.value(), -7.0, 0.001);
}

GEEYOOU_TEST(keypad, set_value_round_trips) {
  NumericKeypad k;
  k.setValue(3.5);
  CHECK_NEAR(k.value(), 3.5, 0.001);
}

GEEYOOU_TEST(keypad, empty_or_partial_entry_reads_as_zero) {
  NumericKeypad k;
  CHECK_NEAR(k.value(), 0.0, 0.001);  // nothing typed
  k.setText("-");
  CHECK_NEAR(k.value(), 0.0, 0.001);  // just a sign is not a number yet
  k.clear();
  CHECK_NEAR(k.value(), 0.0, 0.001);
}
