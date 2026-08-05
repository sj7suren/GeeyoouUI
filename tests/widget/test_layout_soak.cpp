//
// The long-running soak: does the layout engine come back to where it started?
//
// O10's outstanding debt, and R2's one hard FAIL.  Every other case in the
// suite runs one scenario once and asks whether the answer is right.  That is
// the wrong shape for the failure mode this engine actually has: nothing here
// crashes on the first pass, it accumulates -- a park list that is only drained
// by an event that may never come, a host counter that is incremented on one
// path and decremented on another, a scratch buffer that is grown from a code
// path that never shrinks it.  An HMI that has been up for six weeks is the
// place those are found today, and finding them there is too late.
//
// So this case runs the WHOLE alarming half of the engine in a loop and samples
// four numbers at every cycle boundary:
//
//   * children      -- the persistent root's child count.  Widgets accumulating
//                      in a tree nobody looks at is the simplest leak there is.
//   * layout hosts  -- detail::g_layoutHosts.  Every zero-cost gate in Widget
//                      is keyed on this being zero, so a drift of +1 per cycle
//                      quietly turns the engine on for the whole process.
//   * parked        -- detail::parkedLayoutCount().  A deferred-free queue that
//                      only ever grows IS the leak, and it has two drain points
//                      now, which is two chances to have got the condition
//                      wrong.
//   * live allocs   -- allocations minus frees, process-wide, from the harness's
//                      replaced operator new.  This is the "cache entries"
//                      series: the engine's caches are the never-shrunk scratch
//                      buffers (BoxLayout::scratch_, the grid's six track
//                      vectors, items_/cells_) and they are deliberately not
//                      countable one by one -- what matters about them is
//                      exactly what this measures, which is whether the heap a
//                      cycle leaves behind is the heap it found.
//
// The assertion is NOT "these numbers are small".  It is that none of them is
// larger at the end of the run than it was after the warm-up, and that none of
// them ever peaked above it in between: a steady state, reached early and held.
//
// Cycles come from GY_SOAK_CYCLES.  verify.bat sets a short one, because a gate
// that takes four minutes is a gate people stop running; the nightly job sets a
// long one, which is where a slow drift of one byte a cycle actually shows up.
//
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "framework/Test.hpp"
#include "geeyoou/widget/BoxLayout.hpp"
#include "geeyoou/widget/GridLayout.hpp"
#include "geeyoou/widget/GroupBox.hpp"
#include "geeyoou/widget/Layout.hpp"
#include "geeyoou/widget/ScrollArea.hpp"
#include "geeyoou/widget/Widget.hpp"
#include "showcase/Shell.hpp"

using geeyoou::BoxLayout;
using geeyoou::GridLayout;
using geeyoou::GroupBox;
using geeyoou::ScrollArea;
using geeyoou::Size;
using geeyoou::SizeHint;
using geeyoou::Widget;

namespace {

// How many cycles, from the environment.  Default deliberately small: this is
// in the gate, and the gate has to stay fast enough to be run.
int soakCycles() {
#ifdef _MSC_VER
  std::size_t len = 0;
  char buf[32] = {};
  if (getenv_s(&len, buf, sizeof(buf), "GY_SOAK_CYCLES") != 0 || len <= 1) {
    return 400;
  }
  const int n = std::atoi(buf);
#else
  const char* v = std::getenv("GY_SOAK_CYCLES");
  if (!v || !*v) return 400;
  const int n = std::atoi(v);
#endif
  return n > 0 ? n : 400;
}

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

// Fires once from inside sizeHint() -- the measuring door.
class HintHook : public Sized {
 public:
  HintHook() : Sized(30.0f, 20.0f) {}
  mutable std::function<void()> once;

  SizeHint sizeHint() const override {
    // Built BEFORE the callback and returned from a local: the callback
    // destroys the host, which owns this widget, so `this` is gone by the time
    // it returns.  See the same note in test_r2_remediation.cpp.
    const SizeHint out = Sized::sizeHint();
    if (once) {
      std::function<void()> f;
      f.swap(once);
      f();
    }
    return out;
  }
};

// ...and once from inside onGeometryChanged() -- the arranging door.
class GeometryHook : public Sized {
 public:
  GeometryHook() : Sized(30.0f, 20.0f) {}
  std::function<void()> once;

 protected:
  void onGeometryChanged() override {
    if (!once) return;
    std::function<void()> f;
    f.swap(once);
    f();
  }
};

// How many times each of the five hint-side doors below actually opened.
//
// Not decoration, and not a fifth series: the four series can only prove that a
// cycle left nothing behind, and the cheapest way to leave nothing behind is to
// never build anything.  A door that stops opening -- because a rectangle
// stopped changing and M3 short-circuited the call, because a layout stopped
// being installed, because somebody reordered two lines -- turns its group into
// dead code that keeps passing.  Five counters, asserted to be EXACTLY the
// number of cycles, are what stop that.
//
// Fix-invariant on purpose: every hook fires from inside the door, BEFORE the
// frame it is attacking gets to notice anything, so E3/E4 landing must not move
// any of these numbers by one.  A guard that changed them would be a guard that
// suppressed the call instead of surviving it.
struct DoorLog {
  std::size_t groupBoxMeasure = 0;   // GroupBox.cpp:53
  std::size_t scrollHint = 0;        // ScrollArea.cpp:158
  std::size_t scrollContent = 0;     // ScrollArea.cpp:22
  std::size_t scrollViewport = 0;    // ScrollArea.cpp:164
  std::size_t shellResize = 0;       // Shell.cpp:330 -> ScrollArea.cpp:158
};

DoorLog g_doors;

struct Sample {
  std::size_t children = 0;
  std::size_t hosts = 0;
  std::size_t parked = 0;
  std::uint64_t liveAllocs = 0;
};

Sample take(const Widget& root) {
  Sample s;
  s.children = root.children().size();
  s.hosts = geeyoou::detail::g_layoutHosts;
  s.parked = geeyoou::detail::parkedLayoutCount();
  s.liveAllocs = geeyoou::test::allocCount() - geeyoou::test::freeCount();
  return s;
}

// ONE cycle: build a laid-out subtree under `root`, put every hazardous path in
// the engine through it, and take the whole thing away again.  Deterministic on
// purpose -- no RNG -- so a failure at cycle 91 337 is a failure at cycle
// 91 337 the next time too.
void cycle(Widget& root, int n) {
  // --- a plain nested tree, resized both ways ------------------------------
  Widget* page = root.add<Widget>();
  BoxLayout* column = page->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
  for (int i = 0; i < 4; ++i) column->addWidget(page->add<Sized>(30.0f, 20.0f), 1);

  Widget* form = page->add<Widget>();
  column->addWidget(form, 2);
  GridLayout* grid = form->setLayout<GridLayout>();
  for (int r = 0; r < 3; ++r) {
    grid->addRow(form->add<Sized>(40.0f, 16.0f), form->add<Sized>(60.0f, 16.0f));
    (void)r;
  }

  // Two different rectangles, so nothing is skipped by M3's idempotence check,
  // and a size that moves with the cycle number so no pass is ever a repeat of
  // the last one.
  page->setGeometry({0.0f, 0.0f, 300.0f + float(n % 7), 240.0f});
  page->setGeometry({0.0f, 0.0f, 180.0f, 500.0f + float(n % 5)});

  // --- visibility, which takes items in and out of the chain ---------------
  page->children()[1]->setVisible(false);
  page->children()[1]->setVisible(true);

  // --- items arriving and leaving mid-pass ---------------------------------
  Sized* doomed = page->add<Sized>(25.0f, 25.0f);
  column->addWidget(doomed);
  page->setGeometry({0.0f, 0.0f, 200.0f, 400.0f});
  page->removeChild(doomed);

  // --- a refused pass: an invalidation raised from inside a measurement ----
  {
    HintHook* trigger = form->add<HintHook>();
    grid->addWidget(trigger, 3, 0);
    trigger->once = [form] { form->invalidateSizeHint(); };
    const SizeHint measured = form->sizeHint();
    (void)measured;
  }

  // --- the layout replaced from inside its own arrange ---------------------
  {
    Widget* swap = root.add<Widget>();
    swap->setGeometry({0.0f, 0.0f, 120.0f, 90.0f});
    BoxLayout* before = swap->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
    GeometryHook* trigger = swap->add<GeometryHook>();
    before->addWidget(trigger, 1);
    Sized* other = swap->add<Sized>();
    before->addWidget(other, 1);
    trigger->once = [swap, trigger, other] {
      BoxLayout* after =
          swap->setLayout<BoxLayout>(BoxLayout::Orientation::Horizontal);
      after->addWidget(trigger, 1);
      after->addWidget(other, 1);
    };
    swap->setGeometry({0.0f, 0.0f, 260.0f, 140.0f});
    root.removeChild(swap);
  }

  // --- the host destroyed from inside a PURE measurement -------------------
  // The path that parks a layout with no pass on the stack, and therefore the
  // one that exercises the park list's second drain point.  If that drain is
  // ever wrong again, `parked` is the series that says so.
  {
    Widget* dying = root.add<Widget>();
    BoxLayout* box = dying->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
    HintHook* trigger = dying->add<HintHook>();
    box->addWidget(trigger);
    for (int i = 0; i < 3; ++i) box->addWidget(dying->add<Sized>());
    trigger->once = [&root, dying] { root.removeChild(dying); };
    const SizeHint measured = dying->sizeHint();
    (void)measured;
  }

  // --- the same, from inside an arrange ------------------------------------
  {
    Widget* dying = root.add<Widget>();
    GridLayout* g = dying->setLayout<GridLayout>();
    GeometryHook* trigger = dying->add<GeometryHook>();
    g->addWidget(trigger, 0, 0);
    g->setColumnStretch(0, 1);
    g->setRowStretch(0, 1);
    for (int i = 1; i < 4; ++i) g->addWidget(dying->add<Sized>(), i, i);
    trigger->once = [&root, dying] { root.removeChild(dying); };
    dying->setGeometry({0.0f, 0.0f, 220.0f, 180.0f});
  }

  // --- a host taken out of the tree while it is arranging, then dropped ----
  {
    std::unique_ptr<Widget> held;
    Widget* moving = root.add<Widget>();
    BoxLayout* box = moving->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
    GeometryHook* trigger = moving->add<GeometryHook>();
    box->addWidget(trigger);
    for (int i = 0; i < 3; ++i) box->addWidget(moving->add<Sized>());
    trigger->once = [&root, moving, &held] { held = root.takeChild(moving); };
    moving->setGeometry({0.0f, 0.0f, 150.0f, 200.0f});
    if (held) held->setGeometry({0.0f, 0.0f, 170.0f, 210.0f});
  }

  // ==========================================================================
  // The OTHER kind of door: sizeHint()'s CALLERS
  // --------------------------------------------------------------------------
  // Everything above this line attacks a frame that belongs to a Layout, and
  // Layout.hpp's contract already tells those frames to re-check hostAlive()
  // after every re-entry into application code.  The five groups below attack
  // frames in Widget SUBCLASSES -- GroupBox and ScrollArea -- which are not
  // Layout implementations, are not addressed by that contract, and check
  // nothing.  They CALL sizeHint(), the call runs an application override, and
  // then they go on reading (and in two places WRITING) through pointers that
  // override was entitled to free.  See docs/iterations/02-layout-engine.md,
  // section 11, whose table names the same two sites.
  //
  // Placed in the soak rather than in five stand-alone cases because the fix
  // for all of them is a stack cursor, and a cursor is only correct if it also
  // comes back OFF its list, every time, on every path out of the frame --
  // including the early return the guard itself introduces.  One leaked cursor
  // is invisible to a case that runs the path once; it is a list that grows
  // with the cycle count here.
  //
  // NOT ONE OF THE FIVE GOES THROUGH A POPUP.  The removal-side cases in
  // test_removal.cpp all reach application code through the single door
  // Window::widgetDetached owns (closePopup -> popupClosed; focus and hover are
  // dropped silently, Window.cpp).  These five reach it through sizeHint() and
  // onGeometryChanged() overrides, which are doors the APPLICATION implements.
  // So a second detach-side door -- an onDescendantDetached broadcast, say --
  // would not change the coverage argument for anything below.

  // --- a GroupBox emptied from inside its own measurement -------------------
  //
  // BoxLayout.cpp:155 `c->sizeHint()` -> GroupBox.cpp:53 `Widget::sizeHint()`,
  // which is the INNER layout's measure -> that layout's gather -> an
  // application sizeHint() override -> the band the GroupBox sits in is
  // destroyed -> back at GroupBox.cpp:54, which reads layout(), then title_,
  // then makes a virtual call through style().
  //
  // The whole band goes, not just the panel, because that is the shape the
  // showcase produces -- a page rebuilding a section drops the section -- and
  // because it covers both frames in one go: BoxLayout::gather notices, at its
  // hostAlive() check; GroupBox::sizeHint has nothing to notice with.
  {
    Widget* band = root.add<Widget>();
    BoxLayout* outer = band->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
    GroupBox* panel = band->add<GroupBox>();
    // Titled: the title is what makes :62-:65 read a std::string and take the
    // style path out of the freed object, and a fix that hoisted that work in
    // front of the door (R3-G5 forbids it) would still have to keep passing
    // here.
    panel->setTitle("参数");
    outer->addWidget(panel, 1);
    outer->addWidget(band->add<Sized>(40.0f, 24.0f), 1);

    BoxLayout* inner = panel->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
    HintHook* trigger = panel->add<HintHook>();
    inner->addWidget(trigger, 1);
    inner->addWidget(panel->add<Sized>(), 1);
    // Armed last: every addWidget above marks the tree dirty and runs a pass of
    // its own, and a hook armed earlier would fire in one of those instead of
    // in the door this group is about.
    trigger->once = [&root, band] {
      ++g_doors.groupBoxMeasure;
      root.removeChild(band);
    };

    band->setGeometry({0.0f, 0.0f, 240.0f, 180.0f});
  }

  // --- a ScrollArea destroyed from inside its content's MEASUREMENT ---------
  //
  // ScrollArea.cpp:158 `content_->sizeHint()` -- the branch taken by every page
  // that hands its content to the layout engine, which is five of the ten --
  // and on the way back :159 reads geometry_, :164 and :165 WRITE a 16-byte
  // Rect through viewport_ and content_.  Pre-reading before the door, which is
  // the trick that would fix a pure read, cannot fix a write.
  {
    ScrollArea* sa = root.add<ScrollArea>();
    Widget* content = sa->content();
    BoxLayout* box = content->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
    HintHook* trigger = content->add<HintHook>();
    box->addWidget(trigger, 1);
    box->addWidget(content->add<Sized>(60.0f, 40.0f), 1);
    trigger->once = [&root, sa] {
      ++g_doors.scrollHint;
      root.removeChild(sa);
    };

    // onGeometryChanged -> relayout: the same entry a window resize uses.
    sa->setGeometry({0.0f, 0.0f, 200.0f, 150.0f});
  }

  // --- ...and from inside its content's ARRANGE ------------------------------
  //
  // The other way into the same object.  ScrollArea.cpp:22 sizes the content,
  // the content owns a layout, the layout arranges, a child's
  // onGeometryChanged destroys the scroll area -- and setContentSize carries on
  // at :24 relayout(), :26 scrollTo(scrollOffset()), :27 update(), all three
  // through a freed `this`.
  {
    ScrollArea* sa = root.add<ScrollArea>();
    // Given a geometry FIRST, so the content has a real rectangle to be
    // re-arranged out of: M3 would short-circuit a child's setGeometry that did
    // not move, and a door that does not open is not a test.
    sa->setGeometry({0.0f, 0.0f, 200.0f, 150.0f});

    Widget* content = sa->content();
    BoxLayout* box = content->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
    GeometryHook* trigger = content->add<GeometryHook>();
    box->addWidget(trigger, 1);
    box->addWidget(content->add<Sized>(50.0f, 30.0f), 1);
    trigger->once = [&root, sa] {
      ++g_doors.scrollContent;
      root.removeChild(sa);
    };

    sa->setContentSize({320.0f, 260.0f});
  }

  // --- the THIRD door in relayout(), the one in series with the other two ----
  //
  // :164 `viewport_->setGeometry(...)` is itself a door, and :165 is a write
  // through content_ after it.  A fix that only guards the sizeHint() at :158
  // leaves this one open, and the frame it leaves open is the one that WRITES.
  //
  // The viewport is ScrollArea's only direct child (see its constructor) and is
  // reachable through the public children() accessor -- the same route section
  // 11's Q5 takes to show that viewport_ and content_ are not invariants.  It
  // needs a layout to be a door at all: setGeometry on a plain widget with no
  // layout and no onGeometryChanged override runs no application code.
  {
    ScrollArea* sa = root.add<ScrollArea>();
    Widget* content = sa->content();
    BoxLayout* cbox = content->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
    cbox->addWidget(content->add<Sized>(80.0f, 60.0f), 1);

    Widget* viewport = sa->children()[0].get();
    BoxLayout* vbox = viewport->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
    GeometryHook* trigger = viewport->add<GeometryHook>();
    vbox->addWidget(trigger, 1);
    trigger->once = [&root, sa] {
      ++g_doors.scrollViewport;
      root.removeChild(sa);
    };

    // The content's hint (80x60) fits, so neither bar is needed, so :164 hands
    // the viewport the full rectangle -- a change from (0,0,0,0), so the pass
    // really runs.
    sa->setGeometry({0.0f, 0.0f, 220.0f, 160.0f});
  }

  // --- the production path, end to end --------------------------------------
  //
  // Nothing in this group is constructed for the test except the widget that
  // does the tearing down.  Shell.cpp's relayout() sizes every page host on
  // every window resize (`pg.host->setGeometry(...)`), the host is a
  // ScrollArea, its onGeometryChanged is relayout(), and its content owns a
  // layout because that is what the five migrated pages do.  That is the whole
  // chain from "the operator dragged the window" to ScrollArea.cpp:158 -- no
  // scaffolding, no injected defect.
  {
    showcase::Shell* shell = root.add<showcase::Shell>();
    shell->setGeometry({0.0f, 0.0f, 640.0f, 480.0f});

    Widget* pageContent = nullptr;
    HintHook* trigger = nullptr;
    shell->addPage("演示", "布局", "", geeyoou::Icon::None,
                   [&pageContent, &trigger](Widget* content) {
                     pageContent = content;
                     BoxLayout* box = content->setLayout<BoxLayout>(
                         BoxLayout::Orientation::Vertical);
                     trigger = content->add<HintHook>();
                     box->addWidget(trigger, 1);
                     box->addWidget(content->add<Sized>(120.0f, 40.0f), 1);
                     return Size{300.0f, 400.0f};
                   });
    shell->showPage(0);

    // Armed AFTER the page is built: showPage() reaches the same door twice on
    // its own (setContentSize, then the shell's own relayout), and a hook armed
    // in the builder would fire there instead of on the resize this group is
    // about.
    //
    // content -> viewport -> ScrollArea -> pageArea, through parent() only: a
    // control on a page dropping its own page while the page is being measured
    // is application code doing something D7 allows.
    Widget* host = pageContent->parent()->parent();
    Widget* pageArea = host->parent();
    trigger->once = [pageArea, host] {
      ++g_doors.shellResize;
      pageArea->removeChild(host);
    };

    // The resize.  Shell::relayout -> pg.host->setGeometry -> ScrollArea::
    // onGeometryChanged -> relayout -> content_->sizeHint() -> the page's
    // override -> the host is gone -> ScrollArea.cpp:159 onwards.
    shell->setGeometry({0.0f, 0.0f, 720.0f, 540.0f});

    // The shell outlives the page host, so this group takes itself away rather
    // than being taken away by its own hook like the four above.
    root.removeChild(shell);
  }

  root.removeChild(page);
}

}  // namespace

GEEYOOU_TEST(layout_soak, nothing_the_engine_holds_grows_with_the_number_of_cycles) {
  const int cycles = soakCycles();

  // Reset BEFORE the warm-up, so the degradation count below covers every cycle
  // this case ran and none that anybody else did.  Nothing else in this case
  // reads the diagnostics, so there is nothing to lose by clearing them.
  geeyoou::detail::resetLayoutDiagnostics();

  Widget root;
  root.setGeometry({0.0f, 0.0f, 800.0f, 600.0f});

  // Reserved BEFORE the baseline is taken.  A samples vector that reallocated
  // during the run would show up in the live-allocation series as growth caused
  // by the measurement itself, which is the most embarrassing way for a soak
  // test to fail.
  std::vector<Sample> samples;
  samples.reserve(std::size_t(cycles) + 1);

  // Warm-up.  First-use statics, the first growth of every never-shrunk scratch
  // buffer, and the harness's own lazily-built strings all happen once; a run
  // that counted them as drift would be measuring its own start-up.
  for (int i = 0; i < 3; ++i) cycle(root, i);
  const Sample base = take(root);

  for (int i = 0; i < cycles; ++i) {
    cycle(root, i);
    samples.push_back(take(root));
  }

  Sample peak = base;
  for (const Sample& s : samples) {
    if (s.children > peak.children) peak.children = s.children;
    if (s.hosts > peak.hosts) peak.hosts = s.hosts;
    if (s.parked > peak.parked) peak.parked = s.parked;
    if (s.liveAllocs > peak.liveAllocs) peak.liveAllocs = s.liveAllocs;
  }
  const Sample last = samples.empty() ? base : samples.back();

  std::printf(
      "[soak] %d cycles  series(base/peak/last):"
      " children %zu/%zu/%zu  hosts %zu/%zu/%zu  parked %zu/%zu/%zu"
      "  live-allocs %llu/%llu/%llu\n",
      cycles, base.children, peak.children, last.children, base.hosts,
      peak.hosts, last.hosts, base.parked, peak.parked, last.parked,
      static_cast<unsigned long long>(base.liveAllocs),
      static_cast<unsigned long long>(peak.liveAllocs),
      static_cast<unsigned long long>(last.liveAllocs));

  // Steady state, reached in the warm-up and held: not one of the four is ever
  // above where it started, at any cycle boundary, however many cycles there
  // are.  `peak` rather than `last` on purpose -- a series that climbs for
  // 10 000 cycles and is then released by something at the end is still a leak
  // for the ten thousand cycles it was climbing.
  CHECK_EQ(peak.children, base.children);
  CHECK_EQ(peak.hosts, base.hosts);
  CHECK_EQ(peak.parked, base.parked);
  CHECK_EQ(peak.liveAllocs, base.liveAllocs);
  CHECK_EQ(last.children, base.children);
  CHECK_EQ(last.hosts, base.hosts);
  CHECK_EQ(last.parked, base.parked);
  CHECK_EQ(last.liveAllocs, base.liveAllocs);

  // The engine is genuinely off again afterwards, which is the property every
  // "one load and one predicted branch" claim in Widget rests on.
  CHECK_EQ(geeyoou::detail::parkedLayoutCount(), std::size_t(0));
  CHECK(root.children().empty());

  // Every one of the five hint-side doors opened on every cycle, warm-up
  // included: exactly once each, no more (a door opening twice would mean a
  // second, unaccounted pass) and no fewer (a door that stopped opening turns
  // its group into scenery that keeps passing).  This is also the determinism
  // statement: there is no RNG anywhere in cycle(), so these are the same five
  // numbers on every machine and in every configuration.
  const std::size_t opened = std::size_t(cycles) + 3;  // + the warm-up cycles
  CHECK_EQ(g_doors.groupBoxMeasure, opened);
  CHECK_EQ(g_doors.scrollHint, opened);
  CHECK_EQ(g_doors.scrollContent, opened);
  CHECK_EQ(g_doors.scrollViewport, opened);
  CHECK_EQ(g_doors.shellResize, opened);

  // ...and every one of them was SURVIVED rather than avoided.
  //
  // The five counters above say the doors opened.  They cannot say what
  // happened on the way back, and "the process is still alive" cannot either --
  // a guard wired the wrong way round, a checkpoint deleted, a door that stopped
  // reaching its checkpoint because a rectangle stopped changing all leave the
  // suite green.  This is the positive fact: five frames per cycle found the
  // tree moved under them and said so.
  //
  // FIVE PER CYCLE IS ARITHMETIC ABOUT THESE FIVE GROUPS, NOT A LAW.  It is
  // emphatically NOT "one per door" and not "one per operation" -- REM3-G8
  // counts FRAMES, and section 11.3 of docs/iterations/02-layout-engine.md
  // records the case where one setContentSize honestly raises it twice
  // (relayout's own frame, then setContentSize's CP-C2 after it returns).
  // tests/widget/test_rem3_doors.cpp holds that case.  Each of the five groups
  // below happens to kill its target at the FIRST checkpoint the frame reaches,
  // which is what makes the number here one apiece:
  //
  //   groupBoxMeasure -> CP-G1     scrollHint      -> CP-S1
  //   scrollContent   -> CP-C1     scrollViewport  -> CP-S2
  //   shellResize     -> CP-S1
  //
  // Add a sixth group, or move an existing one's hook one door later, and this
  // number changes -- correctly.  Recompute it from the table above; do not
  // relax it to a >=.
  CHECK_EQ(std::size_t(geeyoou::detail::layoutDiagnostics().framesDegraded),
           opened * 5);

  // The other four diagnostics are NOT asserted at zero here on purpose: the
  // refused-pass group raises `reentered` by design, and pinning a number this
  // case does not exist to check would make it fail for somebody else's
  // perfectly correct change.
  geeyoou::detail::resetLayoutDiagnostics();
}
