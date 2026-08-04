#include "geeyoou/widget/TextArea.hpp"

#include <algorithm>
#include <cmath>

#include "geeyoou/core/Utf8.hpp"
#include "geeyoou/platform/Platform.hpp"
#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/Window.hpp"

namespace geeyoou {
namespace {
constexpr float kPad = 10.0f;
constexpr float kScrollbarWidth = 8.0f;
constexpr float kLineSpacing = 1.35f;
constexpr int kBlinkTicks = 16;
}  // namespace

// ---------------------------------------------------------------- setters ---
void TextArea::setText(std::string utf8) {
  if (text_ == utf8) return;
  text_ = std::move(utf8);
  caret_ = selAnchor_ = text_.size();
  scrollY_ = 0.0f;
  layoutWidth_ = -1.0f;
  rebuildLayout();
  update();
  textChanged.emit(text_);
}

void TextArea::setPlaceholder(std::string utf8) {
  placeholder_ = std::move(utf8);
  update();
}

void TextArea::setReadOnly(bool on) {
  readOnly_ = on;
  update();
}

void TextArea::onGeometryChanged() {
  layoutWidth_ = -1.0f;  // width changed => wrapping changed
  rebuildLayout();
}

// ----------------------------------------------------------------- layout ---
float TextArea::lineHeight() const {
  return std::round(fontLineHeight(Theme::current().fontBody) * kLineSpacing);
}

Rect TextArea::contentRect() const {
  const Rect r = localRect();
  const float sb = (float(lines_.size()) * lineHeight() > r.height() - kPad * 2.0f)
                       ? kScrollbarWidth + 4.0f
                       : 0.0f;
  const float w = r.width() - kPad * 2.0f - sb;
  const float h = r.height() - kPad * 2.0f;
  if (w <= 0.0f || h <= 0.0f) return {};
  return {kPad, kPad, w, h};
}

void TextArea::rebuildLayout() {
  const Theme& t = Theme::current();
  const Rect r = localRect();
  // Compute the wrap width WITHOUT calling contentRect(): that consults
  // lines_, which is exactly what we are about to rebuild.
  const float avail = r.width() - kPad * 2.0f - (kScrollbarWidth + 4.0f);
  if (avail == layoutWidth_ && !lines_.empty()) return;
  layoutWidth_ = avail;

  lines_.clear();
  if (avail <= 0.0f) return;

  std::size_t pos = 0;
  while (true) {
    // Find the end of this hard line (up to '\n' or end of text).
    std::size_t hardEnd = text_.find('\n', pos);
    const bool hasNewline = (hardEnd != std::string::npos);
    if (!hasNewline) hardEnd = text_.size();

    std::size_t lineStart = pos;
    do {
      VisualLine vl;
      vl.start = lineStart;
      vl.offs.push_back(lineStart);
      vl.xs.push_back(0.0f);

      std::size_t i = lineStart;
      std::size_t lastSpace = std::string::npos;  // break opportunity
      while (i < hardEnd) {
        const std::size_t next = utf8::nextBoundary(text_, i);
        const float w =
            measureText(std::string_view(text_).substr(lineStart, next - lineStart),
                        t.fontBody)
                .width;
        if (w > avail && i > lineStart) {
          // Prefer breaking after the last space so Latin words stay whole;
          // CJK has no spaces and simply breaks per character, which is the
          // correct behaviour for it anyway.
          if (lastSpace != std::string::npos && lastSpace > lineStart) {
            while (!vl.offs.empty() && vl.offs.back() > lastSpace) {
              vl.offs.pop_back();
              vl.xs.pop_back();
            }
            i = lastSpace;
          }
          break;
        }
        if (text_[i] == ' ') lastSpace = next;
        i = next;
        vl.offs.push_back(i);
        vl.xs.push_back(w);
      }

      vl.end = i;
      lines_.push_back(std::move(vl));
      lineStart = i;
    } while (lineStart < hardEnd);

    if (!hasNewline) break;
    pos = hardEnd + 1;
    // A trailing newline must still produce an empty final row, otherwise the
    // caret has nowhere to sit after pressing Enter at the end of the text.
    if (pos > text_.size()) break;
    if (pos == text_.size()) {
      VisualLine vl;
      vl.start = vl.end = pos;
      vl.offs.push_back(pos);
      vl.xs.push_back(0.0f);
      lines_.push_back(std::move(vl));
      break;
    }
  }

  if (lines_.empty()) {
    VisualLine vl;
    vl.offs.push_back(0);
    vl.xs.push_back(0.0f);
    lines_.push_back(std::move(vl));
  }
}

std::size_t TextArea::lineOfOffset(std::size_t off) const {
  for (std::size_t i = 0; i < lines_.size(); ++i) {
    const VisualLine& l = lines_[i];
    // `<= end` so a caret parked at a wrap point belongs to the earlier row;
    // the next row's start equals this row's end.
    if (off >= l.start && off <= l.end) return i;
  }
  return lines_.empty() ? 0 : lines_.size() - 1;
}

Point TextArea::caretPoint() const {
  if (lines_.empty()) return {0.0f, 0.0f};
  const std::size_t li = lineOfOffset(caret_);
  const VisualLine& l = lines_[li];
  float x = 0.0f;
  for (std::size_t k = 0; k < l.offs.size(); ++k) {
    if (l.offs[k] == caret_) { x = l.xs[k]; break; }
    if (l.offs[k] > caret_) break;
    x = l.xs[k];
  }
  return {x, float(li) * lineHeight()};
}

std::size_t TextArea::offsetAtPoint(Point p) const {
  if (lines_.empty()) return 0;
  const float lh = lineHeight();
  int li = int(std::floor(p.y / lh));
  li = std::clamp(li, 0, int(lines_.size()) - 1);
  const VisualLine& l = lines_[std::size_t(li)];

  std::size_t best = l.start;
  for (std::size_t k = 0; k + 1 < l.xs.size(); ++k) {
    const float mid = (l.xs[k] + l.xs[k + 1]) * 0.5f;
    if (p.x < mid) return l.offs[k];
    best = l.offs[k + 1];
  }
  return best;
}

float TextArea::maxScroll() const {
  const Rect c = contentRect();
  if (c.isEmpty()) return 0.0f;
  return std::max(0.0f, float(lines_.size()) * lineHeight() - c.height());
}

// ------------------------------------------------------------------- edit ---
void TextArea::emitChanged() {
  layoutWidth_ = -1.0f;
  rebuildLayout();
  update();
  textChanged.emit(text_);
}

void TextArea::deleteSelection() {
  if (!hasSelection()) return;
  const std::size_t a = std::min(caret_, selAnchor_);
  const std::size_t b = std::max(caret_, selAnchor_);
  text_.erase(a, b - a);
  caret_ = selAnchor_ = a;
}

void TextArea::insertText(const std::string& utf8) {
  if (readOnly_ || utf8.empty()) return;
  deleteSelection();
  text_.insert(caret_, utf8);
  caret_ += utf8.size();
  selAnchor_ = caret_;
  desiredX_ = -1.0f;
  emitChanged();
  ensureCaretVisible();
}

void TextArea::moveCaret(std::size_t to, bool extend) {
  caret_ = utf8::clampToBoundary(text_, std::min(to, text_.size()));
  if (!extend) selAnchor_ = caret_;
  caretOn_ = true;
  blinkCounter_ = 0;
  ensureCaretVisible();
  update();
}

void TextArea::ensureCaretVisible() {
  const Rect c = contentRect();
  if (c.isEmpty()) return;
  const float lh = lineHeight();
  const float y = caretPoint().y;
  if (y < scrollY_) scrollY_ = y;
  else if (y + lh > scrollY_ + c.height()) scrollY_ = y + lh - c.height();
  scrollY_ = std::clamp(scrollY_, 0.0f, maxScroll());
  notifyImeCaret();
}

void TextArea::notifyImeCaret() {
  if (!hasFocus()) return;
  Window* win = window();
  if (!win) return;
  const Rect c = contentRect();
  const Point cp = caretPoint();
  const Point origin = mapToWindow({c.x() + cp.x, c.y() + cp.y - scrollY_});
  win->setImeCaret({origin, Size(1.0f, lineHeight())});
}

void TextArea::selectAll() {
  selAnchor_ = 0;
  caret_ = text_.size();
  update();
}

std::string TextArea::selectedText() const {
  if (!hasSelection()) return {};
  const std::size_t a = std::min(caret_, selAnchor_);
  const std::size_t b = std::max(caret_, selAnchor_);
  return text_.substr(a, b - a);
}

// ------------------------------------------------------------------ paint ---
void TextArea::onPaint(Painter& p, const Rect&) {
  const Theme& t = Theme::current();
  const Rect r = localRect();
  const bool en = isEffectivelyEnabled();
  const bool focused = hasFocus() && en;

  // Same rule as LineEdit: read-only loses the sunken field background so it
  // never reads as an editable box.
  Color bg = t.field;
  if (!en) bg = t.field.lerp(t.background, 0.5f);
  else if (readOnly_) bg = t.panel;

  p.fillRoundRect(r, t.radius, bg);
  p.strokeRoundRect(r.deflated(0.5f), t.radius,
                    focused ? t.focusRing
                            : (readOnly_ ? t.panelBorder.lerp(t.background, 0.4f)
                                         : t.panelBorder),
                    focused ? 1.5f : 1.0f);

  rebuildLayout();  // no-op unless the width changed since the last pass
  const Rect c = contentRect();
  if (c.isEmpty()) return;

  const float lh = lineHeight();

  p.save();
  p.clip(c);

  if (text_.empty() && !placeholder_.empty()) {
    p.drawText({c.x(), c.y()}, placeholder_, t.fontBody, t.placeholder,
               HAlign::Left, VAlign::Top);
  } else {
    const std::size_t selA = std::min(caret_, selAnchor_);
    const std::size_t selB = std::max(caret_, selAnchor_);

    // Only the rows intersecting the viewport are touched -- a 5000-line note
    // costs the same to draw as a 5-line one.
    const int first = std::max(0, int(std::floor(scrollY_ / lh)));
    const int last = std::min(int(lines_.size()) - 1,
                              int(std::ceil((scrollY_ + c.height()) / lh)));

    for (int li = first; li <= last; ++li) {
      const VisualLine& l = lines_[std::size_t(li)];
      const float y = c.y() + float(li) * lh - scrollY_;

      if (focused && selB > selA && l.end >= selA && l.start <= selB) {
        const auto xAt = [&](std::size_t off) {
          float x = 0.0f;
          for (std::size_t k = 0; k < l.offs.size(); ++k) {
            if (l.offs[k] > off) break;
            x = l.xs[k];
          }
          return x;
        };
        const float x1 = xAt(std::max(selA, l.start));
        const float x2 = xAt(std::min(selB, l.end));
        // A selection spanning past this row's end should show the newline as
        // a sliver of highlight, otherwise multi-line selections look broken.
        const float x2adj = (selB > l.end) ? std::max(x2, x1) + 5.0f : x2;
        if (x2adj > x1) {
          p.fillRect({c.x() + x1, y, x2adj - x1, lh}, t.selection);
        }
      }

      if (l.end > l.start) {
        p.drawText({c.x(), y}, std::string_view(text_).substr(l.start, l.end - l.start),
                   t.fontBody,
                   !en ? t.textDisabled : (readOnly_ ? t.textDim : t.text),
                   HAlign::Left, VAlign::Top);
      }
    }
  }

  if (focused && caretOn_ && !readOnly_) {
    const Point cp = caretPoint();
    const float cx = c.x() + cp.x;
    const float cy = c.y() + cp.y - scrollY_;
    p.strokeLine({cx, cy + 1.0f}, {cx, cy + lh - 2.0f}, t.text, 1.0f);
  }
  p.restore();

  // --- scrollbar ---
  const float ms = maxScroll();
  if (ms > 0.0f) {
    const float trackH = c.height();
    const float thumbH = std::max(24.0f, trackH * (c.height() / (c.height() + ms)));
    const float thumbY = c.y() + (trackH - thumbH) * (scrollY_ / ms);
    const float x = r.right() - kPad - kScrollbarWidth + 2.0f;
    p.fillRoundRect({x, c.y(), kScrollbarWidth, trackH}, kScrollbarWidth * 0.5f,
                    t.track);
    p.fillRoundRect({x, thumbY, kScrollbarWidth, thumbH}, kScrollbarWidth * 0.5f,
                    t.scrollbar);
  }
}

// ------------------------------------------------------------------ input ---
void TextArea::onMouse(const MouseEvent& e) {
  if (!isEffectivelyEnabled()) return;
  const Rect c = contentRect();

  switch (e.action) {
    case MouseAction::Enter: hovered_ = true; update(); e.accept(); break;
    case MouseAction::Leave: hovered_ = false; dragging_ = false; update(); e.accept(); break;

    case MouseAction::Press:
      if (e.button == MouseButton::Left && !c.isEmpty()) {
        dragging_ = true;
        desiredX_ = -1.0f;
        moveCaret(offsetAtPoint({e.pos.x - c.x(), e.pos.y - c.y() + scrollY_}),
                  e.shift);
        e.accept();
      }
      break;

    case MouseAction::Move:
      if (dragging_ && !c.isEmpty()) {
        moveCaret(offsetAtPoint({e.pos.x - c.x(), e.pos.y - c.y() + scrollY_}), true);
        e.accept();
      }
      break;

    case MouseAction::Release:
      dragging_ = false;
      e.accept();
      break;

    case MouseAction::Wheel: {
      const float before = scrollY_;
      scrollY_ = std::clamp(scrollY_ - e.wheelDelta * lineHeight() * 3.0f, 0.0f,
                            maxScroll());
      if (scrollY_ != before) update();
      e.accept();
      break;
    }

    default: break;
  }
}

void TextArea::onKey(const KeyEvent& e) {
  if (!e.pressed || !isEffectivelyEnabled()) return;

  if (e.ctrl) {
    switch (e.key) {
      case Key::KeyA: selectAll(); e.accept(); return;
      case Key::KeyC:
        if (hasSelection()) platform().setClipboardText(selectedText());
        e.accept();
        return;
      case Key::KeyX:
        if (hasSelection() && !readOnly_) {
          platform().setClipboardText(selectedText());
          deleteSelection();
          emitChanged();
          ensureCaretVisible();
        }
        e.accept();
        return;
      case Key::KeyV:
        insertText(platform().clipboardText());
        e.accept();
        return;
      case Key::Home: moveCaret(0, e.shift); e.accept(); return;
      case Key::End:  moveCaret(text_.size(), e.shift); e.accept(); return;
      default: break;
    }
  }

  const std::size_t li = lineOfOffset(caret_);

  switch (e.key) {
    case Key::Left:
      desiredX_ = -1.0f;
      moveCaret(utf8::prevBoundary(text_, caret_), e.shift);
      e.accept();
      return;
    case Key::Right:
      desiredX_ = -1.0f;
      moveCaret(utf8::nextBoundary(text_, caret_), e.shift);
      e.accept();
      return;

    case Key::Up:
    case Key::Down: {
      // Preserve the column across rows of differing length: without a sticky
      // desiredX_, walking down through a short line permanently pulls the
      // caret to the left.
      if (desiredX_ < 0.0f) desiredX_ = caretPoint().x;
      const int target = int(li) + (e.key == Key::Down ? 1 : -1);
      if (target < 0 || target >= int(lines_.size())) { e.accept(); return; }
      const float lh = lineHeight();
      const std::size_t to =
          offsetAtPoint({desiredX_, float(target) * lh + lh * 0.5f});
      const float keep = desiredX_;
      moveCaret(to, e.shift);
      desiredX_ = keep;
      e.accept();
      return;
    }

    case Key::Home: desiredX_ = -1.0f; moveCaret(lines_[li].start, e.shift); e.accept(); return;
    case Key::End:  desiredX_ = -1.0f; moveCaret(lines_[li].end, e.shift); e.accept(); return;

    case Key::PageUp:
    case Key::PageDown: {
      const Rect c = contentRect();
      const float lh = lineHeight();
      const int rows = std::max(1, int(c.height() / lh) - 1);
      const int target = std::clamp(int(li) + (e.key == Key::PageDown ? rows : -rows),
                                    0, int(lines_.size()) - 1);
      moveCaret(offsetAtPoint({caretPoint().x, float(target) * lh + lh * 0.5f}),
                e.shift);
      e.accept();
      return;
    }

    case Key::Backspace:
      if (!readOnly_) {
        if (hasSelection()) deleteSelection();
        else if (caret_ > 0) {
          const std::size_t prev = utf8::prevBoundary(text_, caret_);
          text_.erase(prev, caret_ - prev);
          caret_ = selAnchor_ = prev;
        }
        desiredX_ = -1.0f;
        emitChanged();
        ensureCaretVisible();
      }
      e.accept();
      return;

    case Key::Delete:
      if (!readOnly_) {
        if (hasSelection()) deleteSelection();
        else if (caret_ < text_.size()) {
          const std::size_t next = utf8::nextBoundary(text_, caret_);
          text_.erase(caret_, next - caret_);
        }
        desiredX_ = -1.0f;
        emitChanged();
        ensureCaretVisible();
      }
      e.accept();
      return;

    case Key::Enter:
      insertText("\n");
      e.accept();
      return;

    default:
      break;
  }

  if (e.character != 0 && !e.ctrl && !e.alt) {
    std::string s;
    utf8::append(s, char32_t(e.character));
    insertText(s);
    e.accept();
  }
}

void TextArea::onFocusChanged(bool focused) {
  if (focused) {
    caretOn_ = true;
    blinkCounter_ = 0;
    notifyImeCaret();
  } else {
    dragging_ = false;
    selAnchor_ = caret_;
    editingFinished.emit(text_);
  }
  update();
}

void TextArea::onAnimationTick() {
  if (!hasFocus() || readOnly_ || !isEffectivelyEnabled()) return;
  if (++blinkCounter_ < kBlinkTicks) return;
  blinkCounter_ = 0;
  caretOn_ = !caretOn_;
  const Rect c = contentRect();
  const Point cp = caretPoint();
  update({c.x() + cp.x - 2.0f, c.y() + cp.y - scrollY_, 5.0f, lineHeight()});
}

}  // namespace geeyoou
