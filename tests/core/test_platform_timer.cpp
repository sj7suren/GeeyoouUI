//
// Platform timer lifetime tests.
//
// startTimer() used to return nothing, so a timer could never be stopped.  That
// is not a missing convenience: Window::enableAnimations registers a callback
// capturing `this` and the timer lives in the platform, so destroying ANY
// window left the tick firing into freed memory a few milliseconds later.  The
// showcase only got away with it because its single window outlives main().
//
// These cases drive the real backend rather than a fake.  The timing primitive
// IS the platform, and a fake would only prove that the fake works.  Nothing
// here includes <windows.h>: the pump is Platform::runEventLoop and the exit is
// Platform::quit, both of which the interface already promises.
//
#include "geeyoou/platform/Platform.hpp"

#include <cstddef>
#include <memory>
#include <vector>

#include "framework/Test.hpp"
#include "geeyoou/widget/Widget.hpp"
#include "geeyoou/widget/Window.hpp"

using geeyoou::platform;
using geeyoou::TimerId;
using geeyoou::Window;

namespace {

// Every case has to leave the queue clean, or the next one pumps somebody
// else's ticks.  A tick budget rather than a wall clock: a test that hangs is
// worse than a test that fails.
constexpr int kIntervalMs = 10;
constexpr int kTickBudget = 40;

// Counts animation ticks through a pointer the CALLER owns: the probe is a
// child of the window under test, so it is destroyed with it and could not hold
// the count itself -- which is the whole point of the case below.
class TickProbe : public geeyoou::Widget {
 public:
  explicit TickProbe(int* ticks) : ticks_(ticks) {}

 protected:
  void onAnimationTick() override {
    if (ticks_) ++*ticks_;
  }

 private:
  int* ticks_ = nullptr;
};

}  // namespace

GEEYOOU_TEST(timer, start_hands_out_distinct_non_zero_ids) {
  std::vector<TimerId> ids;
  for (int i = 0; i < 4; ++i) {
    const TimerId id = platform().startTimer(1000, [] {});
    CHECK_NE(id, TimerId(0));
    ids.push_back(id);
  }
  for (std::size_t i = 0; i < ids.size(); ++i) {
    for (std::size_t j = i + 1; j < ids.size(); ++j) CHECK_NE(ids[i], ids[j]);
  }
  for (const TimerId id : ids) platform().stopTimer(id);
}

GEEYOOU_TEST(timer, stopping_something_that_is_not_running_is_harmless) {
  platform().stopTimer(0);  // the value a default-initialised member holds

  const TimerId id = platform().startTimer(1000, [] {});
  REQUIRE(id != 0);
  platform().stopTimer(id);
  platform().stopTimer(id);  // ids are never reused, so this names nothing

  // Two orders of magnitude past anything this process has handed out.
  platform().stopTimer(id + 1000000);
}

GEEYOOU_TEST(timer, a_stopped_timer_stops_firing) {
  // The defect in one case: `victim` stands in for a window's animation tick
  // and `driver` for the rest of the application.  Stopping the victim from
  // outside must silence it while the driver keeps running -- if stopTimer were
  // a no-op, victimTicks would keep climbing to the end of the budget.
  int victimTicks = 0;
  int driverTicks = 0;
  int victimTicksAtStop = -1;

  const TimerId victim = platform().startTimer(kIntervalMs, [&] { ++victimTicks; });
  REQUIRE(victim != 0);

  TimerId driver = 0;
  driver = platform().startTimer(kIntervalMs, [&] {
    ++driverTicks;
    if (driverTicks == 3) {
      platform().stopTimer(victim);
      victimTicksAtStop = victimTicks;
    }
    if (driverTicks >= 12 || driverTicks >= kTickBudget) {
      platform().stopTimer(driver);
      platform().quit(0);
    }
  });
  REQUIRE(driver != 0);

  platform().runEventLoop();

  CHECK_GE(driverTicks, 12);
  CHECK_GE(victimTicksAtStop, 0);  // the stop actually ran
  // The victim may legitimately have ticked once more between the stop and the
  // callback observing it, since WM_TIMER messages already in the queue are not
  // recalled -- but it must not have kept pace with the nine driver ticks that
  // followed.
  CHECK_LT(victimTicks, victimTicksAtStop + 2);
}

GEEYOOU_TEST(timer, a_second_windows_animation_clock_dies_with_the_window) {
  // What Window::~Window's stopTimer(animationTimer_) is FOR, and until now no
  // test reached it: nothing in the suite called enableAnimations(), so every
  // run took the stopTimer(0) early return and the real path was never
  // executed.  The clock lives in the platform and its callback captures the
  // window, so a window that dies with it running ticks into freed memory a few
  // milliseconds later.
  //
  // A SECOND window on purpose: the showcase only ever had one, and it outlives
  // main(), which is exactly why the defect could sit there unnoticed.
  int ticks = 0;
  int ticksAtClose = -1;
  int pumped = 0;

  auto win = std::make_unique<Window>("geeyoou animation lifetime", 200, 150);
  TickProbe* probe = win->add<TickProbe>(&ticks);
  probe->setGeometry({0.0f, 0.0f, 100.0f, 40.0f});
  win->enableAnimations(60);  // ~16ms, comfortably faster than the driver below
  CHECK(win->animationsEnabled());

  TimerId driver = 0;
  driver = platform().startTimer(kIntervalMs, [&] {
    ++pumped;
    if (pumped == 10) {
      ticksAtClose = ticks;
      win.reset();  // the window goes; its clock must go with it
    }
    // At least twenty more ticks of the pump AFTER the window is gone -- the
    // window's own clock is faster, so a surviving one would be obvious.
    if (pumped >= 40 || pumped >= kTickBudget) {
      platform().stopTimer(driver);
      platform().quit(0);
    }
  });
  REQUIRE(driver != 0);

  platform().runEventLoop();

  CHECK_GE(pumped, 40);
  CHECK_GE(ticksAtClose, 1);     // it really was animating while it was alive
  CHECK_EQ(ticks, ticksAtClose);  // ...and not one tick after it died
  CHECK(win == nullptr);
}

GEEYOOU_TEST(timer, a_timer_may_stop_itself_from_inside_its_own_callback) {
  // The lifetime this has to survive: stopTimer() erases the table entry the
  // running callback lives in.  Also the shape of "the animation tick closes
  // the window", which is how a modal dialog ends.
  int ticks = 0;
  TimerId self = 0;
  self = platform().startTimer(kIntervalMs, [&] {
    ++ticks;
    platform().stopTimer(self);
  });
  REQUIRE(self != 0);

  // A second timer owns termination, so a self-stop that silently failed shows
  // up as a wrong count instead of a hung test.
  int guardTicks = 0;
  TimerId guard = 0;
  guard = platform().startTimer(kIntervalMs, [&] {
    ++guardTicks;
    if (guardTicks >= 10) {
      platform().stopTimer(guard);
      platform().quit(0);
    }
  });
  REQUIRE(guard != 0);

  platform().runEventLoop();

  CHECK_EQ(ticks, 1);
  CHECK_GE(guardTicks, 10);
}
