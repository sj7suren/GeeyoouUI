//
// Tooltip: the hover hint the Window draws, plus the side table that holds the
// text.
//
// TWO things are under test and they fail in different ways:
//
//  1. Storage.  A tooltip is NOT a Widget member -- the R2 size budget asserted
//     in Widget.cpp is fully spent, so the text lives in a side table keyed by
//     the widget pointer (Widget.cpp).  The risk that table introduces is a
//     stale entry: a widget freed while its address is still a key would hand
//     the next widget allocated at that address a tooltip it never set.
//     ~Widget erases the entry; the reuse case below is written to go red under
//     AddressSanitizer AND on a plain value check if that erase regresses.
//
//  2. Lifetime.  Arming a tooltip starts a real timer in the platform whose
//     callback captures the Window.  A window destroyed while that timer is
//     pending -- the cursor rested on a tooltip widget, then the window closed
//     before the delay elapsed -- must stop it in ~Window, or the show fires
//     into freed memory a few hundred milliseconds later.  Same defect shape,
//     and same test shape, as the animation-clock case in test_platform_timer.
//
#include <cstddef>
#include <memory>
#include <string>

#include "framework/Test.hpp"
#include "geeyoou/core/Event.hpp"
#include "geeyoou/core/Types.hpp"
#include "geeyoou/platform/Platform.hpp"
#include "geeyoou/widget/Widget.hpp"
#include "geeyoou/widget/Window.hpp"

using geeyoou::MouseAction;
using geeyoou::MouseButton;
using geeyoou::MouseEvent;
using geeyoou::platform;
using geeyoou::Point;
using geeyoou::TimerId;
using geeyoou::Widget;
using geeyoou::Window;

namespace {

class TestWindow : public geeyoou::Window {
 public:
  TestWindow() : Window("geeyoou tooltip test", 480, 360) {}
  using Window::handleMouse;
};

MouseEvent moveTo(float x, float y) {
  MouseEvent e;
  e.action = MouseAction::Move;
  e.button = MouseButton::None;
  e.windowPos = {x, y};
  return e;
}

// Pumps the real event loop for roughly `ms`, then quits.  A driver timer is
// the clock: the tooltip's own delay is what we are waiting OUT, so a second
// timer we own decides when enough of it has passed.
void pumpFor(int ms) {
  const int interval = 10;
  int ticks = 0;
  const int budget = ms / interval + 1;
  TimerId driver = 0;
  driver = platform().startTimer(interval, [&] {
    if (++ticks >= budget) {
      platform().stopTimer(driver);
      platform().quit(0);
    }
  });
  platform().runEventLoop();
}

}  // namespace

// --------------------------------------------------------------- storage ------
GEEYOOU_TEST(tooltip, set_get_clear_round_trips_through_the_side_table) {
  auto w = std::make_unique<Widget>();
  CHECK(!w->hasTooltip());
  CHECK_EQ(w->tooltip(), std::string());

  w->setTooltip("启动泵组");
  CHECK(w->hasTooltip());
  CHECK_EQ(w->tooltip(), std::string("启动泵组"));

  w->setTooltip("停止泵组");  // overwrite, not append
  CHECK_EQ(w->tooltip(), std::string("停止泵组"));

  w->setTooltip("");  // empty clears
  CHECK(!w->hasTooltip());
  CHECK_EQ(w->tooltip(), std::string());
}

GEEYOOU_TEST(tooltip, resolves_to_the_nearest_ancestor_that_has_one) {
  // A tooltip on a container covers its whole area unless a child sets its own.
  // The Window walks up from the hovered widget; this checks the walk's data
  // source directly -- child wins over parent, and a childless gap falls
  // through to the ancestor.
  TestWindow win;
  Widget* group = win.add<Widget>();
  Widget* labelled = group->add<Widget>();
  Widget* bare = group->add<Widget>();

  group->setTooltip("机组面板");
  labelled->setTooltip("液位");

  CHECK_EQ(labelled->tooltip(), std::string("液位"));
  CHECK(!bare->hasTooltip());
  CHECK_EQ(group->tooltip(), std::string("机组面板"));
}

GEEYOOU_TEST(tooltip, a_freed_widget_leaves_no_entry_behind) {
  // The side table is keyed by the raw pointer.  If ~Widget did not erase, the
  // NEXT widget the allocator hands the same address would inherit a tooltip it
  // never set.  Forcing a reuse is not guaranteed, but destroying with an entry
  // live is exactly the heap-use-after-free ASan is watching for.
  auto first = std::make_unique<Widget>();
  first->setTooltip("旧值");
  CHECK(first->hasTooltip());
  first.reset();  // entry must be gone with it

  auto second = std::make_unique<Widget>();
  CHECK(!second->hasTooltip());  // did not inherit first's entry
  CHECK_EQ(second->tooltip(), std::string());
}

// --------------------------------------------------------------- lifetime -----
GEEYOOU_TEST(tooltip, hovering_a_tooltip_widget_shows_a_bubble_after_the_delay) {
  TestWindow win;
  Widget* w = win.add<Widget>();
  w->setGeometry({40.0f, 40.0f, 160.0f, 40.0f});
  w->setTooltip("回路 A");

  CHECK(!win.isTooltipVisible());
  win.handleMouse(moveTo(120.0f, 60.0f));  // rest on it -> arms the timer
  CHECK(!win.isTooltipVisible());          // not yet -- there is a delay

  pumpFor(900);  // comfortably past the ~600ms rest delay
  CHECK(win.isTooltipVisible());

  win.handleMouse(moveTo(300.0f, 300.0f));  // move off -> hides at once
  CHECK(!win.isTooltipVisible());
}

GEEYOOU_TEST(tooltip, a_press_dismisses_a_shown_tooltip) {
  TestWindow win;
  Widget* w = win.add<Widget>();
  w->setGeometry({40.0f, 40.0f, 160.0f, 40.0f});
  w->setTooltip("回路 B");

  win.handleMouse(moveTo(120.0f, 60.0f));
  pumpFor(900);
  CHECK(win.isTooltipVisible());

  MouseEvent press;
  press.action = MouseAction::Press;
  press.button = MouseButton::Left;
  press.windowPos = {120.0f, 60.0f};
  win.handleMouse(press);
  CHECK(!win.isTooltipVisible());
}

GEEYOOU_TEST(tooltip, a_window_closed_with_a_tooltip_pending_does_not_tick_into_freed_memory) {
  // The window is destroyed WHILE the rest-delay timer is still pending -- the
  // cursor armed it, then the window went away before the delay elapsed.  If
  // ~Window did not stop the timer, its show callback would fire a few hundred
  // milliseconds later through the freed Window.  Destroyed from inside the
  // pump, then the pump runs on well past the delay: ASan is the real assertion.
  auto win = std::make_unique<TestWindow>();
  Widget* w = win->add<Widget>();
  w->setGeometry({40.0f, 40.0f, 160.0f, 40.0f});
  w->setTooltip("回路 C");
  win->handleMouse(moveTo(120.0f, 60.0f));  // arm; ~600ms out

  const int interval = 10;
  int ticks = 0;
  bool closed = false;
  TimerId driver = 0;
  driver = platform().startTimer(interval, [&] {
    ++ticks;
    if (ticks == 3) {  // ~30ms -- timer still pending
      win.reset();
      closed = true;
    }
    if (ticks >= 100) {  // ~1s -- well past when the tooltip would have fired
      platform().stopTimer(driver);
      platform().quit(0);
    }
  });
  platform().runEventLoop();

  CHECK(closed);
  CHECK_GE(ticks, 100);
}
