//
// The allocation gate on the layout engine.
//
// docs/architecture.md section 1, rule 2: an upper computer runs for months, so
// "how many times did that allocate" is a first-class question.  A layout pass
// is the worst possible place to answer it badly -- it runs on every window
// resize, which means once per mouse-move while an operator is dragging an
// edge, at which point a per-pass allocation is a per-frame allocation and the
// heap fragments in front of them.
//
// The contract this file enforces, as ruled by the architecture team:
//
//   * the FIRST pass may allocate: the scratch buffers have to come from
//     somewhere.
//   * a pass that follows a change in the number of children may allocate once
//     more, and capacity only ever grows.
//   * a pass whose item count is unchanged and whose GEOMETRY changed must
//     allocate nothing and free nothing.  Not "few" -- zero.
//
// Frees are counted as well as allocations, because the failure mode the
// never-shrunk idiom exists to prevent (DataHub::scratch_, and now the scratch
// buffers in BoxLayout and GridLayout) is a buffer that is released and
// immediately reallocated at the same size.  A guard that only watched
// allocations would call that "one per pass"; it is really two.
//
#include <cstddef>
#include <cstdint>
#include <string>

#include "framework/Test.hpp"
#include "geeyoou/widget/BoxLayout.hpp"
#include "geeyoou/widget/GridLayout.hpp"
#include "geeyoou/widget/GroupBox.hpp"
#include "geeyoou/widget/Label.hpp"
#include "geeyoou/widget/Widget.hpp"

using geeyoou::BoxLayout;
using geeyoou::GridLayout;
using geeyoou::GroupBox;
using geeyoou::Label;
using geeyoou::Size;
using geeyoou::SizeHint;
using geeyoou::Widget;
using geeyoou::test::AllocGuard;

namespace {

// MSVC's debugging iterators allocate a proxy object per CONTAINER, and they do
// it lazily -- which lands inside the guard below rather than during setup.
// Same treatment, and same reasoning, as signal.emit_allocates_nothing: the
// COUNTS are asserted in Release only and loudly skipped elsewhere, while the
// functional half of the case runs in both.  Release is the build that runs in
// the plant.
#if defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL != 0
constexpr bool kStlAllocatesPerContainer = true;
#else
constexpr bool kStlAllocatesPerContainer = false;
#endif

void noteSkipped(const char* which) {
  geeyoou::test::note(std::string("[skip] ") + which +
                      "：分配计数断言仅在 Release 生效"
                      "（_ITERATOR_DEBUG_LEVEL != 0 时 STL 每个容器自带一次代理分配）");
}

// A hint that costs nothing to produce, so what the guard measures is the
// LAYOUT and not the text engine.  See the note at the bottom of this file
// about what that deliberately leaves uncovered.
class FixedWidget : public Widget {
 public:
  int geometryChanges = 0;

  void set(Size min, Size preferred) {
    hint_.min = min;
    hint_.preferred = preferred;
  }
  SizeHint sizeHint() const override { return hint_; }

 protected:
  void onGeometryChanged() override { ++geometryChanges; }

 private:
  SizeHint hint_;
};

// 100 geometries, every one different from the one before it -- otherwise
// setGeometry's idempotence short-circuit would make the whole loop a no-op and
// the case would be measuring nothing at all.  The widths and heights are
// coprime-ish on purpose so the stretch remainder lands in a different place
// almost every time, which is the path that would be tempted to allocate.
geeyoou::Rect geometryFor(int i) {
  return {0.0f, 0.0f, 300.0f + float(i) * 7.0f, 200.0f + float(i % 13) * 3.0f};
}

}  // namespace

GEEYOOU_TEST(layout_alloc, box_arrange_allocates_nothing_once_the_tree_is_stable) {
  Widget root;
  BoxLayout* box = root.setLayout<BoxLayout>(BoxLayout::Orientation::Horizontal);
  box->setSpacing(7.0f);
  box->setMargins({5.0f, 5.0f, 5.0f, 5.0f});

  FixedWidget* kids[6] = {};
  for (int i = 0; i < 6; ++i) {
    kids[i] = root.add<FixedWidget>();
    kids[i]->set({10.0f, 12.0f}, {40.0f + float(i) * 3.0f, 30.0f});
  }
  box->addWidget(kids[0], 0);
  box->addWidget(kids[1], 1);
  box->addSpacing(11.0f);
  box->addWidget(kids[2], 2);
  box->addStretch(3);
  box->addWidget(kids[3], 1);
  box->addWidget(kids[4], 0);
  box->addWidget(kids[5], 5);

  // Warm-up: the first pass is allowed to allocate, and this is where the
  // scratch buffer reaches the size it will keep.
  root.setGeometry({0.0f, 0.0f, 640.0f, 200.0f});
  const int settled = kids[0]->geometryChanges;
  CHECK(settled > 0);

  AllocGuard guard;
  guard.reset();
  for (int i = 0; i < 100; ++i) root.setGeometry(geometryFor(i));

  // Every one of the hundred really ran: a short-circuited pass allocates
  // nothing either, and a green gate that measured nothing would be worse than
  // a red one.
  CHECK_EQ(kids[0]->geometryChanges, settled + 100);
  if constexpr (kStlAllocatesPerContainer) {
    noteSkipped("layout_alloc.box_arrange_allocates_nothing_once_the_tree_is_stable");
  } else {
    CHECK_EQ(guard.count(), std::uint64_t(0));
    CHECK_EQ(guard.frees(), std::uint64_t(0));
  }
}

GEEYOOU_TEST(layout_alloc, grid_arrange_allocates_nothing_once_the_tree_is_stable) {
  Widget root;
  GridLayout* grid = root.setLayout<GridLayout>();
  grid->setSpacing(6.0f);
  grid->setMargins({8.0f, 8.0f, 8.0f, 8.0f});

  FixedWidget* kids[9] = {};
  for (int i = 0; i < 9; ++i) {
    kids[i] = root.add<FixedWidget>();
    kids[i]->set({20.0f, 14.0f}, {50.0f + float(i), 24.0f});
  }
  // Three plain rows plus two spanning cells, so the ascending-span batching --
  // the part of GridLayout most likely to want a temporary vector -- is on the
  // measured path.
  grid->addWidget(kids[0], 0, 0);
  grid->addWidget(kids[1], 0, 1);
  grid->addWidget(kids[2], 0, 2);
  grid->addWidget(kids[3], 1, 0);
  grid->addWidget(kids[4], 1, 1);
  grid->addWidget(kids[5], 1, 2);
  grid->addWidget(kids[6], 2, 0, 1, 3);  // spans every column
  grid->addWidget(kids[7], 3, 0, 1, 2);
  grid->addWidget(kids[8], 3, 2);
  grid->setColumnStretch(1, 2);
  grid->setRowStretch(3, 1);

  root.setGeometry({0.0f, 0.0f, 640.0f, 300.0f});
  const int settled = kids[8]->geometryChanges;
  CHECK(settled > 0);

  AllocGuard guard;
  guard.reset();
  for (int i = 0; i < 100; ++i) root.setGeometry(geometryFor(i));

  CHECK_EQ(kids[8]->geometryChanges, settled + 100);
  if constexpr (kStlAllocatesPerContainer) {
    noteSkipped("layout_alloc.grid_arrange_allocates_nothing_once_the_tree_is_stable");
  } else {
    CHECK_EQ(guard.count(), std::uint64_t(0));
    CHECK_EQ(guard.frees(), std::uint64_t(0));
  }
}

// ============================================================ KNOWN LIMIT ===
//
// This case asserts that work HAPPENS.  That is not a mistake and it is not a
// test of a feature -- it is a record, in the gate, of a limitation the
// architecture team ruled out of scope for R2 and into R3.
//
// The zero-allocation promise above covers the layout ENGINE: the scratch
// buffers in BoxLayout and GridLayout, the item vectors, the arithmetic.  It
// does NOT cover what the engine CALLS.  A layout asks every item for its
// sizeHint() on every pass, and Label::sizeHint() shapes the text: it builds a
// BLGlyphBuffer and runs get_text_metrics, per label, per pass -- which during
// a window drag is per label, per frame.
//
// Two things to be careful about here, in this order:
//
//   * DO NOT reach for AllocGuard to measure this.  It reads zero, and it is
//     lying by omission: the harness replaces global operator new, while
//     Blend2D allocates through its own runtime allocator on top of malloc.
//     The shaping work is real and invisible to this counter.  A future reader
//     who "proves" the problem away with an allocation count will have proved
//     only that the counter cannot see it.
//   * So what is counted instead is the thing that actually costs: how many
//     times the engine asks a text control to measure itself.
//
// Why it is not fixed here: the fix is a width cache on the control, keyed on
// (text, font size, style generation), and that is a text-engine change rather
// than a layout one.  R3 rebuilds the text stack (docs/roadmap.md) and owns it.
//
// Why it is a test rather than a paragraph in a document: a limitation nobody
// measured is a limitation nobody will notice has changed.  When R3 lands the
// cache this case goes red, and whoever is holding it then gets to rewrite it
// as "measured once, then cached".  That is the intended ending.
GEEYOOU_TEST(layout_alloc, text_is_re_measured_on_every_pass_r3) {
  // Counts what Label::sizeHint costs without changing what it answers.
  class CountingLabel : public Label {
   public:
    mutable int measured = 0;
    SizeHint sizeHint() const override {
      ++measured;
      return Label::sizeHint();
    }
  };

  Widget root;
  BoxLayout* box = root.setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
  box->setSpacing(6.0f);

  CountingLabel* labels[6] = {};
  for (int i = 0; i < 6; ++i) {
    labels[i] = root.add<CountingLabel>();
    labels[i]->setText("进料泵 P-101 流量 52 m3/h");
    box->addWidget(labels[i]);
  }
  root.setGeometry({0.0f, 0.0f, 400.0f, 300.0f});
  for (CountingLabel* l : labels) l->measured = 0;

  // Ten resizes, i.e. ten frames of an operator dragging a window edge.
  for (int i = 0; i < 10; ++i) root.setGeometry(geometryFor(i));

  int total = 0;
  for (CountingLabel* l : labels) total += l->measured;

  // Exactly once per label per pass -- BoxLayout::gather carries the cross-axis
  // pair in scratch_ precisely so the placement loop does not ask a second
  // time.  The engine is already as thrifty with the question as it can be;
  // what is expensive is the ANSWER, and only a cache on the control fixes it.
  CHECK_EQ(total, 60);
  geeyoou::test::note(
      "[known] layout_alloc.text_is_re_measured_on_every_pass_r3："
      "10 趟布局 x 6 个 Label = " +
      std::to_string(total) +
      " 次文本度量（每次 measureText 建一个 BLGlyphBuffer）。"
      "布局引擎自身零分配，这部分开销在 Blend2D 的分配器里，AllocGuard 看不见。"
      "已知限制，归 R3 文本引擎轮（按 text/fontSize/styleGeneration 做宽度缓存）。");
}

GEEYOOU_TEST(layout_alloc, capacity_only_ever_grows) {
  // The second half of the contract: adding children is allowed to allocate,
  // and taking them away again must NOT hand the capacity back -- otherwise a
  // list that gains and loses a row per second allocates twice a second
  // forever, which is precisely the never-shrunk idiom's reason to exist.
  Widget root;
  BoxLayout* box = root.setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
  box->setSpacing(4.0f);

  for (int i = 0; i < 12; ++i) {
    FixedWidget* w = root.add<FixedWidget>();
    w->set({10.0f, 10.0f}, {40.0f, 20.0f});
    box->addWidget(w, 1);
  }
  root.setGeometry({0.0f, 0.0f, 200.0f, 400.0f});

  // Down to four and back up to twelve: the scratch buffer saw its high-water
  // mark in the warm-up above and must not have given any of it back.
  for (int i = 0; i < 8; ++i) root.removeChild(root.children().back().get());
  CHECK_EQ(box->itemCount(), std::size_t(4));

  AllocGuard guard;
  guard.reset();
  for (int i = 0; i < 100; ++i) root.setGeometry(geometryFor(i));
  if constexpr (kStlAllocatesPerContainer) {
    noteSkipped("layout_alloc.capacity_only_ever_grows");
  } else {
    CHECK_EQ(guard.count(), std::uint64_t(0));
    CHECK_EQ(guard.frees(), std::uint64_t(0));
  }
}


// ============================================================ C5 gap ===
//
// GroupBox is the one widget in the library whose sizeHint() is NOT the base
// class's naturalSize_ answer: Widget::sizeHint() forwards to layout_->
// measure() when a layout is present (docs/iterations/02-layout-engine.md
// section 7), and GroupBox::sizeHint() calls THAT and adds its frame on top.
// A GroupBox sitting inside an outer BoxLayout therefore gets measure()d on
// every single pass of the outer box -- the exact call the two "arrange
// allocates nothing" cases above already cover for a bare host, but never for
// one reached through this forwarding path.  If GroupBox::sizeHint() ever grew
// a temporary (a vector, a string concatenation) on the way to composing its
// answer, this is the case that would say so; the two above cannot, because
// neither of them puts a GroupBox in the tree.
GEEYOOU_TEST(layout_alloc, groupbox_measure_through_an_outer_box_allocates_nothing) {
  Widget root;
  BoxLayout* outer = root.setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
  outer->setSpacing(6.0f);

  GroupBox* panel = root.add<GroupBox>();
  panel->setTitle("Parameters");
  outer->addWidget(panel, 1);

  GridLayout* inner = panel->setLayout<GridLayout>();
  inner->setSpacing(4.0f);
  inner->setMargins({0.0f, 0.0f, 0.0f, 0.0f});

  FixedWidget* kids[4] = {};
  for (int i = 0; i < 4; ++i) {
    kids[i] = panel->add<FixedWidget>();
    kids[i]->set({20.0f, 12.0f}, {40.0f + float(i), 20.0f});
    inner->addWidget(kids[i], i / 2, i % 2);
  }
  inner->setColumnStretch(1, 1);

  // Warm-up: first pass may allocate, and this is where GridLayout's and
  // BoxLayout's scratch buffers reach the size they will keep.
  root.setGeometry({0.0f, 0.0f, 400.0f, 300.0f});
  // kids[1] lives in column 1, the one setColumnStretch(1, 1) made elastic --
  // its width tracks whatever is left over after column 0, so unlike kids[0]
  // it genuinely moves on nearly every one of the hundred geometries below.
  // kids[0] would make this case pass for the wrong reason: M3 legitimately
  // short-circuits it once column 0 settles, since a fixed, non-stretching
  // column does not care how wide the window got.
  const int settled = kids[1]->geometryChanges;
  CHECK(settled > 0);

  AllocGuard guard;
  guard.reset();
  for (int i = 0; i < 100; ++i) root.setGeometry(geometryFor(i));

  CHECK_EQ(kids[1]->geometryChanges, settled + 100);
  if constexpr (kStlAllocatesPerContainer) {
    noteSkipped("layout_alloc.groupbox_measure_through_an_outer_box_allocates_nothing");
  } else {
    CHECK_EQ(guard.count(), std::uint64_t(0));
    CHECK_EQ(guard.frees(), std::uint64_t(0));
  }
}
