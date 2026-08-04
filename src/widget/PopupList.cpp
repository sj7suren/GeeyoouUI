#include "geeyoou/widget/PopupList.hpp"

#include <algorithm>
#include <cmath>

#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"

namespace geeyoou {
namespace {
constexpr float kPad = 5.0f;
constexpr float kRowPadX = 10.0f;
constexpr float kIndentStep = 16.0f;
constexpr float kCheckSide = 15.0f;
constexpr float kExpanderW = 18.0f;
constexpr float kScrollbarW = 6.0f;
}  // namespace

void PopupList::setRows(std::vector<PopupRow> rows) {
  rows_ = std::move(rows);
  highlighted_ = -1;
  hovered_ = -1;
  scrollY_ = 0.0f;
  update();
}

void PopupList::setEmptyText(std::string utf8) {
  emptyText_ = std::move(utf8);
  update();
}

void PopupList::setMaxVisibleRows(int n) { maxVisibleRows_ = std::max(1, n); }

float PopupList::rowHeight() const {
  return std::round(Theme::current().fontBody * 2.15f);
}

float PopupList::preferredHeight() const {
  const int n = rows_.empty() ? 1 : int(rows_.size());
  return float(std::min(n, maxVisibleRows_)) * rowHeight() + kPad * 2.0f;
}

float PopupList::maxScroll() const {
  const float inner = localRect().height() - kPad * 2.0f;
  return std::max(0.0f, float(rows_.size()) * rowHeight() - inner);
}

int PopupList::rowAtY(float y) const {
  if (rows_.empty()) return -1;
  const int i = int(std::floor((y - kPad + scrollY_) / rowHeight()));
  if (i < 0 || i >= int(rows_.size())) return -1;
  return i;
}

void PopupList::scrollTo(int row) {
  if (row < 0) return;
  const float rh = rowHeight();
  const float inner = localRect().height() - kPad * 2.0f;
  const float top = float(row) * rh;
  if (top < scrollY_) scrollY_ = top;
  else if (top + rh > scrollY_ + inner) scrollY_ = top + rh - inner;
  scrollY_ = std::clamp(scrollY_, 0.0f, maxScroll());
}

void PopupList::setHighlighted(int row, bool scrollIntoView) {
  if (row < -1 || row >= int(rows_.size())) return;
  highlighted_ = row;
  if (scrollIntoView) scrollTo(row);
  update();
}

void PopupList::moveHighlight(int delta) {
  if (rows_.empty() || delta == 0) return;
  const int n = int(rows_.size());
  int i = highlighted_;
  // Walk past headers and disabled rows so arrow keys never stop on something
  // that cannot be chosen.  Bounded by n so an all-disabled list terminates.
  for (int steps = 0; steps < n; ++steps) {
    i += (delta > 0 ? 1 : -1);
    if (i < 0) i = n - 1;
    if (i >= n) i = 0;
    if (rows_[std::size_t(i)].enabled && !rows_[std::size_t(i)].header &&
        !rows_[std::size_t(i)].separator) {
      setHighlighted(i);
      return;
    }
  }
}

void PopupList::highlightFirstSelectable() {
  for (std::size_t i = 0; i < rows_.size(); ++i) {
    if (rows_[i].enabled && !rows_[i].header && !rows_[i].separator) {
      setHighlighted(int(i));
      return;
    }
  }
  highlighted_ = -1;
}

void PopupList::activateHighlighted() {
  if (highlighted_ < 0 || highlighted_ >= int(rows_.size())) return;
  const PopupRow& r = rows_[std::size_t(highlighted_)];
  if (!r.enabled || r.header || r.separator) return;
  rowActivated.emit(highlighted_);
}

Rect PopupList::checkRect(int row, float y) const {
  const PopupRow& r = rows_[std::size_t(row)];
  if (!r.checkable) return {};
  const float x = kRowPadX + float(r.indent) * kIndentStep;
  return {x, y + (rowHeight() - kCheckSide) * 0.5f, kCheckSide, kCheckSide};
}

Rect PopupList::expanderRect(int row, float y) const {
  const PopupRow& r = rows_[std::size_t(row)];
  if (!r.expandable) return {};
  const float x = kRowPadX + float(r.indent) * kIndentStep - 2.0f;
  return {x, y, kExpanderW, rowHeight()};
}

// ------------------------------------------------------------------ paint ---
void PopupList::onPaint(Painter& p, const Rect&) {
  const Theme& t = Theme::current();
  const Rect r = localRect();

  // The popup floats over arbitrary content, so it needs a solid, slightly
  // lifted surface -- not the translucent panel used for inline cards.
  p.fillRoundRect(r, t.radius, t.panel);
  p.strokeRoundRect(r.deflated(0.5f), t.radius, t.panelBorder.lerp(t.text, 0.18f),
                    1.0f);

  if (rows_.empty()) {
    p.drawText(r.center(), emptyText_, t.fontBody, t.textDim, HAlign::Center,
               VAlign::Middle);
    return;
  }

  const float rh = rowHeight();
  const bool needBar = maxScroll() > 0.0f;
  const float rowW = r.width() - (needBar ? kScrollbarW + 6.0f : 0.0f);

  p.save();
  p.clip({0.0f, kPad, r.width(), r.height() - kPad * 2.0f});

  // Only the rows on screen are touched, so a 10 000-tag list costs the same
  // as a 10-tag one.
  const int first = std::max(0, int(std::floor(scrollY_ / rh)));
  const int last = std::min(int(rows_.size()) - 1,
                            int(std::ceil((scrollY_ + r.height()) / rh)));

  for (int i = first; i <= last; ++i) {
    const PopupRow& row = rows_[std::size_t(i)];
    const float y = kPad + float(i) * rh - scrollY_;
    const Rect rowRect(2.0f, y, rowW - 4.0f, rh);

    if (row.separator) {
      // Kept at FULL row height rather than a thin strip: every offset in this
      // widget (rowAtY, scrollTo, the visible-range loop) assumes a uniform
      // row pitch, and a variable-height row would cost all of that for a few
      // pixels of vertical space.
      p.strokeLine({kRowPadX, y + rh * 0.5f}, {rowW - kRowPadX, y + rh * 0.5f},
                   t.panelBorder, 1.0f);
      continue;
    }

    if (row.header) {
      p.drawText({kRowPadX, y + rh * 0.5f}, row.text, t.fontSmall, t.textDim,
                 HAlign::Left, VAlign::Middle);
      p.strokeLine({kRowPadX, y + rh - 1.0f}, {rowW - kRowPadX, y + rh - 1.0f},
                   t.panelBorder, 1.0f);
      continue;
    }

    if (i == highlighted_) {
      p.fillRoundRect(rowRect, 4.0f, t.accent.withAlpha(58));
    } else if (i == hovered_) {
      p.fillRoundRect(rowRect, 4.0f, t.panelBorder.withAlpha(90));
    }

    float x = kRowPadX + float(row.indent) * kIndentStep;
    const Color fg = row.enabled ? t.text : t.textDisabled;

    if (row.expandable) {
      drawIcon(p, row.expanded ? Icon::ChevronDown : Icon::ChevronRight,
               {x - 3.0f, y, kExpanderW, rh}, t.textDim, 0.9f);
      x += kExpanderW;
    } else if (row.indent > 0) {
      x += kExpanderW;  // keep leaves aligned with their expandable siblings
    }

    if (row.checkable) {
      const Rect cb(x, y + (rh - kCheckSide) * 0.5f, kCheckSide, kCheckSide);
      p.fillRoundRect(cb, 3.0f, row.checked ? t.accent : t.field);
      p.strokeRoundRect(cb.deflated(0.5f), 3.0f,
                        row.checked ? t.accent : t.panelBorder, 1.0f);
      if (row.checked) {
        p.strokeLine({cb.x() + 3.5f, cb.center().y},
                     {cb.x() + 6.0f, cb.bottom() - 4.0f}, t.background, 1.8f);
        p.strokeLine({cb.x() + 6.0f, cb.bottom() - 4.0f},
                     {cb.right() - 3.5f, cb.y() + 4.0f}, t.background, 1.8f);
      }
      x += kCheckSide + 8.0f;
    }

    if (row.icon != Icon::None) {
      drawIcon(p, row.icon, {x, y, rh, rh}, row.enabled ? t.textDim : t.textDisabled);
      x += rh - 2.0f;
    }

    // Search-match highlight: draw a tinted band behind just the matched
    // substring, using prefix widths so it lands on the right glyphs.
    if (row.matchLen > 0 && row.matchStart + row.matchLen <= row.text.size()) {
      const float mx =
          measureText(std::string_view(row.text).substr(0, row.matchStart),
                      t.fontBody)
              .width;
      const float mw = measureText(std::string_view(row.text)
                                       .substr(row.matchStart, row.matchLen),
                                   t.fontBody)
                           .width;
      p.fillRect({x + mx, y + 4.0f, mw, rh - 8.0f}, t.accent.withAlpha(75));
    }

    p.drawText({x, y + rh * 0.5f}, row.text, t.fontBody, fg, HAlign::Left,
               VAlign::Middle);

    float rightEdge = rowW - kRowPadX;

    if (row.shortcut >= 1 && row.shortcut <= 9) {
      const float bw = 18.0f, bh = 15.0f;
      const Rect badge(rightEdge - bw, y + (rh - bh) * 0.5f, bw, bh);
      p.fillRoundRect(badge, 3.0f, t.panelBorder.withAlpha(150));
      p.drawText(badge.center(), std::to_string(row.shortcut), t.fontSmall,
                 t.textDim, HAlign::Center, VAlign::Middle);
      rightEdge -= bw + 8.0f;
    }

    if (row.selected && !row.checkable) {
      drawIcon(p, Icon::Check, {rightEdge - 16.0f, y, 16.0f, rh}, t.accent);
      rightEdge -= 22.0f;
    }

    if (!row.shortcutText.empty()) {
      p.drawText({rightEdge, y + rh * 0.5f}, row.shortcutText, t.fontSmall,
                 t.textDim, HAlign::Right, VAlign::Middle);
      rightEdge -= measureText(row.shortcutText, t.fontSmall).width + 12.0f;
    }

    if (!row.secondary.empty()) {
      p.drawText({rightEdge, y + rh * 0.5f}, row.secondary, t.fontSmall, t.textDim,
                 HAlign::Right, VAlign::Middle);
    }
  }
  p.restore();

  if (needBar) {
    const float trackH = r.height() - kPad * 2.0f;
    const float ms = maxScroll();
    const float thumbH = std::max(20.0f, trackH * (trackH / (trackH + ms)));
    const float thumbY = kPad + (trackH - thumbH) * (scrollY_ / ms);
    const float x = r.right() - kScrollbarW - 3.0f;
    p.fillRoundRect({x, kPad, kScrollbarW, trackH}, kScrollbarW * 0.5f, t.track);
    p.fillRoundRect({x, thumbY, kScrollbarW, thumbH}, kScrollbarW * 0.5f,
                    t.scrollbar);
  }
}

// ------------------------------------------------------------------ input ---
void PopupList::onMouse(const MouseEvent& e) {
  switch (e.action) {
    case MouseAction::Leave:
      hovered_ = -1;
      update();
      e.accept();
      break;

    case MouseAction::Enter:
    case MouseAction::Move: {
      const int i = rowAtY(e.pos.y);
      const int h = (i >= 0 && rows_[std::size_t(i)].enabled &&
                     !rows_[std::size_t(i)].header && !rows_[std::size_t(i)].separator)
                        ? i
                        : -1;
      if (h != hovered_) {
        hovered_ = h;
        update();
      }
      e.accept();
      break;
    }

    case MouseAction::Press: {
      const int i = rowAtY(e.pos.y);
      if (i < 0) { e.accept(); break; }
      const PopupRow& row = rows_[std::size_t(i)];
      if (row.header || row.separator || !row.enabled) { e.accept(); break; }
      const float y = kPad + float(i) * rowHeight() - scrollY_;

      // Expander and checkbox are hit-tested BEFORE the row itself, so
      // expanding a tree node does not also select it.
      if (row.expandable && expanderRect(i, y).contains(e.pos)) {
        expanderToggled.emit(i);
      } else if (row.checkable && checkRect(i, y).contains(e.pos)) {
        rowToggled.emit(i);
      } else {
        setHighlighted(i, false);
        rowActivated.emit(i);
      }
      e.accept();
      break;
    }

    case MouseAction::Wheel: {
      const float before = scrollY_;
      scrollY_ = std::clamp(scrollY_ - e.wheelDelta * rowHeight() * 3.0f, 0.0f,
                            maxScroll());
      if (scrollY_ != before) update();
      e.accept();
      break;
    }

    case MouseAction::Release:
      e.accept();
      break;

    default:
      break;
  }
}

}  // namespace geeyoou

