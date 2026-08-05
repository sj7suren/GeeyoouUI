//
// E15/E16: what the three real containers do when something is taken out from
// under them -- REM3-RES-1, the half a frame guard cannot fix.
//
// The guards are FRAME-scoped: they buy the frame that is running one safe
// return, and they do not repair object state.  E14 added the notification
// (Widget::onDescendantDetached) but no container listened to it, so on the day
// E14 landed this was still true of every ScrollArea in the library:
//
//     sa->content()->parent()->removeChild(sa->content());
//     paintTree(...);   // <- heap-use-after-free, no application call in between
//
// E15 makes ScrollArea and AppWindow listen.  These cases are the acceptance,
// and they are written to fail in the ONE leg that can see this family.
//
// ⚠️ TWO SIGNALS, NOT ONE -- the lesson E14 paid for and the reason several
// assertions below look redundant.  A test that reads a freed member and throws
// the value away is not a test: `(void)sa->contentSize();` under RelWithDebInfo
// /O2 is a load with no consumer, the optimiser deletes it outright, and the
// case then FAILS on its own flag assertion while AddressSanitizer reports
// NOTHING.  Every read of a possibly-freed member below is therefore CONSUMED
// -- passed to CHECK_NEAR / CHECK_EQ -- so the load survives to the sanitiser.
// "The case went red" and "ASan went red" are two independent facts here, and
// this whole defect family is visible only in the second.
//
// The degradation table these assert is Elena's, recorded in
// docs/iterations/02-layout-engine.md section 11.12.  It is DELIBERATELY not a
// self-heal: a ScrollArea whose content was taken away answers nullptr for ever
// and never grows a replacement.  Reviving one silently would swap content()
// for a different widget than the one the application put there, which turns
// "it crashes" into "it quietly answers wrong" -- the trade this whole
// remediation exists to refuse.
//
#include <cstddef>
#include <memory>

#include "framework/Test.hpp"
#include "geeyoou/core/Event.hpp"
#include "geeyoou/core/Types.hpp"
#include "geeyoou/render/Canvas.hpp"
#include "geeyoou/render/Offscreen.hpp"
#include "geeyoou/render/Painter.hpp"
#include "geeyoou/widget/AppWindow.hpp"
#include "geeyoou/widget/BoxLayout.hpp"
#include "geeyoou/widget/Layout.hpp"
#include "geeyoou/widget/ScrollArea.hpp"
#include "geeyoou/widget/Widget.hpp"

using geeyoou::AppWindow;
using geeyoou::BoxLayout;
using geeyoou::Canvas;
using geeyoou::MouseAction;
using geeyoou::MouseButton;
using geeyoou::MouseEvent;
using geeyoou::OffscreenImage;
using geeyoou::Painter;
using geeyoou::Point;
using geeyoou::Rect;
using geeyoou::ScrollArea;
using geeyoou::Size;
using geeyoou::SizeHint;
using geeyoou::Widget;
using geeyoou::WindowHeader;

namespace {

constexpr float kEps = 0.0005f;

// --- the exercise a detached container has to survive -------------------------
//
// Item 5 of the acceptance: THREE paints, one wheel and one resize, in that
// order, with nothing in between that could repair anything.  Three rather than
// one because a container that repaired itself lazily on the first paint would
// pass a one-paint test; the second and third are what make "permanent" an
// assertion rather than a hope.
class PaintRig {
 public:
  PaintRig() : img_(240, 240) {}

  bool valid() const { return img_.valid(); }

  bool once(Widget& w) {
    const Rect whole(0.0f, 0.0f, 240.0f, 240.0f);
    Canvas canvas;
    if (!canvas.begin(img_.surface(), whole)) return false;
    Painter p = canvas.painter();
    w.paintTree(p, whole, whole);
    canvas.end();
    return true;
  }

 private:
  OffscreenImage img_;
};

MouseEvent wheelAt(float x, float y, float notches) {
  MouseEvent e;
  e.action = MouseAction::Wheel;
  e.button = MouseButton::None;
  e.windowPos = {x, y};
  e.wheelDelta = notches;
  return e;
}

// Every read below is consumed by an assertion.  Factored out because the same
// six answers are the acceptance for BOTH reproducers, and a second hand-written
// copy is a second place to forget one.
//
// `ctx_` is the harness's per-case object; taking it as a parameter is what lets
// the CHECK macros work from a free function.
void checkDegradedAnswers(geeyoou::test::Context& ctx_, ScrollArea& sa,
                          float expectedViewportW, float expectedViewportH) {
  CHECK_EQ(sa.content(), static_cast<Widget*>(nullptr));

  const Size cs = sa.contentSize();
  CHECK_NEAR(cs.width, 0.0f, kEps);
  CHECK_NEAR(cs.height, 0.0f, kEps);

  const Point off = sa.scrollOffset();
  CHECK_NEAR(off.x, 0.0f, kEps);
  CHECK_NEAR(off.y, 0.0f, kEps);

  // viewportSize() is private, so the contract "no content => no bars => the
  // viewport is the whole local rect" is read through the one public thing that
  // depends on it: with no bars there is nothing to scroll to, so scrollTo()
  // must clamp everything to the origin.
  sa.scrollTo({500.0f, 500.0f});
  const Point after = sa.scrollOffset();
  CHECK_NEAR(after.x, 0.0f, kEps);
  CHECK_NEAR(after.y, 0.0f, kEps);

  // ...and the area itself did not move, which is the other half of "relayout()
  // returns without writing any geometry".
  CHECK_NEAR(sa.geometry().width(), expectedViewportW, kEps);
  CHECK_NEAR(sa.geometry().height(), expectedViewportH, kEps);
}

// --- probes -------------------------------------------------------------------

// Counts announcements and remembers the last one.  REM3-G9 says an override
// may null its own member pointers and do nothing else; a counter is what
// CachingBox in test_removal.cpp already does, and it reaches no application
// code -- no signal, no update(), no removal, no virtual call.
class Recorder : public Widget {
 public:
  int notifications = 0;
  Widget* lastNode = nullptr;

 protected:
  void onDescendantDetached(Widget* node) override {
    ++notifications;
    lastNode = node;
  }
};

// Counts into memory the CALLER owns, so the count survives the widget.  The
// whole point of the ~Widget case is to read the counter after the tree is
// gone, which a member could not answer.
class OutboardRecorder : public Widget {
 public:
  explicit OutboardRecorder(int* counter) : counter_(counter) {}

 protected:
  void onDescendantDetached(Widget*) override { ++*counter_; }

 private:
  int* counter_;
};

// Takes the scroll area's content out of the tree from inside a door, and KEEPS
// IT ALIVE.  That combination is the whole point: it produces the third state
// E14 created -- the object the member named is perfectly alive, and the member
// no longer names it -- which is the only thing that can reach the `content_ !=
// ct0` half of a checkpoint.  A removeChild here would kill the object too, and
// then the cursor half of the same condition would fire first and the member
// re-read would still never be the reason.
class ContentThief : public Widget {
 public:
  ContentThief(ScrollArea* sa, std::unique_ptr<Widget>* parked)
      : sa_(sa), parked_(parked) {}

  // ARMED EXPLICITLY, and that is not decoration.  Building the tree already
  // runs the door under test: add<T> calls childAppended(), addWidget() marks
  // the chain dirty, and either one runs a pass that measures this widget.  An
  // unarmed thief would therefore steal the content during SETUP, before the
  // case had captured the pointers it is going to compare against -- which is
  // exactly what the first version of this file did, and the symptom was a case
  // that failed with `contentBefore == nullptr`.
  void arm() const { armed_ = true; }

  void steal() const {
    if (!armed_ || fired_) return;
    Widget* c = sa_->content();
    if (!c || !c->parent()) return;
    fired_ = true;
    *parked_ = c->parent()->takeChild(c);
  }

  bool fired() const { return fired_; }

 private:
  ScrollArea* sa_;
  std::unique_ptr<Widget>* parked_;
  mutable bool armed_ = false;
  mutable bool fired_ = false;
};

// ...through the MEASUREMENT door (ScrollArea.cpp, relayout, CP-S1).
class MeasureThief : public ContentThief {
 public:
  using ContentThief::ContentThief;

  SizeHint sizeHint() const override {
    steal();
    return SizeHint{Size{40.0f, 40.0f}, Size{40.0f, 40.0f},
                    Size{geeyoou::kUnbounded, geeyoou::kUnbounded}};
  }
};

// ...and through the ARRANGE door that setContentSize opens (CP-C1): the
// content's own layout places this child, and placing it runs its
// onGeometryChanged, which is application code by definition.
class ArrangeThief : public ContentThief {
 public:
  using ContentThief::ContentThief;

  SizeHint sizeHint() const override {
    return SizeHint{Size{40.0f, 40.0f}, Size{40.0f, 40.0f},
                    Size{geeyoou::kUnbounded, geeyoou::kUnbounded}};
  }

 protected:
  void onGeometryChanged() override { steal(); }
};

}  // namespace

// ============================================================ E16 item 1+5+6 ===
//
// REPRODUCER ONE, VERBATIM: the content is taken out through its own parent,
// which is what an application that reaches for content()->parent() writes.
GEEYOOU_TEST(detach_notify, a_scrollarea_that_lost_its_content_survives_paint_wheel_and_resize) {
  ScrollArea sa;
  sa.setGeometry({0.0f, 0.0f, 200.0f, 150.0f});
  sa.setContentSize({600.0f, 900.0f});   // both bars, so there is state to lose
  sa.scrollTo({50.0f, 80.0f});
  REQUIRE(sa.content() != nullptr);
  CHECK_NEAR(sa.scrollOffset().y, 80.0f, kEps);

  PaintRig rig;
  REQUIRE(rig.valid());
  // Healthy first, so nothing below can pass because the paint never happened.
  REQUIRE(rig.once(sa));

  // ------------------------------------------------------------------ the line
  sa.content()->parent()->removeChild(sa.content());

  // POSITIVE, not "it did not crash": the hook has already run, before any
  // repaint, and the container has already forgotten the pointer.
  CHECK_EQ(sa.content(), static_cast<Widget*>(nullptr));

  // Item 5: three paints, a wheel and a resize, with no repair in between.
  // Every one of these reaches contentSize() -> content_->geometry() in the
  // library, which is the read that is a use-after-free without the override.
  REQUIRE(rig.once(sa));
  REQUIRE(rig.once(sa));
  REQUIRE(rig.once(sa));
  sa.dispatchMouse(wheelAt(20.0f, 20.0f, 1.0f));
  sa.setGeometry({0.0f, 0.0f, 260.0f, 190.0f});

  // Item 6: no self-heal.  Not "content() is still valid" -- content() is still
  // NULL, and no new widget appeared to take the old one's place.
  CHECK_EQ(sa.content(), static_cast<Widget*>(nullptr));
  CHECK_EQ(sa.children().size(), std::size_t(1));       // the viewport, alone
  CHECK_EQ(sa.children()[0]->children().size(), std::size_t(0));

  checkDegradedAnswers(ctx_, sa, 260.0f, 190.0f);
}

// ============================================================== E16 item 1+7 ===
//
// REPRODUCER TWO, VERBATIM: the VIEWPORT is taken, which is reachable from any
// caller through children().  Both members have to end up null -- content_ is
// the grandchild, and it is announced by the same walk without anybody naming
// it.
GEEYOOU_TEST(detach_notify, taking_the_viewport_clears_both_of_a_scrollareas_pointers) {
  ScrollArea sa;
  sa.setGeometry({0.0f, 0.0f, 200.0f, 150.0f});
  sa.setContentSize({600.0f, 900.0f});
  REQUIRE(sa.children().size() == std::size_t(1));

  PaintRig rig;
  REQUIRE(rig.valid());
  REQUIRE(rig.once(sa));

  // ------------------------------------------------------------------ the line
  sa.removeChild(sa.children()[0].get());

  CHECK_EQ(sa.children().size(), std::size_t(0));
  CHECK_EQ(sa.content(), static_cast<Widget*>(nullptr));
  // viewport_ is private, so its nullness is read through the one public method
  // that touches nothing else: a non-null viewport_ here names freed memory, so
  // this answer is either 0,0 or an ASan report.
  const Point off = sa.scrollOffset();
  CHECK_NEAR(off.x, 0.0f, kEps);
  CHECK_NEAR(off.y, 0.0f, kEps);

  REQUIRE(rig.once(sa));
  REQUIRE(rig.once(sa));
  REQUIRE(rig.once(sa));
  sa.dispatchMouse(wheelAt(20.0f, 20.0f, -1.0f));
  sa.setGeometry({0.0f, 0.0f, 300.0f, 220.0f});

  CHECK_EQ(sa.children().size(), std::size_t(0));  // and no replacement viewport
  checkDegradedAnswers(ctx_, sa, 300.0f, 220.0f);

  // ensureVisible / setContentSize are the two remaining entry points in the
  // table; both must return without writing anything.
  sa.ensureVisible({0.0f, 0.0f, 400.0f, 400.0f});
  sa.setContentSize({1000.0f, 1000.0f});
  CHECK_NEAR(sa.contentSize().width, 0.0f, kEps);
  CHECK_NEAR(sa.contentSize().height, 0.0f, kEps);
  CHECK_EQ(sa.children().size(), std::size_t(0));
}

// ================================================================ E16 item 2+3 ===
//
// The announcement reaches an ancestor THREE levels above the departing node,
// and it names that node.  This is the property a parent-only notification
// fails -- and every cached pointer in the library's containers is at least a
// grandchild.
GEEYOOU_TEST(detach_notify, the_announcement_climbs_the_whole_ancestor_chain) {
  Widget root;
  Recorder* rec = root.add<Recorder>();
  Widget* a = rec->add<Widget>();
  Widget* b = a->add<Widget>();
  Widget* c = b->add<Widget>();
  REQUIRE(c->depth() == rec->depth() + 3);

  CHECK_EQ(rec->notifications, 0);
  b->removeChild(c);

  // POSITIVE: it fired, exactly once, and it carried the node three levels down
  // rather than the child it went through.
  CHECK_EQ(rec->notifications, 1);
  CHECK_EQ(rec->lastNode, c);

  // ...and every ancestor got its own copy, which is what "broadcast" means.
  // `a` and `b` are plain widgets whose default override does nothing; the one
  // that matters is that `rec` -- three levels up, and the only one that caches
  // anything in the real containers -- was reached at all.
  Recorder* deep = b->add<Recorder>();
  Widget* leaf = deep->add<Widget>();
  Widget* leafChild = leaf->add<Widget>();
  rec->notifications = 0;
  rec->lastNode = nullptr;

  deep->removeChild(leaf);
  // Two nodes departed (leaf and its child), so both the recorder inside the
  // subtree's parent and the recorder at the top saw two announcements each.
  CHECK_EQ(deep->notifications, 2);
  CHECK_EQ(rec->notifications, 2);
  CHECK_EQ(rec->lastNode, leafChild);  // the deeper one is announced second
}

// ================================================================== E16 item 4 ===
//
// ~Widget does NOT announce, and it must not: a container being destroyed owns
// its children, they are destroyed immediately after its body runs, and telling
// a dying object that its member is about to die is work with no reader.  The
// completeness argument for the whole mechanism rests on this -- a subtree
// leaves a LIVING tree only through takeChild -- so it is asserted rather than
// assumed.
GEEYOOU_TEST(detach_notify, destruction_announces_nothing) {
  int announcements = 0;

  {
    Widget root;
    OutboardRecorder* rec = root.add<OutboardRecorder>(&announcements);
    Widget* mid = rec->add<Widget>();
    mid->add<Widget>();
    // Nothing is removed.  The whole tree dies with `root` at the closing brace.
  }
  CHECK_EQ(announcements, 0);

  // The contrast, so the zero above cannot be a probe that never worked: the
  // same shape, one explicit removal, two announcements (the node and its
  // child).
  int removals = 0;
  {
    Widget root;
    OutboardRecorder* rec = root.add<OutboardRecorder>(&removals);
    Widget* mid = rec->add<Widget>();
    mid->add<Widget>();
    rec->removeChild(mid);
    CHECK_EQ(removals, 2);
  }
  CHECK_EQ(removals, 2);  // and the destruction that followed added nothing
}

// ==================================================================== E15: AppWindow ===
//
// The three null tests in AppWindow::relayout() have been in the source since it
// was written and no line of code could satisfy them.  E15 changes nothing in
// that function; it makes the state it already describes reachable.  That is
// why the assertions below are about relayout() NOT crashing and NOT writing,
// and why the diff of AppWindow.cpp contains no change to it.
GEEYOOU_TEST(detach_notify, an_appwindow_that_lost_its_content_lays_out_and_paints) {
  AppWindow win("geeyoou detach notify", 400, 300);
  REQUIRE(win.header() != nullptr);
  REQUIRE(win.content() != nullptr);

  Widget* fill = win.setContent<Widget>();
  REQUIRE(fill != nullptr);
  CHECK_NEAR(fill->geometry().width(), 398.0f, kEps);

  // Take the FILL first -- a grandchild of the window, exactly the shape
  // ScrollArea::content_ has.  `fill_` is the member that goes null; header_ and
  // content_ are untouched, so relayout() takes its normal path.
  const float hh = win.header()->height();
  win.content()->removeChild(fill);
  win.relayout();
  CHECK_NEAR(win.content()->geometry().height(), 298.0f - hh, kEps);
  CHECK_EQ(win.content()->children().size(), std::size_t(0));

  // setContent<T> on a live content area still works -- the guard E15 adds is
  // for the null case below, and it must not have cost the normal one anything.
  Widget* again = win.setContent<Widget>();
  REQUIRE(again != nullptr);
  CHECK_NEAR(again->geometry().width(), 398.0f, kEps);

  // Now the content area itself.  Both content_ and fill_ (its child) are
  // announced by the same walk.
  win.removeChild(win.content());
  CHECK_EQ(win.content(), static_cast<Widget*>(nullptr));

  // The dead branch, now alive: relayout() returns at its first line and writes
  // nothing.  The header keeps the geometry it had.
  const Rect headerBefore = win.header()->geometry();
  win.relayout();
  CHECK_NEAR(win.header()->geometry().x(), headerBefore.x(), kEps);
  CHECK_NEAR(win.header()->geometry().width(), headerBefore.width(), kEps);

  // setContent<T> with no content area answers nullptr instead of dereferencing
  // one.  THE VALUE IS CONSUMED -- see the file header.
  Widget* orphan = win.setContent<Widget>();
  CHECK_EQ(orphan, static_cast<Widget*>(nullptr));

  // ...and no self-heal here either: nothing grew a replacement content area.
  CHECK_EQ(win.content(), static_cast<Widget*>(nullptr));

  PaintRig rig;
  REQUIRE(rig.valid());
  REQUIRE(rig.once(win));
  REQUIRE(rig.once(win));
  REQUIRE(rig.once(win));
  win.setHeaderVisible(false);
  win.setBorderVisible(false);
  CHECK_EQ(win.content(), static_cast<Widget*>(nullptr));
}

// E15's own blast radius, closed one round late.  header_ could not be null
// before E15 -- it was assigned in the constructor and never written again --
// so this slot dereferenced it unconditionally and was right to.  E15 made the
// member nullable and did not revisit the slot, leaving a null dereference on a
// path an application reaches BY PRESSING THE MAXIMISE BUTTON.
//
// Degrading from a dangling pointer to a null one is an improvement (a
// deterministic crash beats a use-after-free), but "crashes every time you
// maximise" is not a resting state.
//
// This case goes RED without the null test: not a sanitiser report, a straight
// access violation, so it is visible on all three legs rather than just the
// ASan one -- unlike the rest of this file.
GEEYOOU_TEST(detach_notify, an_appwindow_that_lost_its_header_survives_a_maximize_change) {
  AppWindow win("geeyoou detach notify", 400, 300);
  REQUIRE(win.header() != nullptr);
  REQUIRE(win.content() != nullptr);

  // Control group FIRST, so the guard cannot pass by turning the slot into a
  // no-op: with a header present the state still tracks.  maximizedChanged is
  // emitted here exactly as the platform emits it (Window.cpp's
  // onWindowStateChanged hook is one line: maximizedChanged.emit(isMaximized())),
  // which keeps the case free of a real window-manager round trip.
  win.maximizedChanged.emit(true);
  CHECK(win.header()->isMaximized());
  win.maximizedChanged.emit(false);
  CHECK(!win.header()->isMaximized());

  const Rect contentBefore = win.content()->geometry();

  // Take the title bar out of the tree.  header() answers nullptr from here on.
  win.removeChild(win.header());
  CHECK_EQ(win.header(), static_cast<WindowHeader*>(nullptr));

  // THE READ THAT USED TO CRASH.  Both edges, because the slot is not
  // symmetric-by-construction and a guard that only covered one of them would
  // still leave the other live.
  win.maximizedChanged.emit(true);
  win.maximizedChanged.emit(false);

  // Nothing was rebuilt -- REM3-G9 / the no-self-heal rule.
  CHECK_EQ(win.header(), static_cast<WindowHeader*>(nullptr));

  // And relayout() stayed at its first line rather than half-laying-out a
  // window with no header: the content area keeps the geometry it had.  The
  // values are CONSUMED, per the file header.
  const Rect contentAfter = win.content()->geometry();
  CHECK_NEAR(contentAfter.x(), contentBefore.x(), kEps);
  CHECK_NEAR(contentAfter.y(), contentBefore.y(), kEps);
  CHECK_NEAR(contentAfter.width(), contentBefore.width(), kEps);
  CHECK_NEAR(contentAfter.height(), contentBefore.height(), kEps);

  // The window still paints, three times, with no header in it.
  PaintRig rig;
  REQUIRE(rig.valid());
  REQUIRE(rig.once(win));
  REQUIRE(rig.once(win));
  REQUIRE(rig.once(win));
  CHECK_EQ(win.header(), static_cast<WindowHeader*>(nullptr));
}

// ============================================== B7: the member re-read branches ===
//
// docs section 11.11 forecast that E15 would give CP-S1 / CP-C1's `viewport_ !=
// vp0` / `content_ != ct0` sub-branches their first coverage, and recorded that
// E14 alone could not: nothing outside ScrollArea can write those members, so
// before the override existed the sub-branch was unreachable code.
//
// These two cases are that forecast, checked.  Each one changes the member from
// INSIDE a door while leaving the object it named ALIVE -- parked in a
// unique_ptr the case owns -- so the three cursor halves of the condition are
// all true and the member re-read is the only thing left that can be the
// reason.
GEEYOOU_TEST(detach_notify, a_measurement_that_steals_the_content_degrades_on_the_member_re_read) {
  ScrollArea sa;
  std::unique_ptr<Widget> parked;

  BoxLayout* box =
      sa.content()->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
  MeasureThief* thief = sa.content()->add<MeasureThief>(&sa, &parked);
  box->addWidget(thief);
  Widget* const contentBefore = sa.content();
  Widget* const viewportBefore = sa.children()[0].get();

  geeyoou::detail::resetLayoutDiagnostics();
  CHECK_EQ(int(geeyoou::detail::layoutDiagnostics().framesDegraded), 0);
  CHECK(!thief->fired());  // the setup above already measured it, harmlessly
  thief->arm();

  // CP-S1's door is `content_->sizeHint()` in ScrollArea::relayout, which a
  // resize reaches through onGeometryChanged.
  sa.setGeometry({0.0f, 0.0f, 200.0f, 150.0f});

  CHECK(thief->fired());
  // EXACTLY one, not "at least": REM3-G8 makes the unit a FRAME, one
  // setContentSize can honestly raise it twice (test_rem3_doors.cpp), and a
  // >= here would keep passing on the day the checkpoint above it started
  // firing for a second reason.
  CHECK_EQ(int(geeyoou::detail::layoutDiagnostics().framesDegraded), 1);

  // The three facts that make the member re-read the ONLY false predicate in
  // that condition: the scroll area is alive (it is a stack object of this
  // frame), the viewport is alive and unchanged, and the content object the
  // cursor was taken on is alive -- parked here rather than freed.
  CHECK_EQ(sa.children().size(), std::size_t(1));
  CHECK_EQ(sa.children()[0].get(), viewportBefore);
  REQUIRE(parked != nullptr);
  CHECK_EQ(parked.get(), contentBefore);
  CHECK_EQ(parked->parent(), static_cast<Widget*>(nullptr));
  // Alive, and readable -- consumed, so the read is not optimised away.
  CHECK_GE(parked->children().size(), std::size_t(1));

  // ...and the member really did change, which is the other end of the same
  // statement.
  CHECK_EQ(sa.content(), static_cast<Widget*>(nullptr));

  // The area degraded rather than wrote: nothing placed the viewport at the new
  // size, because relayout returned at CP-S1.
  PaintRig rig;
  REQUIRE(rig.valid());
  REQUIRE(rig.once(sa));
  checkDegradedAnswers(ctx_, sa, 200.0f, 150.0f);
}

GEEYOOU_TEST(detach_notify, an_arrange_that_steals_the_content_degrades_on_the_member_re_read) {
  ScrollArea sa;
  std::unique_ptr<Widget> parked;

  BoxLayout* box =
      sa.content()->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
  ArrangeThief* thief = sa.content()->add<ArrangeThief>(&sa, &parked);
  box->addWidget(thief);
  Widget* const contentBefore = sa.content();

  // The scroll area is deliberately left with NO geometry of its own, so the
  // only thing that can size the content is the setContentSize below.  That is
  // what keeps this case on the setContentSize door rather than the relayout
  // one the previous case uses -- the two are different checkpoints.
  geeyoou::detail::resetLayoutDiagnostics();
  CHECK_EQ(int(geeyoou::detail::layoutDiagnostics().framesDegraded), 0);
  CHECK(!thief->fired());
  thief->arm();

  // CP-C1's door is `content_->setGeometry(...)` in ScrollArea::setContentSize.
  // The content owns a layout, so that call arranges the thief, and arranging it
  // runs its onGeometryChanged -- application code, by the definition in
  // section 11.4.
  sa.setContentSize({500.0f, 700.0f});

  CHECK(thief->fired());
  // EXACTLY one, not "at least": REM3-G8 makes the unit a FRAME, one
  // setContentSize can honestly raise it twice (test_rem3_doors.cpp), and a
  // >= here would keep passing on the day the checkpoint above it started
  // firing for a second reason.
  CHECK_EQ(int(geeyoou::detail::layoutDiagnostics().framesDegraded), 1);

  REQUIRE(parked != nullptr);
  CHECK_EQ(parked.get(), contentBefore);
  CHECK_EQ(parked->parent(), static_cast<Widget*>(nullptr));
  CHECK_GE(parked->children().size(), std::size_t(1));
  CHECK_EQ(sa.content(), static_cast<Widget*>(nullptr));
  CHECK_EQ(sa.children().size(), std::size_t(1));  // the viewport stayed

  PaintRig rig;
  REQUIRE(rig.valid());
  REQUIRE(rig.once(sa));
  CHECK_NEAR(sa.contentSize().width, 0.0f, kEps);
  CHECK_NEAR(sa.contentSize().height, 0.0f, kEps);
}
