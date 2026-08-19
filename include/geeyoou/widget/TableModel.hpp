#pragma once
//
// The data source a TableView reads, and the value types both sides speak.
//
// PULL, like ListView and for the same reason: the view stores a row COUNT and
// asks for a cell's contents only when that cell is about to be drawn.  A
// 200 000-row batch history costs the view nothing -- the rows stay in whatever
// the acquisition layer already owns and are never copied into the widget.
// docs/architecture.md section 1, rule 2.
//
// NOT A WIDGET, and it never goes in the widget tree.  A model is a non-visual
// object; section 3.3 keeps those out of the tree deliberately, unlike Qt where
// everything is a QObject.  A TableView holds a RAW pointer to a model the
// application owns and outlives it by contract.
//
// -----------------------------------------------------------------------------
// THE CONTRACT AN OVERRIDE OWES -- read this before writing one.
//
// Every function here is called FROM INSIDE TableView::onPaint, in the middle of
// a frame that goes on to read its own members and draw the rest of the row.  So
// an override is application code reached through a door, and it carries the
// same obligation Signal's D7 does, for the same reason:
//
//   * it MUST NOT destroy the TableView, or any ancestor of it;
//   * it MUST NOT change the row count, the column list, or the model pointer.
//
// Neither is guarded against, and that is a decision rather than an oversight
// (REM3-RES-2): a guard would have to bracket every one of the dozens of pulls a
// single paint makes, which costs more than the frame it protects and still
// cannot repair a row count that moved halfway through a row.  Read, format,
// return.  Anything that CHANGES is a job for the frame that called into you --
// raise it through a signal and act on it after the paint.
//
// The doors themselves are registered: docs/iterations/02-layout-engine.md
// section 11.4, one row per (file, function), the same grade `styleState()`
// carries for exactly the same shape.
//
// -----------------------------------------------------------------------------
// WHY EVERY VIRTUAL IS SPELLED `table...`
//
// The door lint (tools\lint-door-coverage.ps1) builds its P1 name set from EVERY
// virtual declared anywhere under include\geeyoou, and then greps src\ for calls
// BY NAME.  A virtual called `text()` or `rowCount()` here would therefore turn
// every unrelated `text()` in the library into a door candidate overnight.  The
// prefix is not a style preference; it is what keeps one new class from
// reddening forty files.
//
#include <cstdint>
#include <functional>
#include <string>
#include <utility>

#include "geeyoou/core/Types.hpp"

namespace geeyoou {

// How a cell is DRAWN.  Declared per column, because a column is the unit an
// operator reads down -- a table whose third row is suddenly a progress bar is a
// rendering bug, not a feature.
enum class CellKind : std::uint8_t {
  Text,      // tableText(), aligned per the column
  Index,     // 1-based ordinal, painted by the view; no model call at all
  Selector,  // row-selection checkbox, bound to the VIEW's selection, not to data
  Check,     // data checkbox, bound to tableFlag() / tableSetFlag()
  Switch,    // the ToggleSwitch pill, same binding as Check
  Progress,  // tableNumber() in 0..1, with tableText() as the optional label
  Chip,      // a rounded tag: tableText() coloured by tableAccent()
  Actions,   // the column's own action list, drawn as text links
  Tree,      // expander + indent + tableText(); at most one column may be this
};

// Three-state, and the third state is not "ascending again".  A sorted column
// the operator cannot un-sort is a column that has silently taken over the row
// order for the rest of the session.
enum class SortOrder : std::uint8_t { None, Ascending, Descending };

// What the expander in a CellKind::Tree cell shows.
//
// Loading and Failed are the whole reason this is an enum rather than a bool:
// an asynchronously loaded child list has a visible middle state, and a tree
// that shows a still chevron while a request is in flight is indistinguishable
// from one that is broken.
enum class RowExpansion : std::uint8_t {
  Leaf,       // no children, no expander drawn
  Collapsed,  // has children (or might), currently closed
  Expanded,
  Loading,    // a request is in flight; the view draws a spinner
  Failed,     // the request came back empty or errored; the view draws a retry
};

// A merged cell.
//
// ANCHOR cells answer with positive extents: {2, 3} is "I cover two rows and
// three columns starting here".  COVERED cells answer with the NEGATIVE OFFSET
// back to their anchor: {-1, 0} is "my anchor is one row up, same column".
//
// The negative-offset half is what makes merging affordable on a large table.
// The alternative -- the view scanning upwards until it finds an anchor -- is
// unbounded work per painted cell, and on a 200 000-row model with a merge near
// the top it is a scan of the whole column to draw one screen.  Answering
// locally is something the model can always do (it knows its own grouping) and
// the view can never do.
struct CellSpan {
  int rowSpan = 1;
  int colSpan = 1;

  constexpr bool isAnchor() const { return rowSpan >= 1 && colSpan >= 1; }
  constexpr bool isMerged() const { return rowSpan != 1 || colSpan != 1; }
};

// One action inside a CellKind::Actions cell.
struct CellAction {
  std::string id;     // handed back through TableView::actionTriggered
  std::string label;  // what is drawn
  Color color;        // zero alpha = the theme's link colour
  bool enabled = true;

  CellAction() = default;
  CellAction(std::string id_, std::string label_)
      : id(std::move(id_)), label(std::move(label_)) {}
  CellAction(std::string id_, std::string label_, Color c)
      : id(std::move(id_)), label(std::move(label_)), color(c) {}
};

class TableModel {
 public:
  virtual ~TableModel() = default;

  // --- required ------------------------------------------------------------
  virtual int tableRowCount() const = 0;
  virtual std::string tableText(int row, int col) const = 0;

  // --- optional, defaulted so a text-only model overrides two functions -----

  // Progress fill, in 0..1.  Anything outside is clamped by the view.
  virtual double tableNumber(int row, int col) const {
    (void)row;
    (void)col;
    return 0.0;
  }

  // Check / Switch state.
  virtual bool tableFlag(int row, int col) const {
    (void)row;
    (void)col;
    return false;
  }

  // Tint for Chip cells, and the leading severity bar when col < 0.  A zero
  // ALPHA means "no tint" -- not black, which is why this is not a Color that
  // defaults to opaque.
  virtual Color tableAccent(int row, int col) const {
    (void)row;
    (void)col;
    return Color::rgba(0, 0, 0, 0);
  }

  // Merging.  The default says "every cell is its own 1x1 anchor", which is the
  // zero-cost path: TableView only asks at all once setMergingEnabled(true) has
  // been called, so a model that never merges is never called here.
  virtual CellSpan tableSpan(int row, int col) const {
    (void)row;
    (void)col;
    return {};
  }

  // --- tree ----------------------------------------------------------------
  // Indent level of a row, 0 for a root.  Only read by a CellKind::Tree column.
  virtual int tableDepth(int row) const {
    (void)row;
    return 0;
  }

  virtual RowExpansion tableExpansion(int row) const {
    (void)row;
    return RowExpansion::Leaf;
  }

  // The user clicked the expander.  A synchronous model flips its own state
  // here; an asynchronous one moves the row to Loading and asks its application
  // for children.
  //
  // THIS ONE MAY CHANGE THE ROW COUNT -- it is the single exception to the
  // contract at the top of this file, and it is safe for one reason: the view
  // calls it from a MOUSE handler and re-reads everything afterwards, never from
  // inside a paint.  See TableView::onMouse.
  virtual void tableToggleExpansion(int row) { (void)row; }

  // --- editing -------------------------------------------------------------
  // Return true if the value was accepted.  A model that validates and rejects
  // returns false, and the view leaves the cell as it was -- there is no second
  // "rejected" callback, because a return value the caller must check is harder
  // to ignore than a signal it can forget to connect.
  //
  // Called from a committed editor, never from a paint.
  virtual bool tableSetText(int row, int col, const std::string& value) {
    (void)row;
    (void)col;
    (void)value;
    return false;
  }

  virtual bool tableSetFlag(int row, int col, bool on) {
    (void)row;
    (void)col;
    (void)on;
    return false;
  }
};

// The adapter for everything that is not worth a class: hand it lambdas.
//
// It exists because half the tables in an HMI project are five rows of settings
// that will never be sorted, merged or paged, and making those inherit from
// TableModel is ceremony with no payoff.  The doors stay VISIBLE either way --
// the call TableView makes is still the virtual one, and the lint still sees it.
//
// ⚠️ The std::function hop INSIDE these overrides is a door the door predicate
// cannot see, which is a property of callbacks in general rather than of this
// class; ListView already ships with it.  Registered in
// docs/iterations/02-layout-engine.md section 12.4 rather than pretended away.
class FunctionTableModel : public TableModel {
 public:
  int rowCount = 0;

  std::function<std::string(int row, int col)> text;
  std::function<double(int row, int col)> number;
  std::function<bool(int row, int col)> flag;
  std::function<Color(int row, int col)> accent;
  std::function<CellSpan(int row, int col)> span;
  std::function<bool(int row, int col, const std::string& value)> setText;
  std::function<bool(int row, int col, bool on)> setFlag;

  int tableRowCount() const override { return rowCount; }

  std::string tableText(int row, int col) const override {
    return text ? text(row, col) : std::string();
  }
  double tableNumber(int row, int col) const override {
    return number ? number(row, col) : 0.0;
  }
  bool tableFlag(int row, int col) const override {
    return flag ? flag(row, col) : false;
  }
  Color tableAccent(int row, int col) const override {
    return accent ? accent(row, col) : Color::rgba(0, 0, 0, 0);
  }
  CellSpan tableSpan(int row, int col) const override {
    return span ? span(row, col) : CellSpan{};
  }
  bool tableSetText(int row, int col, const std::string& value) override {
    return setText ? setText(row, col, value) : false;
  }
  bool tableSetFlag(int row, int col, bool on) override {
    return setFlag ? setFlag(row, col, on) : false;
  }
};

}  // namespace geeyoou
