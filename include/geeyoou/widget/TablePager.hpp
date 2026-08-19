#pragma once
//
// Page strip: total, page size, and the numbered pages.
//
// -----------------------------------------------------------------------------
// IT HOLDS NO POINTER TO A TABLE, and that is the whole design.
//
// A pager that owned a TableView would have to know how that view gets its rows,
// which is the one thing a pull-model view refuses to say.  So this control
// knows three integers -- total, page size, current page -- and emits when they
// change.  The application answers by moving its own model window and calling
// TableView::rowsReset().  One consequence worth stating out loud: the same
// pager drives a table, a card grid, or a printed report, because none of them
// are mentioned here.
//
// PAGING AND VIRTUAL SCROLLING ARE NOT ALTERNATIVES.  The view virtualises
// whatever it is given, always; paging only changes what it is given.  A 50-row
// page is virtualised as surely as a 200 000-row model is -- the view cannot
// tell, and nothing in it has to.
//
// Self-painted, with no child widgets: every chip is a rounded rect and a
// string, and the arithmetic that places them is shared by the painter and the
// hit test so a page number can never be clickable beside where it is drawn.
//
#include <string>
#include <vector>

#include "geeyoou/core/Signal.hpp"
#include "geeyoou/render/Icon.hpp"
#include "geeyoou/render/Painter.hpp"
#include "geeyoou/widget/Widget.hpp"

namespace geeyoou {

class TablePager : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(TablePager, Widget)

  TablePager() { setFocusPolicy(FocusPolicy::Tab); }

  // Total number of rows BEHIND the pager -- not the number on this page.
  void setTotal(int rows);
  int total() const { return total_; }

  void setPageSize(int rows);
  // Change the page size and stay on the page that still contains `firstRow()`
  // -- an operator switching from 20 rows to 50 should see MORE of what they
  // were looking at, not be sent back to page 1.  This is what clicking the size
  // chip does; it is public because the rule is worth stating once and testing,
  // rather than living inside a mouse handler.
  void setPageSizeKeepingPlace(int rows);
  int pageSize() const { return pageSize_; }
  // The sizes the chip cycles through.  Clicking it steps to the next one and
  // stays on the page containing the first row the operator was already looking
  // at -- switching from 20 to 50 rows should not send them back to page 1.
  void setPageSizeOptions(std::vector<int> options);

  // 1-based, because it is a label an operator reads, and an off-by-one between
  // what is drawn and what is emitted is the classic pager defect.
  void setPage(int page);
  int page() const { return page_; }
  int pageCount() const;

  // 0-based offset of this page's first row, for slicing the model window.
  int firstRow() const;
  int rowsOnPage() const;

  Signal<int> pageChanged;      // 1-based
  Signal<int> pageSizeChanged;  // rows per page

  SizeHint sizeHint() const override;

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;
  void onMouse(const MouseEvent& e) override;
  void onKey(const KeyEvent& e) override;

 private:
  enum class ItemKind : std::uint8_t { Size, Prev, Page, Ellipsis, Next };

  struct Item {
    ItemKind kind = ItemKind::Page;
    int page = 0;  // Page only
    Rect rect;
    bool enabled = true;
  };

  // ONE piece of arithmetic, used by onPaint and onMouse alike.  Written twice,
  // the two drift by a pixel and the last page becomes unclickable at exactly
  // the moment somebody adds a wider total label.
  void layoutItems(std::vector<Item>& out) const;
  // One statement each, and that statement is the emit.  See the call site in
  // onMouse for why the announcement is hoisted out of the switch.
  void emitPage();
  void emitPageSize();
  std::string sizeLabel() const;
  int itemAt(Point p, const std::vector<Item>& items) const;

  int total_ = 0;
  int pageSize_ = 20;
  int page_ = 1;
  std::vector<int> sizeOptions_{10, 20, 50, 100};
  int hovered_ = -1;
};

}  // namespace geeyoou
