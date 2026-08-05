#include "geeyoou/widget/Widget.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>

#include "geeyoou/render/Painter.hpp"
#include "geeyoou/widget/Window.hpp"

namespace geeyoou {
namespace {

// --- size budget -------------------------------------------------------------
//
// The layout engine was allowed to add at most 16 bytes to every widget in
// every tree.  Rather than assert against a number somebody measured by hand
// once -- which would be wrong in Debug, where MSVC's containers carry an extra
// pointer each -- the budget is measured against a struct holding exactly the
// member set Widget had before R2, in the same order.  It compiles to nothing.
struct WidgetSizeBeforeR2 {
  virtual ~WidgetSizeBeforeR2() = default;
  Rect geometry_;
  Point contentOffset_;
  void* parent_;
  std::vector<std::unique_ptr<int>> children_;
  bool visible_;
  bool enabled_;
  FocusPolicy focusPolicy_;
  std::string objectName_;
  std::vector<std::string> classes_;
  StyleProps styleCache_;
  StyleState styleCacheState_;
  std::uint64_t styleCacheGen_;
};

static_assert(sizeof(Widget) <= sizeof(WidgetSizeBeforeR2) + 16,
              "R2 budget: the layout engine may add at most 16 bytes to Widget. "
              "The flags belong in the padding after focusPolicy_; anything "
              "bigger than a pointer belongs in the Layout object instead.");

// --- in-flight frames a widget can die underneath ----------------------------
//
// THREE places in this file hand control to application code and then carry on
// touching the widget they were standing on:
//
//   * dispatchMouse/dispatchKey walk from the target widget up to the root,
//     calling a handler at every step, and then read `w->parent_`;
//   * setGeometry calls onGeometryChanged -- AppWindow::relayout emits
//     contentResized from inside one -- and then reads visible_ and repaints;
//   * runLayoutIfAny calls Layout::arrange, which calls setGeometry on every
//     child, and then writes the pass result back.
//
// Application code may destroy widgets from any of them (contract D7 in
// core/Signal.hpp lets a slot destroy other objects), so every one of those
// reads is a potential use-after-free: the systemic risk recorded in
// docs/iterations/01-lifecycle-and-tests.md.
//
// Pre-reading what is needed BEFORE the call only covers the narrow case where
// the handler removes exactly the current node.  A liveness cursor covers all
// of them, and is sound because what leaves the tree is always a whole SUBTREE:
// if any ancestor of the current node is going, the current node is going with
// it.  So the remaining work is unsafe if and only if the CURRENT node is
// doomed -- one pointer compare per departing node, and no ancestry search.
//
// The cursors live in the calling stack frames and are threaded onto one list
// per kind, so nesting simply nests and nothing allocates.  Plain statics
// rather than thread_locals: the widget tree is UI-thread-only by construction
// (docs/architecture.md section 3.4), and a TLS lookup on every mouse move and
// every setGeometry would be paid to describe a case that cannot happen.
//
// ONE mechanism written once, three lists: a second hand-rolled copy of this
// pattern is a second place to forget a check.
struct LiveCursor {
  Widget* node = nullptr;
  LiveCursor* outer = nullptr;
};

LiveCursor* g_bubbles = nullptr;      // dispatchMouse / dispatchKey
LiveCursor* g_geometries = nullptr;   // setGeometry, across onGeometryChanged
LiveCursor* g_layouts = nullptr;      // runLayoutIfAny, across arrange
std::uint32_t g_layoutDepth = 0;

template <LiveCursor*& List>
class LiveGuard {
 public:
  explicit LiveGuard(Widget* node) {
    cursor_.node = node;
    cursor_.outer = List;
    List = &cursor_;
  }
  ~LiveGuard() { List = cursor_.outer; }

  LiveGuard(const LiveGuard&) = delete;
  LiveGuard& operator=(const LiveGuard&) = delete;

  // False once the widget this frame is standing on has been destroyed.
  bool alive() const { return cursor_.node != nullptr; }
  Widget* node() const { return cursor_.node; }
  void moveTo(Widget* w) { cursor_.node = w; }

 private:
  LiveCursor cursor_;
};

using BubbleGuard = LiveGuard<g_bubbles>;
using GeometryGuard = LiveGuard<g_geometries>;

// Cancels every cursor in `list` standing on `doomed`.
void cancelOn(LiveCursor* list, const Widget* doomed) {
  for (LiveCursor* c = list; c; c = c->outer) {
    if (c->node == doomed) c->node = nullptr;
  }
}

// --- layouts that must outlive their owner -----------------------------------
//
// Cancelling the cursor stops runLayoutIfAny from touching a dead host, but it
// is one frame too late for the arrange() UNDER it: BoxLayout::arrange returns
// from setGeometry straight into `i < items_.size()`, and items_ lives in the
// Layout, which the host owns.  Freeing the layout with its host is therefore a
// use-after-free that no amount of checking in Widget can reach.
//
// So the layout is PARKED instead of destroyed, and released when the outermost
// pass unwinds and nothing is reading it any more.  Its host_ is cleared on the
// way, which is what Layout::hostAlive() reports and what makes arrange() stop
// at its next check -- the `host` REFERENCE it was handed is dangling too, and
// keeping the object alive does not fix that.
//
// "Is a pass standing on this host?" is asked of the host's own layoutRunning_
// flag rather than of the cursor list, because the cursor may already have been
// cancelled by announceDetached on the way here -- and a widget that is merely
// DETACHED still owns its layout.
//
// The list itself lives in Layout.cpp (detail::parkLayout) and is intrusive,
// chained through the layouts themselves, so this path -- which runs inside a
// destructor -- allocates nothing.

class LayoutGuard {
 public:
  explicit LayoutGuard(Widget* host) : live_(host) { ++g_layoutDepth; }
  ~LayoutGuard() {
    --g_layoutDepth;
    // The outermost frame: no arrange() is on the stack any more, so nothing
    // can still be reading a parked layout.
    if (g_layoutDepth == 0) detail::releaseParkedLayouts();
  }

  LayoutGuard(const LayoutGuard&) = delete;
  LayoutGuard& operator=(const LayoutGuard&) = delete;

  // False once the host has been destroyed under us.
  bool alive() const { return live_.alive(); }

 private:
  LiveGuard<g_layouts> live_;
};

#ifndef NDEBUG
// M2, the downward one-way rule.  Non-null ONLY while control is directly
// inside a Layout::arrange -- the moment that arrange calls setGeometry, the
// scope below parks it for the duration, so the application code a child's
// onGeometryChanged runs is not mistaken for the layout still writing.  That
// distinction is the difference between an assert that catches a Layout
// reaching past its own children and one that fires on every container in the
// library the first time it is put inside a layout.
const Widget* g_arrangeHost = nullptr;

class ArrangeSuspend {
 public:
  ArrangeSuspend() : saved_(g_arrangeHost) { g_arrangeHost = nullptr; }
  ~ArrangeSuspend() { g_arrangeHost = saved_; }

  ArrangeSuspend(const ArrangeSuspend&) = delete;
  ArrangeSuspend& operator=(const ArrangeSuspend&) = delete;

 private:
  const Widget* saved_;
};
#endif

// --- detach bookkeeping ------------------------------------------------------
constexpr std::size_t kNotAChild = std::size_t(-1);

std::size_t indexOfChild(const std::vector<std::unique_ptr<Widget>>& v,
                         const Widget* child) {
  for (std::size_t i = 0; i < v.size(); ++i) {
    if (v[i].get() == child) return i;
  }
  return kNotAChild;
}

// Is `w` STILL a child of `parent`?  `hint` is where it was last seen, which it
// almost always still is, so the answer costs one compare instead of a scan;
// `hint` is corrected when the list did move underneath us.
//
// Only pointer VALUES are compared -- `w` is never dereferenced -- so this is
// also the right question to ask about a widget an application slot may already
// have destroyed.
bool stillAChild(const Widget& parent, const Widget* w, std::size_t& hint) {
  const std::vector<std::unique_ptr<Widget>>& v = parent.children();
  if (hint < v.size() && v[hint].get() == w) return true;
  const std::size_t i = indexOfChild(v, w);
  if (i == kNotAChild) return false;
  hint = i;
  return true;
}

// Pre-order, and BEFORE anything is unlinked: `win` is still reachable through
// the parent chain and every observer pointer still names a live widget when it
// is dropped.
//
// Every announcement runs APPLICATION code -- widgetDetached closes a departing
// popup, which emits popupClosed -- and contract D7 lets a slot destroy other
// objects.  Two consequences, and both cost a re-check rather than a copy:
//
//   * `node` itself may be gone when widgetDetached returns (a slot is entitled
//     to removeChild(node) from wherever it is parented).  Everything below
//     dereferences it, so it is proved to still be in `parent` first.
//   * a slot may remove a child of `node`.  Walking the live vector by index is
//     NOT enough: re-reading size() prevents the overrun, but removing a child
//     ahead of the cursor shifts every later one DOWN by one, and the walk then
//     skips a whole subtree -- whose nodes keep the window's focus/hover/grab
//     pointers aimed at memory that is about to be freed.  So the list is
//     snapshotted and each entry re-checked against the live one.
void announceDetached(const Widget& parent, Widget* node, std::size_t nodeHint,
                      Window* win) {
  cancelOn(g_bubbles, node);
  cancelOn(g_geometries, node);
  // NOT parked here: `node` is only being ANNOUNCED, not destroyed -- takeChild
  // may still hand it back alive.  ~Widget is the one door every departure goes
  // through, and that is where the layout is parked.
  cancelOn(g_layouts, node);
  if (win) win->widgetDetached(node);
  if (!stillAChild(parent, node, nodeHint)) return;
  if (node->children().empty()) return;  // the common case: no snapshot, no allocation

  std::vector<Widget*> kids;
  kids.reserve(node->children().size());
  for (const std::unique_ptr<Widget>& c : node->children()) kids.push_back(c.get());

  for (std::size_t i = 0; i < kids.size(); ++i) {
    // Announcing a sibling can take `node` away too -- it is a legal thing for a
    // slot to do, and node->children() below would then be a read of freed
    // memory.
    if (!stillAChild(parent, node, nodeHint)) return;
    std::size_t hint = i;
    if (!stillAChild(*node, kids[i], hint)) continue;  // already removed for us
    announceDetached(*node, kids[i], hint, win);
  }
}

}  // namespace

Widget::~Widget() {
  // A bubble standing on this widget must not walk into the parent pointer that
  // is about to be freed.  announceDetached() already does this for the removal
  // API, but destruction has other doors: dropping the unique_ptr takeChild()
  // returned, a Window going out of scope, or any subclass destructor at all.
  //
  // Descendants are covered without a walk: children_ is destroyed right after
  // this body, so every one of them arrives here on its own.
  cancelOn(g_bubbles, this);
  // Same argument, same doors, for a setGeometry and for a layout pass standing
  // on this widget.
  cancelOn(g_geometries, this);
  cancelOn(g_layouts, this);

  if (layout_) {
    --detail::g_layoutHosts;
    // An arrange() of ours is still on the stack, reading items_, scratch_ and
    // the `host` reference it was handed.  Freeing the layout here would pull
    // all of that out from under it; parking hands it to the outermost pass.
    if (layoutRunning_) detail::parkLayout(layout_.release());
  }
}

std::unique_ptr<Widget> Widget::takeChild(Widget* child) {
  if (!child) return nullptr;
  const std::size_t hint = indexOfChild(children_, child);
  if (hint == kNotAChild) return nullptr;

  // Repaint what the subtree is vacating, while it is still attached: update()
  // maps through the parent chain to find the Window it must report damage to.
  if (child->visible_) child->update();

  announceDetached(*this, child, hint, window());

  // Re-found rather than reused: announceDetached can run popupClosed slots,
  // and one of those may already have removed `child` -- or a sibling ahead of
  // it, which moves the index either way.
  const std::size_t index = indexOfChild(children_, child);
  if (index == kNotAChild) return nullptr;

  std::unique_ptr<Widget> owned = std::move(children_[index]);
  children_.erase(children_.begin() + std::ptrdiff_t(index));
  owned->parent_ = nullptr;
  // After the unlink, so a layout that re-arranges from here sees the vector it
  // is about to index into, not the one that still holds the departing child.
  if (detail::g_layoutHosts != 0) childRemoved(index);
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
  // M3, idempotence.  A layout pass hands the same rectangle back to most of
  // its children most of the time -- resizing a window by one pixel moves one
  // edge and nothing else -- and without this the work, the two repaints and
  // every onGeometryChanged below would all happen anyway.  It is also what
  // stops "child changed -> re-arrange -> child's geometry set -> child changed"
  // from being a cycle.
  //
  // The dirty flag is part of the test: a host whose layout was invalidated
  // DURING a pass must still re-run even though its own rectangle did not move.
  if (r == geometry_ && !layoutDirty_) return;

#ifndef NDEBUG
  // M2: an arrange may only write to its host's DIRECT children.
  assert((g_arrangeHost == nullptr || parent_ == g_arrangeHost) &&
         "Layout::arrange may only setGeometry on its host's direct children");
  const ArrangeSuspend suspend;
#endif

  if (visible_) update();  // repaint what we are vacating
  geometry_ = r;
  // The natural size is the first real size a widget was ever given BY HAND,
  // and after that it is frozen.  Both halves are decided in one place; the
  // test here is only the fast path out of a call that would do nothing.
  // ADR-R2-09.
  if (naturalSize_.isEmpty()) latchNaturalSize();
  // Before onGeometryChanged, not after: explicit code wins.  A container that
  // both owns a layout and overrides onGeometryChanged gets to place whatever
  // the layout did not, and its placement is the one that survives.
  if (layout_ && !runLayoutIfAny()) return;  // destroyed under us -- touch nothing

  // onGeometryChanged runs APPLICATION code: AppWindow::relayout emits
  // contentResized from inside one, and a slot is entitled to destroy widgets
  // -- this one included.  Everything below reads a member, so the frame needs
  // the same cursor the event bubble and the layout pass already have.  Without
  // it a real BoxLayout::arrange, which does not return after the removal the
  // way the test-suite's SuicidalLayout does, walks straight back into visible_
  // and then into window() on a freed object.
  {
    GeometryGuard alive(this);
    onGeometryChanged();
    if (!alive.alive()) return;  // freed under us -- touch nothing, `this` included
  }
  if (visible_) update();  // ...and what we now occupy
}

// ------------------------------------------------------------------ layout ---
void Widget::adoptLayout(std::unique_ptr<Layout> l) {
  const bool hadLayout = layout_ != nullptr;
  // Replacing a layout from inside its own arrange() is ordinary application
  // code -- an onGeometryChanged that switches a panel from a row to a column
  // -- and destroying the old object there would free the items_ and scratch_
  // that arrange is still reading two frames up.  Parked, exactly as a dead
  // host's layout is, and released when the outermost pass unwinds.  Clearing
  // its host_ is what stops the outgoing arrange at its next check; the
  // incoming layout re-places the same children in this pass's second round.
  if (layout_ && layoutRunning_) detail::parkLayout(layout_.release());

  layout_ = std::move(l);
  if (!layout_) {
    if (hadLayout) --detail::g_layoutHosts;  // setLayout cannot get here; a future reset could
    return;
  }
  if (!hadLayout) ++detail::g_layoutHosts;
  layout_->host_ = this;

  // NOTE: this used to latch the natural size of the host and of every child it
  // already had.  Both were dead: setGeometry latches unconditionally, so
  // anything that had ever been given a size by hand was already latched.  What
  // they were NOT dead against is a widget whose only size came from a layout
  // pass -- which is exactly the size that must never be latched (ADR-R2-09,
  // and see latchNaturalSize below).
  performLayout();
}

// A container answers for its contents; everything else answers with the size
// it was built at.  See the contract on the declaration.
//
// The recursion is bounded by the tree and visits each node once per pass of
// its parent, so a page costs O(nodes) per layout pass -- not O(nodes) per
// level.  What it is NOT free of is text measurement: a Label's hint runs the
// shaper, so a layout with labels in it measures text on every pass.  That is
// the known limitation recorded in tests/widget/test_layout_alloc.cpp and
// docs/iterations/02-layout-engine.md; the fix is a per-control width cache in
// the text round (R3), not here.
SizeHint Widget::sizeHint() const {
  // measureFor, not measure: the layout's measuring buffers are the ones its
  // arrange() is using, and this is the door application code comes back
  // through in the middle of one.  See Layout::measureFor.
  if (layout_) return layout_->measureFor(*this);
  return SizeHint{Size{0.0f, 0.0f}, naturalSize_, Size{kUnbounded, kUnbounded}};
}

const LayoutOverflow& Widget::lastLayoutOverflow() const {
  // A widget with no layout never overflowed anything.  A file-scope constant
  // rather than a member keeps those 12 bytes off every widget in the tree.
  static const LayoutOverflow kNone;
  return layout_ ? layout_->lastOverflow() : kNone;
}

void Widget::invalidateSizeHint() { markLayoutDirty(); }

void Widget::performLayout() {
  if (!layout_) return;
  layoutDirty_ = true;
  runLayoutIfAny();
}

void Widget::latchNaturalSize() {
  if (!naturalSize_.isEmpty()) return;  // already latched, and it is for life
  // ADR-R2-09 is one-way in BOTH directions.  It stopped a window dragged
  // smaller from ratcheting its contents down; latching the OUTPUT of a layout
  // pass is the same mistake pointing the other way -- a plain panel first
  // arranged into a 500 wide host would claim preferred.width = 500 for ever
  // and never fit back into a 120 wide one.  The natural size is the first
  // non-empty size a HUMAN gave this widget, which is what sizeHint()'s
  // contract says it is; a size a layout computed is not one.
  if (detail::layoutPassActive()) return;
  const Size s = geometry_.size();
  if (s.isEmpty()) return;  // nothing worth remembering yet
  naturalSize_ = s;
}

Rect Widget::contentRect() const {
  const Rect r = layoutRect();
  if (!layout_) return r;
  const Margins& m = layout_->margins();
  return {r.x() + m.left, r.y() + m.top,
          std::max(0.0f, r.width() - m.left - m.right),
          std::max(0.0f, r.height() - m.top - m.bottom)};
}

// Marks every layout host from here to the root, then -- unless a pass is
// already running -- runs the topmost one.
//
// Only widgets that actually own a layout are marked.  Setting the flag on the
// rest would leave it stuck on forever (nothing ever clears it for them) and
// permanently disable the idempotence check in setGeometry, which is the whole
// reason the flag exists.
void Widget::markLayoutDirty() {
  if (detail::g_layoutHosts == 0) return;

  Widget* top = nullptr;
  for (Widget* w = this; w; w = w->parent_) {
    if (!w->layout_) continue;
    w->layoutDirty_ = true;
    top = w;
  }
  if (!top) return;
  // Inside a pass there is nothing to start: the flag we just set is what the
  // running pass re-reads, and starting a second pass under it is the
  // re-entrancy M1 exists to prevent.
  if (g_layouts) return;
  top->runLayoutIfAny();
}

void Widget::childAppended() {
  if (layout_) layout_->onChildAppended();
  markLayoutDirty();
}

void Widget::childRemoved(std::size_t index) {
  if (layout_) layout_->onChildRemoved(index);
  markLayoutDirty();
}

void Widget::rebaseSubtreeDepth() {
  for (const std::unique_ptr<Widget>& c : children_) {
    assert(depth_ + 1 < int(kMaxTreeDepth) && "widget tree deeper than kMaxTreeDepth");
    c->depth_ = std::uint16_t(depth_ + 1);
    if (!c->children_.empty()) c->rebaseSubtreeDepth();
  }
}

// The four anti-re-entrancy mechanisms, in one function.  See
// docs/iterations/02-layout-engine.md for why each of them is here.
bool Widget::runLayoutIfAny() {
  if (!layout_) return true;

  // M1, the re-entrancy latch.  An arrange that ends up back here -- through a
  // child's onGeometryChanged, through a slot, through anything -- does not
  // recurse.  It leaves a note, and the loop below picks it up.
  if (layoutRunning_) {
    layoutDirty_ = true;
    return true;
  }

  // M4, the global ceiling.  Nesting deeper than a legal tree can be means the
  // other three missed a cycle.  Abandoning the pass leaves the geometry of the
  // last converged one on screen -- wrong but stable, which in a control room
  // beats a stack overflow.
  if (g_layoutDepth >= kMaxTreeDepth) {
    detail::layoutDepthExceeded(this);
    return true;
  }

  LayoutGuard guard(this);
  layoutRunning_ = true;
  int rounds = 0;
  do {
    layoutDirty_ = false;
    const Rect content = contentRect();
    // Captured, not re-read: application code run from inside arrange() may
    // replace this widget's layout, and the result below belongs to the object
    // that PRODUCED it, not to whatever is installed by the time it returns.
    // adoptLayout parks the outgoing object, so this pointer stays valid until
    // the outermost pass unwinds.
    Layout* const running = layout_.get();
#ifndef NDEBUG
    const Widget* savedArrange = g_arrangeHost;
    g_arrangeHost = this;
#endif
    const LayoutOverflow result = running->arrangeFor(*this, content);
#ifndef NDEBUG
    g_arrangeHost = savedArrange;
#endif
    // arrange() ran application code; this widget may be gone.  Nothing below
    // may touch a member, layout_ included -- it died with us.
    if (!guard.alive()) return false;
    running->lastOverflow_ = result;
    // At most ONE re-run.  A layout that has not settled after seeing its own
    // output once will not settle on the third try either; it will just burn a
    // frame budget doing it.
  } while (layoutDirty_ && ++rounds < 2);

  if (layoutDirty_) detail::layoutNotConverged(this);
  layoutDirty_ = false;
  layoutRunning_ = false;
  return true;
}

namespace detail {

bool layoutPassActive() { return g_layouts != nullptr; }

const Widget* currentLayoutHost() {
  return g_layouts ? g_layouts->node : nullptr;
}

}  // namespace detail

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
  // Content-driven: a layout that skips hidden items has just lost or gained
  // one, so the space it was holding has to be redistributed.
  markLayoutDirty();
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
