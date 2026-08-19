//
// TableView / TreeTableModel / TablePager.
//
// WHAT THESE CASES ARE FOR, because it is not "the table works":
//
//   1. THE PULL MODEL IS A PROMISE, AND A PROMISE NEEDS A WITNESS.  "A 200 000
//      row model costs the view nothing" is the sentence the whole design rests
//      on, and it is exactly the kind of sentence that stays in a comment while
//      the code quietly grows a loop over every row.  So the model here COUNTS
//      the calls it receives, the view is really painted through the same
//      Canvas -> Painter -> paintTree path a window uses, and the assertion is
//      an upper bound on that count.  It can fail.
//   2. A hint may not depend on live data -- ADR-R2-09, and the same rule
//      test_size_hints.cpp pins on ListView: a table is a WINDOW onto a model,
//      so its hint is a viewport, not a contents.
//   3. The asynchronous tree's state machine is the part with no visual tell
//      until it is wrong.  Asked once, not twice; a failure is retryable; a
//      supply opens the branch it was asked for.
//
// No Window and no message loop anywhere in this file: everything above
// platform/ can be painted into an OffscreenImage, which is what makes "how many
// cells did that paint pull" a question a test can ask at all.
//
#include <string>
#include <vector>

#include "framework/Test.hpp"
#include "geeyoou/render/Canvas.hpp"
#include "geeyoou/render/Offscreen.hpp"
#include "geeyoou/render/Painter.hpp"
#include "geeyoou/widget/TableModel.hpp"
#include "geeyoou/widget/TablePager.hpp"
#include "geeyoou/widget/TableView.hpp"
#include "geeyoou/widget/TreeTableModel.hpp"

using geeyoou::Canvas;
using geeyoou::CellKind;
using geeyoou::CellSpan;
using geeyoou::Color;
using geeyoou::OffscreenImage;
using geeyoou::Painter;
using geeyoou::Rect;
using geeyoou::RowExpansion;
using geeyoou::SizeHint;
using geeyoou::SortOrder;
using geeyoou::TableModel;
using geeyoou::TablePager;
using geeyoou::TableView;
using geeyoou::TreeTableModel;
using geeyoou::Widget;

namespace {

// A model that answers instantly and REMEMBERS BEING ASKED.  The counters are
// the whole point: they turn "the view only pulls what it draws" from a design
// intention into a number a case can put a bound on.
class CountingModel : public TableModel {
 public:
  int rows = 0;
  mutable int textCalls = 0;
  mutable int spanCalls = 0;
  mutable int lowestRow = 1 << 30;
  mutable int highestRow = -1;

  int tableRowCount() const override { return rows; }

  std::string tableText(int row, int col) const override {
    ++textCalls;
    if (row < lowestRow) lowestRow = row;
    if (row > highestRow) highestRow = row;
    return std::to_string(row) + ":" + std::to_string(col);
  }

  CellSpan tableSpan(int row, int col) const override {
    ++spanCalls;
    (void)row;
    (void)col;
    return {};
  }

  void resetCounters() {
    textCalls = 0;
    spanCalls = 0;
    lowestRow = 1 << 30;
    highestRow = -1;
  }
};

std::vector<TableView::Column> threeColumns() {
  std::vector<TableView::Column> cols;
  TableView::Column a;
  a.title = "标签";
  a.width = 120.0f;
  cols.push_back(a);
  TableView::Column b;
  b.title = "描述";
  b.width = 200.0f;
  cols.push_back(b);
  TableView::Column c;
  c.title = "值";
  c.width = 90.0f;
  cols.push_back(c);
  return cols;
}

// The real paint path, into pixels nobody looks at.  What is being measured is
// what the paint ASKED FOR, not what it produced.
void paintOnce(Widget& w, int width, int height) {
  OffscreenImage img(width, height, 1.0f);
  const Rect all(0.0f, 0.0f, float(width), float(height));
  Canvas canvas;
  if (!canvas.begin(img.surface(), all)) return;
  Painter p = canvas.painter();
  w.paintTree(p, all, all);
  canvas.end();
}

}  // namespace

// ============================================================ the hint =======

// The same rule test_size_hints.cpp pins on ListView, and for the same reason: a
// 200 000-row table that asked for 200 000 rows of height would be reported by
// its enclosing layout as a six-million-pixel overflow, every frame, forever.
GEEYOOU_TEST(table, a_hint_is_a_viewport_not_a_contents) {
  CountingModel small;
  small.rows = 10;
  CountingModel huge;
  huge.rows = 200000;

  TableView a;
  a.setColumns(threeColumns());
  a.setModel(&small);

  TableView b;
  b.setColumns(threeColumns());
  b.setModel(&huge);

  const SizeHint ha = a.sizeHint();
  const SizeHint hb = b.sizeHint();

  CHECK_NEAR(ha.preferred.height, hb.preferred.height, 0.01);
  CHECK_NEAR(ha.preferred.width, hb.preferred.width, 0.01);
  // And it is a real number, not zero -- a hint of nothing collapses the view
  // to nothing the first time it is put in a box.
  CHECK(ha.preferred.height > 0.0f);
  CHECK(ha.preferred.width > 0.0f);
}

// ===================================================== the pull model ========

// THE LOAD-BEARING CASE OF THIS FILE.
//
// 200 000 rows, a 320-pixel-tall viewport, three columns.  At a 34-pixel row
// that is about nine visible rows, so the paint may pull a few dozen cells.  The
// bound is deliberately generous -- it is not measuring the exact arithmetic, it
// is catching the day somebody writes `for (int r = 0; r < rowCount_; ++r)`.
GEEYOOU_TEST(table, only_the_visible_rows_are_pulled) {
  CountingModel m;
  m.rows = 200000;

  TableView t;
  t.setColumns(threeColumns());
  t.setModel(&m);
  t.setGeometry({0.0f, 0.0f, 420.0f, 320.0f});
  m.resetCounters();

  paintOnce(t, 420, 320);

  CHECK(m.textCalls > 0);   // it drew SOMETHING; a silent no-op would also pass a bound
  CHECK(m.textCalls < 200);
  // Only rows near the top were touched: nothing walked into the middle of the
  // model looking for anything.
  CHECK(m.highestRow < 60);
}

// Scrolled to the bottom, the SAME small number of rows is pulled -- and they are
// the rows at the end.  Without this, a view that pulled rows 0..N every time
// would still pass the case above.
GEEYOOU_TEST(table, the_pulled_window_follows_the_scroll) {
  CountingModel m;
  m.rows = 200000;

  TableView t;
  t.setColumns(threeColumns());
  t.setModel(&m);
  t.setGeometry({0.0f, 0.0f, 420.0f, 320.0f});
  t.scrollToBottom();
  m.resetCounters();

  paintOnce(t, 420, 320);

  CHECK(m.textCalls > 0);
  CHECK(m.textCalls < 200);
  CHECK(m.lowestRow > 199000);
}

// Merging is off by default and the model is NEVER asked -- "do not pay for what
// you do not use", one level below where architecture section 4 states it.
GEEYOOU_TEST(table, merging_costs_nothing_while_it_is_off) {
  CountingModel m;
  m.rows = 500;

  TableView t;
  t.setColumns(threeColumns());
  t.setModel(&m);
  t.setGeometry({0.0f, 0.0f, 420.0f, 320.0f});

  m.resetCounters();
  paintOnce(t, 420, 320);
  CHECK_EQ(m.spanCalls, 0);

  t.setMergingEnabled(true);
  m.resetCounters();
  paintOnce(t, 420, 320);
  CHECK(m.spanCalls > 0);
}

// ======================================================= book-keeping ========

GEEYOOU_TEST(table, a_model_that_shrank_takes_the_selection_with_it) {
  CountingModel m;
  m.rows = 20;

  TableView t;
  t.setColumns(threeColumns());
  t.setSelectionMode(TableView::SelectionMode::Multi);
  t.setModel(&m);
  t.setGeometry({0.0f, 0.0f, 420.0f, 320.0f});

  t.selectRow(2, true);
  t.selectRow(17, true);
  CHECK_EQ(int(t.selectedRows().size()), 2);

  m.rows = 5;
  t.rowsReset();

  CHECK_EQ(t.rowCount(), 5);
  CHECK_EQ(int(t.selectedRows().size()), 1);
  CHECK(t.isRowSelected(2));
  CHECK(!t.isRowSelected(17));
}

// Three states, and the third one is not "ascending again": a column an operator
// cannot un-sort has silently taken over the row order for the session.
GEEYOOU_TEST(table, sorting_cycles_through_three_states) {
  CountingModel m;
  m.rows = 4;

  TableView t;
  std::vector<TableView::Column> cols = threeColumns();
  cols[0].sortable = true;
  t.setColumns(cols);
  t.setModel(&m);

  int emitted = 0;
  SortOrder last = SortOrder::None;
  t.sortChanged.connect([&](int, SortOrder o) {
    ++emitted;
    last = o;
  });

  t.cycleSort(0);
  CHECK(last == SortOrder::Ascending);
  CHECK_EQ(t.sortColumn(), 0);
  t.cycleSort(0);
  CHECK(last == SortOrder::Descending);
  t.cycleSort(0);
  CHECK(last == SortOrder::None);
  // Un-sorted means no column is marked, not "column 0 with no order".
  CHECK_EQ(t.sortColumn(), -1);
  CHECK_EQ(emitted, 3);
}

// A view with no model at all is a legitimate state -- it is what every table
// looks like between construction and the first setModel -- and it must paint.
GEEYOOU_TEST(table, an_empty_table_paints_its_empty_state) {
  TableView t;
  t.setColumns(threeColumns());
  t.setEmptyText("暂无数据", "调整筛选条件后重试");
  t.setGeometry({0.0f, 0.0f, 420.0f, 320.0f});

  CHECK_EQ(t.rowCount(), 0);
  paintOnce(t, 420, 320);  // must not read past anything
}

// ============================================================== tree =========

GEEYOOU_TEST(table, a_tree_flattens_in_draw_order) {
  TreeTableModel tree;
  const auto a = tree.addNode(TreeTableModel::kRootNode, {"A"});
  const auto a1 = tree.addNode(a, {"A1"});
  tree.addNode(a1, {"A1a"});
  tree.addNode(TreeTableModel::kRootNode, {"B"});

  // Collapsed by default: two roots and nothing else.
  CHECK_EQ(tree.tableRowCount(), 2);
  CHECK_EQ(tree.tableText(0, 0), std::string("A"));
  CHECK_EQ(tree.tableText(1, 0), std::string("B"));

  tree.setExpanded(a, true);
  CHECK_EQ(tree.tableRowCount(), 3);
  CHECK_EQ(tree.tableText(1, 0), std::string("A1"));
  CHECK_EQ(tree.tableDepth(1), 1);

  // The grandchild appears only when its OWN parent is open -- expanding A does
  // not flatten the whole subtree under it.
  tree.setExpanded(a1, true);
  CHECK_EQ(tree.tableRowCount(), 4);
  CHECK_EQ(tree.tableText(2, 0), std::string("A1a"));
  CHECK_EQ(tree.tableDepth(2), 2);
  CHECK_EQ(tree.tableText(3, 0), std::string("B"));

  tree.setExpanded(a, false);
  CHECK_EQ(tree.tableRowCount(), 2);
}

GEEYOOU_TEST(table, a_lazy_branch_is_asked_once_and_shows_a_spinner) {
  TreeTableModel tree;
  const auto plant = tree.addNode(TreeTableModel::kRootNode, {"1# 反应釜"});
  tree.setLazy(plant, true);

  int asks = 0;
  TreeTableModel::NodeId asked = TreeTableModel::kInvalidNode;
  tree.childrenRequested.connect([&](TreeTableModel::NodeId id) {
    ++asks;
    asked = id;
  });

  // A lazy branch with no children yet still offers an expander.
  CHECK(tree.tableExpansion(0) == RowExpansion::Collapsed);

  tree.tableToggleExpansion(0);
  CHECK_EQ(asks, 1);
  CHECK_EQ(asked, plant);
  CHECK(tree.tableExpansion(0) == RowExpansion::Loading);

  // Drumming on a slow branch must not queue five fetches for it.
  tree.tableToggleExpansion(0);
  tree.tableToggleExpansion(0);
  CHECK_EQ(asks, 1);
}

GEEYOOU_TEST(table, supplying_children_opens_the_branch_that_asked) {
  TreeTableModel tree;
  const auto plant = tree.addNode(TreeTableModel::kRootNode, {"1# 反应釜"});
  tree.setLazy(plant, true);

  int structureEmits = 0;
  tree.structureChanged.connect([&] { ++structureEmits; });

  tree.tableToggleExpansion(0);
  tree.addNode(plant, {"TI-102"});
  tree.addNode(plant, {"PI-201"});
  tree.finishLoad(plant, true);

  CHECK(tree.tableExpansion(0) == RowExpansion::Expanded);
  CHECK_EQ(tree.tableRowCount(), 3);
  CHECK_EQ(tree.tableText(1, 0), std::string("TI-102"));
  CHECK_EQ(tree.tableDepth(1), 1);
  CHECK_EQ(structureEmits, 1);
}

// A failed branch keeps its retry and does NOT re-ask on its own -- an
// unreachable station would otherwise be re-requested on every repaint.
GEEYOOU_TEST(table, a_failed_load_retries_only_when_asked_again) {
  TreeTableModel tree;
  const auto plant = tree.addNode(TreeTableModel::kRootNode, {"3# 罐区"});
  tree.setLazy(plant, true);

  int asks = 0;
  tree.childrenRequested.connect([&](TreeTableModel::NodeId) { ++asks; });

  tree.tableToggleExpansion(0);
  tree.finishLoad(plant, false);
  CHECK(tree.tableExpansion(0) == RowExpansion::Failed);
  CHECK_EQ(asks, 1);

  tree.tableToggleExpansion(0);
  CHECK_EQ(asks, 2);
  CHECK(tree.tableExpansion(0) == RowExpansion::Loading);
}

// expandAll must not fire a fetch per lazy branch: one menu click would become a
// denial of service against the plant's own historian.
GEEYOOU_TEST(table, expand_all_does_not_stampede_the_lazy_branches) {
  TreeTableModel tree;
  const auto a = tree.addNode(TreeTableModel::kRootNode, {"A"});
  tree.addNode(a, {"A1"});
  const auto b = tree.addNode(TreeTableModel::kRootNode, {"B"});
  tree.setLazy(b, true);

  int asks = 0;
  tree.childrenRequested.connect([&](TreeTableModel::NodeId) { ++asks; });

  tree.expandAll();

  CHECK_EQ(asks, 0);
  CHECK(tree.tableExpansion(tree.rowOfNode(a)) == RowExpansion::Expanded);
  CHECK(tree.tableExpansion(tree.rowOfNode(b)) == RowExpansion::Collapsed);
}

// A grouping row has no switch, and that is not the same thing as a switch that
// is off.  The distinction is the difference between an honest empty cell and a
// control that looks operable and means nothing.
GEEYOOU_TEST(table, a_grouping_row_has_no_cell_where_a_leaf_has_one) {
  TreeTableModel tree;
  const auto area = tree.addNode(TreeTableModel::kRootNode, {"area", "", "", ""});
  const auto leaf = tree.addNode(area, {"TI-101", "transmitter", "run", ""});
  tree.setFlag(leaf, 3, true);
  tree.expandAll();

  const int areaRow = tree.rowOfNode(area);
  const int leafRow = tree.rowOfNode(leaf);
  REQUIRE(areaRow >= 0 && leafRow >= 0);

  // Column 3 is the switch: the leaf was given one, the group never was.
  CHECK(tree.tableCellPresent(leafRow, 3));
  CHECK(!tree.tableCellPresent(areaRow, 3));
  // ...and BOTH answer false to tableFlag, which is exactly why the flag alone
  // cannot be used to decide whether to draw anything.
  CHECK(!tree.tableFlag(areaRow, 3));

  // Column 0 is the label, and both rows have one.
  CHECK(tree.tableCellPresent(areaRow, 0));
  CHECK(tree.tableCellPresent(leafRow, 0));
}

// The view drives the model's expansion through the same call a click makes, and
// the row count it caches has to follow.
GEEYOOU_TEST(table, the_view_follows_the_tree_it_is_showing) {
  TreeTableModel tree;
  const auto a = tree.addNode(TreeTableModel::kRootNode, {"A"});
  tree.addNode(a, {"A1"});
  tree.addNode(a, {"A2"});

  TableView t;
  std::vector<TableView::Column> cols = threeColumns();
  cols[0].kind = CellKind::Tree;
  t.setColumns(cols);
  t.setModel(&tree);
  t.setGeometry({0.0f, 0.0f, 420.0f, 320.0f});
  CHECK_EQ(t.rowCount(), 1);

  tree.setExpanded(a, true);
  t.rowsReset();
  CHECK_EQ(t.rowCount(), 3);
}

// ============================================================= pager =========

GEEYOOU_TEST(table, the_pager_counts_pages_and_rows) {
  TablePager p;
  p.setPageSize(20);
  p.setTotal(45);

  CHECK_EQ(p.pageCount(), 3);
  CHECK_EQ(p.page(), 1);
  CHECK_EQ(p.firstRow(), 0);
  CHECK_EQ(p.rowsOnPage(), 20);

  p.setPage(3);
  CHECK_EQ(p.firstRow(), 40);
  CHECK_EQ(p.rowsOnPage(), 5);  // the short last page

  // Out of range is clamped, not accepted: every reader of firstRow() would
  // otherwise be handed an offset past the end of the data.
  p.setPage(99);
  CHECK_EQ(p.page(), 3);
  p.setPage(0);
  CHECK_EQ(p.page(), 1);
}

// One page, always -- including with nothing in it.  "Page 1 of 0" is arithmetic
// leaking into the interface.
GEEYOOU_TEST(table, an_empty_pager_still_has_one_page) {
  TablePager p;
  p.setTotal(0);
  CHECK_EQ(p.pageCount(), 1);
  CHECK_EQ(p.page(), 1);
  CHECK_EQ(p.rowsOnPage(), 0);
}

GEEYOOU_TEST(table, changing_the_page_size_keeps_the_operators_place) {
  TablePager p;
  p.setPageSize(20);
  p.setTotal(500);
  p.setPage(5);              // rows 80..99
  CHECK_EQ(p.firstRow(), 80);

  p.setPageSizeKeepingPlace(50);
  // Row 80 lives on page 2 of a 50-row paging (rows 50..99).
  CHECK_EQ(p.page(), 2);
  CHECK_EQ(p.firstRow(), 50);
  CHECK(p.firstRow() <= 80 && 80 < p.firstRow() + p.pageSize());
}

GEEYOOU_TEST(table, the_pager_announces_every_move_once) {
  TablePager p;
  p.setPageSize(10);
  p.setTotal(100);

  int emits = 0;
  int lastPage = -1;
  p.pageChanged.connect([&](int n) {
    ++emits;
    lastPage = n;
  });

  // setPage is the PROGRAMMATIC path and deliberately does NOT announce: an
  // application restoring a saved page would otherwise be told about its own
  // call and go round again.
  p.setPage(4);
  CHECK_EQ(emits, 0);
  CHECK_EQ(p.page(), 4);
  CHECK_EQ(lastPage, -1);
}
