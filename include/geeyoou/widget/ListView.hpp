#pragma once
#include <cstddef>
#include <functional>
#include <string>
#include <vector>

#include "geeyoou/core/Signal.hpp"
#include "geeyoou/render/Icon.hpp"
#include "geeyoou/render/Painter.hpp"
#include "geeyoou/widget/Widget.hpp"

namespace geeyoou {

// Virtualised multi-column list.
//
// PULL model, not push: the widget stores a row COUNT and asks the application
// for a cell's text only when that cell is about to be drawn.  A million-row
// alarm history therefore costs the widget nothing -- the data stays in
// whatever ring buffer the acquisition layer already owns, and is never copied.
// This is the same reason TrendChart holds samples in a fixed ring rather than
// accepting a vector: docs/architecture.md section 1, rule 2.
class ListView : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(ListView, Widget)

  enum class SelectionMode { None, Single, Multi };

  struct Column {
    std::string title;
    float width = 100.0f;   // <= 0 means "take the remaining space"
    HAlign align = HAlign::Left;
  };

  ListView() { setFocusPolicy(FocusPolicy::Tab); }

  void setColumns(std::vector<Column> cols);
  void setRowCount(int n);
  int rowCount() const { return rowCount_; }

  // Required. Called only for visible cells.
  std::function<std::string(int row, int col)> cellText;
  // Optional per-row tint (severity colouring) and leading icon.
  std::function<Color(int row)> rowAccent;
  std::function<Icon(int row)> rowIcon;

  void setSelectionMode(SelectionMode m);
  int currentRow() const { return current_; }
  void setCurrentRow(int row, bool scrollIntoView = true);
  const std::vector<int>& selectedRows() const { return selected_; }
  void clearSelection();

  void setHeaderVisible(bool on);
  void setRowHeight(float px);
  float rowHeight() const { return rowHeight_; }
  void setAlternatingRows(bool on);

  // Follows the tail as rows are appended -- but only while the operator has
  // not scrolled away, so a live alarm feed never yanks the view out from
  // under someone reading history.
  void setAutoScrollToBottom(bool on);
  bool isAtBottom() const;
  void scrollToBottom();
  void ensureRowVisible(int row);

  SizeHint sizeHint() const override;

  Signal<int> rowClicked;
  Signal<int> rowActivated;   // double-ish: Enter, or a click on the current row
  Signal<> selectionChanged;

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;
  void onMouse(const MouseEvent& e) override;
  void onKey(const KeyEvent& e) override;
  void onGeometryChanged() override;

 private:
  float headerHeight() const;
  Rect bodyRect() const;
  float maxScroll() const;
  int rowAtY(float y) const;
  void resolveColumnWidths(std::vector<float>& out) const;
  void toggleSelection(int row, bool additive);

  std::vector<Column> columns_;
  int rowCount_ = 0;
  int current_ = -1;
  std::vector<int> selected_;
  SelectionMode mode_ = SelectionMode::Single;

  float scrollY_ = 0.0f;
  float rowHeight_ = 26.0f;
  bool header_ = true;
  bool alternating_ = true;
  bool autoBottom_ = false;
  int hovered_ = -1;
};

}  // namespace geeyoou
