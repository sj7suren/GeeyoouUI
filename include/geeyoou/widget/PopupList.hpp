#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include "geeyoou/core/Signal.hpp"
#include "geeyoou/render/Icon.hpp"
#include "geeyoou/widget/Widget.hpp"

namespace geeyoou {

// A single rendered row.
//
// PopupList is a pure VIEW: it knows nothing about trees, filtering or
// multi-selection.  Each select control flattens its own model into rows and
// hands them over.  That split is what lets one list serve a flat combo, a
// filtered search, a checkbox multi-select and an expandable tree without
// growing a mode flag for each.
struct PopupRow {
  std::string text;
  std::string secondary;
  int modelIndex = -1;  // opaque; meaningful only to the owner

  int indent = 0;
  bool header = false;
  bool enabled = true;
  bool selected = false;  // current value, drawn with a check

  bool checkable = false;
  bool checked = false;

  bool expandable = false;
  bool expanded = false;

  Icon icon = Icon::None;
  int shortcut = 0;  // 1..9, shown as a badge; 0 = none
  // Free-text hint right-aligned in dim type ("Ctrl+S").  Distinct from
  // `secondary` only in intent, but menus and selects want different spacing.
  std::string shortcutText;
  bool separator = false;  // a rule, not a row -- takes half height

  // Byte range inside `text` to highlight as a search match.
  std::size_t matchStart = 0;
  std::size_t matchLen = 0;
};

class PopupList : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(PopupList, Widget)

  PopupList() { setVisible(false); }

  void setRows(std::vector<PopupRow> rows);
  const std::vector<PopupRow>& rows() const { return rows_; }
  bool empty() const { return rows_.empty(); }

  void setEmptyText(std::string utf8);
  void setMaxVisibleRows(int n);
  int maxVisibleRows() const { return maxVisibleRows_; }
  float rowHeight() const;

  // Height this list wants for its current rows, capped by maxVisibleRows.
  float preferredHeight() const;

  int highlighted() const { return highlighted_; }
  void setHighlighted(int row, bool scrollIntoView = true);
  // Steps over headers and disabled rows so the keyboard never parks on one.
  void moveHighlight(int delta);
  void highlightFirstSelectable();
  void activateHighlighted();

  // row index, not model index -- the owner maps it back.
  Signal<int> rowActivated;
  Signal<int> rowToggled;        // checkbox clicked
  Signal<int> expanderToggled;   // tree expander clicked

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;
  void onMouse(const MouseEvent& e) override;

 private:
  int rowAtY(float y) const;
  float maxScroll() const;
  void scrollTo(int row);
  Rect checkRect(int row, float y) const;
  Rect expanderRect(int row, float y) const;

  std::vector<PopupRow> rows_;
  std::string emptyText_ = "无匹配项";
  int highlighted_ = -1;
  int hovered_ = -1;
  int maxVisibleRows_ = 9;
  float scrollY_ = 0.0f;
};

}  // namespace geeyoou
