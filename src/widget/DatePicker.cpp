#include "geeyoou/widget/DatePicker.hpp"

#include <algorithm>
#include <cstdio>

#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/Window.hpp"

namespace geeyoou {
namespace {
constexpr float kPad = 8.0f;
constexpr float kHeaderH = 34.0f;
constexpr float kWeekHeaderH = 22.0f;
constexpr int kCols = 7;
constexpr int kRows = 6;  // fixed: a month spans 4-6 weeks, and a grid that
                          // changes height month to month makes the popup jump
const char* const kWeekNames[7] = {"一", "二", "三", "四", "五", "六", "日"};
}  // namespace

// ============================================================ CalendarView ===
CalendarView::CalendarView() { setVisible(false); }

void CalendarView::setSelected(Date d) {
  selected_ = d;
  if (d.valid()) shown_ = Date{d.year, d.month, 1};
  update();
}

void CalendarView::setVisibleMonth(Date d) {
  if (!d.valid()) return;
  shown_ = Date{d.year, d.month, 1};
  update();
}

void CalendarView::setRange(Date lo, Date hi) {
  min_ = lo;
  max_ = hi;
  update();
}

void CalendarView::setToday(Date d) {
  today_ = d;
  update();
}

bool CalendarView::inRange(const Date& d) const { return d >= min_ && d <= max_; }

Size CalendarView::preferredSize() const {
  return {7.0f * 36.0f + kPad * 2.0f,
          kHeaderH + kWeekHeaderH + 6.0f * 32.0f + kPad * 2.0f};
}

Rect CalendarView::headerRect() const {
  return {kPad, kPad, localRect().width() - kPad * 2.0f, kHeaderH};
}

Rect CalendarView::gridRect() const {
  const Rect r = localRect();
  const float top = kPad + kHeaderH + kWeekHeaderH;
  return {kPad, top, r.width() - kPad * 2.0f, r.height() - top - kPad};
}

Rect CalendarView::prevRect() const {
  const Rect h = headerRect();
  return {h.x(), h.y(), kHeaderH, kHeaderH};
}

Rect CalendarView::nextRect() const {
  const Rect h = headerRect();
  return {h.right() - kHeaderH, h.y(), kHeaderH, kHeaderH};
}

float CalendarView::cellW() const { return gridRect().width() / float(kCols); }
float CalendarView::cellH() const { return gridRect().height() / float(kRows); }

Date CalendarView::dateAt(Point p, bool* inGrid) const {
  const Rect g = gridRect();
  if (inGrid) *inGrid = false;
  if (!g.contains(p)) return {};
  const int col = std::clamp(int((p.x - g.x()) / cellW()), 0, kCols - 1);
  const int row = std::clamp(int((p.y - g.y()) / cellH()), 0, kRows - 1);
  const int lead = weekdayMondayFirst(Date{shown_.year, shown_.month, 1});
  const int offset = row * kCols + col - lead;
  if (inGrid) *inGrid = true;
  return addDays(Date{shown_.year, shown_.month, 1}, offset);
}

void CalendarView::onPaint(Painter& p, const Rect&) {
  const Theme& t = Theme::current();
  const Rect r = localRect();

  p.fillRoundRect(r, t.radius, t.panel);
  p.strokeRoundRect(r.deflated(0.5f), t.radius, t.panelBorder.lerp(t.text, 0.18f),
                    1.0f);

  // --- header: < 2026 年 8 月 > ---
  const Rect h = headerRect();
  char title[32];
  std::snprintf(title, sizeof(title), "%d 年 %d 月", shown_.year, shown_.month);
  p.drawText(h.center(), title, t.fontBody, t.text, HAlign::Center, VAlign::Middle);
  drawIcon(p, Icon::ChevronLeft, prevRect(), t.textDim, 0.9f);
  drawIcon(p, Icon::ChevronRight, nextRect(), t.textDim, 0.9f);

  // --- weekday strip ---
  const Rect g = gridRect();
  const float cw = cellW();
  for (int c = 0; c < kCols; ++c) {
    const float x = g.x() + cw * (float(c) + 0.5f);
    // Weekend heads tinted so the eye finds Saturday/Sunday without counting.
    p.drawText({x, kPad + kHeaderH + kWeekHeaderH * 0.5f}, kWeekNames[c],
               t.fontSmall, c >= 5 ? t.warn.lerp(t.textDim, 0.4f) : t.textDim,
               HAlign::Center, VAlign::Middle);
  }

  // --- day grid ---
  const Date first{shown_.year, shown_.month, 1};
  const int lead = weekdayMondayFirst(first);
  const float ch = cellH();

  for (int i = 0; i < kRows * kCols; ++i) {
    const Date d = addDays(first, i - lead);
    const int col = i % kCols;
    const int row = i / kCols;
    const Rect cell(g.x() + cw * float(col), g.y() + ch * float(row), cw, ch);

    const bool thisMonth = (d.month == shown_.month && d.year == shown_.year);
    const bool enabled = inRange(d);
    const bool isSel = selected_.valid() && d == selected_;
    const bool isToday = today_.valid() && d == today_;

    if (isSel) {
      p.fillRoundRect(cell.deflated(3.0f), 4.0f, t.accent);
    } else if (i == hoverCell_ && enabled) {
      p.fillRoundRect(cell.deflated(3.0f), 4.0f, t.panelBorder.withAlpha(120));
    }
    if (isToday && !isSel) {
      p.strokeRoundRect(cell.deflated(3.5f), 4.0f, t.accent, 1.0f);
    }

    Color fg = t.text;
    if (!enabled) fg = t.textDisabled;
    else if (isSel) fg = t.onFilled;
    else if (!thisMonth) fg = t.textDim.lerp(t.background, 0.45f);
    else if (col >= 5) fg = t.warn.lerp(t.text, 0.55f);

    char num[8];
    std::snprintf(num, sizeof(num), "%d", d.day);
    p.drawText(cell.center(), num, t.fontBody, fg, HAlign::Center, VAlign::Middle);
  }
}

void CalendarView::onMouse(const MouseEvent& e) {
  switch (e.action) {
    case MouseAction::Leave:
      hoverCell_ = -1;
      update();
      e.accept();
      break;

    case MouseAction::Enter:
    case MouseAction::Move: {
      const Rect g = gridRect();
      int cell = -1;
      if (g.contains(e.pos)) {
        const int col = std::clamp(int((e.pos.x - g.x()) / cellW()), 0, kCols - 1);
        const int row = std::clamp(int((e.pos.y - g.y()) / cellH()), 0, kRows - 1);
        cell = row * kCols + col;
      }
      if (cell != hoverCell_) { hoverCell_ = cell; update(); }
      e.accept();
      break;
    }

    case MouseAction::Press:
      if (e.button != MouseButton::Left) break;
      if (prevRect().contains(e.pos)) {
        shown_ = addMonths(shown_, -1);
        update();
      } else if (nextRect().contains(e.pos)) {
        shown_ = addMonths(shown_, +1);
        update();
      } else {
        bool inGrid = false;
        const Date d = dateAt(e.pos, &inGrid);
        if (inGrid && inRange(d)) {
          selected_ = d;
          // Clicking a trailing/leading day also navigates to that month, so
          // the operator is not left staring at a selection they cannot see.
          shown_ = Date{d.year, d.month, 1};
          update();
          dateChosen.emit(d);
        }
      }
      e.accept();
      break;

    case MouseAction::Wheel:
      shown_ = addMonths(shown_, e.wheelDelta > 0 ? -1 : +1);
      update();
      e.accept();
      break;

    default:
      break;
  }
}

// ============================================================== DatePicker ===
void DatePicker::setDate(Date d) {
  if (date_ == d) return;
  date_ = d;
  if (calendar_) calendar_->setSelected(d);
  update();
  dateChanged.emit(date_);
}

void DatePicker::clearDate() {
  date_ = Date{};
  update();
  dateChanged.emit(date_);
}

void DatePicker::setRange(Date lo, Date hi) {
  min_ = lo;
  max_ = hi;
  if (calendar_) calendar_->setRange(lo, hi);
}

void DatePicker::setToday(Date d) {
  today_ = d;
  if (calendar_) calendar_->setToday(d);
}

std::string DatePicker::displayText() const {
  return date_.valid() ? toIsoString(date_) : std::string{};
}

void DatePicker::open() {
  if (!isEffectivelyEnabled() || isOpen()) return;
  Window* w = window();
  if (!w) return;

  if (!calendar_) {
    calendar_ = w->add<CalendarView>();
    calendar_->setRange(min_, max_);
    calendar_->setToday(today_);
    // Owned by conns_: the calendar belongs to the Window and survives us.
    //
    // CLOSE FIRST, dispatch second.  setDate() emits dateChanged, an emit runs
    // application code, and picking a date is exactly when a form navigates
    // away and destroys the page holding this picker -- which contract D7
    // permits, since dateChosen belongs to the Window's CalendarView rather
    // than to us.  So the emit has to be the last statement: with close() after
    // it, this lambda called into a freed `this`.
    //
    // The visible trade: the application observes "popup closed" BEFORE
    // "date changed".
    conns_ += calendar_->dateChosen.connect([this](Date d) {
      close();
      setDate(d);
    });
  }
  calendar_->setSelected(date_);
  if (!date_.valid() && today_.valid()) calendar_->setVisibleMonth(today_);

  const Size s = calendar_->preferredSize();
  calendar_->setGeometry({0.0f, 0.0f, s.width, s.height});
  showCustomPopup(calendar_);
}

void DatePicker::close() {
  if (calendar_) hideCustomPopup(calendar_);
}

}  // namespace geeyoou
