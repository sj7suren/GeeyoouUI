//
// What the five REM3 checkpoints actually RETURN, and how often giving up is
// counted.
//
// The reproducers for the five doors live in the soak (test_layout_soak.cpp):
// they run every cycle, and what they prove is that the process survives and
// that nothing accumulates.  That is the expensive half of the question and it
// is deliberately phrased as a negative -- nothing crashed, nothing grew.
//
// The two cases here are the positive half, and neither of them fits in a soak:
//
//   * CP-G1's degraded ANSWER is written down to the last float in section 11.3
//     of docs/iterations/02-layout-engine.md, and until now nothing had ever
//     read it.  A degraded frame returns a value that looks like a value; the
//     only thing that distinguishes "gave up correctly" from "computed
//     nonsense out of a freed object" is what the value IS.
//   * framesDegraded's unit is a FRAME (REM3-G8), and that is not the same as
//     an operation and not the same as a door.  One setContentSize can honestly
//     raise it twice.  That was true from the day E3/E4 landed and was written
//     down nowhere, so the obvious assertion -- "one degradation per door" --
//     was waiting to be written and to be wrong.
//
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include "framework/Test.hpp"
#include "geeyoou/widget/AppWindow.hpp"
#include "geeyoou/widget/BoxLayout.hpp"
#include "geeyoou/widget/GroupBox.hpp"
#include "geeyoou/widget/Layout.hpp"
#include "geeyoou/widget/ScrollArea.hpp"
#include "geeyoou/widget/Widget.hpp"
#include "geeyoou/widget/Window.hpp"
#include "geeyoou/widget/WindowHeader.hpp"

using geeyoou::AppWindow;
using geeyoou::BoxLayout;
using geeyoou::GroupBox;
using geeyoou::Rect;
using geeyoou::ScrollArea;
using geeyoou::Size;
using geeyoou::SizeHint;
using geeyoou::Widget;

namespace {

// The frame GroupBox cuts out of itself, restated here because the constants
// are private to GroupBox.cpp.  RESTATED ON PURPOSE rather than exported: these
// four numbers are section 11.3's contract for what a dead GroupBox answers,
// and a test that imported them from the implementation would agree with
// whatever the implementation did.
constexpr float kInsetX = 12.0f;
constexpr float kInsetBottom = 12.0f;
constexpr float kTopPlain = 12.0f;
constexpr float kTopTitled = 34.0f;

class Sized : public Widget {
 public:
  explicit Sized(float w = 30.0f, float h = 20.0f) : w_(w), h_(h) {}

  SizeHint sizeHint() const override {
    return SizeHint{Size{w_, h_}, Size{w_, h_},
                    Size{geeyoou::kUnbounded, geeyoou::kUnbounded}};
  }

 private:
  float w_;
  float h_;
};

// Fires once from inside sizeHint() -- the measuring door.  Same shape as the
// soak's, including the reason the answer is built BEFORE the callback: the
// callback destroys an ancestor of this widget, so `this` is gone by the time
// it returns.
class HintHook : public Sized {
 public:
  HintHook() : Sized(30.0f, 20.0f) {}
  mutable std::function<void()> once;

  SizeHint sizeHint() const override {
    const SizeHint out = Sized::sizeHint();
    if (once) {
      std::function<void()> f;
      f.swap(once);
      f();
    }
    return out;
  }
};

// ...and one that waits for the RIGHT door.
//
// A ScrollArea::setContentSize reaches this widget's sizeHint() twice: once
// from inside the arrange that CP-C1's door starts, and once from the pure
// measurement relayout() issues afterwards.  Firing on a call COUNT would pin
// the case to today's call sequence, which is exactly the sort of assertion
// that turns into scenery when somebody adds a round.  The phase is what the
// case actually means, and the engine already publishes it: inside the arrange
// a layout pass is on the stack, and inside relayout's measurement none is.
class SecondDoorHook : public Sized {
 public:
  SecondDoorHook() : Sized(30.0f, 20.0f) {}
  mutable std::function<void()> once;

  SizeHint sizeHint() const override {
    const SizeHint out = Sized::sizeHint();
    if (once && !geeyoou::detail::layoutPassActive()) {
      std::function<void()> f;
      f.swap(once);
      f();
    }
    return out;
  }
};

std::uint32_t degraded() {
  return geeyoou::detail::layoutDiagnostics().framesDegraded;
}

}  // namespace

// =========================================================== CP-G1 ==========
//
// Section 11.3, written out: `h.min == h.preferred == {frameW, frameH}`, `h.max`
// left at its default, `inner` and `titleW` counted as zero BY DEFINITION and
// never computed.
//
// The two halves of the case are the titled and the untitled box, and they are
// both here for one reason: they are what pins `frameH` to the value `top` had
// IN FRONT OF THE DOOR.  A degraded answer that had recomputed `top` after the
// door would have to read title_ out of a freed std::string, and the two halves
// would stop differing by the 22 pixels of title rule that separate them.
GEEYOOU_TEST(rem3_doors, a_dead_group_box_answers_with_its_frame_and_nothing_else) {
  geeyoou::detail::resetLayoutDiagnostics();

  // --- titled ---------------------------------------------------------------
  {
    Widget root;
    GroupBox* box = root.add<GroupBox>();
    box->setTitle("参数");
    BoxLayout* inner = box->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
    HintHook* trigger = box->add<HintHook>();
    inner->addWidget(trigger, 1);
    inner->addWidget(box->add<Sized>(60.0f, 40.0f), 1);

    // Armed LAST: every addWidget above marks the tree dirty and runs a pass of
    // its own, and a hook armed earlier would fire in one of those.
    trigger->once = [&root, box] { root.removeChild(box); };

    const std::uint32_t before = degraded();
    const SizeHint dead = box->sizeHint();

    // `box` is a dangling pointer from here on and is never named again.
    CHECK(root.children().empty());
    CHECK_EQ(degraded(), before + 1);

    // The answer, to the float.  24 = 2 * kInsetX, 46 = kTopTitled +
    // kInsetBottom: all that is left of a GroupBox that no longer exists is the
    // frame it was drawing.
    CHECK_EQ(dead.min.width, 2.0f * kInsetX);
    CHECK_EQ(dead.min.height, kTopTitled + kInsetBottom);
    // preferred == min: monotone, and it never claims room for a widget that is
    // not there.  Asserted against min rather than against the literals, so
    // this line keeps meaning "the two agree" if the literals ever move.
    CHECK_EQ(dead.preferred.width, dead.min.width);
    CHECK_EQ(dead.preferred.height, dead.min.height);
    // max untouched.  A degraded frame that had built a SizeHint field by field
    // would most likely have left this at zero, which reads as "refuses to be
    // any size at all" and would collapse whatever box holds it.
    CHECK_EQ(dead.max.width, geeyoou::kUnbounded);
    CHECK_EQ(dead.max.height, geeyoou::kUnbounded);
  }

  // --- untitled -------------------------------------------------------------
  {
    Widget root;
    GroupBox* box = root.add<GroupBox>();
    BoxLayout* inner = box->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
    HintHook* trigger = box->add<HintHook>();
    inner->addWidget(trigger, 1);
    inner->addWidget(box->add<Sized>(60.0f, 40.0f), 1);
    trigger->once = [&root, box] { root.removeChild(box); };

    const std::uint32_t before = degraded();
    const SizeHint dead = box->sizeHint();

    CHECK(root.children().empty());
    CHECK_EQ(degraded(), before + 1);
    CHECK_EQ(dead.min.width, 2.0f * kInsetX);
    CHECK_EQ(dead.min.height, kTopPlain + kInsetBottom);
    CHECK_EQ(dead.preferred.width, dead.min.width);
    CHECK_EQ(dead.preferred.height, dead.min.height);
    CHECK_EQ(dead.max.width, geeyoou::kUnbounded);
    CHECK_EQ(dead.max.height, geeyoou::kUnbounded);
  }

  geeyoou::detail::resetLayoutDiagnostics();
}

// ================================================ framesDegraded's unit ======
//
// ONE call to setContentSize, TWO degraded frames, and both of them are right.
//
// The soak's scrollContent group kills the scroll area at CP-C1's door, so
// setContentSize gives up at its first checkpoint and raises the counter once.
// This case kills it one door later, and then the arithmetic is different:
//
//   * relayout() is a frame of its own.  Its content_->sizeHint() is the door,
//     CP-S1 finds the tree gone, and IT gives up -- one frame, one record;
//   * relayout() returns to setContentSize, which is a DIFFERENT frame that has
//     been standing on the same three pointers all along.  Its CP-C2 asks the
//     same five questions, gets the same answers, and gives up too -- a second
//     frame, a second record.
//
// REM3-G8 says once per FRAME, so two is not a double count; it is the rule
// working.  What it rules out is the assertion that looks natural and is wrong:
// framesDegraded is NOT the number of doors that were crossed, NOT the number
// of operations that failed, and NOT the number of objects that died.  Section
// 11.3 now says so; this is what holds it there.
GEEYOOU_TEST(rem3_doors, one_operation_can_degrade_two_frames) {
  geeyoou::detail::resetLayoutDiagnostics();

  Widget root;
  root.setGeometry({0.0f, 0.0f, 800.0f, 600.0f});

  ScrollArea* area = root.add<ScrollArea>();
  // Sized BEFORE the content gets a layout, so relayout() takes its
  // hand-placed branch during set-up and no measurement happens outside a pass
  // until the one this case is about.
  area->setGeometry({0.0f, 0.0f, 200.0f, 150.0f});

  Widget* content = area->content();
  BoxLayout* box = content->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
  SecondDoorHook* trigger = content->add<SecondDoorHook>();
  box->addWidget(trigger, 1);
  box->addWidget(content->add<Sized>(50.0f, 30.0f), 1);

  trigger->once = [&root, area] { root.removeChild(area); };

  const std::uint32_t before = degraded();
  area->setContentSize({320.0f, 260.0f});

  // `area`, `content` and `trigger` are all dangling from here on.
  CHECK(root.children().empty());
  CHECK_EQ(degraded(), before + 2);

  // ...and the give-up left nothing behind: both frames returned without
  // touching anything, the content's layout was parked while its measurement
  // was on the stack, and the park list drained on the way out.
  CHECK_EQ(geeyoou::detail::parkedLayoutCount(), std::size_t(0));
  CHECK_EQ(geeyoou::detail::deathWatchDepth(), std::size_t(0));

  geeyoou::detail::resetLayoutDiagnostics();
}

// ======================================================= the W2 S1 doors ======
//
// Three doors from section 12.4's A group, closed in one round: N1
// (Layout::invalidate), N4 (Window::setFocusWidget) and #16
// (AppWindow::relayout).  What they have in common is only their grade; the
// three failure MODES are different, and each case below is shaped by the mode
// rather than by the checkpoint, because the mode is what a reader has to be
// able to reproduce.
//
//   * N1  -- the park list's first miss.  invalidate() runs with neither
//            layoutRunning_ nor buffersBusy_ set, so ~Widget DELETES the layout
//            instead of parking it and the frame's own object is gone.  Mode:
//            heap-use-after-free, ASan leg only.
//   * N4  -- Window::widgetDetached already clears focus_ for every removal, so
//            the mode here is NOT the one section 12.4 sketched.  What is left
//            is a widget destroyed WITHOUT a takeChild (an orphan, or a subtree
//            the application detached earlier and is now dropping), plus the
//            window dying in its own door.  Mode: heap-use-after-free.
//   * #16 -- E15's onDescendantDetached nulls content_ and fill_ BEFORE
//            anything is freed, so a lost content area is a NULL dereference,
//            not a dangling one, and it is visible on all three legs.  Losing
//            the window itself is still a use-after-free.
//
// Every read of a possibly-freed object below is consumed by a CHECK -- see the
// header of verify.bat and section 11.8's evidence standard.

namespace {

// N1's application layout.  onInvalidated() is a protected extension point with
// ZERO overrides anywhere in the library, examples included, so this subclass is
// the first thing in the process ever to make Layout::invalidate() cross a door.
//
// Derived from Layout itself rather than from BoxLayout, which is final -- and
// that is the right base anyway: P1's subject includes Layout precisely because
// an application may implement one, which is what N1 is about.
class SuicidalLayout : public geeyoou::Layout {
 public:
  std::function<void()> once;

 protected:
  SizeHint measure(const Widget&) const override {
    return SizeHint{Size{10.0f, 10.0f}, Size{10.0f, 10.0f},
                    Size{geeyoou::kUnbounded, geeyoou::kUnbounded}};
  }

  geeyoou::LayoutOverflow arrange(Widget& host, const Rect& content) override {
    for (const auto& child : host.children()) {
      child->setGeometry(content);
    }
    return {};
  }

  void onInvalidated() override {
    if (!once) return;
    std::function<void()> f;
    f.swap(once);
    f();
  }
};

// N4's focusable widget.  The base implementation calls update(), so it runs
// FIRST -- after the hook this object may not exist.
class FocusHook : public Widget {
 public:
  FocusHook() { setFocusPolicy(geeyoou::FocusPolicy::Click); }

  std::function<void()> onLoss;
  int gained = 0;
  int lost = 0;

 protected:
  void onFocusChanged(bool focused) override {
    Widget::onFocusChanged(focused);
    if (focused) {
      ++gained;
      return;
    }
    ++lost;
    if (!onLoss) return;
    std::function<void()> f;
    f.swap(onLoss);
    f();
  }
};

// #16's hook, in whichever of AppWindow::relayout's three setGeometry calls the
// case needs: as a trailing item in the header (door one) or as the content
// fill (door three).
class GeometryHook : public Widget {
 public:
  std::function<void()> once;

 protected:
  void onGeometryChanged() override {
    if (!once) return;
    std::function<void()> f;
    f.swap(once);
    f();
  }
};

}  // namespace

// ============================================================== N1 ============
//
// THE ONE PLACE IN THIS FAMILY WHERE THE PARK LIST DOES NOT CATCH THE FALL.
//
// Everywhere else a host that dies under a layout leaves the Layout object
// itself alive: ~Widget tests `layoutRunning_ || layout_->buffersBusy_` and
// parks instead of deleting.  Reached through setMargins / setSpacing / any
// subclass setter, BOTH of those flags are false -- there is no pass and no
// measurement -- so the unique_ptr runs and the object whose member function is
// on the stack is freed.  `if (host_)` on the next line is then a read of freed
// memory, and it is a read with a consumer: it decides whether performLayout()
// is called on what is by then a freed Widget as well.
//
// parkedLayoutCount() is asserted at zero afterwards for exactly that reason:
// it is what distinguishes "the park list saved it" from "the guard did".
GEEYOOU_TEST(rem3_doors, a_layout_that_loses_its_host_in_on_invalidated_stops_there) {
  geeyoou::detail::resetLayoutDiagnostics();

  Widget root;
  root.setGeometry({0.0f, 0.0f, 400.0f, 300.0f});
  Widget* host = root.add<Widget>();
  host->setGeometry({0.0f, 0.0f, 200.0f, 150.0f});

  host->add<Sized>(40.0f, 20.0f);
  SuicidalLayout* lay = host->setLayout<SuicidalLayout>();

  // Armed LAST: setLayout runs a pass of its own, and a hook armed any earlier
  // would fire during set-up rather than at the door under test.
  lay->once = [&root, host] { root.removeChild(host); };

  const std::uint32_t before = degraded();
  lay->setSpacing(9.0f);

  // `lay` and `host` are dangling from here on and are never named again.
  CHECK(root.children().empty());
  CHECK_EQ(degraded(), before + 1);
  // NOT parked -- see the comment above.  If this is ever 1, the case has
  // stopped testing what it was written for.
  CHECK_EQ(geeyoou::detail::parkedLayoutCount(), std::size_t(0));
  CHECK_EQ(geeyoou::detail::deathWatchDepth(), std::size_t(0));

  geeyoou::detail::resetLayoutDiagnostics();
}

// ============================================================== N4 ============
//
// Three blocks, and the FIRST one is why this case is shaped the way it is.
//
// Section 12.4 describes N4 as reachable today through SelectBase::
// onFocusChanged(false) -> close() -> openStateChanged.emit(false), with a slot
// destroying the widget that is about to receive the focus.  Walk that path and
// it does not arrive: destroying a widget that is IN THE TREE goes through
// Widget::takeChild -> announceDetached -> Window::widgetDetached, which has
// cleared focus_ since R1 -- per node of the departing subtree, so a grandchild
// is covered too.  Block one is that path, and it survives on an unguarded
// build.  The gap is narrower and it is real: a widget destroyed WITHOUT a
// takeChild -- an orphan, or a subtree detached earlier and dropped now -- is
// announced to nobody, and the window itself dying in its own door was never
// covered by that bookkeeping at all.
GEEYOOU_TEST(rem3_doors, a_focus_handover_that_loses_the_winner_stops_there) {
  geeyoou::detail::resetLayoutDiagnostics();

  // --- block 1: the removal path, which widgetDetached already covers --------
  {
    AppWindow win("geeyoou rem3 doors", 400, 300);
    REQUIRE(win.content() != nullptr);
    FocusHook* loser = win.content()->add<FocusHook>();
    FocusHook* winner = win.content()->add<FocusHook>();

    win.setFocusWidget(loser);
    CHECK_EQ(loser->gained, 1);

    loser->onLoss = [&win, winner] { win.content()->removeChild(winner); };

    const std::uint32_t before = degraded();
    win.setFocusWidget(winner);

    // widgetDetached got there first, so the member re-read is what this frame
    // gives up on -- not a cursor, and not a crash even before the guard.
    CHECK_EQ(win.focusWidget(), static_cast<Widget*>(nullptr));
    CHECK_EQ(degraded(), before + 1);
    CHECK_EQ(loser->lost, 1);
  }

  // --- block 2: an orphan winner, which nothing announces -------------------
  {
    AppWindow win("geeyoou rem3 doors", 400, 300);
    REQUIRE(win.content() != nullptr);
    FocusHook* loser = win.content()->add<FocusHook>();

    // Never added to any tree, so its destructor announces nothing to anybody:
    // focus_ keeps pointing at it and :166 makes a VIRTUAL CALL through it.
    std::unique_ptr<FocusHook> winner = std::make_unique<FocusHook>();
    FocusHook* winnerRaw = winner.get();

    win.setFocusWidget(loser);
    loser->onLoss = [&winner] { winner.reset(); };

    const std::uint32_t before = degraded();
    win.setFocusWidget(winnerRaw);

    CHECK_EQ(degraded(), before + 1);
    CHECK_EQ(loser->lost, 1);
    // NO SELF-HEAL, and no repair either: a guard is frame-scoped, so focus_
    // still names the widget that died.  Pointer VALUES are compared here and
    // the object is never touched -- the same rule cancelOn is written under.
    // Registered as a residue rather than quietly fixed.
    CHECK_EQ(win.focusWidget(), static_cast<Widget*>(winnerRaw));
  }

  // --- block 3: the window itself dies in its own door ----------------------
  {
    auto win = std::make_unique<AppWindow>("geeyoou rem3 doors", 400, 300);
    REQUIRE(win->content() != nullptr);
    FocusHook* loser = win->content()->add<FocusHook>();
    FocusHook* winner = win->content()->add<FocusHook>();

    win->setFocusWidget(loser);
    AppWindow* raw = win.get();
    loser->onLoss = [&win] { win.reset(); };

    const std::uint32_t before = degraded();
    raw->setFocusWidget(winner);

    // `raw`, `loser` and `winner` are all dangling from here on.
    CHECK_EQ(win.get(), static_cast<AppWindow*>(nullptr));
    CHECK_EQ(degraded(), before + 1);
  }

  CHECK_EQ(geeyoou::detail::deathWatchDepth(), std::size_t(0));
  geeyoou::detail::resetLayoutDiagnostics();
}

// ============================================================== #16 ===========
//
// The highest-graded row in section 11.4's table, and the one the comment above
// GeometryGuard in Widget.cpp has been pointing at all along.
//
// Door one is header_->setGeometry, which reaches application code through
// WindowHeader::relayoutItems and any trailing item's onGeometryChanged.  The
// two statements after it read content_ and fill_ out of `this` and write
// through both.
//
// Since E15 the mode is a NULL dereference rather than a dangling one --
// AppWindow::onDescendantDetached nulls both members while the objects are
// still alive -- so this case reddens on all three legs, and the load-bearing
// half of the checkpoint is the member re-read rather than a cursor.
// TWO BLOCKS, and the first one is shaped so that EXACTLY ONE of CP-A1's five
// checks can fire.  That is not tidiness: with a fill present and the content
// destroyed, four of the five fire at once, and an experiment that removes any
// one of them proves nothing because the other three still catch it (measured,
// not assumed -- removing the content re-read on its own left the suite green).
//
// The separating construction is Q4's cancellation policy used on purpose.
// takeChild DETACHES without destroying, so the content object stays alive in
// the test's own unique_ptr and its cursor stays TRUE -- "dead, not merely
// detached" is the whole of what g_deathWatch means.  With no setContent<T>
// either, fill_ is null before and after and its two checks are silent.  What
// is left is one fact -- content_ was nulled by this window's own
// onDescendantDetached -- and one check that can see it.
GEEYOOU_TEST(rem3_doors, an_appwindow_relayout_that_loses_its_content_stops_there) {
  geeyoou::detail::resetLayoutDiagnostics();

  // --- block 1: detached and still alive, so only the re-read can see it ----
  {
    AppWindow win("geeyoou rem3 doors", 400, 300);
    REQUIRE(win.header() != nullptr);
    REQUIRE(win.content() != nullptr);

    std::unique_ptr<Widget> parked;
    GeometryHook* item = win.header()->addTrailingItem<GeometryHook>(24.0f);
    REQUIRE(item != nullptr);

    // Armed after addTrailingItem, which lays the bar out once on its own.
    item->once = [&win, &parked] { parked = win.takeChild(win.content()); };

    const std::uint32_t before = degraded();
    win.setGeometry({0.0f, 0.0f, 520.0f, 360.0f});

    CHECK_EQ(win.content(), static_cast<Widget*>(nullptr));
    // ALIVE, which is the point of this block: the cursor on it still reads
    // true, so nothing but the member re-read stands between this frame and a
    // null dereference one line later.
    CHECK(parked != nullptr);
    CHECK_EQ(degraded(), before + 1);
    // The header got its new geometry BEFORE the door -- the frame gave up
    // after door one, not in front of it, and this is the read that proves the
    // give-up is not simply "relayout did nothing".  520 less two 1px borders.
    CHECK_NEAR(win.header()->geometry().width(), 518.0f, 0.0005f);
  }

  // --- block 2: destroyed outright, which is what an application writes -----
  //
  // Four of the five checks fire here at once and that is correct: the content
  // and the fill are BOTH announced (the walk is per node of the departing
  // subtree), both members are nulled, and both objects are then freed.  Kept
  // because it is the real-world shape, not because it separates anything.
  {
    AppWindow win("geeyoou rem3 doors", 400, 300);
    REQUIRE(win.content() != nullptr);
    REQUIRE(win.setContent<Widget>() != nullptr);

    GeometryHook* item = win.header()->addTrailingItem<GeometryHook>(24.0f);
    REQUIRE(item != nullptr);
    item->once = [&win] { win.removeChild(win.content()); };

    const std::uint32_t before = degraded();
    win.setGeometry({0.0f, 0.0f, 520.0f, 360.0f});

    CHECK_EQ(win.content(), static_cast<Widget*>(nullptr));
    CHECK_EQ(degraded(), before + 1);
    CHECK_NEAR(win.header()->geometry().width(), 518.0f, 0.0005f);
  }

  CHECK_EQ(geeyoou::detail::deathWatchDepth(), std::size_t(0));
  geeyoou::detail::resetLayoutDiagnostics();
}

// The other half of #16: the same function, the LAST of its three doors, and
// the object that dies is the window running the frame.
//
// fill_->setGeometry runs the application's own content widget.  Widget::
// setGeometry guards ITS frame (R2's GeometryGuard) and returns cleanly; what
// nothing guarded until now is the frame underneath, which goes on to read
// contentResized and to call update(), both through a freed `this`.
GEEYOOU_TEST(rem3_doors, an_appwindow_relayout_that_loses_itself_stops_there) {
  geeyoou::detail::resetLayoutDiagnostics();

  auto win = std::make_unique<AppWindow>("geeyoou rem3 doors", 400, 300);
  REQUIRE(win->content() != nullptr);
  GeometryHook* fill = win->setContent<GeometryHook>();
  REQUIRE(fill != nullptr);

  AppWindow* raw = win.get();
  fill->once = [&win] { win.reset(); };

  const std::uint32_t before = degraded();
  raw->setGeometry({0.0f, 0.0f, 520.0f, 360.0f});

  // `raw` and `fill` are dangling from here on.
  CHECK_EQ(win.get(), static_cast<AppWindow*>(nullptr));
  CHECK_EQ(degraded(), before + 1);
  CHECK_EQ(geeyoou::detail::deathWatchDepth(), std::size_t(0));

  geeyoou::detail::resetLayoutDiagnostics();
}
