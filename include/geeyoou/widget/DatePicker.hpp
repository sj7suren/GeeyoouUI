#pragma once
#include <string>

#include "geeyoou/core/ConnectionScope.hpp"
#include "geeyoou/core/Date.hpp"
#include "geeyoou/widget/SelectBase.hpp"

namespace geeyoou {

// The month grid itself.  A separate widget so it can also be embedded inline
// on a screen that wants a permanently visible calendar.
class CalendarView : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(CalendarView, Widget)

  CalendarView();

  void setSelected(Date d);
  Date selected() const { return selected_; }
  void setVisibleMonth(Date anyDayInMonth);
  Date visibleMonth() const { return shown_; }
  void setRange(Date minDate, Date maxDate);
  void setToday(Date d);

  Size preferredSize() const;

  Signal<Date> dateChosen;

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;
  void onMouse(const MouseEvent& e) override;

 private:
  Rect headerRect() const;
  Rect gridRect() const;
  Rect prevRect() const;
  Rect nextRect() const;
  float cellW() const;
  float cellH() const;
  Date dateAt(Point p, bool* inGrid) const;
  bool inRange(const Date& d) const;

  Date selected_{};
  Date shown_{2026, 8, 1};
  Date today_{};
  Date min_{1970, 1, 1};
  Date max_{2999, 12, 31};
  int hoverCell_ = -1;
};

// Dropdown wrapping a CalendarView.
//
// Overrides open()/close() rather than feeding rows to a PopupList: the popup
// content is a grid, not a list.  Everything else -- the closed field, the
// chevron, focus handling, click-outside dismissal -- is inherited unchanged.
class DatePicker : public SelectBase {
 public:
  GEEYOOU_STYLE_TYPE(DatePicker, SelectBase)

  void setDate(Date d);
  Date date() const { return date_; }
  bool hasDate() const { return date_.valid(); }
  void clearDate();

  void setRange(Date minDate, Date maxDate);
  void setToday(Date d);

  void open() override;
  void close() override;

  Signal<Date> dateChanged;

 protected:
  std::string displayText() const override;
  bool hasValue() const override { return date_.valid(); }

 private:
  Date date_{};
  Date today_{};
  Date min_{1970, 1, 1};
  Date max_{2999, 12, 31};
  CalendarView* calendar_ = nullptr;

  // Declared LAST so it is destroyed FIRST.  The calendar is a child of the
  // WINDOW, so the dateChosen slot open() installs -- which captures `this` --
  // outlives this picker unless the subscription is owned.
  ConnectionScope conns_;
};

}  // namespace geeyoou
