//
// Widget::hitTest tests.
//
// Hit testing is where a painting order bug turns into an input bug, and input
// bugs on a plant display are the expensive kind: the operator presses the
// visible button and the invisible one underneath it responds.  The first case
// below is the one that matters -- it fails if the reverse iteration in
// Widget::hitTest is ever turned back into a forward one.
//
#include "geeyoou/widget/Widget.hpp"

#include "framework/Test.hpp"

using geeyoou::Widget;
using geeyoou::test::AllocGuard;

// -------------------------------------------------------------- paint order ---
GEEYOOU_TEST(hittest, the_last_painted_sibling_is_hit_first) {
  Widget root;
  root.setGeometry({0.0f, 0.0f, 200.0f, 200.0f});

  // Same rectangle, added in this order.  `over` is painted second, so it is on
  // top, so it must own every pixel they share.
  Widget* under = root.add<Widget>();
  under->setGeometry({10.0f, 10.0f, 100.0f, 100.0f});
  Widget* over = root.add<Widget>();
  over->setGeometry({10.0f, 10.0f, 100.0f, 100.0f});

  CHECK_EQ(root.hitTest({50.0f, 50.0f}), over);
  CHECK_EQ(root.hitTest({15.0f, 15.0f}), over);

  // Partial overlap: each keeps its exclusive area, the shared strip goes to the
  // one drawn later.
  over->setGeometry({60.0f, 10.0f, 100.0f, 100.0f});
  CHECK_EQ(root.hitTest({20.0f, 50.0f}), under);  // only under
  CHECK_EQ(root.hitTest({80.0f, 50.0f}), over);   // shared -> the later one
  CHECK_EQ(root.hitTest({140.0f, 50.0f}), over);  // only over

  // Three deep in the same spot: the newest still wins.
  Widget* newest = root.add<Widget>();
  newest->setGeometry({10.0f, 10.0f, 150.0f, 100.0f});
  CHECK_EQ(root.hitTest({80.0f, 50.0f}), newest);
}

GEEYOOU_TEST(hittest, the_deepest_descendant_wins_over_its_container) {
  Widget root;
  root.setGeometry({0.0f, 0.0f, 200.0f, 200.0f});
  Widget* panel = root.add<Widget>();
  panel->setGeometry({20.0f, 20.0f, 100.0f, 100.0f});  // window 20..120
  Widget* leaf = panel->add<Widget>();
  leaf->setGeometry({10.0f, 10.0f, 30.0f, 30.0f});  // window 30..60

  CHECK_EQ(root.hitTest({35.0f, 35.0f}), leaf);
  CHECK_EQ(root.hitTest({100.0f, 100.0f}), panel);  // inside panel, outside leaf
  CHECK_EQ(root.hitTest({150.0f, 150.0f}), &root);

  // Asking a subtree directly answers about that subtree only.
  CHECK_EQ(panel->hitTest({35.0f, 35.0f}), leaf);
  CHECK_EQ(panel->hitTest({150.0f, 150.0f}), static_cast<Widget*>(nullptr));
}

GEEYOOU_TEST(hittest, outside_the_root_returns_null) {
  Widget root;
  root.setGeometry({0.0f, 0.0f, 200.0f, 200.0f});
  Widget* child = root.add<Widget>();
  child->setGeometry({10.0f, 10.0f, 20.0f, 20.0f});  // window 10..30

  CHECK_EQ(root.hitTest({-1.0f, 100.0f}), static_cast<Widget*>(nullptr));
  CHECK_EQ(root.hitTest({100.0f, -1.0f}), static_cast<Widget*>(nullptr));
  CHECK_EQ(root.hitTest({200.0f, 100.0f}), static_cast<Widget*>(nullptr));
  CHECK_EQ(root.hitTest({500.0f, 500.0f}), static_cast<Widget*>(nullptr));

  // Bounds are half-open: the top-left edge belongs to the widget, the
  // bottom-right edge belongs to whatever is behind it.  Two adjacent widgets
  // therefore never both claim the seam.
  CHECK_EQ(root.hitTest({10.0f, 10.0f}), child);
  CHECK_EQ(root.hitTest({29.9f, 29.9f}), child);
  CHECK_EQ(root.hitTest({30.0f, 20.0f}), &root);
  CHECK_EQ(root.hitTest({20.0f, 30.0f}), &root);
}

// ---------------------------------------------------------- pruned subtrees ---
GEEYOOU_TEST(hittest, hidden_and_disabled_subtrees_are_transparent_to_the_mouse) {
  Widget root;
  root.setGeometry({0.0f, 0.0f, 200.0f, 200.0f});
  Widget* back = root.add<Widget>();
  back->setGeometry({0.0f, 0.0f, 100.0f, 100.0f});
  Widget* top = root.add<Widget>();
  top->setGeometry({0.0f, 0.0f, 100.0f, 100.0f});
  Widget* topChild = top->add<Widget>();
  topChild->setGeometry({0.0f, 0.0f, 20.0f, 20.0f});

  CHECK_EQ(root.hitTest({10.0f, 10.0f}), topChild);

  // Hiding the container takes its children with it -- the click falls through
  // to what is behind, rather than to a widget nobody can see.
  top->setVisible(false);
  CHECK_EQ(root.hitTest({10.0f, 10.0f}), back);
  top->setVisible(true);
  CHECK_EQ(root.hitTest({10.0f, 10.0f}), topChild);

  // Disabling does the same for input: an interlocked panel must not swallow
  // clicks meant for the screen behind it.
  top->setEnabled(false);
  CHECK_EQ(root.hitTest({10.0f, 10.0f}), back);

  back->setEnabled(false);
  CHECK_EQ(root.hitTest({10.0f, 10.0f}), &root);

  root.setEnabled(false);
  CHECK_EQ(root.hitTest({10.0f, 10.0f}), static_cast<Widget*>(nullptr));

  // A disabled LEAF inside an enabled container yields to the container, not to
  // the sibling behind the container.
  root.setEnabled(true);
  top->setEnabled(true);
  topChild->setEnabled(false);
  CHECK_EQ(root.hitTest({10.0f, 10.0f}), top);
}

GEEYOOU_TEST(hittest, follows_the_containers_content_offset) {
  Widget root;
  root.setGeometry({0.0f, 0.0f, 200.0f, 200.0f});
  Widget* list = root.add<Widget>();
  list->setGeometry({0.0f, 0.0f, 100.0f, 100.0f});
  Widget* row0 = list->add<Widget>();
  row0->setGeometry({0.0f, 0.0f, 100.0f, 20.0f});
  Widget* row1 = list->add<Widget>();
  row1->setGeometry({0.0f, 20.0f, 100.0f, 20.0f});

  CHECK_EQ(root.hitTest({50.0f, 10.0f}), row0);
  CHECK_EQ(root.hitTest({50.0f, 30.0f}), row1);

  // Scrolling one row down: the point that used to hit row0 now hits row1, and
  // row0 has left the container entirely.
  list->setContentOffset({0.0f, 20.0f});
  CHECK_EQ(root.hitTest({50.0f, 10.0f}), row1);
  CHECK_EQ(root.hitTest({50.0f, 30.0f}), list);

  // NOTE: hitTest clips to each widget's OWN rect only, so a scrolled-out row
  // that still overlaps the container's bounds is reachable.  Painting clips it
  // away (paintTree intersects with the ancestor clip); hit testing does not.
  // Recorded rather than asserted as correct -- see the accompanying report.
}

// ------------------------------------------------------------- hot path cost ---
GEEYOOU_TEST(hittest, allocates_nothing) {
  Widget root;
  root.setGeometry({0.0f, 0.0f, 400.0f, 300.0f});
  Widget* panel = root.add<Widget>();
  panel->setGeometry({10.0f, 10.0f, 380.0f, 280.0f});
  for (int i = 0; i < 32; ++i) {
    Widget* row = panel->add<Widget>();
    row->setGeometry({0.0f, float(i) * 8.0f, 380.0f, 8.0f});
    row->add<Widget>()->setGeometry({4.0f, 1.0f, 100.0f, 6.0f});
  }

  // Every mouse move on an HMI screen runs this.  docs/architecture.md section 1
  // rule 2 says the hot path allocates nothing; this is that claim, measured.
  const AllocGuard g;
  const Widget* last = nullptr;
  for (int i = 0; i < 64; ++i) {
    last = root.hitTest({float(20 + i), float(20 + i)});
  }
  CHECK_EQ(g.count(), std::uint64_t(0));
  CHECK(last != nullptr);
}
