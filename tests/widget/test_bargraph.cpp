//
// Bargraph: value is clamped to the range, and a change notifies exactly once.
//
#include "framework/Test.hpp"
#include "geeyoou/hmi/Bargraph.hpp"

using geeyoou::Bargraph;

GEEYOOU_TEST(bargraph, value_clamps_to_the_range) {
  Bargraph bg;
  bg.setRange(0.0, 100.0);

  bg.setValue(150.0);  // above max
  CHECK_NEAR(bg.value(), 100.0, 0.001);

  bg.setValue(-20.0);  // below min
  CHECK_NEAR(bg.value(), 0.0, 0.001);
}

GEEYOOU_TEST(bargraph, an_unchanged_value_does_not_notify) {
  Bargraph bg;
  bg.setRange(0.0, 100.0);
  bg.setValue(42.0);

  int calls = 0;
  auto c = bg.valueChanged.connect([&](double) { ++calls; });
  bg.setValue(42.0);  // same value
  CHECK_EQ(calls, 0);
  bg.setValue(43.0);  // different
  CHECK_EQ(calls, 1);
  c.disconnect();
}
