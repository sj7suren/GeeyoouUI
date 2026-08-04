#include "geeyoou/widget/SelectBase.hpp"

#include <algorithm>

#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/Window.hpp"

namespace geeyoou {
namespace {
constexpr float kPadX = 10.0f;
constexpr float kArrowW = 24.0f;
}  // namespace

SelectBase::~SelectBase() {
  // First: our slots live in a PopupList that the WINDOW owns, and closing the
  // popup below does not unsubscribe them.  Left connected, they would keep a
  // captured `this` in a list that is still alive and still firing.
  conns_.clear();

  // The popup is a child of the WINDOW, not of us, so it outlives this object
  // unless we take it down.  Leaving it visible would strand an orphan list on
  // screen that nothing can close.
  if (popup_ && popup_->isVisible()) {
    if (Window* w = popup_->window()) w->closePopup();
  }
}

void SelectBase::setPlaceholder(std::string utf8) {
  placeholder_ = std::move(utf8);
  update();
}

void SelectBase::setInvalid(bool on) {
  if (invalid_ == on) return;
  invalid_ = on;
  update();
}

void SelectBase::setMaxVisibleRows(int n) {
  maxVisibleRows_ = std::max(1, n);
  if (popup_) popup_->setMaxVisibleRows(maxVisibleRows_);
}

void SelectBase::setPopupWidth(float w) { popupWidth_ = std::max(0.0f, w); }

bool SelectBase::isOpen() const {
  // Asks the WINDOW, not our own PopupList, so subclasses whose popup is a
  // calendar or a column stack report their state correctly too.
  Window* w = const_cast<SelectBase*>(this)->window();
  return w && w->popup() != nullptr && w->popup()->isVisible() &&
         (w->popup() == popup_ || customPopup_ == w->popup());
}

void SelectBase::showCustomPopup(Widget* popup) {
  if (!popup) return;
  Window* w = window();
  if (!w) return;
  customPopup_ = popup;
  w->openPopup(popup, windowRect());
  update();
  openStateChanged.emit(true);
}

void SelectBase::hideCustomPopup(Widget* popup) {
  Window* w = window();
  if (!w || w->popup() != popup) return;
  w->closePopup();
  customPopup_ = nullptr;
  update();
  openStateChanged.emit(false);
}

void SelectBase::ensurePopup() {
  if (popup_) return;
  Window* w = window();
  if (!w) return;

  popup_ = w->add<PopupList>();
  popup_->setMaxVisibleRows(maxVisibleRows_);
  // Owned by conns_, not fire-and-forget: the popup belongs to the Window and
  // survives us, so these three have to be released when WE go.
  conns_ += popup_->rowActivated.connect([this](int row) {
    onRowActivated(row);
    if (closeOnActivate()) close();
  });
  conns_ += popup_->rowToggled.connect([this](int row) { onRowToggled(row); });
  conns_ += popup_->expanderToggled.connect([this](int row) { onExpanderToggled(row); });
}

void SelectBase::refreshRows() {
  if (!popup_) return;
  popup_->setRows(buildRows());
  // Re-size to the new content: a filtered list that keeps the height of the
  // unfiltered one leaves a slab of empty panel under two results.
  const float w = popupWidth_ > 0.0f ? popupWidth_ : geometry().width();
  const Rect g = popup_->geometry();
  popup_->setGeometry({g.x(), g.y(), w, popup_->preferredHeight()});
}

void SelectBase::open() {
  if (!isEffectivelyEnabled() || isOpen()) return;
  ensurePopup();
  if (!popup_) return;

  const float w = popupWidth_ > 0.0f ? popupWidth_ : geometry().width();
  popup_->setRows(buildRows());
  popup_->setGeometry({0.0f, 0.0f, w, popup_->preferredHeight()});
  window()->openPopup(popup_, windowRect());
  popup_->highlightFirstSelectable();
  onOpened();
  update();
  openStateChanged.emit(true);
}

void SelectBase::close() {
  if (!isOpen()) return;
  if (Window* w = window()) w->closePopup();
  onClosed();
  update();
  openStateChanged.emit(false);
}

Rect SelectBase::fieldTextRect() const {
  const Rect r = localRect();
  return {kPadX, r.y(), std::max(0.0f, r.width() - kPadX - kArrowW), r.height()};
}

// ------------------------------------------------------------------ paint ---
void SelectBase::onPaint(Painter& p, const Rect&) {
  const Theme& t = Theme::current();
  const Rect r = localRect();
  const bool en = isEffectivelyEnabled();
  const bool focused = hasFocus() && en;
  const bool opened = isOpen();

  Color border = t.panelBorder;
  if (invalid_) border = t.danger;
  else if (focused || opened) border = t.focusRing;
  else if (hovered_ && en) border = t.panelBorder.lerp(t.accent, 0.5f);

  p.fillRoundRect(r, t.radius, en ? t.field : t.field.lerp(t.background, 0.5f));
  p.strokeRoundRect(r.deflated(0.5f), t.radius, border,
                    (focused || opened || invalid_) ? 1.5f : 1.0f);

  const Rect tr = fieldTextRect();
  const std::string txt = displayText();
  const bool showPlaceholder = txt.empty() && !hasValue();

  p.save();
  p.clip(tr);
  if (showPlaceholder) {
    if (!placeholder_.empty()) {
      p.drawText({tr.x(), tr.center().y}, placeholder_, t.fontBody, t.placeholder,
                 HAlign::Left, VAlign::Middle);
    }
  } else {
    const Color fg = !en ? t.textDisabled
                         : (opened && showCaret() ? t.accent : t.text);
    p.drawText({tr.x(), tr.center().y}, txt, t.fontBody, fg, HAlign::Left,
               VAlign::Middle);
    if (opened && showCaret()) {
      const float cx = tr.x() + measureText(txt, t.fontBody).width + 1.5f;
      p.strokeLine({cx, tr.center().y - 8.0f}, {cx, tr.center().y + 8.0f}, t.text,
                   1.0f);
    }
  }
  p.restore();

  drawIcon(p, opened ? Icon::ChevronUp : Icon::ChevronDown,
           {r.right() - kArrowW, r.y(), kArrowW - 4.0f, r.height()},
           en ? t.textDim : t.textDisabled, 0.9f);
}

// ------------------------------------------------------------------ input ---
void SelectBase::onMouse(const MouseEvent& e) {
  if (!isEffectivelyEnabled()) return;
  switch (e.action) {
    case MouseAction::Enter: hovered_ = true; update(); e.accept(); break;
    case MouseAction::Leave: hovered_ = false; update(); e.accept(); break;
    case MouseAction::Press:
      if (e.button == MouseButton::Left) {
        if (isOpen()) close(); else open();
        e.accept();
      }
      break;
    case MouseAction::Release:
      e.accept();
      break;
    default: break;
  }
}

void SelectBase::onKey(const KeyEvent& e) {
  if (!e.pressed || !isEffectivelyEnabled()) return;

  if (!isOpen()) {
    if (e.key == Key::Enter || e.key == Key::Space || e.key == Key::Down) {
      open();
      e.accept();
    }
    return;
  }

  // Alt+1..9 picks the badged row.  Checked before the subclass hook so a
  // search field cannot swallow it as a typed character.
  int digit = 0;
  if (e.alt && keyToDigit(e.key, digit) && digit >= 1) {
    const auto& rows = popup_->rows();
    for (std::size_t i = 0; i < rows.size(); ++i) {
      if (rows[i].shortcut == digit) {
        popup_->setHighlighted(int(i), false);
        onRowActivated(int(i));
        if (closeOnActivate()) close();
        break;
      }
    }
    e.accept();
    return;
  }

  switch (e.key) {
    case Key::Up:   popup_->moveHighlight(-1); e.accept(); return;
    case Key::Down: popup_->moveHighlight(+1); e.accept(); return;
    case Key::PageUp:   popup_->moveHighlight(-maxVisibleRows_); e.accept(); return;
    case Key::PageDown: popup_->moveHighlight(+maxVisibleRows_); e.accept(); return;
    case Key::Home:
      popup_->setHighlighted(-1, false);
      popup_->moveHighlight(+1);
      e.accept();
      return;
    case Key::End:
      popup_->setHighlighted(int(popup_->rows().size()), false);
      popup_->moveHighlight(-1);
      e.accept();
      return;
    case Key::Enter: {
      const int hi = popup_->highlighted();
      if (hi >= 0) {
        onRowActivated(hi);
        if (closeOnActivate()) close();
      } else {
        close();
      }
      e.accept();
      return;
    }
    case Key::Escape:
      close();
      e.accept();
      return;
    default:
      break;
  }

  if (handleKeyWhileOpen(e)) e.accept();
}

void SelectBase::onFocusChanged(bool focused) {
  if (!focused) close();
  update();
}

void SelectBase::onEnabledChanged() {
  if (!isEffectivelyEnabled()) close();
  update();
}

}  // namespace geeyoou
