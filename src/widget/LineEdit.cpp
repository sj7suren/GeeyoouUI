#include "geeyoou/widget/LineEdit.hpp"

#include <algorithm>

#include "geeyoou/core/Utf8.hpp"
#include "geeyoou/platform/Platform.hpp"
#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/Window.hpp"

namespace geeyoou {
namespace {
constexpr float kPadX = 10.0f;
constexpr float kButtonWidth = 26.0f;
constexpr float kIconWidth = 26.0f;
const char* const kBullet = "\xE2\x80\xA2";  // U+2022, 3 bytes in UTF-8
constexpr std::size_t kBulletLen = 3;
constexpr int kBlinkTicks = 16;  // ~530 ms at 30 fps

// How much EDITABLE strip the field asks for, and the least it will accept.
// Not derived from the current text: a field is sized for what will be typed
// into it, and an empty one that asked for zero would be laid out as a slit and
// then never grow, because sizeHint() must not read back the width a previous
// pass gave it (ADR-R2-09).  180 fits a filename or an IP address; 56 still
// shows five or six characters at a time.
constexpr float kStripPreferred = 180.0f;
constexpr float kStripMin = 56.0f;
// Padding above and below the text.  The comfortable one gives the 32px field
// every showcase form uses at the default 13px body font.
constexpr float kPadYComfortable = 8.0f;
constexpr float kPadYTight = 3.0f;
}  // namespace

// ---------------------------------------------------------------- setters ---
void LineEdit::setText(std::string utf8) {
  if (text_ == utf8) return;
  text_ = std::move(utf8);
  caret_ = text_.size();
  selAnchor_ = caret_;
  scroll_ = 0.0f;
  update();
  invalidateSizeHint();
  textChanged.emit(text_);
}

void LineEdit::setPlaceholder(std::string utf8) {
  placeholder_ = std::move(utf8);
  update();
}

void LineEdit::setEchoMode(EchoMode m) {
  echo_ = m;
  revealed_ = false;
  update();
  invalidateSizeHint();
}

void LineEdit::setReadOnly(bool on) {
  readOnly_ = on;
  update();
}

void LineEdit::setMaxLength(std::size_t codepoints) { maxLength_ = codepoints; }

void LineEdit::setLeadingIcon(Icon icon) {
  leadingIcon_ = icon;
  update();
  invalidateSizeHint();
}

void LineEdit::setClearButtonEnabled(bool on) {
  clearButton_ = on;
  update();
  invalidateSizeHint();
}

void LineEdit::setRevealButtonEnabled(bool on) {
  revealButton_ = on;
  update();
  invalidateSizeHint();
}

void LineEdit::setInvalid(bool on) {
  if (invalid_ == on) return;
  invalid_ = on;
  update();
}

// ------------------------------------------------------ display / metrics ---
std::string LineEdit::displayString() const {
  if (echo_ == EchoMode::Normal || revealed_) return text_;
  std::string out;
  for (std::size_t i = 0; i < text_.size(); i = utf8::nextBoundary(text_, i)) {
    out += kBullet;
  }
  return out;
}

std::size_t LineEdit::displayIndex(std::size_t textIndex) const {
  if (echo_ == EchoMode::Normal || revealed_) return textIndex;
  // One bullet per codepoint, so the mapping is "count codepoints, times the
  // bullet's byte length" -- NOT a byte-for-byte copy of the caret offset.
  std::size_t n = 0;
  for (std::size_t i = 0; i < textIndex && i < text_.size();
       i = utf8::nextBoundary(text_, i)) {
    ++n;
  }
  return n * kBulletLen;
}

float LineEdit::xForIndex(std::size_t textIndex) const {
  const Theme& t = Theme::current();
  const std::string disp = displayString();
  const std::size_t di = std::min(displayIndex(textIndex), disp.size());
  return measureText(std::string_view(disp).substr(0, di), t.fontBody).width;
}

std::size_t LineEdit::indexAtX(float x) const {
  // Walks codepoint boundaries and picks the one whose midpoint the click
  // passed, which is what makes clicking the right half of a glyph put the
  // caret AFTER it.  O(n) measurements; a single-line field is short enough
  // that a smarter cached layout is not worth the invalidation bugs.
  if (text_.empty()) return 0;
  std::size_t best = 0;
  float bestX = 0.0f;
  std::size_t i = 0;
  while (true) {
    const float cx = xForIndex(i);
    if (i == 0 || x >= (bestX + cx) * 0.5f) {
      best = i;
      bestX = cx;
    }
    if (i >= text_.size()) break;
    const std::size_t next = utf8::nextBoundary(text_, i);
    const float nx = xForIndex(next);
    if (x < (cx + nx) * 0.5f) return i;
    i = next;
    bestX = nx;
    best = i;
  }
  return best;
}

// ----------------------------------------------------------------- layout ---
float LineEdit::trailingWidth() const {
  float w = 0.0f;
  if (revealButton_ && echo_ == EchoMode::Password) w += kButtonWidth;
  if (clearButton_ && !text_.empty()) w += kButtonWidth;
  return w;
}

Rect LineEdit::contentRect() const {
  const Rect r = localRect();
  const float left = (leadingIcon_ != Icon::None) ? kIconWidth + 4.0f : kPadX;
  const float right = trailingWidth() + kPadX;
  const float w = r.width() - left - right;
  if (w <= 0.0f) return {};
  return {left, r.y(), w, r.height()};
}

Rect LineEdit::buttonRect(Button b) const {
  const Rect r = localRect();
  float x = r.right() - kPadX * 0.5f;
  // Laid out right-to-left in the same order they are counted in
  // trailingWidth(), so the two stay in sync.
  if (clearButton_ && !text_.empty()) {
    x -= kButtonWidth;
    if (b == Button::Clear) return {x, r.y(), kButtonWidth, r.height()};
  }
  if (revealButton_ && echo_ == EchoMode::Password) {
    x -= kButtonWidth;
    if (b == Button::Reveal) return {x, r.y(), kButtonWidth, r.height()};
  }
  return {};
}

// The chrome is measured, the strip is asked for.
//
// left/right come from the SAME expressions contentRect() uses, so a field that
// gains a leading icon or a clear button widens by exactly the room that icon
// or button is about to take -- rather than by a second, hand-copied guess at
// it that drifts the first time one of those constants changes.
SizeHint LineEdit::sizeHint() const {
  const float left = (leadingIcon_ != Icon::None) ? kIconWidth + 4.0f : kPadX;
  const float right = trailingWidth() + kPadX;
  const float chrome = left + right;
  const float line = fontLineHeight(Theme::current().fontBody);

  SizeHint h;
  h.preferred = Size{chrome + kStripPreferred, line + 2.0f * kPadYComfortable};
  h.min = Size{chrome + kStripMin, line + 2.0f * kPadYTight};
  return h;
}

LineEdit::Button LineEdit::buttonAt(Point p) const {
  if (buttonRect(Button::Clear).contains(p)) return Button::Clear;
  if (buttonRect(Button::Reveal).contains(p)) return Button::Reveal;
  return Button::None;
}

// ------------------------------------------------------------------- edit ---
void LineEdit::emitChanged() {
  update();
  // Typing the first character makes the clear button appear, which widens the
  // chrome by a whole button -- so the hint really does change on a keystroke,
  // and a field inside a layout has to be told.  Free when nothing in the
  // process owns a Layout: markLayoutDirty tests g_layoutHosts first.
  invalidateSizeHint();
  textChanged.emit(text_);
}

void LineEdit::deleteSelection() {
  if (!hasSelection()) return;
  const std::size_t a = std::min(caret_, selAnchor_);
  const std::size_t b = std::max(caret_, selAnchor_);
  text_.erase(a, b - a);
  caret_ = a;
  selAnchor_ = a;
}

void LineEdit::insertText(const std::string& utf8) {
  if (readOnly_ || utf8.empty()) return;
  deleteSelection();
  if (maxLength_ > 0) {
    const std::size_t have = utf8::codepointCount(text_);
    if (have >= maxLength_) return;
    // Truncate the incoming run rather than rejecting it wholesale, so pasting
    // an over-long string fills the field instead of doing nothing.
    const std::size_t room = maxLength_ - have;
    std::size_t taken = 0, i = 0;
    while (i < utf8.size() && taken < room) {
      i = utf8::nextBoundary(utf8, i);
      ++taken;
    }
    text_.insert(caret_, utf8, 0, i);
    caret_ += i;
  } else {
    text_.insert(caret_, utf8);
    caret_ += utf8.size();
  }
  selAnchor_ = caret_;
  ensureCaretVisible();
  emitChanged();
}

void LineEdit::moveCaret(std::size_t to, bool extend) {
  caret_ = utf8::clampToBoundary(text_, std::min(to, text_.size()));
  if (!extend) selAnchor_ = caret_;
  caretOn_ = true;
  blinkCounter_ = 0;
  ensureCaretVisible();
  update();
}

void LineEdit::ensureCaretVisible() {
  const Rect c = contentRect();
  if (c.isEmpty()) return;
  const float cx = xForIndex(caret_);
  if (cx - scroll_ < 0.0f) scroll_ = cx;
  else if (cx - scroll_ > c.width()) scroll_ = cx - c.width();

  // Never leave blank space on the right while text is scrolled off the left.
  const float total = xForIndex(text_.size());
  scroll_ = std::clamp(scroll_, 0.0f, std::max(0.0f, total - c.width()));
  notifyImeCaret();
}

void LineEdit::notifyImeCaret() {
  if (!hasFocus()) return;
  Window* win = window();
  if (!win) return;
  const Rect c = contentRect();
  const Point origin = mapToWindow({c.x() + xForIndex(caret_) - scroll_, c.y()});
  win->setImeCaret({origin, Size(1.0f, c.height())});
}

void LineEdit::selectAll() {
  selAnchor_ = 0;
  caret_ = text_.size();
  update();
}

std::string LineEdit::selectedText() const {
  if (!hasSelection()) return {};
  const std::size_t a = std::min(caret_, selAnchor_);
  const std::size_t b = std::max(caret_, selAnchor_);
  return text_.substr(a, b - a);
}

// ------------------------------------------------------------------ paint ---
StyleState LineEdit::styleState() const {
  StyleState s = Widget::styleState();
  if (hovered_) s |= StyleState::Hover;
  if (readOnly_) s |= StyleState::ReadOnly;
  if (invalid_) s |= StyleState::Invalid;
  return s;
}

void LineEdit::onPaint(Painter& p, const Rect&) {
  const Theme& t = Theme::current();
  const Rect r = localRect();
  const bool en = isEffectivelyEnabled();
  const bool focused = hasFocus() && en;

  const StyleProps& sp = style(styleState());
  const Color baseBorder = sp.borderColorOr(t.panelBorder);
  const float radius = sp.radiusOr(t.radius);

  Color border = baseBorder;
  if (invalid_) border = t.danger;
  else if (focused) border = t.focusRing;
  else if (hovered_ && en && !readOnly_) border = baseBorder.lerp(t.accent, 0.5f);

  // A read-only field must not look like an editable one.  "Can I change this?"
  // has to be answerable without clicking, so read-only drops the sunken field
  // background and takes a dashed-looking dim border instead.
  Color bg = sp.backgroundOr(t.field);
  if (!en) bg = bg.lerp(t.background, 0.5f);
  else if (readOnly_) bg = sp.backgroundOr(t.panel);

  p.fillRoundRect(r, radius, bg);
  p.strokeRoundRect(r.deflated(0.5f), radius,
                    readOnly_ && !focused ? baseBorder.lerp(t.background, 0.4f)
                                          : border,
                    sp.borderWidthOr(focused || invalid_ ? 1.5f : 1.0f));

  if (leadingIcon_ != Icon::None) {
    drawIcon(p, leadingIcon_, {kPadX * 0.4f, r.y(), kIconWidth, r.height()},
             en ? t.textDim : t.textDisabled);
  }

  const Rect c = contentRect();
  if (c.isEmpty()) return;

  const std::string disp = displayString();
  const float baselineY = c.center().y;

  p.save();
  p.clip(c);

  if (disp.empty() && !placeholder_.empty()) {
    p.drawText({c.x(), baselineY}, placeholder_, t.fontBody, t.placeholder,
               HAlign::Left, VAlign::Middle);
  } else {
    // Selection highlight goes behind the glyphs.
    if (hasSelection() && focused) {
      const float x1 = xForIndex(std::min(caret_, selAnchor_)) - scroll_;
      const float x2 = xForIndex(std::max(caret_, selAnchor_)) - scroll_;
      p.fillRect({c.x() + x1, c.y() + 4.0f, x2 - x1, c.height() - 8.0f},
                 t.selection);
    }
    p.drawText({c.x() - scroll_, baselineY}, disp, t.fontBody,
               !en ? t.textDisabled : (readOnly_ ? t.textDim : t.text),
               HAlign::Left, VAlign::Middle);
  }

  if (focused && caretOn_ && !readOnly_) {
    const float cx = c.x() + xForIndex(caret_) - scroll_;
    p.strokeLine({cx, c.center().y - 8.0f}, {cx, c.center().y + 8.0f}, t.text, 1.0f);
  }
  p.restore();

  // --- trailing buttons ---
  const auto paintButton = [&](Button b, Icon icon) {
    const Rect br = buttonRect(b);
    if (br.isEmpty()) return;
    const bool hot = en && hovered_ && hoverButton_ == b;
    if (hot) p.fillRoundRect(br.deflated(3.0f), 4.0f, t.panelBorder.withAlpha(110));
    drawIcon(p, icon, br.deflated(6.0f),
             en ? (hot ? t.text : t.textDim) : t.textDisabled);
  };
  paintButton(Button::Reveal, revealed_ ? Icon::EyeOff : Icon::Eye);
  paintButton(Button::Clear, Icon::Close);
}

// ------------------------------------------------------------------ input ---
void LineEdit::onMouse(const MouseEvent& e) {
  if (!isEffectivelyEnabled()) return;
  const Rect c = contentRect();

  switch (e.action) {
    case MouseAction::Enter:
      hovered_ = true;
      hoverButton_ = buttonAt(e.pos);
      update();
      e.accept();
      break;

    case MouseAction::Leave:
      hovered_ = false;
      hoverButton_ = Button::None;
      dragging_ = false;
      update();
      e.accept();
      break;

    case MouseAction::Move: {
      const Button b = buttonAt(e.pos);
      if (b != hoverButton_) {
        hoverButton_ = b;
        update();
      }
      if (dragging_) moveCaret(indexAtX(e.pos.x - c.x() + scroll_), true);
      e.accept();
      break;
    }

    case MouseAction::Press:
      if (e.button == MouseButton::Left) {
        const Button b = buttonAt(e.pos);
        if (b == Button::Clear) {
          clear();
        } else if (b == Button::Reveal) {
          revealed_ = !revealed_;
          update();
        } else if (!c.isEmpty()) {
          dragging_ = true;
          moveCaret(indexAtX(e.pos.x - c.x() + scroll_), e.shift);
        }
        e.accept();
      }
      break;

    case MouseAction::Release:
      dragging_ = false;
      e.accept();
      break;

    default:
      break;
  }
}

void LineEdit::onKey(const KeyEvent& e) {
  if (!e.pressed || !isEffectivelyEnabled()) return;

  // --- shortcuts ---
  if (e.ctrl) {
    switch (e.key) {
      case Key::KeyA: selectAll(); e.accept(); return;
      case Key::KeyC:
        if (hasSelection() && echo_ == EchoMode::Normal) {
          platform().setClipboardText(selectedText());
        }
        e.accept();
        return;
      case Key::KeyX:
        // Copying out of a password field would defeat the masking, so cut and
        // copy are refused there; deleting the selection is still allowed.
        if (hasSelection() && !readOnly_) {
          if (echo_ == EchoMode::Normal) platform().setClipboardText(selectedText());
          deleteSelection();
          ensureCaretVisible();
          emitChanged();
        }
        e.accept();
        return;
      case Key::KeyV: {
        std::string s = platform().clipboardText();
        // A pasted newline would silently corrupt a single-line value.
        s.erase(std::remove(s.begin(), s.end(), '\n'), s.end());
        s.erase(std::remove(s.begin(), s.end(), '\r'), s.end());
        insertText(s);
        e.accept();
        return;
      }
      default: break;
    }
  }

  switch (e.key) {
    case Key::Left:
      moveCaret(hasSelection() && !e.shift ? std::min(caret_, selAnchor_)
                                           : utf8::prevBoundary(text_, caret_),
                e.shift);
      e.accept();
      return;
    case Key::Right:
      moveCaret(hasSelection() && !e.shift ? std::max(caret_, selAnchor_)
                                           : utf8::nextBoundary(text_, caret_),
                e.shift);
      e.accept();
      return;
    case Key::Home: moveCaret(0, e.shift); e.accept(); return;
    case Key::End:  moveCaret(text_.size(), e.shift); e.accept(); return;

    case Key::Backspace:
      if (!readOnly_) {
        if (hasSelection()) deleteSelection();
        else if (caret_ > 0) {
          const std::size_t prev = utf8::prevBoundary(text_, caret_);
          text_.erase(prev, caret_ - prev);  // whole codepoint, never one byte
          caret_ = prev;
          selAnchor_ = prev;
        }
        ensureCaretVisible();
        emitChanged();
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
        ensureCaretVisible();
        emitChanged();
      }
      e.accept();
      return;

    case Key::Enter:
      returnPressed.emit();
      editingFinished.emit(text_);
      textOnFocus_ = text_;
      e.accept();
      return;

    case Key::Escape:
      if (text_ != textOnFocus_) {
        text_ = textOnFocus_;
        caret_ = selAnchor_ = text_.size();
        ensureCaretVisible();
        emitChanged();
      }
      e.accept();
      return;

    default:
      break;
  }

  // --- typed characters (including everything an IME commits) ---
  if (e.character != 0 && !e.ctrl && !e.alt) {
    std::string s;
    utf8::append(s, char32_t(e.character));
    insertText(s);
    e.accept();
  }
}

void LineEdit::onFocusChanged(bool focused) {
  if (focused) {
    textOnFocus_ = text_;
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

void LineEdit::onAnimationTick() {
  if (!hasFocus() || readOnly_ || !isEffectivelyEnabled()) return;
  if (++blinkCounter_ < kBlinkTicks) return;
  blinkCounter_ = 0;
  caretOn_ = !caretOn_;
  // Repaint ONLY the caret column, not the field: on a screen with several
  // fields this is the difference between a 2 Hz full repaint and nothing.
  const Rect c = contentRect();
  const float cx = c.x() + xForIndex(caret_) - scroll_;
  update({cx - 2.0f, c.y(), 5.0f, c.height()});
}

}  // namespace geeyoou
