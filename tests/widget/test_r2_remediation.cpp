//
// What the REMEDIATION of R2's memory-safety review has to survive.
//
// test_r2_safety.cpp covers the three defects the first appsec pass found, all
// of them on the ARRANGING half of a layout pass.  The re-review found the same
// defect family on the MEASURING half, plus three regressions the fixes
// themselves introduced, and this file is the regression net for those five.
//
// The common root cause is one sentence that was written too narrowly.  The
// contract on Layout::arrange said "check hostAlive() after every setGeometry",
// so every implementation did -- and nobody checked after sizeHint(), which is
// the OTHER call that re-enters application code and is reachable from measure()
// as well as from arrange().  The contract now says "after every call that
// re-enters application code, sizeHint() included" (Layout.hpp), and the cases
// below are what holds it there.
//
// Each case names the probe it is reduced from and what ASan says without the
// fix.  All five are GREEN under the whole suite in both configurations without
// their fix -- the ONLY thing that sees them is /fsanitize=address, which is
// why verify.bat now has a third leg.
//
#include <cstddef>
#include <functional>
#include <memory>
#include <type_traits>

#include "framework/Test.hpp"
#include "geeyoou/widget/BoxLayout.hpp"
#include "geeyoou/widget/GridLayout.hpp"
#include "geeyoou/widget/Layout.hpp"
#include "geeyoou/widget/Widget.hpp"

using geeyoou::BoxLayout;
using geeyoou::GridLayout;
using geeyoou::Layout;
using geeyoou::Rect;
using geeyoou::Size;
using geeyoou::SizeHint;
using geeyoou::Widget;

namespace {
constexpr float kEps = 0.0005f;

// A widget with an opinion, so the arithmetic below is predictable.
class Sized : public Widget {
 public:
  explicit Sized(float w = 30.0f, float h = 20.0f) : w_(w), h_(h) {}

  void setWidth(float w) { w_ = w; }

  SizeHint sizeHint() const override {
    return SizeHint{Size{w_, h_}, Size{w_, h_},
                    Size{geeyoou::kUnbounded, geeyoou::kUnbounded}};
  }

 private:
  float w_;
  float h_;
};

// Runs one callback from inside its own sizeHint().
//
// This is the door the whole file is about.  sizeHint() is an application
// override -- the library's own controls measure text in theirs -- and nothing
// stops one from doing what any other handler may do.  Fires ONCE: a hook that
// re-armed itself would be testing the convergence limiter instead.
class HintHook : public Sized {
 public:
  HintHook() : Sized(30.0f, 20.0f) {}

  mutable std::function<void()> once;

  SizeHint sizeHint() const override {
    // THE ANSWER IS BUILT FIRST, and returned from a local.  The callback below
    // destroys the host, and this widget is one of the host's children -- so
    // `this` is freed by the time it returns, and `return Sized::sizeHint()`
    // would be a read of two floats inside a dead object.  That is the
    // application's own bug rather than the library's (a slot destroying the
    // object it is running inside is what contract D7 is about), but it is the
    // same shape as the defect under test and it would sit on top of the real
    // report.  ASan caught it here; it went unnoticed in the review probe only
    // because that one returned literals.
    const SizeHint out = Sized::sizeHint();
    if (once) {
      std::function<void()> f;
      f.swap(once);
      f();
    }
    return out;
  }
};

// ...and one from inside its own onGeometryChanged, the door test_r2_safety.cpp
// already uses.  Not one-shot: NEW-5 needs a container that hand-places its
// children on EVERY resize, which is what a real one does.
class GeometryHook : public Sized {
 public:
  GeometryHook() : Sized(30.0f, 20.0f) {}

  std::function<void()> every;

 protected:
  void onGeometryChanged() override {
    if (every) every();
  }
};

// The two numbers that say the engine's bookkeeping came back to where it
// started.  Sampled around each case rather than asserted against absolutes:
// the suite shares one process, and a case that assumed it owned the counters
// would fail on the day somebody reordered the file.
struct Ledger {
  std::size_t hosts = geeyoou::detail::g_layoutHosts;
  std::size_t parked = geeyoou::detail::parkedLayoutCount();
};

}  // namespace

// ================================================== NEW-1: measuring half ===
//
// Probe n1.  A child's sizeHint() destroys the host while BoxLayout::gather is
// walking it -- and gather is the FIRST thing arrange() does, so this happens
// before a single rectangle has been written and the hostAlive() check at the
// bottom of the placement loop never gets a turn.
//
// Without the fix, ASan:
//   heap-use-after-free READ of size 8
//     #1 BoxLayout::itemWidget  BoxLayout.cpp:98   <- host.children()
//     #2 BoxLayout::gather      BoxLayout.cpp:141
//   freed by: #7 BoxLayout::gather BoxLayout.cpp:148  <- c->sizeHint()
GEEYOOU_TEST(r2_remediation, a_box_survives_its_host_dying_inside_gather_from_arrange) {
  const Ledger before;
  {
    Widget root;
    root.setGeometry({0.0f, 0.0f, 400.0f, 400.0f});
    Widget* host = root.add<Widget>();
    BoxLayout* box = host->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);

    // First in the column, so six more items are still to be gathered when it
    // takes the whole subtree away.
    HintHook* trigger = host->add<HintHook>();
    box->addWidget(trigger);
    for (int i = 0; i < 6; ++i) box->addWidget(host->add<Sized>());

    trigger->once = [&root, host] { root.removeChild(host); };
    host->setGeometry({0.0f, 0.0f, 200.0f, 300.0f});

    CHECK(root.children().empty());
    // ...and the tree is still usable, which is the other half of "survives".
    Widget* replacement = root.add<Widget>();
    replacement->setGeometry({0.0f, 0.0f, 10.0f, 10.0f});
    CHECK_EQ(root.children().size(), std::size_t(1));
  }
  const Ledger after;
  CHECK_EQ(after.hosts, before.hosts);
  CHECK_EQ(after.parked, before.parked);
}

// Probe n3: GridLayout::measureAxis, same shape, same absence of a check.
//   #1 GridLayout::cellWidget  GridLayout.cpp:105
//   #2 GridLayout::measureAxis GridLayout.cpp:160
//   freed by: GridLayout.cpp:163  <- w->sizeHint()
GEEYOOU_TEST(r2_remediation, a_grid_survives_its_host_dying_inside_measureaxis_from_arrange) {
  const Ledger before;
  {
    Widget root;
    root.setGeometry({0.0f, 0.0f, 400.0f, 400.0f});
    Widget* host = root.add<Widget>();
    GridLayout* grid = host->setLayout<GridLayout>();

    HintHook* trigger = host->add<HintHook>();
    grid->addWidget(trigger, 0, 0);
    for (int i = 1; i < 6; ++i) grid->addWidget(host->add<Sized>(), i, i);
    // Without stretch the trigger's rectangle never moves, M3's idempotence
    // check skips the whole thing, and the case quietly tests nothing.
    grid->setColumnStretch(0, 1);
    grid->setRowStretch(0, 1);

    trigger->once = [&root, host] { root.removeChild(host); };
    host->setGeometry({0.0f, 0.0f, 200.0f, 300.0f});

    CHECK(root.children().empty());
  }
  const Ledger after;
  CHECK_EQ(after.hosts, before.hosts);
  CHECK_EQ(after.parked, before.parked);
}

// ============================ NEW-2: a measurement is not a layout pass ======
//
// Probe n2, and the deeper of the two.  The same removal, but from a PURE
// sizeHint() with no pass anywhere on the stack -- which is exactly the
// position ScrollArea::relayout issues content_->sizeHint() from.
//
// layoutRunning_ is false there, so ~Widget did not park the layout: the
// unique_ptr ran straight through the BoxLayout whose gather() was on the
// stack.  Without the fix, ASan:
//   heap-use-after-free READ of size 8
//     #0 BoxLayout::gather   BoxLayout.cpp:153  <- &scratch_[i * kStride]
//     #3 Widget::sizeHint    Widget.cpp:410
//   freed by: #3 BoxLayout::~BoxLayout  #5 Widget::~Widget Widget.cpp:275
// ...and then Layout::measureFor WRITES lastMeasure_ into the freed object on
// the way out, which is why parking is the fix and a check alone is not.
GEEYOOU_TEST(r2_remediation, a_box_survives_its_host_dying_inside_a_pure_measure) {
  const Ledger before;
  {
    Widget root;
    root.setGeometry({0.0f, 0.0f, 400.0f, 400.0f});
    Widget* host = root.add<Widget>();
    BoxLayout* box = host->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);

    HintHook* trigger = host->add<HintHook>();
    box->addWidget(trigger);
    for (int i = 0; i < 6; ++i) box->addWidget(host->add<Sized>());

    trigger->once = [&root, host] { root.removeChild(host); };

    // No setGeometry, no pass: just the question a scroll area asks its content
    // on every resize.  `host` is freed by the time this returns, so nothing
    // below may name it.
    const SizeHint measured = host->sizeHint();
    (void)measured;

    CHECK(root.children().empty());
  }
  const Ledger after;
  CHECK_EQ(after.hosts, before.hosts);
  // The park list is drained by the MEASUREMENT on its way out.  Draining it
  // only from ~LayoutGuard, as it used to be, leaks this layout for as long as
  // the process never runs another pass -- which for a screen sitting idle is
  // the rest of its life.
  CHECK_EQ(after.parked, before.parked);
}

GEEYOOU_TEST(r2_remediation, a_grid_survives_its_host_dying_inside_a_pure_measure) {
  const Ledger before;
  {
    Widget root;
    root.setGeometry({0.0f, 0.0f, 400.0f, 400.0f});
    Widget* host = root.add<Widget>();
    GridLayout* grid = host->setLayout<GridLayout>();

    HintHook* trigger = host->add<HintHook>();
    grid->addWidget(trigger, 0, 0);
    for (int i = 1; i < 6; ++i) grid->addWidget(host->add<Sized>(), i, i);

    trigger->once = [&root, host] { root.removeChild(host); };

    const SizeHint measured = host->sizeHint();
    (void)measured;

    CHECK(root.children().empty());
  }
  const Ledger after;
  CHECK_EQ(after.hosts, before.hosts);
  CHECK_EQ(after.parked, before.parked);
}

// ============= NEW-3: detaching the arranging host must not freeze it ========
//
// Probe n4.  takeChild() of the host whose arrange is running is ordinary
// application code -- a page moving a panel between two containers on a resize
// -- and the host SURVIVES it.  announceDetached cancelled the layout cursor
// anyway, which sent runLayoutIfAny down the path meant for a host that has
// been FREED: that path deliberately writes nothing, layoutRunning_ included.
//
// Two consequences, and this case asserts both:
//   * M1 then turns every later pass into "mark dirty and return", so the
//     subtree's geometry is frozen for the rest of the process;
//   * ~Widget sees layoutRunning_ still true and parks a layout nobody is
//     reading, onto a list only a future pass would ever drain.
GEEYOOU_TEST(r2_remediation, taking_the_arranging_host_out_of_the_tree_does_not_freeze_it) {
  const Ledger before;
  std::unique_ptr<Widget> held;
  {
    Widget root;
    root.setGeometry({0.0f, 0.0f, 400.0f, 400.0f});
    Widget* host = root.add<Widget>();
    BoxLayout* box = host->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);

    GeometryHook* trigger = host->add<GeometryHook>();
    box->addWidget(trigger);
    for (int i = 0; i < 3; ++i) box->addWidget(host->add<Sized>());

    trigger->every = [&root, host, &held] {
      if (!held) held = root.takeChild(host);
    };

    host->setGeometry({0.0f, 0.0f, 200.0f, 300.0f});

    REQUIRE(held != nullptr);
    CHECK(root.children().empty());
    // The pass that was interrupted still finished: four 20px rows, 6px apart,
    // filling the 200px width it was arranged into.
    REQUIRE(held->children().size() == std::size_t(4));
    for (std::size_t i = 0; i < 4; ++i) {
      CHECK_NEAR(held->children()[i]->geometry().y(), float(i) * 26.0f, kEps);
      CHECK_NEAR(held->children()[i]->geometry().width(), 200.0f, kEps);
    }
  }

  // ...and it is a live widget, not a picture.  This is the assertion that goes
  // red without the fix: layoutRunning_ never came down, so M1 swallowed this
  // resize and every one after it, and the children stayed 200 wide for ever.
  held->setGeometry({0.0f, 0.0f, 380.0f, 390.0f});
  for (const std::unique_ptr<Widget>& c : held->children()) {
    CHECK_NEAR(c->geometry().width(), 380.0f, kEps);
  }
  held->performLayout();
  CHECK_NEAR(held->children()[3]->geometry().y(), 78.0f, kEps);

  held.reset();
  const Ledger after;
  CHECK_EQ(after.hosts, before.hosts);
  // Nothing was reading this layout when its host died, so nothing should have
  // been parked -- and a park that is never released is a leak whether or not
  // anything ever reads it again.
  CHECK_EQ(after.parked, before.parked);
}

// ================= NEW-4: a refused pass is owed, not cancelled ==============
//
// Probe n6.  runLayoutIfAny clears layoutDirty_ and THEN asks the layout to
// arrange; Layout::arrangeFor refuses when a measurement of the same layout is
// on the stack (ADR-R2-04, and refilling the buffers it is walking would free
// them under it).  The refusal used to be indistinguishable from a pass that
// ran and changed nothing, so the flag stayed clear and the request evaporated:
// the widget kept the geometry of the previous pass and nothing anywhere
// remembered it was owed a new one.
//
// The `reentered` counter recorded every one of these and no case in the suite
// read it, which is the other half of why this shipped.
GEEYOOU_TEST(r2_remediation, a_pass_refused_during_a_measurement_is_re_run_afterwards) {
  geeyoou::detail::resetLayoutDiagnostics();

  Widget root;
  root.setGeometry({0.0f, 0.0f, 400.0f, 400.0f});
  Widget* host = root.add<Widget>();
  BoxLayout* box = host->setLayout<BoxLayout>(BoxLayout::Orientation::Horizontal);

  HintHook* trigger = host->add<HintHook>();
  box->addWidget(trigger);
  Sized* first = host->add<Sized>(40.0f, 20.0f);
  box->addWidget(first);
  Sized* second = host->add<Sized>(40.0f, 20.0f);
  box->addWidget(second);

  host->setGeometry({0.0f, 0.0f, 200.0f, 300.0f});
  CHECK_NEAR(first->geometry().width(), 40.0f, kEps);
  CHECK_EQ(int(geeyoou::detail::layoutDiagnostics().reentered), 0);

  // Now ask for something new from inside a measurement: `first` wants 180, and
  // the invalidation that says so is raised from a sizeHint() override, which
  // is where a Label whose text changed would raise it.
  first->setWidth(180.0f);
  trigger->once = [host] { host->invalidateSizeHint(); };

  const SizeHint measured = host->sizeHint();
  (void)measured;

  // Exactly one refusal, and it was recorded.
  CHECK_EQ(int(geeyoou::detail::layoutDiagnostics().reentered), 1);
  // ...and the pass it refused HAPPENED, on the way out of the measurement that
  // was in the way.  Without the fix this reads 40: the request was dropped and
  // only an unrelated later invalidation ever picked it up.
  //
  // 180 rather than something smaller because the row no longer fits -- 30 +
  // 180 + 40 plus two 6px gaps is 262 in a 200px host -- and a box that cannot
  // fit its minimums places them at their minimums and lets the tail run off
  // the end, which lastLayoutOverflow() reports.
  CHECK_NEAR(first->geometry().width(), 180.0f, kEps);
  CHECK_NEAR(second->geometry().x(), 222.0f, kEps);
  CHECK(host->lastLayoutOverflow().widthShort > 0.0f);

  // A refusal is not non-convergence: nothing diverged, one pass was simply
  // asked for at a moment it could not run.
  CHECK_EQ(int(geeyoou::detail::layoutDiagnostics().notConverged), 0);

  geeyoou::detail::resetLayoutDiagnostics();
}

// ============ NEW-5: hand-placed children still latch a natural size ========
//
// Probe n5.  latchNaturalSize asked detail::layoutPassActive() -- "is a pass
// running ANYWHERE in this process" -- when the question it meant was "did this
// geometry come from my own parent's arrange".  The two differ for a container
// that positions its own children by hand from onGeometryChanged: those calls
// are by hand, but they run underneath the pass that sized the container, so
// every one of them was thrown away.  The children reported preferred = 0x0 for
// ever, and were arranged at nothing the day the container went into a layout.
namespace {

// The container from that description, with NO sizeHint() of its own: what it
// reports IS its latched natural size, which is what both halves of this case
// need to be able to read.  GeometryHook above cannot be used -- it inherits
// Sized's hint, which would answer 30x20 whatever the latch did.
class HandPlacer : public Widget {
 public:
  std::function<void()> every;

 protected:
  void onGeometryChanged() override {
    if (every) every();
  }
};

}  // namespace

GEEYOOU_TEST(r2_remediation, a_container_that_hand_places_its_children_still_latches_them) {
  Widget root;
  BoxLayout* box = root.setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);

  HandPlacer* panel = root.add<HandPlacer>();
  box->addWidget(panel, 1);
  Widget* handA = panel->add<Widget>();
  Widget* handB = panel->add<Widget>();
  panel->every = [handA, handB] {
    handA->setGeometry({0.0f, 0.0f, 120.0f, 40.0f});
    handB->setGeometry({0.0f, 50.0f, 90.0f, 30.0f});
  };

  root.setGeometry({0.0f, 0.0f, 400.0f, 400.0f});
  root.setGeometry({0.0f, 0.0f, 400.0f, 401.0f});

  // What a human wrote, remembered.  Both read 0x0 without the fix.
  CHECK_NEAR(handA->sizeHint().preferred.width, 120.0f, kEps);
  CHECK_NEAR(handA->sizeHint().preferred.height, 40.0f, kEps);
  CHECK_NEAR(handB->sizeHint().preferred.width, 90.0f, kEps);
  CHECK_NEAR(handB->sizeHint().preferred.height, 30.0f, kEps);

  // ...and the half of ADR-R2-09 the old test was protecting is still there:
  // `panel` was sized by root's arrange, and a size a LAYOUT computed is never
  // a natural size.  Getting this back to 0 is the whole reason the fix is a
  // narrower test rather than no test.
  CHECK_NEAR(panel->sizeHint().preferred.width, 0.0f, kEps);
  CHECK_NEAR(panel->sizeHint().preferred.height, 0.0f, kEps);
}

// ============ NEW-6: the busy latch cannot be walked around any more ========
//
// Probe n7 used `w->layout()->measure(*w)` from inside that layout's own
// arrange, which is a heap-use-after-free written entirely in public API -- and
// the suite itself called measure() that way at test_box_grid_layout.cpp:302,
// so the hole was being documented as an idiom.
//
// The fix is an access change, so the regression test is a compile-time one:
// there is no runtime to reach.  Access control participates in substitution,
// so a protected member simply makes the detector's specialisation
// non-viable -- which is the property being asserted.
namespace {

template <class L, class = void>
struct CallableMeasure : std::false_type {};
template <class L>
struct CallableMeasure<
    L, std::void_t<decltype(std::declval<const L&>().measure(
           std::declval<const Widget&>()))>> : std::true_type {};

template <class L, class = void>
struct CallableArrange : std::false_type {};
template <class L>
struct CallableArrange<L, std::void_t<decltype(std::declval<L&>().arrange(
                              std::declval<Widget&>(),
                              std::declval<const Rect&>()))>> : std::true_type {
};

static_assert(!CallableMeasure<Layout>::value,
              "Layout::measure must not be reachable from application code: it "
              "shares scratch buffers with arrange(), and the latch that keeps "
              "them apart lives in the private measureFor/arrangeFor.");
static_assert(!CallableMeasure<BoxLayout>::value, "BoxLayout::measure is public");
static_assert(!CallableMeasure<GridLayout>::value, "GridLayout::measure is public");
static_assert(!CallableArrange<Layout>::value, "Layout::arrange is public");
static_assert(!CallableArrange<BoxLayout>::value, "BoxLayout::arrange is public");
static_assert(!CallableArrange<GridLayout>::value, "GridLayout::arrange is public");

}  // namespace

GEEYOOU_TEST(r2_remediation, measuring_a_container_goes_through_its_widget) {
  // The replacement idiom, asserted so the static_asserts above have something
  // that shows they did not simply remove the capability.
  Widget root;
  BoxLayout* box = root.setLayout<BoxLayout>(BoxLayout::Orientation::Horizontal);
  box->setSpacing(10.0f);
  box->addWidget(root.add<Sized>(40.0f, 20.0f));
  box->addWidget(root.add<Sized>(60.0f, 30.0f));

  const SizeHint h = root.sizeHint();
  CHECK_NEAR(h.preferred.width, 110.0f, kEps);  // 40 + 10 + 60
  CHECK_NEAR(h.preferred.height, 30.0f, kEps);  // the taller child wins
}

// ================================ a span is clamped against where it starts ==
//
// toIndex() and toSpan() each clamped at kMaxTrack independently, so row 4096
// with rowSpan 4096 declared track 8191 -- twice the bound the constant
// documents, and six vectors resized to 8192 floats on every pass.
GEEYOOU_TEST(r2_remediation, a_span_cannot_reach_past_the_last_track) {
  geeyoou::detail::resetLayoutDiagnostics();

  Widget root;
  root.setGeometry({0.0f, 0.0f, 200.0f, 200.0f});
  GridLayout* grid = root.setLayout<GridLayout>();
  grid->addWidget(root.add<Sized>(20.0f, 10.0f), 4096, 0, 4096, 1);

  // kMaxTrack is the largest INDEX, so kMaxTrack + 1 is the most tracks an axis
  // can have -- and the span, not the row, is what gives way.
  CHECK_EQ(grid->rowCount(), std::size_t(4097));
  CHECK_EQ(grid->columnCount(), std::size_t(1));
  CHECK_EQ(int(geeyoou::detail::layoutDiagnostics().indexClamped), 1);

  // A span that fits is left exactly alone, which is what stops the clamp from
  // being a silent tax on every grid in the application.
  geeyoou::detail::resetLayoutDiagnostics();
  Widget plain;
  plain.setGeometry({0.0f, 0.0f, 200.0f, 200.0f});
  GridLayout* ok = plain.setLayout<GridLayout>();
  ok->addWidget(plain.add<Sized>(20.0f, 10.0f), 1, 2, 3, 4);
  CHECK_EQ(ok->rowCount(), std::size_t(4));
  CHECK_EQ(ok->columnCount(), std::size_t(6));
  CHECK_EQ(int(geeyoou::detail::layoutDiagnostics().indexClamped), 0);

  geeyoou::detail::resetLayoutDiagnostics();
}
