#include "geeyoou/widget/Window.hpp"

#include <algorithm>
#include <vector>

#include "geeyoou/platform/Platform.hpp"
#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Skin.hpp"

namespace geeyoou {

Window::Window(const std::string& title, int logicalWidth, int logicalHeight,
               const WindowOptions& options) {
  platformWindow_ =
      platform().createWindow(title, logicalWidth, logicalHeight, options);
  setGeometry({0.0f, 0.0f, float(logicalWidth), float(logicalHeight)});

  platformWindow_->onPaint = [this](const Surface& s, const Rect& dirtyPhysical) {
    handleSurface(s, dirtyPhysical);
  };
  platformWindow_->onMouse = [this](const MouseEvent& e) { handleMouse(e); };
  platformWindow_->onKey = [this](const KeyEvent& e) { handleKey(e); };
  platformWindow_->onResize = [this](const ResizeEvent& e) { handleResize(e); };
  platformWindow_->onClose = [this] { closed.emit(); };
  platformWindow_->onHitTest = [this](Point p) { return hitZoneAt(p); };
  platformWindow_->onWindowStateChanged = [this] {
    maximizedChanged.emit(isMaximized());
  };

  // Every widget resolves its colours through Theme::current() as it paints, so
  // a full repaint is all a skin change needs -- no per-widget notification and
  // no cached-colour invalidation walk.
  //
  // skins() is a process-lifetime singleton, so this subscription MUST be owned:
  // conns_ drops it during member destruction, before anything it touches goes.
  conns_ += skins().changed.connect([this] { update(); });
}

Window::~Window() {
  // First, before any of the state the tick walks is dismantled: the timer
  // lives in the platform and its callback captures `this`, so a window that
  // dies with its animation clock still running is a use-after-free on the very
  // next tick.
  platform().stopTimer(animationTimer_);
  animationTimer_ = 0;
  animationsOn_ = false;

  // Drop the observer pointers before the child vector is destroyed, so nothing
  // can dereference a half-destroyed widget during teardown.
  focus_ = nullptr;
  hovered_ = nullptr;
  pressGrab_ = nullptr;
  popup_ = nullptr;
}

void Window::show() { platformWindow_->show(); }

void Window::setTitle(const std::string& utf8) { platformWindow_->setTitle(utf8); }

float Window::scaleFactor() const { return platformWindow_->scaleFactor(); }

// ---------------------------------------------------------------- commands ---
void Window::close() { platformWindow_->close(); }
void Window::minimize() { platformWindow_->minimize(); }
void Window::maximize() { platformWindow_->maximize(); }
void Window::restore() { platformWindow_->restore(); }
bool Window::isMaximized() const { return platformWindow_->isMaximized(); }
bool Window::isFrameless() const { return platformWindow_->isFrameless(); }

void Window::toggleMaximize() {
  if (isMaximized()) restore(); else maximize();
}

void Window::setMinimumSize(Size logical) {
  platformWindow_->setMinimumSize(logical);
}

void Window::addDirtyRect(const Rect& windowRect) {
  if (windowRect.isEmpty()) return;
  platformWindow_->invalidate(windowRect);
}

void Window::enableAnimations(int fps) {
  if (animationsOn_) return;
  animationsOn_ = true;
  const int interval = (fps > 0) ? (1000 / fps) : 33;
  // The id is kept, not discarded: it is the only thing the destructor can use
  // to stop a clock that outlives this object otherwise.
  animationTimer_ = platform().startTimer(interval, [this] { animationTickTree(); });
}

void Window::setImeCaret(const Rect& windowRect) {
  platformWindow_->setImeCaret(windowRect);
}

// ------------------------------------------------------------------ popup ---
void Window::openPopup(Widget* popup, const Rect& anchor) {
  if (!popup) return;
  if (popup_ && popup_ != popup) closePopup();

  const Rect win = localRect();
  const Size want = popup->geometry().size();

  // Below the anchor by default; above it when there is no room, which is what
  // keeps a dropdown near the bottom of the screen usable.
  float y = anchor.bottom() + 2.0f;
  if (y + want.height > win.bottom() && anchor.top() - want.height - 2.0f >= 0.0f) {
    y = anchor.top() - want.height - 2.0f;
  }
  y = std::clamp(y, 0.0f, std::max(0.0f, win.height() - want.height));

  float x = anchor.x();
  if (x + want.width > win.right()) x = win.right() - want.width;
  x = std::max(0.0f, x);

  popup->setGeometry({x, y, want.width, want.height});
  popup->setVisible(true);
  popup_ = popup;
  update();
}

void Window::closePopup() {
  if (!popup_) return;
  Widget* p = popup_;
  popup_ = nullptr;
  p->setVisible(false);
  if (hovered_ == p) hovered_ = nullptr;
  if (pressGrab_ == p) pressGrab_ = nullptr;
  update();
  popupClosed.emit();
}

// ------------------------------------------------------------------ focus ---
void Window::setFocusWidget(Widget* w) {
  if (w == focus_) return;
  if (w && !w->isFocusable()) return;

  Widget* old = focus_;
  focus_ = w;
  // Notify AFTER focus_ has moved, so a handler that queries hasFocus() during
  // the callback sees the new, settled state rather than a transient one.
  if (old) old->onFocusChanged(false);
  if (focus_) focus_->onFocusChanged(true);
}

bool Window::focusNext(bool backwards) {
  std::vector<Widget*> ring;
  collectFocusable(ring);
  if (ring.empty()) return false;

  auto it = std::find(ring.begin(), ring.end(), focus_);
  std::size_t next;
  if (it == ring.end()) {
    next = backwards ? ring.size() - 1 : 0;
  } else {
    const std::size_t cur = std::size_t(it - ring.begin());
    next = backwards ? (cur + ring.size() - 1) % ring.size()
                     : (cur + 1) % ring.size();
  }
  setFocusWidget(ring[next]);
  return true;
}

// ----------------------------------------------------------------- events ---
void Window::handleSurface(const Surface& s, const Rect& dirtyPhysical) {
  if (!canvas_.begin(s, dirtyPhysical)) return;
  Painter p = canvas_.painter();

  // Physical -> logical happens exactly here, on the way in.  Everything above
  // this line is in logical pixels and never learns the dpr.
  const float k = (s.dpr > 0.0f) ? (1.0f / s.dpr) : 1.0f;
  const Rect dirtyLogical(dirtyPhysical.x() * k, dirtyPhysical.y() * k,
                          dirtyPhysical.width() * k, dirtyPhysical.height() * k);
  handlePaint(p, dirtyLogical);
  canvas_.end();
}

void Window::handlePaint(Painter& p, const Rect& dirty) {
  const Rect whole = localRect();
  p.fillRect(dirty, background_);
  // The root is painted here rather than through paintTree, so its own onPaint
  // has to be invoked explicitly.  The painter needs no translation: the window
  // sits at the origin of its own coordinate space by definition.  This is what
  // lets a subclass draw a backdrop or a frameless window's outline.
  onPaint(p, dirty);
  for (const auto& child : children()) {
    if (child.get() == popup_) continue;  // drawn last, on top of everything
    child->paintTree(p, dirty, whole);
  }
  // The popup's clip starts from the WHOLE window, not from whatever container
  // spawned it -- that is precisely what lets it overhang a GroupBox.
  if (popup_) popup_->paintTree(p, dirty, whole);
}

void Window::handleMouse(const MouseEvent& e) {
  Widget* target = nullptr;

  // A Leave straight from the platform means the pointer has left the client
  // area entirely -- out of the window, or up into whatever the app reported as
  // its caption.  Nothing is under the cursor any more, so the hover state has
  // to be surrendered; the widget-to-widget Leave below never fires for this.
  if (e.action == MouseAction::Leave) {
    // A drag in progress is exempt: the grabbing widget is still tracking the
    // cursor outside its own bounds and must keep its pressed look.
    if (pressGrab_) return;
    if (hovered_) {
      Widget* was = hovered_;
      hovered_ = nullptr;
      was->dispatchMouse(e);
    }
    return;
  }

  if (pressGrab_ && e.action != MouseAction::Press) {
    target = pressGrab_;
  } else if (popup_ && popup_->isVisible()) {
    // The popup is on top, so it gets first refusal on every hit.
    target = popup_->hitTest(e.windowPos);
    if (!target) {
      if (e.action == MouseAction::Press) {
        closePopup();
        // The dismissing click is SWALLOWED rather than passed through.  On an
        // HMI, letting it also land on whatever was underneath could start a
        // pump the operator only meant to stop looking at a list.
        e.accept();
        return;
      }
      target = hitTest(e.windowPos);
    }
  } else {
    target = hitTest(e.windowPos);
  }

  // Enter/leave bookkeeping, so widgets can render a hover state without each
  // of them tracking the cursor themselves.
  if (!pressGrab_ && target != hovered_) {
    if (hovered_) {
      MouseEvent leave = e;
      leave.action = MouseAction::Leave;
      hovered_->dispatchMouse(leave);
    }
    hovered_ = target;
    if (hovered_) {
      MouseEvent enter = e;
      enter.action = MouseAction::Enter;
      hovered_->dispatchMouse(enter);
    }
  }

  if (e.action == MouseAction::Press) {
    pressGrab_ = target;
    // Clicking moves focus to the nearest focusable ancestor of what was hit,
    // and clicking empty background drops focus entirely.  Clicks INSIDE a
    // popup are exempt: focus must stay on the control that opened it, which
    // is what keeps the keyboard driving the list.
    const bool insidePopup = popup_ && target && [&] {
      for (Widget* w = target; w; w = w->parent()) if (w == popup_) return true;
      return false;
    }();
    if (!insidePopup) {
      Widget* f = target;
      while (f && !f->isFocusable()) f = f->parent();
      setFocusWidget(f);
    }
  }

  if (target) target->dispatchMouse(e);

  if (e.action == MouseAction::Release) pressGrab_ = nullptr;
}

void Window::handleKey(const KeyEvent& e) {
  // A Tab while a popup is open would move focus out from under the list, so
  // it closes the popup instead of traversing.
  if (e.pressed && e.key == Key::Tab && popup_) {
    closePopup();
    return;
  }
  // Tab traversal is handled by the window and never reaches widgets, so no
  // control has to remember to pass Tab along.
  if (e.pressed && e.key == Key::Tab) {
    focusNext(e.shift);
    return;
  }
  if (focus_) {
    focus_->dispatchKey(e);
    return;
  }
  onKey(e);
}

void Window::handleResize(const ResizeEvent& e) {
  setGeometry({0.0f, 0.0f, e.size.width, e.size.height});
  // An open popup was anchored to a control that has just moved, so its
  // position is stale; closing is less jarring than leaving it floating.
  if (popup_) closePopup();
  resized.emit(e.size);
  update();
}

}  // namespace geeyoou
