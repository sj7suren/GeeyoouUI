#include "geeyoou/widget/ListView.hpp"

#include <algorithm>
#include <cmath>

#include "geeyoou/render/Theme.hpp"

namespace geeyoou {
namespace {
constexpr float kCellPad = 8.0f;
constexpr float kScrollbarW = 9.0f;
}  // namespace

void ListView::setColumns(std::vector<Column> cols) {
  columns_ = std::move(cols);
  update();
}

void ListView::setRowCount(int n) {
  n = std::max(0, n);
  if (n == rowCount_) return;
  const bool wasAtBottom = isAtBottom();
  rowCount_ = n;
  if (current_ >= rowCount_) current_ = rowCount_ - 1;
  selected_.erase(std::remove_if(selected_.begin(), selected_.end(),
                                 [n](int r) { return r >= n; }),
                  selected_.end());
  // Only follow the tail if the view was ALREADY at the tail.  Otherwise an
  // operator reading yesterday's alarms gets dragged to the bottom every time
  // a new one arrives.
  if (autoBottom_ && wasAtBottom) scrollToBottom();
  else scrollY_ = std::clamp(scrollY_, 0.0f, maxScroll());
  update();
}

void ListView::setSelectionMode(SelectionMode m) {
  mode_ = m;
  if (m == SelectionMode::None) clearSelection();
  update();
}

void ListView::clearSelection() {
  if (selected_.empty()) return;
  selected_.clear();
  update();
  selectionChanged.emit();
}

void ListView::setHeaderVisible(bool on) {
  header_ = on;
  update();
}

void ListView::setRowHeight(float px) {
  rowHeight_ = std::max(12.0f, px);
  update();
}

void ListView::setAlternatingRows(bool on) {
  alternating_ = on;
  update();
}

void ListView::setAutoScrollToBottom(bool on) { autoBottom_ = on; }

void ListView::onGeometryChanged() {
  scrollY_ = std::clamp(scrollY_, 0.0f, maxScroll());
}

// ----------------------------------------------------------------- layout ---
float ListView::headerHeight() const {
  return header_ ? std::round(Theme::current().fontSmall * 2.2f) : 0.0f;
}

Rect ListView::bodyRect() const {
  const Rect r = localRect();
  const float top = headerHeight();
  const bool bar = maxScroll() > 0.0f;
  return {1.0f, top, std::max(0.0f, r.width() - 2.0f - (bar ? kScrollbarW + 2.0f : 0.0f)),
          std::max(0.0f, r.height() - top - 1.0f)};
}

float ListView::maxScroll() const {
  const Rect r = localRect();
  const float body = r.height() - headerHeight() - 1.0f;
  return std::max(0.0f, float(rowCount_) * rowHeight_ - body);
}

bool ListView::isAtBottom() const {
  return scrollY_ >= maxScroll() - 1.0f;
}

void ListView::scrollToBottom() {
  scrollY_ = maxScroll();
  update();
}

void ListView::ensureRowVisible(int row) {
  if (row < 0 || row >= rowCount_) return;
  const Rect body = bodyRect();
  const float top = float(row) * rowHeight_;
  if (top < scrollY_) scrollY_ = top;
  else if (top + rowHeight_ > scrollY_ + body.height()) {
    scrollY_ = top + rowHeight_ - body.height();
  }
  scrollY_ = std::clamp(scrollY_, 0.0f, maxScroll());
  update();
}

int ListView::rowAtY(float y) const {
  const Rect body = bodyRect();
  if (!body.contains({body.center().x, y})) return -1;
  const int i = int(std::floor((y - body.y() + scrollY_) / rowHeight_));
  return (i >= 0 && i < rowCount_) ? i : -1;
}

void ListView::resolveColumnWidths(std::vector<float>& out) const {
  out.assign(columns_.size(), 0.0f);
  const float total = bodyRect().width();
  float fixed = 0.0f;
  int flexCount = 0;
  for (std::size_t i = 0; i < columns_.size(); ++i) {
    if (columns_[i].width > 0.0f) { out[i] = columns_[i].width; fixed += out[i]; }
    else ++flexCount;
  }
  if (flexCount > 0) {
    const float share = std::max(40.0f, (total - fixed) / float(flexCount));
    for (std::size_t i = 0; i < columns_.size(); ++i) {
      if (columns_[i].width <= 0.0f) out[i] = share;
    }
  }
}

void ListView::setCurrentRow(int row, bool scrollIntoView) {
  if (row < -1 || row >= rowCount_) return;
  current_ = row;
  if (scrollIntoView && row >= 0) ensureRowVisible(row);
  update();
}

void ListView::toggleSelection(int row, bool additive) {
  if (mode_ == SelectionMode::None || row < 0) return;
  if (mode_ == SelectionMode::Single || !additive) {
    selected_.assign(1, row);
  } else {
    auto it = std::find(selected_.begin(), selected_.end(), row);
    if (it == selected_.end()) selected_.push_back(row);
    else selected_.erase(it);
  }
  update();
  selectionChanged.emit();
}

// ------------------------------------------------------------------ paint ---
void ListView::onPaint(Painter& p, const Rect&) {
  const Theme& t = Theme::current();
  const Rect r = localRect();
  const bool en = isEffectivelyEnabled();

  p.fillRoundRect(r, t.radius, t.field);
  p.strokeRoundRect(r.deflated(0.5f), t.radius,
                    hasFocus() && en ? t.focusRing : t.panelBorder, 1.0f);

  std::vector<float> widths;
  resolveColumnWidths(widths);
  const Rect body = bodyRect();

  // --- header ---
  if (header_) {
    const float hh = headerHeight();
    p.fillRect({1.0f, 1.0f, r.width() - 2.0f, hh - 1.0f}, t.panel);
    p.strokeLine({1.0f, hh}, {r.right() - 1.0f, hh}, t.panelBorder, 1.0f);
    float x = body.x();
    for (std::size_t c = 0; c < columns_.size(); ++c) {
      const float w = widths[c];
      const float tx = columns_[c].align == HAlign::Right    ? x + w - kCellPad
                       : columns_[c].align == HAlign::Center ? x + w * 0.5f
                                                             : x + kCellPad;
      p.drawText({tx, hh * 0.5f}, columns_[c].title, t.fontSmall, t.textDim,
                 columns_[c].align, VAlign::Middle);
      x += w;
      if (c + 1 < columns_.size()) {
        p.strokeLine({x, 4.0f}, {x, hh - 4.0f}, t.panelBorder, 1.0f);
      }
    }
  }

  if (body.isEmpty() || rowCount_ == 0 || !cellText) return;

  p.save();
  p.clip(body);

  // Only the rows intersecting the viewport are asked for their text.  This is
  // the whole point of the pull model: rowCount_ can be 1 000 000 and this loop
  // still runs a few dozen times.
  const int first = std::max(0, int(std::floor(scrollY_ / rowHeight_)));
  const int last = std::min(rowCount_ - 1,
                            int(std::ceil((scrollY_ + body.height()) / rowHeight_)));

  for (int row = first; row <= last; ++row) {
    const float y = body.y() + float(row) * rowHeight_ - scrollY_;
    const Rect rowRect(body.x(), y, body.width(), rowHeight_);

    const bool isSel = std::find(selected_.begin(), selected_.end(), row) != selected_.end();
    if (isSel) {
      p.fillRect(rowRect, t.selection);
    } else if (row == hovered_) {
      p.fillRect(rowRect, t.panelBorder.withAlpha(80));
    } else if (alternating_ && (row & 1)) {
      p.fillRect(rowRect, t.panel.withAlpha(90));
    }
    if (row == current_ && hasFocus()) {
      p.strokeRect(rowRect.deflated(0.5f), t.focusRing.withAlpha(160), 1.0f);
    }

    // A severity tint is drawn as a left edge bar rather than a full row wash:
    // washing the row makes the text itself harder to read at a glance.
    Color fg = en ? t.text : t.textDisabled;
    if (rowAccent) {
      const Color a = rowAccent(row);
      if (a.alpha() > 0) {
        p.fillRect({body.x(), y, 3.0f, rowHeight_}, a);
        fg = a.lerp(t.text, 0.45f);
      }
    }

    float x = body.x();
    for (std::size_t c = 0; c < columns_.size(); ++c) {
      const float w = widths[c];
      if (x > body.right()) break;
      const std::string txt = cellText(row, int(c));
      if (!txt.empty()) {
        const float tx = columns_[c].align == HAlign::Right    ? x + w - kCellPad
                         : columns_[c].align == HAlign::Center ? x + w * 0.5f
                                                               : x + kCellPad;
        p.save();
        p.clip({x, y, w, rowHeight_});
        p.drawText({tx, y + rowHeight_ * 0.5f}, txt, t.fontBody,
                   c == 0 ? fg : (en ? t.text : t.textDisabled), columns_[c].align,
                   VAlign::Middle);
        p.restore();
      }
      x += w;
    }
  }
  p.restore();

  // --- scrollbar ---
  const float ms = maxScroll();
  if (ms > 0.0f) {
    const float thumbH = std::max(24.0f, body.height() * (body.height() / (body.height() + ms)));
    const float thumbY = body.y() + (body.height() - thumbH) * (scrollY_ / ms);
    const float x = r.right() - kScrollbarW - 2.0f;
    p.fillRoundRect({x, body.y(), kScrollbarW, body.height()}, kScrollbarW * 0.5f, t.track);
    p.fillRoundRect({x, thumbY, kScrollbarW, thumbH}, kScrollbarW * 0.5f, t.scrollbar);
  }
}

// ------------------------------------------------------------------ input ---
void ListView::onMouse(const MouseEvent& e) {
  if (!isEffectivelyEnabled()) return;
  switch (e.action) {
    case MouseAction::Leave:
      hovered_ = -1;
      update();
      e.accept();
      break;

    case MouseAction::Enter:
    case MouseAction::Move: {
      const int row = rowAtY(e.pos.y);
      if (row != hovered_) { hovered_ = row; update(); }
      e.accept();
      break;
    }

    case MouseAction::Press: {
      const int row = rowAtY(e.pos.y);
      if (row < 0) { e.accept(); break; }
      const bool wasCurrent = (row == current_);
      setCurrentRow(row, false);
      toggleSelection(row, e.ctrl);
      rowClicked.emit(row);
      if (wasCurrent) rowActivated.emit(row);
      e.accept();
      break;
    }

    case MouseAction::Wheel:
      scrollY_ = std::clamp(scrollY_ - e.wheelDelta * rowHeight_ * 3.0f, 0.0f,
                            maxScroll());
      update();
      e.accept();
      break;

    default:
      break;
  }
}

void ListView::onKey(const KeyEvent& e) {
  if (!e.pressed || rowCount_ == 0) return;
  const Rect body = bodyRect();
  const int page = std::max(1, int(body.height() / rowHeight_) - 1);

  switch (e.key) {
    case Key::Up:       setCurrentRow(std::max(0, current_ - 1)); e.accept(); break;
    case Key::Down:     setCurrentRow(std::min(rowCount_ - 1, current_ + 1)); e.accept(); break;
    case Key::PageUp:   setCurrentRow(std::max(0, current_ - page)); e.accept(); break;
    case Key::PageDown: setCurrentRow(std::min(rowCount_ - 1, current_ + page)); e.accept(); break;
    case Key::Home:     setCurrentRow(0); e.accept(); break;
    case Key::End:      setCurrentRow(rowCount_ - 1); e.accept(); break;
    case Key::Space:
      toggleSelection(current_, e.ctrl);
      e.accept();
      break;
    case Key::Enter:
      if (current_ >= 0) {
        toggleSelection(current_, false);
        rowActivated.emit(current_);
      }
      e.accept();
      break;
    case Key::KeyA:
      if (e.ctrl && mode_ == SelectionMode::Multi) {
        selected_.clear();
        selected_.reserve(std::size_t(rowCount_));
        for (int i = 0; i < rowCount_; ++i) selected_.push_back(i);
        update();
        selectionChanged.emit();
        e.accept();
      }
      break;
    default:
      break;
  }
}

}  // namespace geeyoou
