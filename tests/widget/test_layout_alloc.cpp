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
#include <string>

#include "framework/Test.hpp"
#include "geeyoou/widget/BoxLayout.hpp"
#include "geeyoou/widget/GridLayout.hpp"
#include "geeyoou/widget/Widget.hpp"

using geeyoou::BoxLayout;
using geeyoou::GridLayout;
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
