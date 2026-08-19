#include "geeyoou/widget/TablePager.hpp"

#include <algorithm>
#include <cstdio>

#include "geeyoou/render/Theme.hpp"

namespace geeyoou {
namespace {
constexpr float kChipH = 26.0f;
constexpr float kChipGap = 6.0f;
constexpr float kChipMinW = 28.0f;
constexpr float kChipPad = 10.0f;
constexpr float kArrowW = 28.0f;

// How many numbered chips before the run is broken with ellipses.  Seven is the
// window that fits "first, gap, three around the current, gap, last" without
// either ellipsis standing next to the number it would have replaced -- at six
// the ellipsis replaces exactly one page, which is a worse affordance than the
// page it hid.
constexpr int kMaxNumbered = 7;

float chipWidth(const std::string& s, float fontSize) {
  return std::max(kChipMinW, measureText(s, fontSize).width + kChipPad * 2.0f);
}
}  // namespace

void TablePager::setTotal(int rows) {
  total_ = std::max(0, rows);
  page_ = std::clamp(page_, 1, pageCount());
  update();
}

void TablePager::setPageSize(int rows) {
  pageSize_ = std::max(1, rows);
  page_ = std::clamp(page_, 1, pageCount());
  update();
}

// The anchor is read BEFORE pageSize_ moves, because firstRow() is computed from
// it: reading it afterwards would anchor to a row the operator was never on.
void TablePager::setPageSizeKeepingPlace(int rows) {
  const int anchor = firstRow();
  pageSize_ = std::max(1, rows);
  page_ = std::clamp(anchor / pageSize_ + 1, 1, pageCount());
  update();
}

void TablePager::setPageSizeOptions(std::vector<int> options) {
  sizeOptions_ = std::move(options);
  if (sizeOptions_.empty()) sizeOptions_.push_back(pageSize_);
  update();
}

void TablePager::setPage(int page) {
  const int p = std::clamp(page, 1, pageCount());
  if (p == page_) return;
  page_ = p;
  update();
}

// At least one page, ALWAYS -- including when the total is zero.  A pager that
// reports "page 1 of 0" is arithmetic leaking into the interface, and every
// clamp below would then have an empty range to clamp into.
int TablePager::pageCount() const {
  if (total_ <= 0) return 1;
  return (total_ + pageSize_ - 1) / pageSize_;
}

int TablePager::firstRow() const { return (page_ - 1) * pageSize_; }

int TablePager::rowsOnPage() const {
  return std::max(0, std::min(pageSize_, total_ - firstRow()));
}

std::string TablePager::sizeLabel() const {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%d 条/页", pageSize_);
  return buf;
}

SizeHint TablePager::sizeHint() const {
  SizeHint h;
  h.preferred = Size{560.0f, 44.0f};
  h.min = Size{320.0f, kChipH + 8.0f};
  return h;
}

// Right-aligned as a group: the navigation is what the hand goes to, and it
// should sit at the same place whether the total reads "共 8 条" or
// "共 1 284 906 条".  The total label is drawn separately, on the left.
void TablePager::layoutItems(std::vector<Item>& out) const {
  out.clear();
  const Theme& t = Theme::current();
  const Rect r = localRect();
  const int pages = pageCount();

  // Which numbers get a chip.  0 is the ellipsis marker.
  std::vector<int> nums;
  if (pages <= kMaxNumbered) {
    for (int i = 1; i <= pages; ++i) nums.push_back(i);
  } else {
    nums.push_back(1);
    const int lo = std::max(2, page_ - 1);
    const int hi = std::min(pages - 1, page_ + 1);
    if (lo > 2) nums.push_back(0);
    for (int i = lo; i <= hi; ++i) nums.push_back(i);
    if (hi < pages - 1) nums.push_back(0);
    nums.push_back(pages);
  }

  // Measure first, place afterwards -- the group's left edge is not known until
  // its width is.
  const std::string szl = sizeLabel();
  float width = chipWidth(szl, t.fontSmall) + kChipGap;
  width += kArrowW + kChipGap;
  for (int n : nums) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", n);
    width += (n == 0 ? kChipMinW : chipWidth(buf, t.fontSmall)) + kChipGap;
  }
  width += kArrowW;

  float x = std::max(kChipPad, r.right() - kChipPad - width);
  const float y = r.center().y - kChipH * 0.5f;

  Item size;
  size.kind = ItemKind::Size;
  size.rect = {x, y, chipWidth(szl, t.fontSmall), kChipH};
  out.push_back(size);
  x += size.rect.width() + kChipGap;

  Item prev;
  prev.kind = ItemKind::Prev;
  prev.rect = {x, y, kArrowW, kChipH};
  prev.enabled = page_ > 1;
  out.push_back(prev);
  x += kArrowW + kChipGap;

  for (int n : nums) {
    Item it;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d", n);
    if (n == 0) {
      it.kind = ItemKind::Ellipsis;
      it.enabled = false;
      it.rect = {x, y, kChipMinW, kChipH};
    } else {
      it.kind = ItemKind::Page;
      it.page = n;
      it.rect = {x, y, chipWidth(buf, t.fontSmall), kChipH};
    }
    out.push_back(it);
    x += it.rect.width() + kChipGap;
  }

  Item next;
  next.kind = ItemKind::Next;
  next.rect = {x, y, kArrowW, kChipH};
  next.enabled = page_ < pages;
  out.push_back(next);
}

int TablePager::itemAt(Point p, const std::vector<Item>& items) const {
  for (std::size_t i = 0; i < items.size(); ++i) {
    if (items[i].enabled && items[i].rect.contains(p)) return int(i);
  }
  return -1;
}

void TablePager::onPaint(Painter& p, const Rect&) {
  const Theme& t = Theme::current();
  const Rect r = localRect();
  const bool en = isEffectivelyEnabled();

  // The count, spelled out.  "共 0 条" rather than a hidden label: a pager that
  // vanishes when a filter matches nothing looks like a broken screen.
  char buf[64];
  std::snprintf(buf, sizeof(buf), "共 %d 条 · 第 %d/%d 页", total_, page_,
                pageCount());
  p.drawText({kChipPad, r.center().y}, buf, t.fontSmall,
             en ? t.textDim : t.textDisabled, HAlign::Left, VAlign::Middle);

  std::vector<Item> items;
  layoutItems(items);

  for (std::size_t i = 0; i < items.size(); ++i) {
    const Item& it = items[i];
    const bool hot = (int(i) == hovered_) && it.enabled && en;
    const bool current = it.kind == ItemKind::Page && it.page == page_;

    if (it.kind != ItemKind::Ellipsis) {
      Color fill = current ? t.accent : (hot ? t.panelBorder.withAlpha(140) : t.field);
      if (!en) fill = fill.lerp(t.background, 0.5f);
      p.fillRoundRect(it.rect, t.radius, fill);
      p.strokeRoundRect(it.rect.deflated(0.5f), t.radius,
                        current ? t.accent : t.panelBorder, 1.0f);
    }

    Color fg = current ? t.onFilled : (en && it.enabled ? t.text : t.textDisabled);

    switch (it.kind) {
      case ItemKind::Size:
        p.drawText(it.rect.center(), sizeLabel(), t.fontSmall,
                   en ? t.text : t.textDisabled, HAlign::Center, VAlign::Middle);
        break;
      case ItemKind::Prev:
        drawIcon(p, Icon::ChevronLeft, it.rect.deflated(7.0f), fg);
        break;
      case ItemKind::Next:
        drawIcon(p, Icon::ChevronRight, it.rect.deflated(7.0f), fg);
        break;
      case ItemKind::Ellipsis:
        p.drawText(it.rect.center(), "…", t.fontSmall, t.textDim, HAlign::Center,
                   VAlign::Middle);
        break;
      case ItemKind::Page: {
        char pbuf[16];
        std::snprintf(pbuf, sizeof(pbuf), "%d", it.page);
        p.drawText(it.rect.center(), pbuf, t.fontSmall, fg, HAlign::Center,
                   VAlign::Middle);
        break;
      }
    }
  }

  if (hasFocus() && en) {
    p.strokeRoundRect(r.deflated(1.5f), t.radius, t.focusRing.withAlpha(150), 1.0f);
  }
}

void TablePager::onMouse(const MouseEvent& e) {
  if (!isEffectivelyEnabled()) return;

  std::vector<Item> items;
  layoutItems(items);

  enum class Fire { None, Page, Size };
  Fire fire = Fire::None;

  switch (e.action) {
    case MouseAction::Leave:
      hovered_ = -1;
      update();
      e.accept();
      break;

    case MouseAction::Enter:
    case MouseAction::Move: {
      const int i = itemAt(e.pos, items);
      if (i != hovered_) { hovered_ = i; update(); }
      e.accept();
      break;
    }

    case MouseAction::Press: {
      if (e.button != MouseButton::Left) break;
      const int i = itemAt(e.pos, items);
      e.accept();
      if (i < 0) break;
      const Item& it = items[std::size_t(i)];

      switch (it.kind) {
        case ItemKind::Size: {
          // Step to the next size and KEEP THE OPERATOR'S PLACE: the page that
          // contains the row they were looking at, not page 1.  Recomputed from
          // the old first row, before pageSize_ moves under it.
          auto pos = std::find(sizeOptions_.begin(), sizeOptions_.end(), pageSize_);
          const std::size_t nextIdx =
              (pos == sizeOptions_.end())
                  ? 0
                  : (std::size_t(pos - sizeOptions_.begin()) + 1) % sizeOptions_.size();
          setPageSizeKeepingPlace(sizeOptions_[nextIdx]);
          fire = Fire::Size;
          break;
        }
        case ItemKind::Prev:
          if (page_ > 1) { page_ -= 1; fire = Fire::Page; }
          break;
        case ItemKind::Next:
          if (page_ < pageCount()) { page_ += 1; fire = Fire::Page; }
          break;
        case ItemKind::Page:
          if (it.page != page_) { page_ = it.page; fire = Fire::Page; }
          break;
        case ItemKind::Ellipsis:
          break;
      }
      break;
    }

    default:
      break;
  }

  // DECIDED ABOVE, ANNOUNCED HERE.  A signal emitted inside the switch has a
  // `break` after it, and a break is code after a door: the slot may have
  // destroyed this pager, and the frame would then run its way out through a
  // destroyed object.  Hoisting the announcement to the end of the function --
  // and into a helper that is nothing but the emit -- is the pattern the rest of
  // this library uses for the same reason.
  if (fire == Fire::None) return;
  if (fire == Fire::Size) {
    emitPageSize();
    return;
  }
  emitPage();
}

// Two helpers, each exactly one statement, so that in both of them the emit IS
// the last thing the frame does.  They exist for that property and for nothing
// else.
void TablePager::emitPage() { pageChanged.emit(page_); }

void TablePager::emitPageSize() { pageSizeChanged.emit(pageSize_); }

void TablePager::onKey(const KeyEvent& e) {
  if (!e.pressed) return;
  const int pages = pageCount();
  int next = page_;

  switch (e.key) {
    case Key::Left:
    case Key::PageUp:   next = page_ - 1; break;
    case Key::Right:
    case Key::PageDown: next = page_ + 1; break;
    case Key::Home:     next = 1; break;
    case Key::End:      next = pages; break;
    default: return;
  }

  next = std::clamp(next, 1, pages);
  e.accept();
  if (next == page_) return;
  page_ = next;
  update();
  emitPage();
}

}  // namespace geeyoou
