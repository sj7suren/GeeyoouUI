//
// The two concrete layouts and the six size hints.
//
// test_layout_engine.cpp pins down the PROTOCOL (when a pass runs, what it may
// touch, what happens when the tree changes underneath it).  This file pins
// down the ARITHMETIC, and the arithmetic is where a layout engine is actually
// wrong: a pixel of rounding handed to the wrong item is not a rounding error,
// it is a control that twitches every frame on a screen somebody watches for a
// twelve-hour shift.
//
#include <cstddef>
#include <string>

#include "framework/Test.hpp"
#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/BoxLayout.hpp"
#include "geeyoou/widget/CheckBox.hpp"
#include "geeyoou/widget/GridLayout.hpp"
#include "geeyoou/widget/GroupBox.hpp"
#include "geeyoou/widget/Label.hpp"
#include "geeyoou/widget/LineEdit.hpp"
#include "geeyoou/widget/PushButton.hpp"
#include "geeyoou/widget/SpinBox.hpp"
#include "geeyoou/widget/Widget.hpp"

using geeyoou::BoxLayout;
using geeyoou::CheckBox;
using geeyoou::GridLayout;
using geeyoou::GroupBox;
using geeyoou::Icon;
using geeyoou::Label;
using geeyoou::LineEdit;
using geeyoou::PushButton;
using geeyoou::Rect;
using geeyoou::Size;
using geeyoou::SizeHint;
using geeyoou::SpinBox;
using geeyoou::Widget;

namespace {
constexpr float kEps = 0.0005f;
constexpr float kUnbounded = geeyoou::kUnbounded;

// A widget whose hint is whatever the case says it is.  Every number below is
// therefore a property of the LAYOUT, not of the font this machine happens to
// have installed.
class FixedWidget : public Widget {
 public:
  FixedWidget() = default;

  void set(Size min, Size preferred, Size max = Size{kUnbounded, kUnbounded}) {
    hint_.min = min;
    hint_.preferred = preferred;
    hint_.max = max;
    invalidateSizeHint();
  }

  SizeHint sizeHint() const override { return hint_; }

 private:
  SizeHint hint_;
};

FixedWidget* addFixed(Widget& parent, Size min, Size preferred,
                      Size max = Size{kUnbounded, kUnbounded}) {
  FixedWidget* w = parent.add<FixedWidget>();
  w->set(min, preferred, max);
  return w;
}

}  // namespace

// =================================================================== T-06 ===
GEEYOOU_TEST(box, items_get_their_preferred_size_with_spacing_between_widgets) {
  Widget root;
  BoxLayout* box = root.setLayout<BoxLayout>(BoxLayout::Orientation::Horizontal);
  FixedWidget* a = addFixed(root, {0.0f, 0.0f}, {40.0f, 20.0f});
  FixedWidget* b = addFixed(root, {0.0f, 0.0f}, {60.0f, 20.0f});
  box->addWidget(a);
  box->addWidget(b);
  box->setSpacing(10.0f);

  // Exactly enough: 40 + 10 + 60.
  root.setGeometry({0.0f, 0.0f, 110.0f, 50.0f});
  CHECK_NEAR(a->geometry().x(), 0.0f, kEps);
  CHECK_NEAR(a->geometry().width(), 40.0f, kEps);
  CHECK_NEAR(b->geometry().x(), 50.0f, kEps);
  CHECK_NEAR(b->geometry().width(), 60.0f, kEps);
  // No stretch anywhere: the cross axis still fills, because nothing said it
  // should not.
  CHECK_NEAR(a->geometry().height(), 50.0f, kEps);
  CHECK(!root.lastLayoutOverflow().any());

  // More room than anybody asked for, and nobody stretches: the leftover is
  // simply not used, and the row stays packed at the start.
  root.setGeometry({0.0f, 0.0f, 300.0f, 50.0f});
  CHECK_NEAR(a->geometry().width(), 40.0f, kEps);
  CHECK_NEAR(b->geometry().x(), 50.0f, kEps);
  CHECK_NEAR(b->geometry().width(), 60.0f, kEps);

  // Margins come out before the layout ever sees the rectangle.
  box->setMargins({5.0f, 4.0f, 5.0f, 4.0f});
  CHECK_NEAR(a->geometry().x(), 5.0f, kEps);
  CHECK_NEAR(a->geometry().y(), 4.0f, kEps);
  CHECK_NEAR(a->geometry().height(), 42.0f, kEps);
}

GEEYOOU_TEST(box, the_leftover_is_split_by_weight_and_the_remainder_is_placed) {
  Widget root;
  BoxLayout* box = root.setLayout<BoxLayout>(BoxLayout::Orientation::Horizontal);
  FixedWidget* a = addFixed(root, {0.0f, 0.0f}, {10.0f, 10.0f});
  FixedWidget* b = addFixed(root, {0.0f, 0.0f}, {10.0f, 10.0f});
  FixedWidget* c = addFixed(root, {0.0f, 0.0f}, {10.0f, 10.0f});
  box->setSpacing(0.0f);
  box->addWidget(a, 1);
  box->addWidget(b, 1);
  box->addWidget(c, 1);

  // 100 - 30 preferred = 70 to share three ways.  70/3 is 23 and a third, so
  // the shares are 23 each and ONE pixel is left over.  Equal weights: it goes
  // to the last item, and the row ends exactly on the content edge.
  root.setGeometry({0.0f, 0.0f, 100.0f, 20.0f});
  CHECK_NEAR(a->geometry().width(), 33.0f, kEps);
  CHECK_NEAR(b->geometry().width(), 33.0f, kEps);
  CHECK_NEAR(c->geometry().width(), 34.0f, kEps);
  CHECK_NEAR(c->geometry().right(), 100.0f, kEps);

  // Two pixels over, this time: 71 to share three ways is 23 each with TWO
  // left, and both go to the same place.  The row still ends on the edge.
  root.setGeometry({0.0f, 0.0f, 101.0f, 20.0f});
  CHECK_NEAR(a->geometry().width(), 33.0f, kEps);
  CHECK_NEAR(b->geometry().width(), 33.0f, kEps);
  CHECK_NEAR(c->geometry().width(), 35.0f, kEps);
  CHECK_NEAR(c->geometry().right(), 101.0f, kEps);

  // Landing on the same pixels a hundred times over is the whole reason the
  // remainder has a rule at all: an item that gained a pixel one frame and lost
  // it the next reads as a shimmer on a screen somebody watches all shift.
  const Rect first = a->geometry();
  const Rect firstC = c->geometry();
  for (int i = 0; i < 100; ++i) {
    root.setGeometry({0.0f, 0.0f, 40.0f, 20.0f});
    root.setGeometry({0.0f, 0.0f, 101.0f, 20.0f});
    CHECK(a->geometry() == first);
    CHECK(c->geometry() == firstC);
  }
}

GEEYOOU_TEST(box, the_remainder_follows_the_largest_weight) {
  Widget root;
  BoxLayout* box = root.setLayout<BoxLayout>(BoxLayout::Orientation::Horizontal);
  FixedWidget* a = addFixed(root, {0.0f, 0.0f}, {10.0f, 10.0f});
  FixedWidget* b = addFixed(root, {0.0f, 0.0f}, {10.0f, 10.0f});
  FixedWidget* c = addFixed(root, {0.0f, 0.0f}, {10.0f, 10.0f});
  box->setSpacing(0.0f);
  box->addWidget(a, 3);
  box->addWidget(b, 1);
  box->addWidget(c, 1);

  // 101 - 30 = 71 shared as 3:1:1 -> 42, 14, 14 = 70; the spare pixel goes to
  // the weight-3 item.
  root.setGeometry({0.0f, 0.0f, 101.0f, 20.0f});
  CHECK_NEAR(a->geometry().width(), 53.0f, kEps);
  CHECK_NEAR(b->geometry().width(), 24.0f, kEps);
  CHECK_NEAR(c->geometry().width(), 24.0f, kEps);
  CHECK_NEAR(c->geometry().right(), 101.0f, kEps);
}

GEEYOOU_TEST(box, an_item_never_grows_past_its_maximum) {
  Widget root;
  BoxLayout* box = root.setLayout<BoxLayout>(BoxLayout::Orientation::Horizontal);
  // Capped at 30 -- a toggle that would look absurd stretched across a panel.
  FixedWidget* a = addFixed(root, {0.0f, 0.0f}, {10.0f, 10.0f}, {30.0f, kUnbounded});
  FixedWidget* b = addFixed(root, {0.0f, 0.0f}, {10.0f, 10.0f});
  box->setSpacing(0.0f);
  box->addWidget(a, 1);
  box->addWidget(b, 1);

  // 180 to share equally would be 90 each; `a` saturates at 30 and the rest is
  // handed on rather than lost.
  root.setGeometry({0.0f, 0.0f, 200.0f, 20.0f});
  CHECK_NEAR(a->geometry().width(), 30.0f, kEps);
  CHECK_NEAR(b->geometry().width(), 170.0f, kEps);
  CHECK_NEAR(b->geometry().right(), 200.0f, kEps);

  // Nobody can take it: the space is genuinely unusable and stays empty rather
  // than being forced on a widget that said no.
  b->set({0.0f, 0.0f}, {10.0f, 10.0f}, {40.0f, kUnbounded});
  root.setGeometry({0.0f, 0.0f, 201.0f, 20.0f});
  CHECK_NEAR(a->geometry().width(), 30.0f, kEps);
  CHECK_NEAR(b->geometry().width(), 40.0f, kEps);
}

GEEYOOU_TEST(box, spacers_take_room_without_taking_spacing) {
  Widget root;
  BoxLayout* box = root.setLayout<BoxLayout>(BoxLayout::Orientation::Horizontal);
  FixedWidget* a = addFixed(root, {0.0f, 0.0f}, {30.0f, 10.0f});
  FixedWidget* b = addFixed(root, {0.0f, 0.0f}, {30.0f, 10.0f});
  box->setSpacing(8.0f);
  box->addWidget(a);
  box->addSpacing(20.0f);
  box->addWidget(b);

  // 20 means twenty, not twenty plus two helpings of spacing().
  root.setGeometry({0.0f, 0.0f, 200.0f, 20.0f});
  CHECK_NEAR(a->geometry().right(), 30.0f, kEps);
  CHECK_NEAR(b->geometry().x(), 50.0f, kEps);

  // addStretch pushes what follows to the far end -- and eats nothing else.
  Widget root2;
  BoxLayout* box2 = root2.setLayout<BoxLayout>(BoxLayout::Orientation::Horizontal);
  FixedWidget* c = addFixed(root2, {0.0f, 0.0f}, {30.0f, 10.0f});
  FixedWidget* d = addFixed(root2, {0.0f, 0.0f}, {30.0f, 10.0f});
  box2->setSpacing(8.0f);
  box2->addWidget(c);
  box2->addStretch();
  box2->addWidget(d);
  root2.setGeometry({0.0f, 0.0f, 200.0f, 20.0f});
  CHECK_NEAR(c->geometry().x(), 0.0f, kEps);
  CHECK_NEAR(d->geometry().right(), 200.0f, kEps);
  CHECK_NEAR(d->geometry().x(), 170.0f, kEps);
}

GEEYOOU_TEST(box, too_little_room_clamps_at_the_minimum_and_reports_the_shortfall) {
  Widget root;
  BoxLayout* box = root.setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
  FixedWidget* a = addFixed(root, {10.0f, 40.0f}, {10.0f, 60.0f});
  FixedWidget* b = addFixed(root, {10.0f, 40.0f}, {10.0f, 60.0f});
  FixedWidget* c = addFixed(root, {10.0f, 40.0f}, {10.0f, 60.0f});
  box->setSpacing(0.0f);
  box->addWidget(a, 1);
  box->addWidget(b, 1);
  box->addWidget(c, 1);

  // 100 for three items that will not go below 40 each.  Everyone keeps their
  // minimum and the tail runs off the end: a squashed 33px row is unreadable,
  // a clipped one is obviously wrong and the counter says so.
  root.setGeometry({0.0f, 0.0f, 50.0f, 100.0f});
  CHECK_NEAR(a->geometry().height(), 40.0f, kEps);
  CHECK_NEAR(b->geometry().height(), 40.0f, kEps);
  CHECK_NEAR(c->geometry().height(), 40.0f, kEps);
  CHECK_NEAR(c->geometry().y(), 80.0f, kEps);
  CHECK(root.lastLayoutOverflow().any());
  CHECK_NEAR(root.lastLayoutOverflow().heightShort, 20.0f, kEps);  // 120 - 100
  CHECK_NEAR(root.lastLayoutOverflow().widthShort, 0.0f, kEps);
  CHECK_EQ(root.lastLayoutOverflow().clippedCount, 1);  // only the third runs over

  // The CROSS axis is reported separately, and by the widest requirement rather
  // than by a sum: `a` needs 80 in a 50-wide column, so the box is 30 short
  // sideways while having room to spare lengthways.
  a->set({80.0f, 40.0f}, {80.0f, 60.0f});
  root.setGeometry({0.0f, 0.0f, 50.0f, 200.0f});
  CHECK_NEAR(root.lastLayoutOverflow().widthShort, 30.0f, kEps);
  CHECK_NEAR(root.lastLayoutOverflow().heightShort, 0.0f, kEps);
  CHECK_EQ(root.lastLayoutOverflow().clippedCount, 1);  // `a` alone
  // Clamped UP to its own minimum rather than squeezed into the panel: the
  // widget draws at the size it said it needs and the parent's clip cuts it.
  CHECK_NEAR(a->geometry().width(), 80.0f, kEps);
  CHECK_NEAR(b->geometry().width(), 50.0f, kEps);
}

GEEYOOU_TEST(box, hiding_and_removing_close_the_gap_they_leave) {
  Widget root;
  BoxLayout* box = root.setLayout<BoxLayout>(BoxLayout::Orientation::Horizontal);
  FixedWidget* a = addFixed(root, {0.0f, 0.0f}, {30.0f, 10.0f});
  FixedWidget* b = addFixed(root, {0.0f, 0.0f}, {30.0f, 10.0f});
  FixedWidget* c = addFixed(root, {0.0f, 0.0f}, {30.0f, 10.0f});
  box->setSpacing(10.0f);
  box->addWidget(a);
  box->addWidget(b);
  box->addWidget(c);
  root.setGeometry({0.0f, 0.0f, 200.0f, 20.0f});
  CHECK_NEAR(c->geometry().x(), 80.0f, kEps);

  // A hidden widget takes no space AND no spacing: one gap, not two.
  b->setVisible(false);
  CHECK_NEAR(c->geometry().x(), 40.0f, kEps);
  b->setVisible(true);
  CHECK_NEAR(c->geometry().x(), 80.0f, kEps);

  // Removing the middle child shifts every index above it down by one, and the
  // items have to follow -- this is the whole price of index addressing.
  root.removeChild(b);
  CHECK_EQ(box->itemCount(), std::size_t(2));
  CHECK_NEAR(a->geometry().x(), 0.0f, kEps);
  CHECK_NEAR(c->geometry().x(), 40.0f, kEps);
  CHECK_NEAR(c->geometry().width(), 30.0f, kEps);
}

GEEYOOU_TEST(box, measure_reports_what_the_host_would_need) {
  Widget root;
  BoxLayout* box = root.setLayout<BoxLayout>(BoxLayout::Orientation::Horizontal);
  FixedWidget* a = addFixed(root, {5.0f, 8.0f}, {40.0f, 20.0f});
  FixedWidget* b = addFixed(root, {5.0f, 12.0f}, {60.0f, 30.0f});
  box->setSpacing(10.0f);
  box->setMargins({4.0f, 3.0f, 4.0f, 3.0f});
  box->addWidget(a);
  box->addWidget(b);

  // Margins and spacing are the host's problem, so measure() includes both.
  const SizeHint h = box->measure(root);
  CHECK_NEAR(h.preferred.width, 40.0f + 10.0f + 60.0f + 8.0f, kEps);
  CHECK_NEAR(h.preferred.height, 30.0f + 6.0f, kEps);  // the taller child wins
  CHECK_NEAR(h.min.width, 5.0f + 10.0f + 5.0f + 8.0f, kEps);
  CHECK_NEAR(h.min.height, 12.0f + 6.0f, kEps);

  // And it moves nothing: measure() is a query.
  const Rect before = a->geometry();
  (void)box->measure(root);
  CHECK(a->geometry() == before);
}

// =================================================================== T-07 ===
GEEYOOU_TEST(grid, columns_take_the_widest_cell_and_rows_the_tallest) {
  Widget root;
  GridLayout* grid = root.setLayout<GridLayout>();
  FixedWidget* a = addFixed(root, {0.0f, 0.0f}, {40.0f, 10.0f});
  FixedWidget* b = addFixed(root, {0.0f, 0.0f}, {20.0f, 30.0f});
  FixedWidget* c = addFixed(root, {0.0f, 0.0f}, {60.0f, 20.0f});
  FixedWidget* d = addFixed(root, {0.0f, 0.0f}, {10.0f, 10.0f});
  grid->setSpacing(0.0f);
  grid->addWidget(a, 0, 0);
  grid->addWidget(b, 0, 1);
  grid->addWidget(c, 1, 0);
  grid->addWidget(d, 1, 1);
  CHECK_EQ(grid->columnCount(), std::size_t(2));
  CHECK_EQ(grid->rowCount(), std::size_t(2));

  // Column 0 is the wider of 40 and 60; row 0 is the taller of 10 and 30.
  // "Taller of", not "the first one seen" -- which is the single most likely
  // way to write this loop wrong.
  root.setGeometry({0.0f, 0.0f, 80.0f, 50.0f});
  CHECK_NEAR(a->geometry().width(), 60.0f, kEps);
  CHECK_NEAR(a->geometry().height(), 30.0f, kEps);
  CHECK_NEAR(b->geometry().x(), 60.0f, kEps);
  CHECK_NEAR(b->geometry().width(), 20.0f, kEps);
  CHECK_NEAR(b->geometry().height(), 30.0f, kEps);
  CHECK_NEAR(c->geometry().y(), 30.0f, kEps);
  CHECK_NEAR(c->geometry().height(), 20.0f, kEps);
  CHECK_NEAR(d->geometry().y(), 30.0f, kEps);
  CHECK_NEAR(d->geometry().height(), 20.0f, kEps);
}

GEEYOOU_TEST(grid, a_span_wider_than_its_columns_shares_the_shortfall_out) {
  // THE case ADR-R2-09 is about: a colSpan=3 cell states a requirement about a
  // SUM, and how much each column has to give only becomes answerable after
  // every span-1 cell has had its say.  Nothing here iterates.
  Widget root;
  GridLayout* grid = root.setLayout<GridLayout>();
  FixedWidget* a = addFixed(root, {20.0f, 10.0f}, {20.0f, 10.0f});
  FixedWidget* b = addFixed(root, {30.0f, 10.0f}, {30.0f, 10.0f});
  FixedWidget* c = addFixed(root, {0.0f, 10.0f}, {0.0f, 10.0f});
  FixedWidget* wide = addFixed(root, {100.0f, 10.0f}, {100.0f, 10.0f});
  grid->setSpacing(0.0f);
  grid->addWidget(a, 0, 0);
  grid->addWidget(b, 0, 1);
  grid->addWidget(c, 0, 2);
  grid->addWidget(wide, 1, 0, 1, 3);

  // The three columns supply 20 + 30 + 0 = 50; the span needs 100.  With no
  // column marked stretchable the 50 short is spread over all three: 16 each,
  // and the two-pixel remainder on the last of them.
  root.setGeometry({0.0f, 0.0f, 100.0f, 40.0f});
  CHECK_NEAR(a->geometry().width(), 36.0f, kEps);
  CHECK_NEAR(b->geometry().width(), 46.0f, kEps);
  CHECK_NEAR(c->geometry().width(), 18.0f, kEps);
  CHECK_NEAR(wide->geometry().width(), 100.0f, kEps);
  CHECK_NEAR(wide->geometry().x(), 0.0f, kEps);
  CHECK(!root.lastLayoutOverflow().any());

  // Name a stretchable column and the whole shortfall goes there instead: that
  // column is the one the author said may grow.
  grid->setColumnStretch(2, 1);
  CHECK_NEAR(a->geometry().width(), 20.0f, kEps);
  CHECK_NEAR(b->geometry().width(), 30.0f, kEps);
  CHECK_NEAR(c->geometry().width(), 50.0f, kEps);
  CHECK_NEAR(wide->geometry().width(), 100.0f, kEps);

  // The spacing between the columns a span covers is space the span gets to
  // use, so it counts towards what those columns already supply: with 6px gaps
  // the three columns supply 50 + 12, and only 38 is missing.  (setSpacing
  // invalidates, so the pass has already re-run by the time these are read.)
  grid->setSpacing(6.0f);
  CHECK_NEAR(a->geometry().width(), 20.0f, kEps);
  CHECK_NEAR(b->geometry().width(), 30.0f, kEps);
  CHECK_NEAR(c->geometry().width(), 38.0f, kEps);
  CHECK_NEAR(c->geometry().x(), 62.0f, kEps);
  CHECK_NEAR(wide->geometry().width(), 100.0f, kEps);
  CHECK_NEAR(wide->geometry().right(), 100.0f, kEps);
}

GEEYOOU_TEST(grid, a_row_span_works_the_same_way_on_the_other_axis) {
  Widget root;
  GridLayout* grid = root.setLayout<GridLayout>();
  FixedWidget* r0 = addFixed(root, {10.0f, 20.0f}, {10.0f, 20.0f});
  FixedWidget* r1 = addFixed(root, {10.0f, 20.0f}, {10.0f, 20.0f});
  FixedWidget* tall = addFixed(root, {10.0f, 90.0f}, {10.0f, 90.0f});
  grid->setSpacing(0.0f);
  grid->addWidget(r0, 0, 0);
  grid->addWidget(r1, 1, 0);
  grid->addWidget(tall, 0, 1, 2, 1);
  grid->setRowStretch(1, 1);

  // Rows supply 20 + 20; the span needs 90, and row 1 is the stretchable one.
  root.setGeometry({0.0f, 0.0f, 30.0f, 90.0f});
  CHECK_NEAR(r0->geometry().height(), 20.0f, kEps);
  CHECK_NEAR(r1->geometry().height(), 70.0f, kEps);
  CHECK_NEAR(tall->geometry().height(), 90.0f, kEps);
  CHECK_NEAR(tall->geometry().y(), 0.0f, kEps);
}

GEEYOOU_TEST(grid, add_row_builds_a_form_whose_field_column_grows) {
  Widget root;
  GridLayout* grid = root.setLayout<GridLayout>();
  FixedWidget* l1 = addFixed(root, {50.0f, 20.0f}, {50.0f, 20.0f});
  FixedWidget* f1 = addFixed(root, {40.0f, 20.0f}, {80.0f, 20.0f});
  FixedWidget* l2 = addFixed(root, {70.0f, 20.0f}, {70.0f, 20.0f});
  FixedWidget* f2 = addFixed(root, {40.0f, 20.0f}, {80.0f, 20.0f});
  grid->setSpacing(0.0f);
  grid->addRow(l1, f1);
  grid->addRow(l2, f2);
  CHECK_EQ(grid->rowCount(), std::size_t(2));
  CHECK_EQ(grid->columnCount(), std::size_t(2));

  // The label column is as wide as the widest label and stays there; every
  // spare pixel goes to the fields.  A form whose labels stretched and whose
  // inputs did not would be exactly backwards.
  root.setGeometry({0.0f, 0.0f, 300.0f, 40.0f});
  CHECK_NEAR(l1->geometry().width(), 70.0f, kEps);
  CHECK_NEAR(l2->geometry().width(), 70.0f, kEps);
  CHECK_NEAR(f1->geometry().x(), 70.0f, kEps);
  CHECK_NEAR(f1->geometry().width(), 230.0f, kEps);
  CHECK_NEAR(f2->geometry().width(), 230.0f, kEps);
  // Both rows are on the same grid lines: that is what a form is.
  CHECK_NEAR(f2->geometry().y(), 20.0f, kEps);
  CHECK_NEAR(f2->geometry().x(), f1->geometry().x(), kEps);
}

GEEYOOU_TEST(grid, too_little_room_clamps_and_reports) {
  Widget root;
  GridLayout* grid = root.setLayout<GridLayout>();
  FixedWidget* a = addFixed(root, {60.0f, 30.0f}, {60.0f, 30.0f});
  FixedWidget* b = addFixed(root, {60.0f, 30.0f}, {60.0f, 30.0f});
  grid->setSpacing(0.0f);
  grid->addWidget(a, 0, 0);
  grid->addWidget(b, 0, 1);

  root.setGeometry({0.0f, 0.0f, 80.0f, 20.0f});
  CHECK_NEAR(a->geometry().width(), 60.0f, kEps);
  CHECK_NEAR(b->geometry().width(), 60.0f, kEps);
  CHECK_NEAR(root.lastLayoutOverflow().widthShort, 40.0f, kEps);
  CHECK_NEAR(root.lastLayoutOverflow().heightShort, 10.0f, kEps);
  CHECK_EQ(root.lastLayoutOverflow().clippedCount, 2);  // both run past an edge

  // Removing a child re-indexes every cell above it, exactly as in a box.  The
  // column it vacated stays -- the grid's SHAPE is what was declared, so the
  // remaining field does not slide sideways into somebody else's column.
  root.removeChild(a);
  root.setGeometry({0.0f, 0.0f, 200.0f, 60.0f});
  CHECK_EQ(grid->columnCount(), std::size_t(2));
  CHECK_NEAR(b->geometry().x(), 0.0f, kEps);  // column 0 is now empty, so zero wide
  CHECK_NEAR(b->geometry().width(), 60.0f, kEps);
  CHECK_NEAR(b->geometry().height(), 30.0f, kEps);
  CHECK(!root.lastLayoutOverflow().any());
}

// =================================================================== T-08 ===
//
// The six hints are asserted as RELATIONS -- "gaining a leading icon widens it
// by exactly the icon's slot" -- rather than as absolute pixel counts wherever
// text is involved.  Which font file this machine has decides those pixels, and
// a test that hard-coded them would be a text golden pretending to be a unit
// test.
GEEYOOU_TEST(hint, label_measures_its_text_and_only_wraps_if_told_to) {
  Label label;
  label.setText("A");
  const SizeHint one = label.sizeHint();
  CHECK(one.preferred.width > 0.0f);
  CHECK_NEAR(one.preferred.height, label.lineSpacing(), kEps);
  // Not wrapping: squeezing the label does not reflow anything, it only cuts
  // the text off, so the full width is the minimum.
  CHECK_NEAR(one.min.width, one.preferred.width, kEps);

  label.setText("AAAAAAAAAA");
  const SizeHint ten = label.sizeHint();
  CHECK(ten.preferred.width > one.preferred.width);

  // Hard newlines are counted; the width is the longest LINE, not the whole
  // string.
  label.setText("AAAAAAAAAA\nA");
  const SizeHint twoLines = label.sizeHint();
  CHECK_NEAR(twoLines.preferred.width, ten.preferred.width, kEps);
  CHECK_NEAR(twoLines.preferred.height, 2.0f * label.lineSpacing(), kEps);

  // Wrapping: "squeeze me, I will cope".
  label.setWordWrap(true);
  CHECK_NEAR(label.sizeHint().min.width, 0.0f, kEps);
  CHECK_NEAR(label.sizeHint().preferred.width, ten.preferred.width, kEps);
}

GEEYOOU_TEST(hint, push_button_may_give_up_padding_but_never_a_letter) {
  PushButton button;
  button.setText("确认");
  const SizeHint plain = button.sizeHint();
  CHECK_NEAR(plain.preferred.height, 36.0f, kEps);
  CHECK_NEAR(plain.min.height, 26.0f, kEps);
  // 16px of comfortable padding a side against 8px of tight padding: exactly
  // 16 pixels of give, and not one pixel of the label.
  CHECK_NEAR(plain.preferred.width - plain.min.width, 16.0f, kEps);

  // An icon adds its box and the gap before the label.
  button.setIcon(Icon::Check);
  CHECK_NEAR(button.sizeHint().preferred.width - plain.preferred.width, 18.0f + 7.0f,
             kEps);

  // The loading label is measured too, so the button does not resize under the
  // operator's finger the moment they press it.
  PushButton wide;
  wide.setText("确认");
  wide.setLoadingText("正在下发指令中");
  CHECK(wide.sizeHint().preferred.width > plain.preferred.width);
}

GEEYOOU_TEST(hint, check_box_keeps_its_box_and_its_whole_label) {
  CheckBox box;
  const SizeHint empty = box.sizeHint();
  CHECK_NEAR(empty.preferred.width, 16.0f, kEps);
  CHECK_NEAR(empty.min.height, 16.0f, kEps);
  CHECK_NEAR(empty.preferred.height, 22.0f, kEps);

  box.setText("启用联锁");
  const SizeHint labelled = box.sizeHint();
  CHECK(labelled.preferred.width > empty.preferred.width + 9.0f);
  // Nothing to give up horizontally: half a "启用联锁" is a different
  // instruction, not a smaller one.
  CHECK_NEAR(labelled.min.width, labelled.preferred.width, kEps);
}

GEEYOOU_TEST(hint, line_edit_widens_by_exactly_the_chrome_it_gains) {
  LineEdit edit;
  const SizeHint bare = edit.sizeHint();
  CHECK(bare.preferred.width > bare.min.width);  // the strip can be squeezed
  CHECK(bare.preferred.height > bare.min.height);

  // A leading icon replaces 10px of padding with a 26+4 slot.
  edit.setLeadingIcon(Icon::Search);
  CHECK_NEAR(edit.sizeHint().preferred.width - bare.preferred.width, 20.0f, kEps);

  // The clear button only exists while there is something to clear, so the
  // chrome grows on the first keystroke -- which is why setText invalidates.
  LineEdit clearable;
  clearable.setClearButtonEnabled(true);
  CHECK_NEAR(clearable.sizeHint().preferred.width, bare.preferred.width, kEps);
  clearable.setText("x");
  CHECK_NEAR(clearable.sizeHint().preferred.width - bare.preferred.width, 26.0f, kEps);
}

GEEYOOU_TEST(hint, spin_box_is_sized_for_its_range_not_for_its_value) {
  SpinBox spin;
  spin.setRange(-100.0, 100.0);
  spin.setDecimals(1);
  const float wide = spin.sizeHint().preferred.width;

  // THE point of this hint.  A field measured from the value it happens to be
  // showing re-flows the whole form every time a pump changes speed.
  spin.setValue(0.0);
  CHECK_NEAR(spin.sizeHint().preferred.width, wide, kEps);
  spin.setValue(-99.9);
  CHECK_NEAR(spin.sizeHint().preferred.width, wide, kEps);

  // Configuration, on the other hand, does change it.
  spin.setRange(0.0, 9.0);
  const float narrow = spin.sizeHint().preferred.width;
  CHECK(narrow < wide);
  spin.setDecimals(4);
  CHECK(spin.sizeHint().preferred.width > narrow);
  spin.setSuffix(" °C");
  CHECK(spin.sizeHint().preferred.width > narrow);
}

GEEYOOU_TEST(hint, group_box_reports_its_own_layout_plus_its_frame) {
  GroupBox group;
  BoxLayout* box = group.setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
  FixedWidget* a = group.add<FixedWidget>();
  a->set({30.0f, 20.0f}, {80.0f, 40.0f});
  FixedWidget* b = group.add<FixedWidget>();
  b->set({30.0f, 20.0f}, {80.0f, 40.0f});
  box->setSpacing(0.0f);
  box->addWidget(a);
  box->addWidget(b);

  // An untitled frame insets 12 a side and 12 top and bottom.
  const SizeHint plain = group.sizeHint();
  CHECK_NEAR(plain.preferred.width, 80.0f + 24.0f, kEps);
  CHECK_NEAR(plain.preferred.height, 80.0f + 24.0f, kEps);
  CHECK_NEAR(plain.min.width, 30.0f + 24.0f, kEps);
  CHECK_NEAR(plain.min.height, 40.0f + 24.0f, kEps);

  // A title raises the top inset from 12 to 34...
  group.setTitle("泵组参数");
  const SizeHint titled = group.sizeHint();
  CHECK_NEAR(titled.preferred.height - plain.preferred.height, 22.0f, kEps);
  // ...and is itself a floor on the width: a title drawn over the corner of its
  // own frame is not a smaller group box, it is a broken one.
  GroupBox narrow;
  narrow.setTitle("一个相当长的分组标题用来占宽度");
  CHECK(narrow.sizeHint().min.width >
        geeyoou::measureText("一个相当长的分组标题用来占宽度",
                             geeyoou::Theme::current().fontBody)
            .width);
}
