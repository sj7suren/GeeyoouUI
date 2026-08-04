#include "geeyoou/widget/Widget.hpp"

#include <algorithm>
#include <cctype>

#include "geeyoou/render/Painter.hpp"
#include "geeyoou/widget/Window.hpp"

namespace geeyoou {
namespace {

// --- in-flight event bubbles -------------------------------------------------
//
// dispatchMouse/dispatchKey walk from the target widget up to the root, calling
// a handler at every step.  A handler runs application code, and application
// code may now destroy widgets -- including the one the walk is standing on.
// Reading `w->parent_` after such a handler returns is then a use-after-free:
// the systemic risk recorded in docs/iterations/01-lifecycle-and-tests.md.
//
// Pre-reading the parent BEFORE the call only covers the narrow case where the
// handler removes exactly the current node.  Cancelling the walk covers all of
// them, and is sound because what leaves the tree is always a whole SUBTREE: if
// any ancestor of the current node is going, the current node is going with it.
// So the remaining path is unsafe if and only if the CURRENT node is doomed --
// one pointer compare per departing node, and no ancestry search.
//
// The cursors live in the dispatching stack frames and are threaded onto one
// list, so nested dispatch simply nests and nothing allocates.  A plain static
// rather than a thread_local: input dispatch is UI-thread-only by construction
// (docs/architecture.md section 3.4), and a TLS lookup on every mouse move
// would be paid to describe a case that cannot happen.
struct BubbleCursor {
  Widget* node = nullptr;
  BubbleCursor* outer = nullptr;
};

BubbleCursor* g_bubbles = nullptr;

class BubbleGuard {
 public:
  explicit BubbleGuard(Widget* start) {
    cursor_.node = start;
    cursor_.outer = g_bubbles;
    g_bubbles = &cursor_;
  }
  ~BubbleGuard() { g_bubbles = cursor_.outer; }

  BubbleGuard(const BubbleGuard&) = delete;
  BubbleGuard& operator=(const BubbleGuard&) = delete;

  Widget* node() const { return cursor_.node; }
  void moveTo(Widget* w) { cursor_.node = w; }

 private:
  BubbleCursor cursor_;
};

void cancelBubblesOn(const Widget* doomed) {
  for (BubbleCursor* c = g_bubbles; c; c = c->outer) {
    if (c->node == doomed) c->node = nullptr;
  }
}

// --- detach bookkeeping ------------------------------------------------------
constexpr std::size_t kNotAChild = std::size_t(-1);

std::size_t indexOfChild(const std::vector<std::unique_ptr<Widget>>& v,
                         const Widget* child) {
  for (std::size_t i = 0; i < v.size(); ++i) {
    if (v[i].get() == child) return i;
  }
  return kNotAChild;
}

// Pre-order, and BEFORE anything is unlinked: `win` is still reachable through
// the parent chain and every observer pointer still names a live widget when it
// is dropped.
void announceDetached(Widget* node, Window* win) {
  cancelBubblesOn(node);
  if (win) win->widgetDetached(node);
  // By index, re-reading size() each time: widgetDetached emits popupClosed for
  // a departing popup, and a slot on that signal may add or remove children of
  // its own.  A range-for would be holding an iterator across that call.
  const std::vector<std::unique_ptr<Widget>>& kids = node->children();
  for (std::size_t i = 0; i < kids.size(); ++i) announceDetached(kids[i].get(), win);
}

}  // namespace

std::unique_ptr<Widget> Widget::takeChild(Widget* child) {
  if (!child) return nullptr;
  if (indexOfChild(children_, child) == kNotAChild) return nullptr;

  // Repaint what the subtree is vacating, while it is still attached: update()
  // maps through the parent chain to find the Window it must report damage to.
  if (child->visible_) child->update();

  announceDetached(child, window());

  // Re-found rather than reused: announceDetached can run popupClosed slots,
  // and one of those may already have removed `child` -- or a sibling ahead of
  // it, which moves the index either way.
  const std::size_t index = indexOfChild(children_, child);
  if (index == kNotAChild) return nullptr;

  std::unique_ptr<Widget> owned = std::move(children_[index]);
  children_.erase(children_.begin() + std::ptrdiff_t(index));
  owned->parent_ = nullptr;
  return owned;
}

void Widget::removeChild(Widget* child) {
  std::unique_ptr<Widget> doomed = takeChild(child);
  doomed.reset();  // the destruction IS the point; spelled out, not implied
}

void Widget::clearChildren() {
  // Back to front: reverse order of construction, and no element ever has to be
  // shifted down the vector.
  while (!children_.empty()) {
    const std::size_t before = children_.size();
    removeChild(children_.back().get());
    // A detach handler may remove children itself, which is fine; what it must
    // not be able to do is grow the list back faster than we drain it.  Bailing
    // out beats spinning forever.
    if (children_.size() >= before) break;
  }
}

void Widget::setGeometry(const Rect& r) {
  if (visible_) update();  // repaint what we are vacating
  geometry_ = r;
  onGeometryChanged();
  if (visible_) update();  // ...and what we now occupy
}

void Widget::setContentOffset(Point offset) {
  if (contentOffset_.x == offset.x && contentOffset_.y == offset.y) return;
  contentOffset_ = offset;
  update();
}

Point Widget::mapToWindow(Point local) const {
  const Widget* w = this;
  while (w) {
    local.x += w->geometry_.x();
    local.y += w->geometry_.y();
    // A widget sits at (its geometry - its PARENT's content offset).  Folding
    // the scroll in here means every derived coordinate -- windowRect, hit
    // testing, dirty rects, the IME caret -- scrolls for free.
    if (w->parent_) {
      local.x -= w->parent_->contentOffset_.x;
      local.y -= w->parent_->contentOffset_.y;
    }
    w = w->parent_;
  }
  return local;
}

Rect Widget::windowRect() const {
  const Point origin = mapToWindow({0.0f, 0.0f});
  return {origin, geometry_.size()};
}

void Widget::setVisible(bool on) {
  if (visible_ == on) return;
  visible_ = on;
  if (!on && hasFocus()) clearFocus();
  update();
}

void Widget::setEnabled(bool on) {
  if (enabled_ == on) return;
  enabled_ = on;
  // Focus must not linger on something the operator can no longer interact
  // with -- otherwise keystrokes vanish into a greyed-out control.
  if (!on && hasFocus()) clearFocus();
  onEnabledChanged();
}

bool Widget::isEffectivelyEnabled() const {
  for (const Widget* w = this; w; w = w->parent_) {
    if (!w->enabled_) return false;
  }
  return true;
}

bool Widget::isFocusable() const {
  return focusPolicy_ != FocusPolicy::None && visible_ && isEffectivelyEnabled();
}

void Widget::setFocus() {
  if (Window* win = window()) win->setFocusWidget(this);
}

void Widget::clearFocus() {
  Window* win = window();
  if (win && win->focusWidget() == this) win->setFocusWidget(nullptr);
}

bool Widget::hasFocus() const {
  // window() is non-const because it walks a virtual; the const_cast is
  // confined to this one read-only query.
  Window* win = const_cast<Widget*>(this)->window();
  return win && win->focusWidget() == this;
}

// ------------------------------------------------------------------ style ---
void Widget::setObjectName(std::string name) {
  if (objectName_ == name) return;
  objectName_ = std::move(name);
  // A GLOBAL bump rather than invalidating just this widget: descendant rules
  // mean our identity can decide a child's style too, and chasing exactly which
  // descendants care costs more than re-resolving them lazily.
  bumpStyleGeneration();
  update();
}

void Widget::setStyleClasses(std::string_view spaceSeparated) {
  classes_.clear();
  std::size_t i = 0;
  while (i < spaceSeparated.size()) {
    while (i < spaceSeparated.size() &&
           std::isspace(static_cast<unsigned char>(spaceSeparated[i]))) {
      ++i;
    }
    const std::size_t start = i;
    while (i < spaceSeparated.size() &&
           !std::isspace(static_cast<unsigned char>(spaceSeparated[i]))) {
      ++i;
    }
    if (i > start) classes_.emplace_back(spaceSeparated.substr(start, i - start));
  }
  bumpStyleGeneration();
  update();
}

void Widget::addStyleClass(std::string cls) {
  if (cls.empty() || hasStyleClass(cls)) return;
  classes_.push_back(std::move(cls));
  bumpStyleGeneration();
  update();
}

void Widget::removeStyleClass(std::string_view cls) {
  const auto it = std::find(classes_.begin(), classes_.end(), cls);
  if (it == classes_.end()) return;
  classes_.erase(it);
  bumpStyleGeneration();
  update();
}

bool Widget::hasStyleClass(std::string_view cls) const {
  for (const std::string& c : classes_) {
    if (c == cls) return true;
  }
  return false;
}

StyleState Widget::styleState() const {
  StyleState s = StyleState::None;
  if (!isEffectivelyEnabled()) s |= StyleState::Disabled;
  if (const_cast<Widget*>(this)->hasFocus()) s |= StyleState::Focus;
  return s;
}

const StyleProps& Widget::style(StyleState state) const {
  const std::uint64_t gen = styleGeneration();
  if (styleCacheGen_ == gen && styleCacheState_ == state) return styleCache_;
  styleCache_ = activeStyleSheet().resolve(*this, state);
  styleCacheState_ = state;
  styleCacheGen_ = gen;
  return styleCache_;
}

void Widget::update() { update(localRect()); }

void Widget::update(const Rect& local) {
  if (local.isEmpty()) return;
  Window* win = window();
  if (!win) return;  // not attached to a window yet -- nothing to repaint
  const Point origin = mapToWindow({local.x(), local.y()});
  win->addDirtyRect({origin, local.size()});
}

Window* Widget::window() {
  Widget* w = this;
  while (w) {
    if (Window* win = w->asWindow()) return win;
    w = w->parent_;
  }
  return nullptr;
}

void Widget::paintTree(Painter& p, const Rect& dirtyInWindow,
                       const Rect& clipInWindow) {
  if (!visible_) return;

  const Rect mine = windowRect();
  // Everything this subtree may touch: our own bounds, narrowed by whatever
  // our ancestors already allow.  Once that is empty the whole subtree is
  // off-screen and can be skipped -- which is what makes a mostly-static HMI
  // screen, and a 100 000-row scrolled list, cheap.
  const Rect visible = mine.intersected(clipInWindow);
  if (visible.isEmpty() || !visible.intersects(dirtyInWindow)) return;

  const Rect dirtyLocal =
      visible.intersected(dirtyInWindow).translated(-mine.x(), -mine.y());

  p.save();
  p.clip(visible);  // window coordinates -- applied BEFORE the local translate
  p.translate(mine.x(), mine.y());
  onPaint(p, dirtyLocal);
  p.restore();

  // Children inherit our visible region, so a child that has been scrolled
  // outside its container simply never draws.
  for (const auto& child : children_) {
    child->paintTree(p, dirtyInWindow, visible);
  }
}

Widget* Widget::hitTest(Point windowPos) {
  if (!visible_ || !enabled_) return nullptr;
  if (!windowRect().contains(windowPos)) return nullptr;

  // Reverse order: later children are painted on top, so they are hit first.
  for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
    if (Widget* hit = (*it)->hitTest(windowPos)) return hit;
  }
  return this;
}

void Widget::dispatchMouse(const MouseEvent& windowEvent) {
  // Bubble from this widget up towards the root until someone accepts.  The
  // guard is what lets a handler destroy widgets: see BubbleCursor above.
  BubbleGuard bubble(this);
  while (Widget* w = bubble.node()) {
    const Rect r = w->windowRect();
    MouseEvent local = windowEvent;
    local.pos = {windowEvent.windowPos.x - r.x(), windowEvent.windowPos.y - r.y()};
    local.accepted = false;
    w->onMouse(local);
    if (local.accepted) {
      windowEvent.accept();
      return;
    }
    // Only now is `w` known to have survived its own handler.  A cancelled
    // cursor means it left the tree -- and so did every ancestor we had left to
    // visit, which is why there is nothing to resume from.
    if (bubble.node() != w) return;
    bubble.moveTo(w->parent_);
  }
}

void Widget::dispatchKey(const KeyEvent& e) {
  // Same bubbling rule as the mouse: the focused widget gets first refusal,
  // then each ancestor, so a GroupBox or Window can implement shortcuts.  And
  // the same guard -- Enter on a dialog's default button is exactly where an
  // application closes the screen it is standing on.
  BubbleGuard bubble(this);
  while (Widget* w = bubble.node()) {
    KeyEvent local = e;
    local.accepted = false;
    w->onKey(local);
    if (local.accepted) {
      e.accept();
      return;
    }
    if (bubble.node() != w) return;
    bubble.moveTo(w->parent_);
  }
}

void Widget::animationTickTree() {
  if (!visible_) return;  // an off-screen widget has nothing to animate
  onAnimationTick();
  for (const auto& child : children_) child->animationTickTree();
}

void Widget::collectFocusable(std::vector<Widget*>& out) {
  if (!visible_ || !enabled_) return;  // a disabled subtree is skipped entirely
  if (focusPolicy_ == FocusPolicy::Tab) out.push_back(this);
  // Pre-order traversal, so Tab order follows construction order.  There is no
  // explicit tab-index: on a fixed-layout HMI screen, "the order you built it"
  // is the order the operator reads it.
  for (const auto& child : children_) child->collectFocusable(out);
}

}  // namespace geeyoou
