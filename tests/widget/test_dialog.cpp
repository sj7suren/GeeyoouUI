//
// Dialog's lifetime contract, as tests rather than as hope.
//
// The whole reason Dialog::close() defers through a timer is contract D7: a
// button's `clicked` handler must not destroy the object that owns the button.
// So the two things worth pinning down are (1) close() does NOT call back
// synchronously, and (2) a dialog destroyed before its deferred close fires
// takes the timer with it -- the case AddressSanitizer turns from "works on my
// machine" into a red gate.
//
// Neither case needs a native Window: close() guards `if (Window* w =
// window())`, so a dialog with no window still runs its deferral and callback.
// That keeps the test headless while still exercising the part that bites.
//
#include "framework/Test.hpp"
#include "geeyoou/platform/Platform.hpp"
#include "geeyoou/widget/Dialog.hpp"
#include "geeyoou/widget/Widget.hpp"

using geeyoou::Dialog;
using geeyoou::platform;
using geeyoou::TimerId;
using geeyoou::Widget;

namespace {
// Pumps the event loop for a few short ticks, then quits -- long enough for a
// zero-delay timer to fire at least once (Win32 clamps 0ms to ~10ms).
void pumpBriefly() {
  int ticks = 0;
  TimerId driver = 0;
  driver = platform().startTimer(5, [&] {
    if (++ticks >= 4) {
      platform().stopTimer(driver);
      platform().quit(0);
    }
  });
  platform().runEventLoop();
}
}  // namespace

GEEYOOU_TEST(dialog, close_does_not_call_back_synchronously) {
  Dialog dlg;
  int got = -999;
  dlg.onResult = [&](int r) { got = r; };

  dlg.close(7);
  // The point of the whole design: onResult has NOT run yet.  If it had, a
  // real button's clicked handler would be destroying its own owner mid-emit.
  CHECK_EQ(got, -999);

  pumpBriefly();
  CHECK_EQ(got, 7);  // fired once the stack was clean
}

GEEYOOU_TEST(dialog, first_answer_wins) {
  Dialog dlg;
  int calls = 0;
  int last = -1;
  dlg.onResult = [&](int r) { ++calls; last = r; };

  dlg.close(1);
  dlg.close(2);  // a second click before the deferred close fires is ignored
  pumpBriefly();

  CHECK_EQ(calls, 1);
  CHECK_EQ(last, 1);
}

GEEYOOU_TEST(dialog, may_be_destroyed_from_inside_onResult) {
  // The contract numericInput/messageBox rely on: onResult runs on a clean
  // stack as the LAST deferred step, so the caller may delete the dialog there.
  // A heap dialog owned by a parent, removed from inside onResult, must not
  // touch freed memory afterwards -- ASan is the judge.
  Widget parent;
  Dialog* dlg = parent.add<Dialog>();
  int got = -1;
  dlg->onResult = [&](int r) {
    got = r;
    parent.removeChild(dlg);  // destroy the dialog from inside its own callback
  };
  dlg->close(9);
  pumpBriefly();
  CHECK_EQ(got, 9);  // fired, and nothing crashed after the self-destroy
}

GEEYOOU_TEST(dialog, destroying_before_the_deferred_close_fires_is_safe) {
  int got = -999;
  {
    Dialog dlg;
    dlg.onResult = [&](int r) { got = r; };
    dlg.close(3);
    // dlg goes out of scope here -- BEFORE the zero-delay timer could fire.
    // ~Dialog must stop that timer; otherwise the pump below fires it into a
    // freed dialog and ASan reports a use-after-free.
  }
  pumpBriefly();
  CHECK_EQ(got, -999);  // the cancelled callback never ran
}
