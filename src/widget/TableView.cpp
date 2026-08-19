#include "geeyoou/widget/TableView.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/ComboBox.hpp"
#include "geeyoou/widget/LineEdit.hpp"
#include "geeyoou/widget/MultiSelect.hpp"
#include "geeyoou/widget/SpinBox.hpp"

namespace geeyoou {
namespace {
constexpr float kCellPad = 10.0f;
constexpr float kScrollbarW = 9.0f;
constexpr float kGlyph = 16.0f;       // checkbox side, switch height
constexpr float kSwitchAspect = 1.85f;  // the same pill ToggleSwitch draws
constexpr float kIndent = 16.0f;      // per tree level
constexpr float kExpander = 18.0f;
constexpr float kActionGap = 14.0f;
constexpr float kSortMark = 8.0f;

// What sizeHint() asks for.  A flexible column still has to name a number, or a
// table made entirely of them declares itself zero pixels wide.
constexpr float kMinFlexColumn = 90.0f;
constexpr float kNoColumnsWidth = 240.0f;
constexpr float kMinWidth = 200.0f;
constexpr float kPreferredRows = 8.0f;
constexpr float kMinRows = 3.0f;

float alignedX(const Rect& cell, HAlign a) {
  if (a == HAlign::Right) return cell.right() - kCellPad;
  if (a == HAlign::Center) return cell.center().x;
  return cell.x() + kCellPad;
}
}  // namespace

// =============================================================== lifetime ===
//
// THE FOUR EDITORS ARE BUILT HERE AND NOWHERE ELSE.
//
// Four add<T> calls, and add<T> is a P2 door -- but a constructor is the one
// frame in this library where that is harmless by CONSTRUCTION rather than by
// argument: the widget has no parent yet, nothing in the process holds a pointer
// to it, and there is therefore nothing that could destroy it from inside the
// door.  The alternative -- building editors lazily on first edit -- would move
// exactly these four doors into a mouse handler, which is a frame that CAN be
// re-entered.  Registered in section 11.4 rather than guarded, and this is the
// reason.
TableView::TableView() {
  setFocusPolicy(FocusPolicy::Tab);

  editText_ = add<LineEdit>();
  editNumber_ = add<SpinBox>();
  editSelect_ = add<ComboBox>();
  editMulti_ = add<MultiSelect>();

  editText_->setVisible(false);
  editNumber_->setVisible(false);
  editSelect_->setVisible(false);
  editMulti_->setVisible(false);

  // COMMIT POINTS, one per editor, chosen by what each control means:
  //
  //   * a text field is finished when the operator says so (Enter) or leaves it;
  //   * a spin box has no "finished" -- every step IS a value, so it commits on
  //     each change and stays open for the next one;
  //   * a single-select closes on activation, so its change IS its commit;
  //   * a MULTI-select deliberately does not close (three picks should be three
  //     clicks, not three reopenings), so it commits when the popup shuts.
  conns_ += editText_->returnPressed.connect([this] { endEdit(true); });
  conns_ += editText_->editingFinished.connect(
      [this](const std::string&) { endEdit(true); });
  conns_ += editNumber_->valueChanged.connect([this](double) { commitNumber(); });
  conns_ += editSelect_->currentIndexChanged.connect([this](int) { endEdit(true); });
  conns_ += editMulti_->openStateChanged.connect([this](bool open) {
    if (!open) endEdit(true);
  });
}

TableView::~TableView() = default;

// The four editors are members AND children.  Nothing in this class removes
// them, but `children()` is public and an application that walked it and called
// removeChild would leave four dangling members behind -- so the hook that
// exists for exactly this repairs them.  Pointer comparison only: this runs in
// the middle of a half-detached walk, and REM3-G9 allows nothing else here.
void TableView::onDescendantDetached(Widget* node) {
  if (node == editText_) editText_ = nullptr;
  if (node == editNumber_) editNumber_ = nullptr;
  if (node == editSelect_) editSelect_ = nullptr;
  if (node == editMulti_) editMulti_ = nullptr;
}

// =================================================================== data ===
void TableView::setModel(TableModel* m) {
  if (model_ == m) return;
  model_ = m;
  editRow_ = -1;
  editCol_ = -1;
  selected_.clear();
  curRow_ = -1;
  curCol_ = -1;
  scrollY_ = 0.0f;
  scrollX_ = 0.0f;
  rowsReset();
}

// The ONE place the row count is pulled.  See the member's comment for why it is
// cached rather than asked for; what matters here is that everything the new
// count invalidates is repaired in this single frame rather than discovered
// later by a painter reading past the end of a shorter model.
void TableView::rowsReset() {
  rowCount_ = model_ ? std::max(0, model_->tableRowCount()) : 0;

  selected_.erase(std::remove_if(selected_.begin(), selected_.end(),
                                 [this](int r) { return r >= rowCount_; }),
                  selected_.end());
  if (curRow_ >= rowCount_) curRow_ = rowCount_ - 1;
  if (anchorRow_ >= rowCount_) anchorRow_ = -1;
  if (hoverRow_ >= rowCount_) hoverRow_ = -1;
  // An editor standing on a row that may have moved under it is worse than no
  // editor: it would commit the operator's typing into whatever row now has that
  // index.  Dropped, not committed.
  if (editRow_ >= rowCount_) {
    editRow_ = -1;
    editCol_ = -1;
  }
  scrollY_ = std::clamp(scrollY_, 0.0f, maxScrollY());
  resolveColumnWidths();
  update();
  syncEditor();
}

void TableView::rowsChanged() { update(); }

int TableView::rowCount() const { return rowCount_; }

void TableView::setColumns(std::vector<Column> cols) {
  columns_ = std::move(cols);
  frozenLead_ = std::min(frozenLead_, int(columns_.size()));
  frozenTrail_ = std::min(frozenTrail_, int(columns_.size()) - frozenLead_);
  if (curCol_ >= int(columns_.size())) curCol_ = -1;
  if (editCol_ >= int(columns_.size())) {
    editRow_ = -1;
    editCol_ = -1;
  }
  if (sortCol_ >= int(columns_.size())) {
    sortCol_ = -1;
    sortOrder_ = SortOrder::None;
  }
  resolveColumnWidths();
  update();
  invalidateSizeHint();  // the columns ARE the width this view asks for
}

// ============================================================ appearance ===
void TableView::setRowHeight(float px) {
  rowHeight_ = std::max(18.0f, px);
  scrollY_ = std::clamp(scrollY_, 0.0f, maxScrollY());
  update();
  invalidateSizeHint();
}

void TableView::setHeaderVisible(bool on) {
  header_ = on;
  update();
  invalidateSizeHint();
}

void TableView::setAlternatingRows(bool on) {
  alternating_ = on;
  update();
}

void TableView::setGridVisible(bool vertical, bool horizontal) {
  gridV_ = vertical;
  gridH_ = horizontal;
  update();
}

void TableView::setHoverHighlight(bool on) {
  hoverHighlight_ = on;
  hoverRow_ = -1;
  update();
}

// Clamped against each other AND against the column count, because the
// degenerate arrangement is not "ugly", it is a middle band of negative width
// that every piece of arithmetic below would then divide space into.
void TableView::setFrozenColumns(int leading, int trailing) {
  const int n = int(columns_.size());
  frozenLead_ = std::clamp(leading, 0, n);
  frozenTrail_ = std::clamp(trailing, 0, n - frozenLead_);
  scrollX_ = std::clamp(scrollX_, 0.0f, maxScrollX());
  resolveColumnWidths();
  update();
}

void TableView::setLoading(bool on) {
  if (loading_ == on) return;
  loading_ = on;
  spinPhase_ = 0;
  update();
}

void TableView::setEmptyText(std::string title, std::string hint) {
  emptyTitle_ = std::move(title);
  emptyHint_ = std::move(hint);
  update();
}

// ============================================================= selection ===
bool TableView::isRowSelected(int row) const {
  return std::find(selected_.begin(), selected_.end(), row) != selected_.end();
}

void TableView::setSelectionMode(SelectionMode m) {
  mode_ = m;
  if (m == SelectionMode::None) clearSelection();
  else update();
}

// The emit is the last statement, which is what keeps this frame off the
// unarchived list: a slot may destroy this widget and there is nothing after it
// to read a freed member.  Every mutator in this file that ends in a signal is
// written this way on purpose.
void TableView::selectRow(int row, bool on) {
  if (mode_ == SelectionMode::None || row < 0 || row >= rowCount_) return;
  const bool was = isRowSelected(row);
  if (was == on) return;
  if (on) {
    if (mode_ == SelectionMode::Single) selected_.clear();
    selected_.push_back(row);
  } else {
    selected_.erase(std::remove(selected_.begin(), selected_.end(), row),
                    selected_.end());
  }
  update();
  selectionChanged.emit();
}

void TableView::selectAllRows() {
  if (mode_ != SelectionMode::Multi || rowCount_ == 0) return;
  selected_.clear();
  selected_.reserve(std::size_t(rowCount_));
  for (int i = 0; i < rowCount_; ++i) selected_.push_back(i);
  update();
  selectionChanged.emit();
}

void TableView::clearSelection() {
  if (selected_.empty()) return;
  selected_.clear();
  update();
  selectionChanged.emit();
}

void TableView::setCurrentCell(int row, int col, bool scrollIntoView) {
  if (row < -1 || row >= rowCount_) return;
  if (col < -1 || col >= int(columns_.size())) return;
  curRow_ = row;
  curCol_ = col;
  if (scrollIntoView && row >= 0) ensureRowVisible(row);
  update();
}

void TableView::setSort(int column, SortOrder order) {
  if (column < -1 || column >= int(columns_.size())) return;
  sortCol_ = (order == SortOrder::None) ? -1 : column;
  sortOrder_ = order;
  update();
}

void TableView::setMergingEnabled(bool on) {
  merging_ = on;
  update();
}

void TableView::setMaxSpan(int rows) {
  maxSpan_ = std::clamp(rows, 1, 4096);
}

// ============================================================== geometry ===
float TableView::headerHeight() const {
  return header_ ? std::round(Theme::current().fontSmall * 2.9f) : 0.0f;
}

// TWO PASSES, and the second one is not belt-and-braces.
//
// Each scrollbar takes a strip away from the axis the OTHER one measures, so a
// table that fits horizontally in the full width may stop fitting once the
// vertical bar appears.  One pass decides "no horizontal bar" from a width that
// is about to shrink, and the last column is then unreachable -- with no bar to
// reach it with.
Rect TableView::bodyRect() const {
  const Rect r = localRect();
  const float top = headerHeight();
  float w = std::max(0.0f, r.width() - 2.0f);
  float h = std::max(0.0f, r.height() - top - 1.0f);

  const float rowsH = float(rowCount_) * rowHeight_;
  const float colsW = totalColumnWidth();

  bool vbar = rowsH > h;
  bool hbar = colsW > w;
  if (vbar) w -= kScrollbarW + 2.0f;
  if (hbar) h -= kScrollbarW + 2.0f;
  if (!vbar && rowsH > h) { vbar = true; w -= kScrollbarW + 2.0f; }
  if (!hbar && colsW > w) { hbar = true; h -= kScrollbarW + 2.0f; }

  return {1.0f, top, std::max(0.0f, w), std::max(0.0f, h)};
}

float TableView::leadingWidth() const {
  float sum = 0.0f;
  for (int i = 0; i < frozenLead_ && i < int(widths_.size()); ++i) sum += widths_[std::size_t(i)];
  return sum;
}

float TableView::trailingWidth() const {
  float sum = 0.0f;
  const int n = int(widths_.size());
  for (int i = std::max(0, n - frozenTrail_); i < n; ++i) sum += widths_[std::size_t(i)];
  return sum;
}

float TableView::totalColumnWidth() const {
  float sum = 0.0f;
  for (float w : widths_) sum += w;
  return sum;
}

Rect TableView::middleRect() const {
  const Rect body = bodyRect();
  const float lead = leadingWidth();
  const float trail = trailingWidth();
  return {body.x() + lead, body.y(),
          std::max(0.0f, body.width() - lead - trail), body.height()};
}

float TableView::maxScrollY() const {
  return std::max(0.0f, float(rowCount_) * rowHeight_ - bodyRect().height());
}

float TableView::maxScrollX() const {
  const float scrolling = totalColumnWidth() - leadingWidth() - trailingWidth();
  return std::max(0.0f, scrolling - middleRect().width());
}

// Fixed columns keep their number; flexible ones share what is left of the
// BODY -- not of the widget, because the body is what the columns are drawn in
// and sizing them against the wider rectangle puts the last one under the
// scrollbar.
void TableView::resolveColumnWidths() {
  widths_.assign(columns_.size(), 0.0f);
  if (columns_.empty()) return;

  const Rect r = localRect();
  const float rowsH = float(rowCount_) * rowHeight_;
  const bool vbar = rowsH > std::max(0.0f, r.height() - headerHeight() - 1.0f);
  const float avail =
      std::max(0.0f, r.width() - 2.0f - (vbar ? kScrollbarW + 2.0f : 0.0f));

  float fixed = 0.0f;
  int flex = 0;
  for (std::size_t i = 0; i < columns_.size(); ++i) {
    if (columns_[i].width > 0.0f) {
      widths_[i] = std::max(columns_[i].minWidth, columns_[i].width);
      fixed += widths_[i];
    } else {
      ++flex;
    }
  }
  if (flex == 0) return;

  const float share = (avail - fixed) / float(flex);
  for (std::size_t i = 0; i < columns_.size(); ++i) {
    if (columns_[i].width <= 0.0f) {
      widths_[i] = std::max(columns_[i].minWidth, share);
    }
  }
}

TableView::Pane TableView::paneOf(int col) const {
  const int n = int(columns_.size());
  if (col < frozenLead_) return Pane::Leading;
  if (col >= n - frozenTrail_) return Pane::Trailing;
  return Pane::Middle;
}

float TableView::columnX(int col) const {
  if (col < 0 || col >= int(widths_.size())) return 0.0f;
  const Rect body = bodyRect();
  const int n = int(widths_.size());

  switch (paneOf(col)) {
    case Pane::Leading: {
      float x = body.x();
      for (int i = 0; i < col; ++i) x += widths_[std::size_t(i)];
      return x;
    }
    case Pane::Trailing: {
      float x = body.right();
      for (int i = n - 1; i >= col; --i) x -= widths_[std::size_t(i)];
      return x;
    }
    case Pane::Middle:
    default: {
      float x = body.x() + leadingWidth() - scrollX_;
      for (int i = frozenLead_; i < col; ++i) x += widths_[std::size_t(i)];
      return x;
    }
  }
}

Rect TableView::cellRect(int row, int col) const {
  if (col < 0 || col >= int(widths_.size())) return {};
  const Rect body = bodyRect();
  return {columnX(col), body.y() + float(row) * rowHeight_ - scrollY_,
          widths_[std::size_t(col)], rowHeight_};
}

Rect TableView::headerCellRect(int col) const {
  if (col < 0 || col >= int(widths_.size())) return {};
  return {columnX(col), 1.0f, widths_[std::size_t(col)],
          std::max(0.0f, headerHeight() - 1.0f)};
}

int TableView::rowAtY(float y) const {
  const Rect body = bodyRect();
  if (y < body.y() || y >= body.bottom()) return -1;
  const int i = int(std::floor((y - body.y() + scrollY_) / rowHeight_));
  return (i >= 0 && i < rowCount_) ? i : -1;
}

// Frozen panes are drawn ON TOP of the middle band, so they must be TESTED
// first: a middle column scrolled underneath a frozen one is not clickable, and
// testing in draw order is the only way to say that once.
int TableView::columnAtX(float x) const {
  const int n = int(widths_.size());
  for (int i = 0; i < frozenLead_ && i < n; ++i) {
    const float cx = columnX(i);
    if (x >= cx && x < cx + widths_[std::size_t(i)]) return i;
  }
  for (int i = std::max(0, n - frozenTrail_); i < n; ++i) {
    const float cx = columnX(i);
    if (x >= cx && x < cx + widths_[std::size_t(i)]) return i;
  }
  const Rect mid = middleRect();
  if (x < mid.x() || x >= mid.right()) return -1;
  for (int i = frozenLead_; i < n - frozenTrail_; ++i) {
    const float cx = columnX(i);
    if (x >= cx && x < cx + widths_[std::size_t(i)]) return i;
  }
  return -1;
}

TableView::CellRef TableView::cellAt(Point p) const {
  CellRef ref;
  ref.col = columnAtX(p.x);
  ref.row = rowAtY(p.y);
  return ref;
}

int TableView::firstVisibleRow() const {
  return std::max(0, int(std::floor(scrollY_ / rowHeight_)));
}

int TableView::lastVisibleRow() const {
  const Rect body = bodyRect();
  return std::min(rowCount_ - 1,
                  int(std::ceil((scrollY_ + body.height()) / rowHeight_)));
}

int TableView::selectorColumn() const {
  for (std::size_t i = 0; i < columns_.size(); ++i) {
    if (columns_[i].kind == CellKind::Selector) return int(i);
  }
  return -1;
}

int TableView::treeColumn() const {
  for (std::size_t i = 0; i < columns_.size(); ++i) {
    if (columns_[i].kind == CellKind::Tree) return int(i);
  }
  return -1;
}

Rect TableView::actionRect(const Rect& cell, const Column& c,
                           std::size_t index) const {
  float x = cell.x() + kCellPad;
  for (std::size_t i = 0; i < c.actions.size(); ++i) {
    const float w = measureText(c.actions[i].label, Theme::current().fontSmall).width;
    if (i == index) return {x, cell.y(), w, cell.height()};
    x += w + kActionGap;
  }
  return {};
}

int TableView::actionAt(const Rect& cell, const Column& c, Point p) const {
  for (std::size_t i = 0; i < c.actions.size(); ++i) {
    // Widened by half the gap on each side: a text link is a few pixels tall in
    // hit-test terms, and an operator who misses it by one pixel simply thinks
    // the table is broken.
    if (actionRect(cell, c, i).deflated(-kActionGap * 0.5f).contains(p)) {
      return int(i);
    }
  }
  return -1;
}

Rect TableView::expanderRect(const Rect& cell, int row) const {
  const int depth = model_ ? model_->tableDepth(row) : 0;
  const float x = cell.x() + kCellPad + float(depth) * kIndent;
  return {x, cell.center().y - kExpander * 0.5f, kExpander, kExpander};
}

// Columns for the width, a ROW COUNT for the height -- never rowCount().  Same
// argument as ListView: a table is a WINDOW onto a model, and a 200 000-row
// model must not turn into a size hint the enclosing layout reports as a
// 6 800 000-pixel overflow.
SizeHint TableView::sizeHint() const {
  float cols = 0.0f;
  for (const Column& c : columns_) {
    cols += (c.width > 0.0f) ? std::max(c.minWidth, c.width) : kMinFlexColumn;
  }
  if (cols <= 0.0f) cols = kNoColumnsWidth;

  const float chrome = 2.0f + kScrollbarW + 2.0f;
  const float head = headerHeight();

  SizeHint h;
  h.preferred = Size{cols + chrome, head + rowHeight_ * kPreferredRows};
  h.min = Size{kMinWidth, head + rowHeight_ * kMinRows};
  return h;
}

void TableView::onGeometryChanged() {
  resolveColumnWidths();
  scrollY_ = std::clamp(scrollY_, 0.0f, maxScrollY());
  scrollX_ = std::clamp(scrollX_, 0.0f, maxScrollX());
  syncEditor();
}

// =============================================================== merging ===
TableView::Merged TableView::mergedAt(int row, int col) const {
  Merged m{row, col, 1, 1};
  if (!merging_ || !model_) return m;

  const CellSpan s = model_->tableSpan(row, col);
  if (s.isAnchor()) {
    m.rowSpan = std::max(1, s.rowSpan);
    m.colSpan = std::max(1, s.colSpan);
    return m;
  }

  // A covered cell names its anchor by NEGATIVE OFFSET, so finding it is two
  // reads rather than a scan.  The bound is still applied: a model that answers
  // -100000 gets its cell drawn unmerged rather than sending the painter that
  // far up the column.
  const int ar = row + s.rowSpan;
  const int ac = col + s.colSpan;
  if (ar < 0 || ac < 0 || ar > row || ac > col) return m;
  if (row - ar > maxSpan_ || col - ac > maxSpan_) return m;

  const CellSpan a = model_->tableSpan(ar, ac);
  if (!a.isAnchor()) return m;
  // The anchor has to actually reach this cell.  A model whose two answers
  // disagree draws unmerged, which is wrong-looking but bounded; trusting the
  // offset alone would paint one cell over another for the rest of the frame.
  if (ar + std::max(1, a.rowSpan) <= row || ac + std::max(1, a.colSpan) <= col) {
    return m;
  }

  m.row = ar;
  m.col = ac;
  m.rowSpan = std::max(1, a.rowSpan);
  m.colSpan = std::max(1, a.colSpan);
  return m;
}

// ================================================================ painting ===
Color TableView::rowBackground(int row) const {
  const Theme& t = Theme::current();
  if (isRowSelected(row)) return t.selection;
  if (hoverHighlight_ && row == hoverRow_) return t.panelBorder.withAlpha(70);
  if (alternating_ && (row & 1)) return t.panel.withAlpha(80);
  return Color::rgba(0, 0, 0, 0);
}

void TableView::onPaint(Painter& p, const Rect&) {
  const Theme& t = Theme::current();
  const Rect r = localRect();
  const bool en = isEffectivelyEnabled();

  p.fillRoundRect(r, t.radius, t.field);
  p.strokeRoundRect(r.deflated(0.5f), t.radius,
                    hasFocus() && en ? t.focusRing : t.panelBorder, 1.0f);

  spinnersSeen_ = 0;
  if (header_) paintHeader(p);
  if (rowCount_ > 0 && model_) paintRows(p);
  paintScrollbars(p);
  paintOverlay(p);
}

void TableView::paintHeader(Painter& p) const {
  const Theme& t = Theme::current();
  const Rect r = localRect();
  const float hh = headerHeight();

  p.fillRect({1.0f, 1.0f, std::max(0.0f, r.width() - 2.0f), hh - 1.0f},
             t.panel.lerp(t.background, 0.15f));
  p.strokeLine({1.0f, hh}, {r.right() - 1.0f, hh}, t.panelBorder, 1.0f);

  const int selCol = selectorColumn();
  const Rect mid = middleRect();

  for (std::size_t c = 0; c < columns_.size(); ++c) {
    const int ci = int(c);
    const Rect cell = headerCellRect(ci);
    if (cell.isEmpty()) continue;

    // A middle-band header scrolls with its column and must be cut at the band,
    // or it slides out over the frozen pane beside it.
    const bool clipped = paneOf(ci) == Pane::Middle;
    if (clipped) {
      if (!cell.intersects({mid.x(), 1.0f, mid.width(), hh})) continue;
      p.save();
      p.clip({mid.x(), 1.0f, mid.width(), hh});
    }

    if (columns_[c].kind == CellKind::Selector && ci == selCol) {
      // The header of a selector column is the select-all box, and it has three
      // states: none, some, all.  "Some" is drawn as a dash rather than a tick,
      // because a half-filled tick reads as a rendering artefact.
      const Rect box(cell.center().x - kGlyph * 0.5f, cell.center().y - kGlyph * 0.5f,
                     kGlyph, kGlyph);
      const int n = int(selected_.size());
      if (n > 0 && n == rowCount_) {
        paintCheckGlyph(p, box, true, true);
      } else {
        paintCheckGlyph(p, box, false, true);
        if (n > 0) {
          p.fillRect({box.x() + 3.0f, box.center().y - 1.0f, box.width() - 6.0f, 2.0f},
                     Theme::current().accent);
        }
      }
    } else {
      const bool active = (ci == sortCol_ && sortOrder_ != SortOrder::None);
      const float reserve = columns_[c].sortable ? kSortMark + 6.0f : 0.0f;
      const Rect textCell(cell.x(), cell.y(), std::max(0.0f, cell.width() - reserve),
                          cell.height());
      p.drawText({alignedX(textCell, columns_[c].align), cell.center().y},
                 columns_[c].title, t.fontSmall, active ? t.text : t.textDim,
                 columns_[c].align, VAlign::Middle);

      if (columns_[c].sortable) {
        const float mx = cell.right() - kCellPad - kSortMark * 0.5f;
        const float my = cell.center().y;
        const Color up = (active && sortOrder_ == SortOrder::Ascending)
                             ? t.accent : t.textDim.withAlpha(110);
        const Color dn = (active && sortOrder_ == SortOrder::Descending)
                             ? t.accent : t.textDim.withAlpha(110);
        p.fillTriangle({mx, my - 6.0f}, {mx - 4.0f, my - 1.0f}, {mx + 4.0f, my - 1.0f}, up);
        p.fillTriangle({mx, my + 6.0f}, {mx - 4.0f, my + 1.0f}, {mx + 4.0f, my + 1.0f}, dn);
      }
    }

    if (clipped) p.restore();

    if (gridV_ && c + 1 < columns_.size()) {
      const float x = cell.right();
      if (x > 1.0f && x < r.right() - 1.0f) {
        p.strokeLine({x, 5.0f}, {x, hh - 5.0f}, t.panelBorder, 1.0f);
      }
    }
  }
}

void TableView::paintRows(Painter& p) const {
  const int first = firstVisibleRow();
  const int last = lastVisibleRow();
  if (last < first) return;

  paintPane(p, Pane::Middle, first, last);
  if (frozenLead_ > 0) paintPane(p, Pane::Leading, first, last);
  if (frozenTrail_ > 0) paintPane(p, Pane::Trailing, first, last);
}

void TableView::paintPane(Painter& p, Pane pane, int firstRow, int lastRow) const {
  const Theme& t = Theme::current();
  const Rect body = bodyRect();
  const int n = int(columns_.size());

  int c0 = 0;
  int c1 = 0;
  Rect band;
  switch (pane) {
    case Pane::Leading:
      c0 = 0;
      c1 = frozenLead_;
      band = {body.x(), body.y(), leadingWidth(), body.height()};
      break;
    case Pane::Trailing:
      c0 = n - frozenTrail_;
      c1 = n;
      band = {body.right() - trailingWidth(), body.y(), trailingWidth(), body.height()};
      break;
    case Pane::Middle:
    default:
      c0 = frozenLead_;
      c1 = n - frozenTrail_;
      band = middleRect();
      break;
  }
  if (c1 <= c0 || band.isEmpty()) return;

  p.save();
  p.clip(band);

  // A frozen pane is opaque: the middle band is drawn first and scrolls
  // UNDERNEATH it, so anything transparent here would show the scrolling text
  // through the pinned column.
  if (pane != Pane::Middle) p.fillRect(band, t.field);

  // When merging is on, an anchor that starts above the viewport still has to be
  // drawn -- its rectangle reaches down into view.  Walking back maxSpan_ rows
  // is what bounds that: the extra rows are clipped away, and the cost is a
  // fixed number of iterations rather than a scan of the column.
  const int drawFrom = merging_ ? std::max(0, firstRow - maxSpan_) : firstRow;

  for (int row = drawFrom; row <= lastRow; ++row) {
    const float y = body.y() + float(row) * rowHeight_ - scrollY_;

    if (row >= firstRow) {
      const Rect rowRect(band.x(), y, band.width(), rowHeight_);
      const Color bg = rowBackground(row);
      if (bg.alpha() > 0) p.fillRect(rowRect, bg);

      const Color accent = model_->tableAccent(row, -1);
      if (accent.alpha() > 0 && pane == (frozenLead_ > 0 ? Pane::Leading : Pane::Middle)) {
        p.fillRect({band.x(), y, 3.0f, rowHeight_}, accent);
      }
      if (gridH_) {
        p.strokeLine({band.x(), y + rowHeight_ - 0.5f},
                     {band.right(), y + rowHeight_ - 0.5f}, t.grid, 1.0f);
      }
    }

    for (int c = c0; c < c1; ++c) {
      const Merged m = mergedAt(row, c);
      // Only the anchor draws.  Covered cells are skipped entirely, which is
      // also what leaves the merged area free of the grid lines below.
      if (m.row != row || m.col != c) continue;

      Rect cell = cellRect(row, c);
      if (m.rowSpan > 1 || m.colSpan > 1) {
        float w = 0.0f;
        for (int k = c; k < std::min(n, c + m.colSpan); ++k) w += widths_[std::size_t(k)];
        cell = {cell.x(), cell.y(), w, rowHeight_ * float(m.rowSpan)};
        p.fillRect(cell, t.field);
        const Color bg = rowBackground(row);
        if (bg.alpha() > 0) p.fillRect(cell, bg);
        p.strokeRect(cell.deflated(0.5f), t.grid, 1.0f);
      }
      if (!cell.intersects(band)) continue;

      p.save();
      p.clip(cell.intersected(band));
      paintCell(p, cell, row, c);
      p.restore();

      if (gridV_ && c + 1 < c1 && m.colSpan <= 1) {
        p.strokeLine({cell.right() - 0.5f, cell.y()},
                     {cell.right() - 0.5f, cell.bottom()}, t.grid, 1.0f);
      }
    }
  }

  p.restore();

  // The seam.  A frozen pane with scrolled content beside it gets an edge
  // shadow, and ONLY when there is something hidden under it -- a permanent
  // line would claim the table scrolls when it does not.
  if (pane == Pane::Leading && scrollX_ > 0.5f) {
    p.strokeLine({band.right() - 0.5f, band.y()}, {band.right() - 0.5f, band.bottom()},
                 t.background.withAlpha(160), 2.0f);
  } else if (pane == Pane::Trailing && scrollX_ < maxScrollX() - 0.5f) {
    p.strokeLine({band.x() + 0.5f, band.y()}, {band.x() + 0.5f, band.bottom()},
                 t.background.withAlpha(160), 2.0f);
  }
}

void TableView::paintCheckGlyph(Painter& p, const Rect& box, bool on,
                                bool enabled) const {
  const Theme& t = Theme::current();
  const Color accent = enabled ? t.accent : t.textDisabled;
  if (on) {
    p.fillRoundRect(box, 3.0f, accent);
    // The same three-segment tick CheckBox draws, at the same proportions.
    const Point a{box.x() + box.width() * 0.24f, box.y() + box.height() * 0.52f};
    const Point b{box.x() + box.width() * 0.43f, box.y() + box.height() * 0.72f};
    const Point c{box.x() + box.width() * 0.78f, box.y() + box.height() * 0.30f};
    p.strokeLine(a, b, t.onFilled, 2.0f);
    p.strokeLine(b, c, t.onFilled, 2.0f);
  } else {
    p.fillRoundRect(box, 3.0f, t.field);
    p.strokeRoundRect(box.deflated(0.5f), 3.0f,
                      enabled ? t.panelBorder : t.textDisabled, 1.0f);
  }
}

void TableView::paintSwitchGlyph(Painter& p, const Rect& box, bool on,
                                 bool enabled) const {
  const Theme& t = Theme::current();
  const float h = std::min(kGlyph, box.height());
  const float w = h * kSwitchAspect;
  const Rect track(box.center().x - w * 0.5f, box.center().y - h * 0.5f, w, h);
  const float radius = h * 0.5f;

  Color trackColor = on ? t.accent : t.track;
  if (!enabled) trackColor = trackColor.lerp(t.background, 0.6f);
  p.fillRoundRect(track, radius, trackColor);
  p.strokeRoundRect(track.deflated(0.5f), radius,
                    enabled ? t.panelBorder : t.textDisabled, 1.0f);
  const float knobR = radius - 2.5f;
  const float knobX = on ? track.right() - radius : track.x() + radius;
  p.fillCircle({knobX, track.center().y}, knobR, enabled ? t.text : t.textDisabled);
}

void TableView::paintCell(Painter& p, const Rect& cell, int row, int col) const {
  const Theme& t = Theme::current();
  const Column& c = columns_[std::size_t(col)];
  const bool en = isEffectivelyEnabled();
  const Color fg = en ? t.text : t.textDisabled;

  // The cell being edited is covered by a real widget; drawing under it wastes a
  // measure and shows through any editor that is not fully opaque.
  if (row == editRow_ && col == editCol_) return;

  switch (c.kind) {
    case CellKind::Index: {
      char buf[16];
      std::snprintf(buf, sizeof(buf), "%d", row + 1);
      p.drawText({alignedX(cell, c.align), cell.center().y}, buf, t.fontBody,
                 en ? t.textDim : t.textDisabled, c.align, VAlign::Middle);
      break;
    }

    case CellKind::Selector: {
      const Rect box(cell.center().x - kGlyph * 0.5f, cell.center().y - kGlyph * 0.5f,
                     kGlyph, kGlyph);
      paintCheckGlyph(p, box, isRowSelected(row), en);
      break;
    }

    case CellKind::Check: {
      const Rect box(cell.center().x - kGlyph * 0.5f, cell.center().y - kGlyph * 0.5f,
                     kGlyph, kGlyph);
      paintCheckGlyph(p, box, model_->tableFlag(row, col), en);
      break;
    }

    case CellKind::Switch: {
      paintSwitchGlyph(p, cell, model_->tableFlag(row, col), en);
      break;
    }

    case CellKind::Progress: {
      const double v = std::clamp(model_->tableNumber(row, col), 0.0, 1.0);
      const float barH = 8.0f;
      const Rect track(cell.x() + kCellPad, cell.center().y - barH * 0.5f,
                       std::max(0.0f, cell.width() - kCellPad * 2.0f -
                                          (c.showValue ? 42.0f : 0.0f)),
                       barH);
      if (track.width() > 0.0f) {
        const float radius = barH * 0.5f;
        p.fillRoundRect(track, radius, t.track);
        if (v > 0.0) {
          // A colour that MEANS something: a batch at 100% is done, one under a
          // fifth is barely started.  An operator reads the hue before the digits.
          const Color bar = v >= 1.0 ? t.success : (v < 0.2 ? t.warn : t.accent);
          const float w = std::max(float(v) * track.width(), radius * 2.0f);
          p.fillRoundRect({track.x(), track.y(), w, barH}, radius,
                          en ? bar : bar.lerp(t.background, 0.6f));
        }
      }
      if (c.showValue) {
        char buf[24];
        const std::string label = model_->tableText(row, col);
        if (label.empty()) {
          std::snprintf(buf, sizeof(buf), "%.0f%%", v * 100.0);
        }
        p.drawText({cell.right() - kCellPad, cell.center().y},
                   label.empty() ? buf : label.c_str(), t.fontSmall,
                   en ? t.textDim : t.textDisabled, HAlign::Right, VAlign::Middle);
      }
      break;
    }

    case CellKind::Chip: {
      const std::string s = model_->tableText(row, col);
      if (s.empty()) break;
      Color tint = model_->tableAccent(row, col);
      if (tint.alpha() == 0) tint = t.accent;
      const float tw = measureText(s, t.fontSmall).width;
      const float chipW = tw + 16.0f;
      const float chipH = std::min(20.0f, cell.height() - 8.0f);
      float x = cell.x() + kCellPad;
      if (c.align == HAlign::Center) x = cell.center().x - chipW * 0.5f;
      else if (c.align == HAlign::Right) x = cell.right() - kCellPad - chipW;
      const Rect chip(x, cell.center().y - chipH * 0.5f, chipW, chipH);
      // Tinted fill plus a matching border, not a solid block: a row of solid
      // chips fights the row highlight underneath it for attention.
      p.fillRoundRect(chip, chipH * 0.5f, tint.withAlpha(46));
      p.strokeRoundRect(chip.deflated(0.5f), chipH * 0.5f, tint.withAlpha(150), 1.0f);
      p.drawText(chip.center(), s, t.fontSmall, en ? tint : t.textDisabled,
                 HAlign::Center, VAlign::Middle);
      break;
    }

    case CellKind::Actions: {
      for (std::size_t i = 0; i < c.actions.size(); ++i) {
        const CellAction& a = c.actions[i];
        const Rect ar = actionRect(cell, c, i);
        if (ar.isEmpty()) continue;
        Color col2 = a.color.alpha() > 0 ? a.color : t.accent;
        if (!a.enabled || !en) col2 = t.textDisabled;
        p.drawText({ar.x(), ar.center().y}, a.label, t.fontSmall, col2,
                   HAlign::Left, VAlign::Middle);
      }
      break;
    }

    case CellKind::Tree:
      paintTreeCell(p, cell, row, col);
      break;

    case CellKind::Text:
    default: {
      const std::string s = model_->tableText(row, col);
      if (s.empty()) break;
      p.drawText({alignedX(cell, c.align), cell.center().y}, s, t.fontBody, fg,
                 c.align, VAlign::Middle);
      break;
    }
  }
}

void TableView::paintTreeCell(Painter& p, const Rect& cell, int row,
                              int col) const {
  const Theme& t = Theme::current();
  const bool en = isEffectivelyEnabled();
  const RowExpansion ex = model_->tableExpansion(row);
  const Rect box = expanderRect(cell, row);

  switch (ex) {
    case RowExpansion::Collapsed:
    case RowExpansion::Expanded: {
      // A triangle rather than a +/- box: it carries the open/closed state in
      // its ORIENTATION, which survives being three pixels tall on a dense row.
      const Point c0 = box.center();
      const Color mark = en ? t.textDim : t.textDisabled;
      if (ex == RowExpansion::Expanded) {
        p.fillTriangle({c0.x - 5.0f, c0.y - 2.5f}, {c0.x + 5.0f, c0.y - 2.5f},
                       {c0.x, c0.y + 3.5f}, mark);
      } else {
        p.fillTriangle({c0.x - 2.5f, c0.y - 5.0f}, {c0.x - 2.5f, c0.y + 5.0f},
                       {c0.x + 3.5f, c0.y}, mark);
      }
      break;
    }
    case RowExpansion::Loading: {
      ++spinnersSeen_;
      // The same ring the whole-table veil uses, one sixth the size.  It turns
      // only when the window's animation clock is running; standing still it is
      // an arc with a gap, which still reads as "not a leaf, not open yet".
      const float deg = float(spinPhase_ * 12 % 360);
      p.strokeArc(box.center(), 6.0f, deg, 270.0f, t.accent, 2.0f);
      break;
    }
    case RowExpansion::Failed:
      drawIcon(p, Icon::Refresh, box.deflated(2.0f), t.warn);
      break;
    case RowExpansion::Leaf:
    default:
      break;
  }

  const std::string s = model_->tableText(row, col);
  if (s.empty()) return;
  const float textX = box.right() + 6.0f;
  p.drawText({textX, cell.center().y}, s, t.fontBody, en ? t.text : t.textDisabled,
             HAlign::Left, VAlign::Middle);
}

void TableView::paintScrollbars(Painter& p) const {
  const Theme& t = Theme::current();
  const Rect r = localRect();
  const Rect body = bodyRect();

  const float msy = maxScrollY();
  if (msy > 0.0f) {
    const float h = body.height();
    const float thumbH = std::max(24.0f, h * (h / (h + msy)));
    const float thumbY = body.y() + (h - thumbH) * (scrollY_ / msy);
    const float x = r.right() - kScrollbarW - 2.0f;
    p.fillRoundRect({x, body.y(), kScrollbarW, h}, kScrollbarW * 0.5f, t.track);
    p.fillRoundRect({x, thumbY, kScrollbarW, thumbH}, kScrollbarW * 0.5f, t.scrollbar);
  }

  const float msx = maxScrollX();
  if (msx > 0.0f) {
    const Rect mid = middleRect();
    const float w = mid.width();
    const float thumbW = std::max(24.0f, w * (w / (w + msx)));
    const float thumbX = mid.x() + (w - thumbW) * (scrollX_ / msx);
    const float y = body.bottom() + 2.0f;
    p.fillRoundRect({mid.x(), y, w, kScrollbarW}, kScrollbarW * 0.5f, t.track);
    p.fillRoundRect({thumbX, y, thumbW, kScrollbarW}, kScrollbarW * 0.5f, t.scrollbar);
  }
}

void TableView::paintOverlay(Painter& p) const {
  const Theme& t = Theme::current();
  const Rect body = bodyRect();
  if (body.isEmpty()) return;

  if (loading_) {
    // A VEIL, not a replacement.  The rows stay visible underneath because a
    // refresh that blanks the table makes the operator lose their place every
    // time the poll interval comes round.
    p.fillRect(body, t.background.withAlpha(150));
    const Point c = body.center();
    p.strokeCircle(c, 16.0f, t.panelBorder, 3.0f);
    p.strokeArc(c, 16.0f, float(spinPhase_ * 9 % 360), 90.0f, t.accent, 3.0f);
    p.drawText({c.x, c.y + 34.0f}, "加载中…", t.fontSmall, t.textDim, HAlign::Center,
               VAlign::Middle);
    return;
  }

  if (rowCount_ == 0) {
    const Point c = body.center();
    // A drawn glyph rather than a big grey word: the empty state is the first
    // thing a new operator sees on a screen that is working correctly, and it
    // should not look like an error.
    const Rect icon(c.x - 18.0f, c.y - 44.0f, 36.0f, 36.0f);
    drawIcon(p, Icon::Filter, icon, t.panelBorder);
    p.drawText({c.x, c.y}, emptyTitle_, t.fontBody, t.textDim, HAlign::Center,
               VAlign::Middle);
    if (!emptyHint_.empty()) {
      p.drawText({c.x, c.y + 22.0f}, emptyHint_, t.fontSmall,
                 t.textDim.withAlpha(160), HAlign::Center, VAlign::Middle);
    }
  }
}

// An idle table does no work: the tick returns immediately unless something is
// actually spinning, and update() is called by THIS widget rather than by the
// clock -- the contract in Widget.hpp is that a tick alone never repaints.
void TableView::onAnimationTick() {
  if (!loading_ && spinnersSeen_ == 0) return;
  ++spinPhase_;
  update();
}

// ================================================================== input ===
void TableView::onMouse(const MouseEvent& e) {
  if (!isEffectivelyEnabled()) return;

  switch (e.action) {
    case MouseAction::Leave:
      hoverRow_ = -1;
      hoverCol_ = -1;
      update();
      e.accept();
      break;

    case MouseAction::Enter:
    case MouseAction::Move: {
      const CellRef ref = cellAt(e.pos);
      if (ref.row != hoverRow_ || ref.col != hoverCol_) {
        hoverRow_ = ref.row;
        hoverCol_ = ref.col;
        if (hoverHighlight_) update();
      }
      e.accept();
      break;
    }

    case MouseAction::Press:
      handlePress(e);
      e.accept();
      break;

    case MouseAction::Wheel:
      // Shift turns the wheel sideways, which is the convention every grid on
      // this platform follows and the only way to reach the far columns on a
      // machine with no horizontal wheel.
      if (e.shift && maxScrollX() > 0.0f) {
        scrollX_ = std::clamp(scrollX_ - e.wheelDelta * 48.0f, 0.0f, maxScrollX());
      } else {
        scrollY_ = std::clamp(scrollY_ - e.wheelDelta * rowHeight_ * 3.0f, 0.0f,
                              maxScrollY());
      }
      update();
      syncEditor();
      e.accept();
      break;

    default:
      break;
  }
}

// The one frame in this file that can reach application code more than one way:
// a header click emits, an expander calls into the model, a toggle emits, an
// action emits.  Every one of those is written as the LAST thing its branch
// does, which is what keeps this function off the unarchived list without a
// guard it would otherwise need.
void TableView::handlePress(const MouseEvent& e) {
  if (e.button != MouseButton::Left) return;
  if (loading_) return;

  // GUARDED, unlike the painting frames below it.  A paint only READS the model
  // and is covered by the contract at the top of TableModel.hpp; this frame
  // calls tableToggleExpansion, tableSetFlag and two signals, every one of which
  // is application code entitled to act -- including by destroying the table it
  // was called from.  Each door that has work after it re-checks.
  const detail::DeathWatch self(this);

  // --- header -------------------------------------------------------------
  if (header_ && e.pos.y < headerHeight()) {
    const int col = columnAtX(e.pos.x);
    if (col < 0) return;
    if (columns_[std::size_t(col)].kind == CellKind::Selector) {
      if (int(selected_.size()) == rowCount_) clearSelection();
      else selectAllRows();
      return;
    }
    if (columns_[std::size_t(col)].sortable) cycleSort(col);
    return;
  }

  const CellRef ref = cellAt(e.pos);
  if (!ref.valid()) return;

  const Merged m = mergedAt(ref.row, ref.col);
  const int row = m.row;
  const int col = m.col;
  const Column& c = columns_[std::size_t(col)];

  // --- the expander, which is NOT the rest of the cell ---------------------
  if (c.kind == CellKind::Tree && model_) {
    const RowExpansion ex = model_->tableExpansion(row);
    const bool clickable = ex == RowExpansion::Collapsed ||
                           ex == RowExpansion::Expanded || ex == RowExpansion::Failed;
    if (clickable && expanderRect(cellRect(row, col), row).contains(e.pos)) {
      // tableToggleExpansion is the one model call allowed to change the row
      // count, and this is why: it is reached from a mouse handler that re-reads
      // everything through rowsReset() afterwards, never from a paint.
      model_->tableToggleExpansion(row);
      if (!self.alive()) {
        detail::frameDegraded();
        return;
      }
      rowsReset();
      if (!self.alive()) {
        detail::frameDegraded();
        return;
      }
      expansionToggled.emit(row);
      return;
    }
  }

  // --- an action link ------------------------------------------------------
  if (c.kind == CellKind::Actions) {
    const int idx = actionAt(cellRect(row, col), c, e.pos);
    if (idx >= 0 && c.actions[std::size_t(idx)].enabled) {
      setCurrentCell(row, col, false);
      actionTriggered.emit(row, c.actions[std::size_t(idx)].id);
    }
    return;
  }

  // --- an in-cell toggle ---------------------------------------------------
  if (c.kind == CellKind::Selector) {
    setCurrentCell(row, col, false);
    toggleSelection(row, true, e.shift);
    return;
  }
  if ((c.kind == CellKind::Check || c.kind == CellKind::Switch) && model_) {
    const bool next = !model_->tableFlag(row, col);
    const bool accepted = model_->tableSetFlag(row, col, next);
    if (!self.alive()) {
      detail::frameDegraded();
      return;
    }
    if (accepted) {
      update();
      cellToggled.emit(row, col, next);
    }
    return;
  }

  // --- an ordinary cell ----------------------------------------------------
  const bool wasCurrent = (row == curRow_ && col == curCol_);
  setCurrentCell(row, col, false);
  toggleSelection(row, e.ctrl, e.shift);
  if (wasCurrent && c.editable) {
    beginEdit(row, col);
    return;
  }
  rowClicked.emit(row);
}

void TableView::toggleSelection(int row, bool additive, bool range) {
  if (mode_ == SelectionMode::None || row < 0) return;

  // ONE exit, and the emit is on it.  Written as a chain rather than as an
  // early return per mode because a `return` after the emit is code after a
  // door -- the slot could have destroyed this widget, and the frame would then
  // be returning through a destroyed object's epilogue.
  if (mode_ == SelectionMode::Single) {
    selected_.assign(1, row);
    anchorRow_ = row;
  } else if (range && anchorRow_ >= 0) {
    const int lo = std::min(anchorRow_, row);
    const int hi = std::max(anchorRow_, row);
    selected_.clear();
    for (int i = lo; i <= hi && i < rowCount_; ++i) selected_.push_back(i);
  } else if (additive && mode_ == SelectionMode::Multi) {
    auto it = std::find(selected_.begin(), selected_.end(), row);
    if (it == selected_.end()) selected_.push_back(row);
    else selected_.erase(it);
    anchorRow_ = row;
  } else {
    selected_.assign(1, row);
    anchorRow_ = row;
  }
  update();
  selectionChanged.emit();
}

// None -> Ascending -> Descending -> None.  The third state is not decoration:
// see the SortOrder comment in TableModel.hpp.
void TableView::cycleSort(int column) {
  const int col = column;
  SortOrder next = SortOrder::Ascending;
  if (col == sortCol_) {
    next = sortOrder_ == SortOrder::Ascending    ? SortOrder::Descending
           : sortOrder_ == SortOrder::Descending ? SortOrder::None
                                                 : SortOrder::Ascending;
  }
  sortCol_ = (next == SortOrder::None) ? -1 : col;
  sortOrder_ = next;
  update();
  sortChanged.emit(col, next);
}

void TableView::onKey(const KeyEvent& e) {
  if (!e.pressed || rowCount_ == 0) return;
  if (isEditing()) return;  // the editor has the focus and its own key handling

  const Rect body = bodyRect();
  const int page = std::max(1, int(body.height() / rowHeight_) - 1);
  const int lastCol = int(columns_.size()) - 1;

  switch (e.key) {
    case Key::Up:
      setCurrentCell(std::max(0, curRow_ - 1), std::max(0, curCol_));
      e.accept();
      break;
    case Key::Down:
      setCurrentCell(std::min(rowCount_ - 1, curRow_ + 1), std::max(0, curCol_));
      e.accept();
      break;
    case Key::Left:
      setCurrentCell(std::max(0, curRow_), std::max(0, curCol_ - 1));
      e.accept();
      break;
    case Key::Right:
      setCurrentCell(std::max(0, curRow_), std::min(lastCol, curCol_ + 1));
      e.accept();
      break;
    case Key::PageUp:
      setCurrentCell(std::max(0, curRow_ - page), std::max(0, curCol_));
      e.accept();
      break;
    case Key::PageDown:
      setCurrentCell(std::min(rowCount_ - 1, curRow_ + page), std::max(0, curCol_));
      e.accept();
      break;
    case Key::Home:
      setCurrentCell(0, std::max(0, curCol_));
      e.accept();
      break;
    case Key::End:
      setCurrentCell(rowCount_ - 1, std::max(0, curCol_));
      e.accept();
      break;
    case Key::Space:
      toggleSelection(curRow_, true, false);
      e.accept();
      break;
    case Key::Enter:
      activateCell(curRow_, curCol_);
      e.accept();
      break;
    case Key::KeyA:
      if (e.ctrl && mode_ == SelectionMode::Multi) {
        selectAllRows();
        e.accept();
      }
      break;
    default:
      break;
  }
}

void TableView::activateCell(int row, int col) {
  if (row < 0 || row >= rowCount_) return;
  if (col >= 0 && col < int(columns_.size()) &&
      columns_[std::size_t(col)].editable) {
    beginEdit(row, col);
    return;
  }
  rowActivated.emit(row);
}

// ============================================================== scrolling ===
bool TableView::isAtBottom() const { return scrollY_ >= maxScrollY() - 1.0f; }

void TableView::scrollToTop() {
  scrollY_ = 0.0f;
  update();
  syncEditor();
}

void TableView::scrollToBottom() {
  scrollY_ = maxScrollY();
  update();
  syncEditor();
}

void TableView::ensureRowVisible(int row) {
  if (row < 0 || row >= rowCount_) return;
  const Rect body = bodyRect();
  const float top = float(row) * rowHeight_;
  if (top < scrollY_) scrollY_ = top;
  else if (top + rowHeight_ > scrollY_ + body.height()) {
    scrollY_ = top + rowHeight_ - body.height();
  }
  scrollY_ = std::clamp(scrollY_, 0.0f, maxScrollY());
  update();
}

// ================================================================ editing ===
Widget* TableView::activeEditor() const {
  if (editCol_ < 0 || editCol_ >= int(columns_.size())) return nullptr;
  switch (columns_[std::size_t(editCol_)].editor) {
    case CellEditor::Number: return editNumber_;
    case CellEditor::Select: return editSelect_;
    case CellEditor::MultiSelect: return editMulti_;
    case CellEditor::Text:
    default: return editText_;
  }
}

void TableView::beginEdit(int row, int col) {
  if (!model_ || loading_) return;
  if (row < 0 || row >= rowCount_) return;
  if (col < 0 || col >= int(columns_.size())) return;
  if (!columns_[std::size_t(col)].editable) return;
  if (row == editRow_ && col == editCol_) return;

  if (isEditing()) endEdit(true);
  // endEdit runs application code (a signal, and the model's setter); it may
  // have destroyed this widget or emptied the model underneath us, so the row
  // is re-checked rather than assumed.
  if (row >= rowCount_ || col >= int(columns_.size())) return;

  const Column& c = columns_[std::size_t(col)];

  // The seed value comes through a door, and everything below it -- four editor
  // pointers, the column reference, this widget -- is read afterwards.
  const detail::DeathWatch self(this);
  const std::string current = model_->tableText(row, col);
  if (!self.alive()) {
    detail::frameDegraded();
    return;
  }

  editRow_ = row;
  editCol_ = col;

  switch (c.editor) {
    case CellEditor::Number:
      if (editNumber_) {
        editNumber_->setRange(c.minValue, c.maxValue);
        editNumber_->setStep(c.step);
        editNumber_->setDecimals(c.decimals);
        editNumber_->setSuffix(c.suffix);
        editNumber_->setValue(std::atof(current.c_str()));
      }
      break;
    case CellEditor::Select:
      if (editSelect_) {
        editSelect_->setItems(c.options);
        editSelect_->setCurrentValue(current);
      }
      break;
    case CellEditor::MultiSelect:
      if (editMulti_) {
        editMulti_->setItems(c.options);
        // The cell's text IS the value list, comma separated -- the same shape
        // the commit writes back, so a round trip through the editor cannot
        // change a value it did not touch.
        for (std::size_t i = 0; i < c.options.size(); ++i) {
          const std::string& v = c.options[i].value.empty() ? c.options[i].text
                                                            : c.options[i].value;
          editMulti_->setChecked(int(i), current.find(v) != std::string::npos);
        }
      }
      break;
    case CellEditor::Text:
    default:
      if (editText_) editText_->setText(current);
      break;
  }

  ensureRowVisible(row);
  syncEditor();

  // A dropdown editor OPENS.  Otherwise picking a value costs two clicks -- one
  // to arm the cell, one to open the list -- and the second one looks to the
  // operator like the first one did not work.  After syncEditor, because a popup
  // is positioned against a geometry the editor does not have until then.
  if (!self.alive()) {
    detail::frameDegraded();
    return;
  }
  if (c.editor == CellEditor::Select && editSelect_) editSelect_->open();
  else if (c.editor == CellEditor::MultiSelect && editMulti_) editMulti_->open();
}

// A SpinBox has no "finished": every step is a value.  So it writes through on
// each change and stays open, which is also why this is not endEdit(true) --
// closing the editor after one click of the up arrow would make a setpoint
// impossible to nudge twice.
void TableView::commitNumber() {
  if (!model_ || !editNumber_) return;
  if (editRow_ < 0 || editCol_ < 0) return;
  const int row = editRow_;
  const int col = editCol_;

  char buf[48];
  std::snprintf(buf, sizeof(buf), "%.*f",
                columns_[std::size_t(col)].decimals, editNumber_->value());
  const std::string value(buf);

  // tableSetText is the application's own store being written to, and a store
  // that reacts by rebuilding the screen is an ordinary thing for it to do.  So
  // the frame is guarded: what is read after the door is `this`.
  const detail::DeathWatch self(this);
  if (!model_->tableSetText(row, col, value)) return;
  if (!self.alive()) {
    detail::frameDegraded();
    return;
  }
  update();
  cellEdited.emit(row, col, value);
}

void TableView::endEdit(bool commit) {
  if (editRow_ < 0) return;

  // Cleared FIRST, and that is what makes this function re-entrant-safe: the
  // editors' own signals (a LineEdit losing focus, a popup closing) come back
  // through here while it is running, and a second pass now returns at the line
  // above instead of committing the same value twice.
  const int row = editRow_;
  const int col = editCol_;
  editRow_ = -1;
  editCol_ = -1;

  std::string value;
  if (commit && model_ && col >= 0 && col < int(columns_.size())) {
    switch (columns_[std::size_t(col)].editor) {
      case CellEditor::Number:
        if (editNumber_) {
          char buf[48];
          std::snprintf(buf, sizeof(buf), "%.*f",
                        columns_[std::size_t(col)].decimals, editNumber_->value());
          value = buf;
        }
        break;
      case CellEditor::Select:
        if (editSelect_) value = editSelect_->currentValue();
        break;
      case CellEditor::MultiSelect:
        if (editMulti_) {
          const std::vector<std::string> vs = editMulti_->checkedValues();
          for (std::size_t i = 0; i < vs.size(); ++i) {
            if (i) value += ", ";
            value += vs[i];
          }
        }
        break;
      case CellEditor::Text:
      default:
        if (editText_) value = editText_->text();
        break;
    }
  }

  syncEditor();

  if (!commit || !model_ || row < 0 || row >= rowCount_) return;

  const detail::DeathWatch self(this);
  const bool accepted = model_->tableSetText(row, col, value);
  if (!self.alive()) {
    detail::frameDegraded();
    return;
  }
  update();
  if (!accepted) return;
  cellEdited.emit(row, col, value);
}

// THE ONLY PLACE AN EDITOR IS MOVED OR SHOWN.
//
// Every setGeometry / setVisible on the four editors is here, and the frame is
// guarded because each of them is a door: hiding a widget marks the layout chain
// dirty, a pass may run, and application code may destroy this table from inside
// one -- while this function is still holding pointers to four of its children.
// The alive() checks are what turn that into a return instead of a crash.
void TableView::syncEditor() {
  Widget* ed = activeEditor();

  Rect cell;
  bool show = false;
  if (ed && editRow_ >= 0 && editCol_ >= 0) {
    cell = cellRect(editRow_, editCol_).deflated(1.0f);
    show = !cell.isEmpty() && cell.intersects(bodyRect());
  }

  const detail::DeathWatch self(this);

  Widget* const all[4] = {editText_, editNumber_, editSelect_, editMulti_};
  for (Widget* w : all) {
    if (!w || (w == ed && show)) continue;
    if (!w->isVisible()) continue;
    w->setVisible(false);
    if (!self.alive()) {
      detail::frameDegraded();
      return;
    }
  }

  if (!show || !ed) return;
  ed->setGeometry(cell);
  if (!self.alive()) {
    detail::frameDegraded();
    return;
  }
  ed->setVisible(true);
  if (!self.alive()) {
    detail::frameDegraded();
    return;
  }
  // AND GIVE IT THE FOCUS, which is not a flourish: keyboard events go to the
  // focused widget, so an editor that is merely visible is one the operator can
  // see and cannot type into.  Last, and after the visibility, because focusing
  // a hidden widget is a state the window would have to unpick again.
  ed->setFocus();
}

void TableView::hideEditors() {
  editRow_ = -1;
  editCol_ = -1;
  syncEditor();
}

}  // namespace geeyoou
