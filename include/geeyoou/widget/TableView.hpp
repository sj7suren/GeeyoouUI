#pragma once
//
// Virtualised data grid.
//
// WHAT IT IS, in one sentence: a ListView whose columns each declare HOW their
// cells are drawn, plus frozen panes, merged cells, a tree column, and one
// re-used editor overlay.
//
// -----------------------------------------------------------------------------
// EVERY CELL IS PAINTED.  NO CELL IS A WIDGET.
//
// This is the load-bearing decision of the whole class, so it is stated first.
// A checkbox cell is a rounded rect and a tick drawn by onPaint; it is NOT a
// CheckBox child.  Three reasons, in the order they decided it:
//
//   1. A widget per visible cell means creating and destroying widgets as the
//      view scrolls, and Widget::add / removeChild / setGeometry / setVisible
//      are all doors (docs/iterations/02-layout-engine.md section 11.4).  A
//      single flick of the wheel would open dozens of them, every one inside a
//      frame that goes on to draw the rest of the table.
//   2. A CheckBox child OWNS a `checked_`.  That is a second copy of a truth the
//      model already holds, and the moment it exists the pull model is a push
//      model with a synchronisation bug waiting in it.
//   3. Cells drawn from the model cost nothing to scroll past.  200 000 rows
//      paint the two dozen that are on screen.
//
// The FOUR EDITORS are the exception that proves it: one LineEdit, one SpinBox,
// one ComboBox and one MultiSelect, built in the constructor, never destroyed,
// never duplicated.  Editing moves ONE of them over the cell.  A table with a
// thousand editable cells still owns exactly four widgets.
//
// -----------------------------------------------------------------------------
// WHAT THIS CLASS DOES NOT DO, on purpose:
//
//   * it does not sort.  Clicking a sortable header emits sortChanged and
//     nothing else -- the model owns the order, and a view that reordered rows
//     behind the model's back would be a third copy of the data;
//   * it does not paginate.  TablePager is a separate control that emits a page
//     number; the application answers by moving its model window;
//   * it does not fetch.  An asynchronous tree emits through the model, and the
//     application supplies children on the UI thread (architecture 3.11).
//
#include <cstddef>
#include <string>
#include <vector>

#include "geeyoou/core/ConnectionScope.hpp"
#include "geeyoou/core/Signal.hpp"
#include "geeyoou/render/Icon.hpp"
#include "geeyoou/render/Painter.hpp"
#include "geeyoou/widget/SelectModel.hpp"
#include "geeyoou/widget/TableModel.hpp"
#include "geeyoou/widget/Widget.hpp"

namespace geeyoou {

class ComboBox;
class LineEdit;
class MultiSelect;
class SpinBox;

// Which of the four resident editors an editable column opens.
enum class CellEditor : std::uint8_t {
  Text,         // LineEdit
  Number,       // SpinBox
  Select,       // ComboBox
  MultiSelect,  // MultiSelect -- the in-cell multi-pick
};

class TableView : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(TableView, Widget)

  enum class SelectionMode : std::uint8_t { None, Single, Multi };

  // A column DESCRIBES itself; the view never learns what the data means.
  //
  // Everything an editor needs lives here rather than in a parallel structure,
  // because the alternative is two lists that have to stay index-aligned, and
  // an editable column whose options were forgotten is a dropdown with nothing
  // in it -- silent, and only visible to whoever clicks that one cell.
  struct Column {
    std::string title;
    float width = 120.0f;  // <= 0 means "share what is left"
    float minWidth = 56.0f;
    HAlign align = HAlign::Left;
    CellKind kind = CellKind::Text;

    bool sortable = false;
    bool editable = false;
    CellEditor editor = CellEditor::Text;

    // CellEditor::Select / MultiSelect
    std::vector<SelectItem> options;
    // CellEditor::Number
    double minValue = 0.0;
    double maxValue = 100.0;
    double step = 1.0;
    int decimals = 0;
    std::string suffix;
    // CellKind::Actions
    std::vector<CellAction> actions;
    // CellKind::Progress -- draw the percentage inside the bar
    bool showValue = true;
  };

  TableView();
  ~TableView() override;

  // --- data -----------------------------------------------------------------
  //
  // The view does NOT own the model and does not extend its lifetime.  The
  // application owns it and must outlive the view, or call setModel(nullptr)
  // first -- the same rule ScrollArea's content pointer follows, and the same
  // one every pull-model control in this library follows.
  void setModel(TableModel* m);
  TableModel* model() const { return model_; }

  // The row count or the whole contents changed.  Clamps the scroll, drops any
  // selection that no longer exists and cancels an editor standing on a row
  // that may have moved.  This is what a page change or a re-sort calls.
  void rowsReset();
  // Same rows, new values -- a repaint and nothing else.
  void rowsChanged();
  int rowCount() const;

  void setColumns(std::vector<Column> cols);
  const std::vector<Column>& columns() const { return columns_; }

  // --- appearance -----------------------------------------------------------
  void setRowHeight(float px);
  float rowHeight() const { return rowHeight_; }
  void setHeaderVisible(bool on);
  void setAlternatingRows(bool on);
  void setGridVisible(bool vertical, bool horizontal);
  void setHoverHighlight(bool on);

  // Columns pinned to the left and right edges while the middle band scrolls.
  //
  // COUNTS FROM THE EDGES rather than a per-column flag: frozen panes are
  // contiguous by construction -- a "frozen" column with scrolling columns on
  // both sides has nowhere to be -- and a count cannot express the impossible
  // arrangement that a flag can.
  void setFrozenColumns(int leading, int trailing);
  int frozenLeadingColumns() const { return frozenLead_; }
  int frozenTrailingColumns() const { return frozenTrail_; }

  // --- state overlays -------------------------------------------------------
  //
  // Loading draws a dimmed veil and a spinner over the body and takes no input.
  // It needs Window::enableAnimations() to actually turn; without the animation
  // clock it is a static ring, which is still an honest "busy" indicator.
  void setLoading(bool on);
  bool isLoading() const { return loading_; }
  // Shown centred when the model has zero rows and the view is not loading.
  void setEmptyText(std::string title, std::string hint = {});

  // --- selection ------------------------------------------------------------
  void setSelectionMode(SelectionMode m);
  SelectionMode selectionMode() const { return mode_; }
  const std::vector<int>& selectedRows() const { return selected_; }
  bool isRowSelected(int row) const;
  void selectRow(int row, bool on);
  void selectAllRows();
  void clearSelection();

  int currentRow() const { return curRow_; }
  int currentColumn() const { return curCol_; }
  void setCurrentCell(int row, int col, bool scrollIntoView = true);

  // --- sorting --------------------------------------------------------------
  //
  // The view holds the INDICATOR, not the order.  Setting it here is how an
  // application restores a saved sort without pretending the user clicked.
  void setSort(int column, SortOrder order);
  // What a click on a sortable header does: None -> Ascending -> Descending ->
  // None.  Public because it is also what a keyboard shortcut, a restored
  // workspace or a test needs, and because a three-state cycle buried in a mouse
  // handler is a rule nobody can check.
  void cycleSort(int column);
  int sortColumn() const { return sortCol_; }
  SortOrder sortOrder() const { return sortOrder_; }

  // --- merged cells ---------------------------------------------------------
  //
  // OFF by default, and off means the model is never asked: a table that does
  // not merge pays nothing for the feature (architecture section 4's rule,
  // applied one level down).
  void setMergingEnabled(bool on);
  bool isMergingEnabled() const { return merging_; }
  // How far back the view is willing to walk to find the anchor of a merge that
  // starts above the viewport.  Beyond this the cell is drawn unmerged rather
  // than scanned for, which keeps painting O(visible rows) on a huge model.
  void setMaxSpan(int rows);
  int maxSpan() const { return maxSpan_; }

  // --- editing --------------------------------------------------------------
  void beginEdit(int row, int col);
  // `commit` false discards; true asks the model, which may still refuse.
  void endEdit(bool commit);
  bool isEditing() const { return editRow_ >= 0; }
  int editingRow() const { return editRow_; }
  int editingColumn() const { return editCol_; }

  // --- scrolling ------------------------------------------------------------
  void ensureRowVisible(int row);
  void scrollToTop();
  void scrollToBottom();
  bool isAtBottom() const;

  SizeHint sizeHint() const override;

  // --- signals --------------------------------------------------------------
  Signal<int> rowClicked;
  Signal<int> rowActivated;
  Signal<> selectionChanged;
  Signal<int, int> cellClicked;                    // row, column
  Signal<int, int, bool> cellToggled;              // Check / Switch cells
  Signal<int, int, const std::string&> cellEdited;  // committed and accepted
  Signal<int, const std::string&> actionTriggered;  // row, CellAction::id
  Signal<int, SortOrder> sortChanged;               // column, new order
  Signal<int> expansionToggled;                     // a tree row was opened/closed

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;
  void onMouse(const MouseEvent& e) override;
  void onKey(const KeyEvent& e) override;
  void onGeometryChanged() override;
  void onAnimationTick() override;
  // The four editors are members AND children.  If one is ever taken out of the
  // tree by anything other than this widget's destructor, the pointer here is
  // dangling from that moment -- see the hook's contract in Widget.hpp.
  void onDescendantDetached(Widget* node) override;

 private:
  // Which horizontal band a column belongs to.  Not an implementation detail of
  // painting alone: hit-testing has to reverse the same mapping, and doing it
  // twice from two different pieces of arithmetic is how a frozen column ends up
  // clickable one pixel to the left of where it is drawn.
  enum class Pane : std::uint8_t { Leading, Middle, Trailing };

  struct CellRef {
    int row = -1;
    int col = -1;
    bool valid() const { return row >= 0 && col >= 0; }
  };

  // --- geometry -------------------------------------------------------------
  float headerHeight() const;
  Rect bodyRect() const;      // rows, excluding header and both scrollbars
  Rect middleRect() const;    // the horizontally scrolling band
  float leadingWidth() const;
  float trailingWidth() const;
  float totalColumnWidth() const;
  float maxScrollY() const;
  float maxScrollX() const;
  void resolveColumnWidths();
  Pane paneOf(int col) const;
  // x of a column's left edge in LOCAL coordinates, scroll already applied.
  float columnX(int col) const;
  Rect cellRect(int row, int col) const;
  Rect headerCellRect(int col) const;
  int rowAtY(float y) const;
  int columnAtX(float x) const;
  CellRef cellAt(Point p) const;
  int firstVisibleRow() const;
  int lastVisibleRow() const;
  // The first column declared with each of the two kinds that may appear only
  // once, or -1.  Asked for rather than stored, so a setColumns() that changes
  // the arrangement cannot leave a stale index behind.
  int selectorColumn() const;
  int treeColumn() const;

  // Where one CellAction is drawn inside its cell, and which one a point is
  // over.  ONE piece of arithmetic used by both the painter and the hit test:
  // written twice, the two drift, and a delete link ends up firing from the gap
  // beside itself.
  Rect actionRect(const Rect& cell, const Column& c, std::size_t index) const;
  int actionAt(const Rect& cell, const Column& c, Point p) const;
  // The expander triangle of a tree row, in local coordinates.
  Rect expanderRect(const Rect& cell, int row) const;

  // --- merging --------------------------------------------------------------
  // Resolves (row, col) to the anchor that owns it and the rectangle that anchor
  // covers.  Without merging enabled this is the identity and costs one branch.
  struct Merged {
    int row = 0;
    int col = 0;
    int rowSpan = 1;
    int colSpan = 1;
  };
  Merged mergedAt(int row, int col) const;

  // --- painting -------------------------------------------------------------
  void paintHeader(Painter& p) const;
  void paintRows(Painter& p) const;
  void paintPane(Painter& p, Pane pane, int firstRow, int lastRow) const;
  void paintCell(Painter& p, const Rect& cell, int row, int col) const;
  void paintTreeCell(Painter& p, const Rect& cell, int row, int col) const;
  void paintCheckGlyph(Painter& p, const Rect& box, bool on, bool enabled) const;
  void paintSwitchGlyph(Painter& p, const Rect& box, bool on, bool enabled) const;
  void paintScrollbars(Painter& p) const;
  void paintOverlay(Painter& p) const;  // loading veil / empty state
  Color rowBackground(int row) const;

  // --- interaction ----------------------------------------------------------
  void handlePress(const MouseEvent& e);
  void toggleSelection(int row, bool additive, bool range);
  void activateCell(int row, int col);
  // The ONLY place in this file that moves or shows an editor.  Its last
  // statements are the geometry and the visibility, and nothing reads a member
  // of this widget afterwards -- see the definition.
  void syncEditor();
  void hideEditors();
  Widget* activeEditor() const;
  // A SpinBox commits on every step and stays open; it therefore needs a write
  // path that is NOT endEdit, or nudging a setpoint twice would take two clicks
  // and a re-open.
  void commitNumber();

  std::vector<Column> columns_;
  std::vector<float> widths_;  // resolved, same size as columns_
  TableModel* model_ = nullptr;

  // CACHED, and it is not an optimisation.
  //
  // tableRowCount() is a virtual on an application-derived class: a door.  Were
  // the geometry helpers to ask for it -- and bodyRect() alone is called a dozen
  // times per paint -- every one of them would become a frame that hands control
  // to application code and then goes on doing arithmetic with the answer.  Far
  // worse, the count could CHANGE between two calls inside one paint, and half a
  // frame drawn against each is not a state any code below is written for.
  //
  // So the count is pulled in exactly one place (rowsReset) and read from here
  // everywhere else.  The price is the contract on rowsReset: a model whose row
  // count changed and did not say so draws its old height.  That is the same
  // bargain ListView::setRowCount makes, stated the other way round.
  int rowCount_ = 0;

  float rowHeight_ = 34.0f;
  bool header_ = true;
  bool alternating_ = true;
  bool gridV_ = true;
  bool gridH_ = true;
  bool hoverHighlight_ = true;
  int frozenLead_ = 0;
  int frozenTrail_ = 0;

  bool loading_ = false;
  int spinPhase_ = 0;
  // How many Loading expanders the LAST paint actually drew.
  //
  // The animation tick needs to know whether anything is spinning, and the only
  // honest answer comes from the paint: whether a tree row is loading is the
  // model's business, and asking every row would defeat the whole point of a
  // view that touches two dozen of two hundred thousand.  So the painter counts
  // what it drew and the tick reads that -- an idle table stays idle, and one
  // with a request in flight animates for exactly as long as the spinner is on
  // screen.
  mutable int spinnersSeen_ = 0;
  std::string emptyTitle_ = "暂无数据";
  std::string emptyHint_;

  SelectionMode mode_ = SelectionMode::Single;
  std::vector<int> selected_;
  int curRow_ = -1;
  int curCol_ = -1;
  int anchorRow_ = -1;  // shift-click range anchor
  int hoverRow_ = -1;
  int hoverCol_ = -1;

  int sortCol_ = -1;
  SortOrder sortOrder_ = SortOrder::None;

  bool merging_ = false;
  int maxSpan_ = 32;

  float scrollY_ = 0.0f;
  float scrollX_ = 0.0f;

  int editRow_ = -1;
  int editCol_ = -1;
  LineEdit* editText_ = nullptr;
  SpinBox* editNumber_ = nullptr;
  ComboBox* editSelect_ = nullptr;
  MultiSelect* editMulti_ = nullptr;
  ConnectionScope conns_;
};

}  // namespace geeyoou
