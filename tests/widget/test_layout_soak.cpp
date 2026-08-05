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
#include "geeyoou/widget/Layout.hpp"
#include "geeyoou/widget/Widget.hpp"

using geeyoou::BoxLayout;
using geeyoou::GridLayout;
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

  root.removeChild(page);
}

}  // namespace

GEEYOOU_TEST(layout_soak, nothing_the_engine_holds_grows_with_the_number_of_cycles) {
  const int cycles = soakCycles();

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
}
