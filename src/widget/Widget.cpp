#include "geeyoou/widget/Widget.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <string>
#include <unordered_map>

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
// FOUR places in this file hand control to application code and then carry on
// touching the widget they were standing on:
//
//   * dispatchMouse/dispatchKey walk from the target widget up to the root,
//     calling a handler at every step, and then read `w->parent_`;
//   * setGeometry calls onGeometryChanged -- AppWindow::relayout emits
//     contentResized from inside one -- and then reads visible_ and repaints;
//   * runLayoutIfAny calls Layout::arrange, which calls setGeometry on every
//     child, and then writes the pass result back;
//   * takeChild announces the departing subtree -- which closes a departing
//     popup and emits popupClosed -- and then goes on reading the child list of
//     the widget it is removing FROM.  REM3-RES-4; see announceDetached.
//
// Application code may destroy widgets from any of them (contract D7 in
// core/Signal.hpp lets a slot destroy other objects), so every one of those
// reads is a potential use-after-free: the systemic risk recorded in
// docs/iterations/01-lifecycle-and-tests.md.
//
// STILL FOUR after E14, and that is the point of REM3-G9.  announceDetached now
// makes a virtual call of its own -- onDescendantDetached, on every ancestor of
// every departing node -- which by P1 looks exactly like a fifth door.  It is
// not one, because the contract on that hook forbids it from reaching
// application code at all: it may null its own member pointers and nothing
// else, and Debug builds assert on the one re-entry that would corrupt the walk
// (see takeChild).  A hook that stops obeying that contract is a fifth door and
// belongs in this list.
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
// ONE mechanism written once, four lists: a second hand-rolled copy of this
// pattern is a second place to forget a check.
//
// The cursor type and the guard template themselves now live in Widget.hpp,
// because the frames that need them are no longer only in this file: a
// container's own sizeHint() and its own relayout() cross into application code
// exactly as the four doors above do, and those are written in widget
// SUBCLASSES, in their own translation units.  Only the death-watch list went
// with them -- a reference template argument may name an internal-linkage
// variable, so the three below stay private to this file.
//
// Named unqualified here so that every use below reads exactly as it did when
// the definitions were in this file: the move is a change of ADDRESS, not of
// meaning, and a diff full of detail:: would say otherwise.
using detail::LiveCursor;
using detail::LiveGuard;

LiveCursor* g_bubbles = nullptr;      // dispatchMouse / dispatchKey
LiveCursor* g_geometries = nullptr;   // setGeometry, across onGeometryChanged
LiveCursor* g_layouts = nullptr;      // runLayoutIfAny, across arrange
std::uint32_t g_layoutDepth = 0;

// ...and the fourth, detail::g_deathWatch, declared in Widget.hpp and defined
// below this namespace: takeChild / announceDetached / clearChildren, across
// Window::widgetDetached, plus (from the next round) a container measuring
// across a child's sizeHint() override.

using BubbleGuard = LiveGuard<g_bubbles>;
using GeometryGuard = LiveGuard<g_geometries>;

// WHY THAT FOURTH LIST IS A FOURTH LIST -- that is, why the removal path may
// not be threaded onto any of the three above.  Two criteria, and a candidate
// has to fail neither:
//
//   1. CANCELLATION POLICY.  cancelOn walks a whole list, so one list cannot
//      run two policies.  g_bubbles and g_geometries are cancelled by
//      announceDetached for every node that is merely DETACHED, and that is
//      right for them -- their remaining work walks a parent chain the detach
//      has just emptied.  A frame that fears only DESTRUCTION must not be
//      cancelled there: a widget being moved between two containers is
//      perfectly alive, so a takeChild standing on it would hand back nullptr
//      and leave the child attached -- a degradation reported for a host that
//      never died.
//   2. READERS THAT DECIDE.  A list whose contents make the ENGINE behave
//      differently cannot take a foreign frame, because the foreign frame sends
//      that decision the wrong way and says nothing.  g_layouts has three such
//      readers: detail::layoutPassActive(), detail::currentLayoutHost(), and
//      markLayoutDirty's "a pass is already running, do not start another".  A
//      cursor parked there for the duration of an announcement tells all three
//      that a pass is in flight when none is.
//
//      Stated precisely, because the loose version invites the wrong fix:
//      markLayoutDirty sets layoutDirty_ on every host up the chain BEFORE it
//      consults the list, so the dirty MARK is never lost -- what is lost is
//      the RUN.  Under a real pass losing the run is correct: that pass re-reads
//      the flag as it unwinds and pays the round back.  Under a borrowed cursor
//      there is no pass to unwind, nobody comes back for the flag, and the
//      subtree keeps the previous frame's geometry until some unrelated later
//      trigger happens to find the list empty.  That is the frozen subtree the
//      comment in announceDetached below reaches from the other direction.
//
// Neither criterion touches the death watch itself: it is cancelled by ~Widget
// and by nothing else -- exactly "dead, not merely detached" -- and nothing
// reads it in order to decide anything.  Widget.hpp says so as a PRECONDITION
// on whatever is added to it later, not as an observation about today.

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
  ~LayoutGuard() { --g_layoutDepth; }

  LayoutGuard(const LayoutGuard&) = delete;
  LayoutGuard& operator=(const LayoutGuard&) = delete;

  // False once the host has been destroyed under us.
  bool alive() const { return live_.alive(); }

 private:
  // Drains the park list when the outermost frame unwinds.  A MEMBER, declared
  // before live_, rather than two lines in ~LayoutGuard's body: members are
  // destroyed in reverse order, so this runs AFTER live_ has taken this frame's
  // cursor off g_layouts.  Done in the body instead -- which is what it used to
  // be -- the list is cleared while detail::layoutPassActive() still reads
  // true, i.e. while the engine is still telling everyone a pass is running,
  // which is precisely the state the release is supposed to be the end of.
  struct DrainOnUnwind {
    ~DrainOnUnwind() {
      if (g_layoutDepth == 0) detail::releaseParkedLayouts();
    }
  };

  DrainOnUnwind drain_;
  LiveGuard<g_layouts> live_;
};

// The host whose arrange() is DIRECTLY on the stack, or null.
//
// Non-null ONLY while control is inside a Layout::arrange -- the moment that
// arrange calls setGeometry, the scope below parks it for the duration, so the
// application code a child's onGeometryChanged runs is not mistaken for the
// layout still writing.
//
// TWO readers, and the second is why this is not Debug-only any more:
//
//   * M2's assert, the downward one-way rule.  That distinction above is the
//     difference between an assert that catches a Layout reaching past its own
//     children and one that fires on every container in the library the first
//     time it is put inside a layout.
//   * latchNaturalSize, which needs to know whether THIS setGeometry is a
//     layout placing its own child.  It used to ask detail::layoutPassActive()
//     instead -- "is a pass running anywhere in this process" -- and that is a
//     different question with a different answer: a container that places its
//     own children by hand from onGeometryChanged does so while its own
//     parent's pass is on the stack, so its children never latched a natural
//     size at all and went on being measured at 0x0 for ever.
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

// --- detach bookkeeping ------------------------------------------------------
constexpr std::size_t kNotAChild = std::size_t(-1);

#ifndef NDEBUG
// REM3-G9's enforcement, and the only reader is the assert in takeChild below.
// Debug-only because there is no second reader: g_arrangeHost above is NOT
// Debug-only precisely because it grew one, and the difference is worth keeping
// visible rather than making every flag in this file look the same.
bool g_inDetachNotify = false;

// Saves and restores rather than clearing, so this is correct even if the
// assert it feeds is ever compiled out and the case it forbids happens anyway.
class DetachNotifyScope {
 public:
  DetachNotifyScope() : saved_(g_inDetachNotify) { g_inDetachNotify = true; }
  ~DetachNotifyScope() { g_inDetachNotify = saved_; }

  DetachNotifyScope(const DetachNotifyScope&) = delete;
  DetachNotifyScope& operator=(const DetachNotifyScope&) = delete;

 private:
  bool saved_;
};
#endif

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
//   * `parent` itself may be gone when widgetDetached returns -- REM3-RES-4.  A
//     slot may remove the widget this announcement is walking the child list
//     OF, and every check below starts by reading that list.  It is the same
//     hazard as the one above, one level up.
//
//     `parent` is a NON-CONST reference, and the reason recorded here used to be
//     that the cursor is a Widget* and the alternative was a const_cast in the
//     one place that must not be casual.  That reason is gone: detail::
//     DeathWatch takes a const Widget* and holds the library's single
//     const_cast, next to the argument for it.  Nothing in this function writes
//     through `parent` any more, so the signature is now wider than it needs
//     to be -- left alone on purpose, as its own change rather than folded into
//     this one.
//   * `node` itself may be gone when widgetDetached returns (a slot is entitled
//     to removeChild(node) from wherever it is parented).  Everything below
//     dereferences it, so it is proved to still be in `parent` first.
//   * a slot may remove a child of `node`.  Walking the live vector by index is
//     NOT enough: re-reading size() prevents the overrun, but removing a child
//     ahead of the cursor shifts every later one DOWN by one, and the walk then
//     skips a whole subtree -- whose nodes keep the window's focus/hover/grab
//     pointers aimed at memory that is about to be freed.  So the list is
//     snapshotted and each entry re-checked against the live one.
void announceDetached(Widget& parent, Widget* node, std::size_t nodeHint,
                      Window* win) {
  // THE CURSOR ON `parent`, AND IT IS THE FIRST STATEMENT OF THE FUNCTION.
  // That placement is the whole of this guard; the argument for HAVING it is
  // further down, next to widgetDetached, and that argument was always right.
  //
  // It used to be constructed down there too -- after the broadcast below,
  // after both cancelOn calls -- and that is one door too late.  A cursor is
  // cancelled by ~Widget.  Register it AFTER the widget has already died and
  // there is nothing left to do the cancelling: alive() answers true FOR EVER,
  // and stillAChild(parent, ...) dereferences freed memory a few lines on.  A
  // guard that reads as present in a diff and is absent in fact is worse than
  // no guard, because it is the state that stops anybody looking -- and section
  // 12.4 A' residue 2 says so in general: a frame holding a cursor is not a
  // lint candidate, so nothing mechanical was ever going to find this.
  //
  // Measured before the move, with a hook that frees `parent`: four
  // heap-use-after-frees in stillAChild.  Section 11.11, unverified item 6.
  //
  // "BEFORE THE FIRST DOOR", not "before the door I was thinking of".  Taking a
  // cursor costs five instructions and no allocation (section 11.5), so there
  // is never a reason to place one anywhere but the top of the frame it guards.
  //
  // Nothing else moved.  The broadcast is still the first CALL, for the reasons
  // in the comment under this one; constructing a cursor is not a call, runs no
  // application code and dereferences nothing.
  detail::DeathWatch host(&parent);

  // E14, and FIRST in the body for two reasons that are both about what is
  // still true at this instant: nothing has been unlinked yet, and no
  // application code has run yet.  So every ancestor is alive, the whole
  // departing subtree is alive, and an override comparing pointers is doing so
  // against objects that certainly exist.  Put it after widgetDetached instead
  // -- which closes a departing popup and emits popupClosed -- and an ancestor
  // may already have died inside it, so the broadcast would need a liveness
  // argument of its own.  It has none this way.
  //
  // Unconditional, and NOT under `if (win)`: a ScrollArea that is not attached
  // to any Window has exactly the same dangling content_ (the test suite is
  // full of them).  The Window is the observer of focus and hover; this is the
  // widget repairing itself.
  //
  // No new traversal: this rides the pre-order walk that was already here, and
  // it is per NODE, so every node of the departing subtree announces itself up
  // its own ancestor chain.
  detail::notifyDetachToAncestors(node);
  // REM3-G3: the check is the statement immediately after the door.  `host` is
  // taken at the very top of this function -- see the note there; this is the
  // first of its three checkpoints.
  //
  // Returning here skips both cancelOn calls, and that is correct rather than
  // merely tolerable: `node` is owned by `parent`, so a dead `parent` means a
  // dead `node`, and ~Widget has already cancelled every cursor naming it.
  if (!host.alive()) return;
  // REM3-G3: the check is the statement immediately after the door.  Returning
  // here skips both cancelOn calls, and that is correct rather than merely
  // tolerable -- `node` is owned by `parent`, so a dead `parent` means a dead
  // `node`, and ~Widget has already cancelled every cursor naming it.
  if (!host.alive()) return;
  cancelOn(g_bubbles, node);
  cancelOn(g_geometries, node);
  // NOTHING is done to g_layouts here, and the layout is not parked either.
  // `node` is only being ANNOUNCED, not destroyed -- takeChild may still hand
  // it back alive -- and ~Widget is the one door every departure really goes
  // through, which is where both of those belong.
  //
  // The cursor used to be cancelled anyway, and it was the argument above that
  // it failed to follow.  A cancelled cursor makes runLayoutIfAny return at its
  // `!guard.alive()` check, which is the path for a host that has been FREED,
  // so it deliberately writes nothing -- layoutRunning_ included.  On a host
  // that is merely detached and perfectly alive that flag then stays true for
  // good, M1 turns every later pass into "mark dirty and return", and the
  // subtree's geometry is frozen for the rest of the process.  takeChild() from
  // inside a layout pass is ordinary application code (a page moving a panel
  // between two containers on a resize), and it made the panel a picture.
  //
  // The cursor on `parent`, on the other hand, is taken in this frame and
  // nowhere else: it is this frame's own, it covers EVERY call in it that runs
  // application code -- the broadcast above and the announcement below -- and
  // each of those is followed immediately by a check.  REM3-G3.  It is armed at
  // the top of the function; the note up there is about the placement.
  if (win) win->widgetDetached(node);
  // Order matters: `parent` is dereferenced by everything after this, the
  // liveness test dereferences nothing, so it comes first.
  if (!host.alive()) return;
  if (!stillAChild(parent, node, nodeHint)) return;
  if (node->children().empty()) return;  // the common case: no snapshot, no allocation

  std::vector<Widget*> kids;
  kids.reserve(node->children().size());
  for (const std::unique_ptr<Widget>& c : node->children()) kids.push_back(c.get());

  for (std::size_t i = 0; i < kids.size(); ++i) {
    // Announcing a sibling can take `parent` away -- the recursive call below
    // reaches the same widgetDetached, and the slot behind it is under no
    // obligation to stop at this subtree.  Asked before the line under it,
    // which reads parent.children().
    if (!host.alive()) return;
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

// The fourth list itself.  Declared in Widget.hpp, because the guards for it
// are written in widget subclasses; defined here, next to the three private
// ones and next to the destructor below that cancels all four.
namespace detail {

LiveCursor* g_deathWatch = nullptr;

// E14.  A free function in `detail` rather than a member of Widget because the
// only caller is announceDetached above, which has internal linkage and so
// cannot be named as a friend in the header; Layout befriends detail::parkLayout
// for the same reason.
//
// WHY THE WHOLE ANCESTOR CHAIN and not the parent alone: the pointer a container
// caches is a GRANDCHILD in every real case in this library -- ScrollArea's
// content_ is created with viewport_->add<Widget>(), and AppWindow::fill_ and
// Shell's page host repeat the shape.  A parent-only notification would reach
// the viewport, which caches nothing, and never reach the ScrollArea, which
// caches everything.  Three containers, three grandchildren; the depth is the
// rule here, not the exception.
//
// COST: this is O(depth) per departing node, so O(nodes x depth) per removal.
// Paid only on a real detach -- not on destruction, and not on any paint,
// layout or event path -- and the loop is a pointer chase plus a virtual call
// per level whose default is an empty body.  Nothing is allocated and nothing
// is remembered between calls.
//
// WHY ~Widget DOES NOT NEED IT.  A subtree can leave a tree that is STILL ALIVE
// only through Widget::takeChild: `children_.erase` appears exactly once in the
// whole library (Widget.cpp, in takeChild), and `parent_` is assigned in exactly
// two places, add<T> and takeChild.  Every other departure is the holder itself
// being destroyed -- and ~Widget destroys children_ right after its own body, so
// a cached pointer in a dying container names an object that dies with it.  A
// hook there would be telling an object about to be freed that its member is
// about to be freed.
void notifyDetachToAncestors(Widget* node) {
#ifndef NDEBUG
  // REM3-G9.  The tree is half detached for the duration of this loop; the
  // assert this arms is in takeChild, which is the one entry point that could
  // re-enter the walk above.
  const DetachNotifyScope inNotify;
#endif
  for (Widget* a = node->parent(); a; a = a->parent()) a->onDescendantDetached(node);
}

std::size_t deathWatchDepth() {
  std::size_t n = 0;
  for (const LiveCursor* c = g_deathWatch; c; c = c->outer) ++n;
  return n;
}

}  // namespace detail

namespace {
// Tooltips live here, not in Widget.  The R2 size budget (the static_assert
// below) is fully spent by layout_ and naturalSize_, and almost no widget ever
// sets a tooltip, so a per-widget member -- even a single pointer -- would tax
// every widget that never has one.  A side table keyed by the widget pointer
// costs the tooltip-less widgets nothing but this map's own existence, and one
// hashed erase each at teardown only while some tooltip is live anywhere.  The
// UI is single-threaded, so the bare static needs no lock.
std::unordered_map<const Widget*, std::string>& tooltipTable() {
  static std::unordered_map<const Widget*, std::string> table;
  return table;
}
}  // namespace

void Widget::setTooltip(std::string s) {
  if (s.empty()) {
    tooltipTable().erase(this);
  } else {
    tooltipTable()[this] = std::move(s);
  }
}

std::string Widget::tooltip() const {
  auto& table = tooltipTable();
  if (table.empty()) return {};
  auto it = table.find(this);
  return it == table.end() ? std::string{} : it->second;
}

bool Widget::hasTooltip() const {
  auto& table = tooltipTable();
  return !table.empty() && table.find(this) != table.end();
}

Widget::~Widget() {
  // A tooltip entry keyed by this pointer would dangle the instant the address
  // is reused; drop it here.  Skipped entirely -- no hash, no lock -- whenever
  // nothing in the process has set a tooltip, which is the common case.
  if (!tooltipTable().empty()) tooltipTable().erase(this);

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
  // ...and for every frame that is merely standing on this widget and will read
  // it again -- a removal holding it as the PARENT it is removing from, and,
  // from the next round, a container measuring across a child's sizeHint().
  // Here rather than in announceDetached on purpose: a detached widget still
  // owns its children and its takeChild is still entitled to finish, which is
  // the whole of what "dead, not merely detached" buys.
  cancelOn(detail::g_deathWatch, this);

  if (layout_) {
    --detail::g_layoutHosts;
    // A call of ours is still on the stack, reading items_, scratch_ and the
    // `host` reference it was handed.  Freeing the layout here would pull all
    // of that out from under it; parking defers it until nothing is reading.
    //
    // TWO flags, not one.  layoutRunning_ is about ARRANGING, and a measurement
    // is not an arrange: ScrollArea::relayout calls content_->sizeHint() with
    // no pass anywhere on the stack, and a child's sizeHint() override is as
    // entitled to destroy this widget from in there as any handler is.  Asking
    // layoutRunning_ alone let that case run the unique_ptr below straight
    // through the BoxLayout that gather() was walking -- and then
    // Layout::measureFor wrote its result into the freed object on the way out.
    if (layoutRunning_ || layout_->buffersBusy_) {
      detail::parkLayout(layout_.release());
    }
  }
}

// DEGRADED RESULT, and what it means (ADR-R2-04: record, do not abort).
//
// The announcement below runs application code, and a slot may destroy THIS
// widget -- removing an ancestor of it is enough, and is a legal thing for a
// slot to do (D7 forbids only destroying the signal's own owner).  When that
// happens `this` is freed while this call is still on its stack, and everything
// after the announcement -- the index lookup, the vector erase, childRemoved --
// is a write into freed memory.  REM3-RES-4, the fifth instance of this family
// and the first one in the core.
//
// So: if `this` did not survive the announcement, takeChild returns nullptr and
// touches NOTHING -- no member of `this`, and not `child` either.  Both are
// already gone: `child` was in the vector `this` owns, so it was destroyed with
// it, which also means there is nothing left that the caller could have been
// given ownership of.  The nullptr is therefore not a lost result, it is the
// only honest description of the situation, and it is the same nullptr this
// function already returns when a slot removed `child` first.
std::unique_ptr<Widget> Widget::takeChild(Widget* child) {
#ifndef NDEBUG
  // REM3-G9.  onDescendantDetached runs in the middle of announceDetached's
  // pre-order walk, over a tree that is half detached: the departing subtree is
  // still linked in, the Window has not been told, and the snapshot the walk is
  // iterating was taken before the hook ran.  Removing anything from in there
  // re-enters this function, and the walk resumes over a vector that no longer
  // means what it meant when it was snapshotted.
  //
  // ONE assert, and this is the site because this is the only door: takeChild
  // is the sole caller of announceDetached and the sole place `children_.erase`
  // appears, so it is the only way a hook can reach the walk it is standing in.
  // It does NOT catch the other things REM3-G9 forbids -- an update(), an
  // emit(), a virtual call -- and the reason is REACH, not price.  takeChild is
  // the only memory-unsafe primitive that this flag can be checked against at
  // all: g_inDetachNotify has INTERNAL LINKAGE in this translation unit, while
  // Signal::emit lives in core/ and core may not depend on widget, so there is
  // nowhere over there for it to read this flag from.
  //
  // Read that as a residue we CANNOT REACH, not one we decided not to pay for.
  // An emit() out of a hook is every bit as memory-unsafe as a takeChild() and
  // harder to find; both remaining paths are written down in section 11.11 of
  // docs/iterations/02-layout-engine.md rather than left implied here.  Price
  // would have been a bad argument anyway: update(), setGeometry, setVisible,
  // invalidateSizeHint and performLayout are all in THIS file and each would
  // cost one Debug-only boolean test.  They carry no assert because they are
  // not memory-unsafe.  The trade is written by HARM, not by cost.
  //
  // Debug-only, abort on hit, same standing as M2's assert in setGeometry.
  assert(!g_inDetachNotify &&
         "REM3-G9: onDescendantDetached may only null its own member pointers");
#endif
  if (!child) return nullptr;
  const std::size_t hint = indexOfChild(children_, child);
  if (hint == kNotAChild) return nullptr;

  // Repaint what the subtree is vacating, while it is still attached: update()
  // maps through the parent chain to find the Window it must report damage to.
  if (child->visible_) child->update();

  {
    // REM3-G3: the check goes immediately after the door, inside the scope that
    // holds the cursor.  Two cursors on this widget end up on g_deathWatch for
    // this call -- this one and announceDetached's own -- and that is what the
    // list is for: the callee's covers the callee's frame and dies with it,
    // this one covers the removal proper, up to the closing brace below.
    //
    // UP TO THAT BRACE, and no further: this function has a SECOND door, after
    // the guard has gone.  See the note on childRemoved at the end.
    detail::DeathWatch alive(this);
    announceDetached(*this, child, hint, window());
    if (!alive.alive()) return nullptr;  // freed under us -- see above
  }

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
  //
  // THE SECOND DOOR OF THIS FUNCTION, and it is outside the guard scope above.
  // childRemoved runs Layout::onChildRemoved -- an application override -- and
  // markLayoutDirty, which may start a pass, whose setGeometry runs every
  // child's onGeometryChanged.  This widget can therefore die HERE too.
  //
  // That is safe today, and it leaks nothing, for a reason worth spelling out
  // because it is not visible from the call: `owned` has already been moved out
  // of children_ and its parent_ has already been cleared, so this widget's
  // destructor no longer reaches the departing subtree, and `owned` is a local
  // that the return below hands to the caller.
  //
  // What that rests on is the ONE line that follows: `return owned;` does not
  // dereference `this`.  Add ANY member access after this call -- a read, an
  // update(), a childRemoved of your own -- and the guard scope above must be
  // extended to the end of the function instead, with a check right here.
  if (detail::g_layoutHosts != 0) childRemoved(index);
  return owned;
}

void Widget::removeChild(Widget* child) {
  std::unique_ptr<Widget> doomed = takeChild(child);
  doomed.reset();  // the destruction IS the point; spelled out, not implied
}

void Widget::clearChildren() {
  // The SAME hazard takeChild degrades on, one frame up and with no result to
  // degrade: removeChild announces, the announcement runs application code, and
  // a slot may destroy this widget.  Every line of the loop below re-reads
  // children_, so takeChild returning nullptr safely is not enough on its own --
  // this frame has to be told too.  REM3-RES-4.
  detail::DeathWatch alive(this);
  // Back to front: reverse order of construction, and no element ever has to be
  // shifted down the vector.
  while (!children_.empty()) {
    const std::size_t before = children_.size();
    removeChild(children_.back().get());
    if (!alive.alive()) return;  // freed under us -- children_ included
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

  // Is THIS call a layout placing one of its own children?  Read HERE, before
  // the suspend below clears g_arrangeHost, and carried down as a parameter:
  // by the time latchNaturalSize runs, the pointer is deliberately null so the
  // application code onGeometryChanged runs is not mistaken for the layout
  // still writing, and re-reading it there would answer "no" for every call.
  const bool fromArrange = (parent_ != nullptr && parent_ == g_arrangeHost);

#ifndef NDEBUG
  // M2: an arrange may only write to its host's DIRECT children.
  assert((g_arrangeHost == nullptr || parent_ == g_arrangeHost) &&
         "Layout::arrange may only setGeometry on its host's direct children");
#endif
  const ArrangeSuspend suspend;

  if (visible_) update();  // repaint what we are vacating
  geometry_ = r;
  // The natural size is the first real size a widget was ever given BY HAND,
  // and after that it is frozen.  Both halves are decided in one place; the
  // test here is only the fast path out of a call that would do nothing.
  // ADR-R2-09.
  if (naturalSize_.isEmpty()) latchNaturalSize(fromArrange);
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
  //
  // buffersBusy_ for the same reason ~Widget tests it: a sizeHint() override is
  // as entitled to call setLayout as an onGeometryChanged is, and a measurement
  // is not a pass, so layoutRunning_ alone would free the object the outgoing
  // measure() is walking.
  if (layout_ && (layoutRunning_ || layout_->buffersBusy_)) {
    detail::parkLayout(layout_.release());
  }

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

void Widget::latchNaturalSize(bool fromArrange) {
  if (!naturalSize_.isEmpty()) return;  // already latched, and it is for life
  // ADR-R2-09 is one-way in BOTH directions.  It stopped a window dragged
  // smaller from ratcheting its contents down; latching the OUTPUT of a layout
  // pass is the same mistake pointing the other way -- a plain panel first
  // arranged into a 500 wide host would claim preferred.width = 500 for ever
  // and never fit back into a 120 wide one.  The natural size is the first
  // non-empty size a HUMAN gave this widget, which is what sizeHint()'s
  // contract says it is; a size a layout computed is not one.
  //
  // "A size a layout computed" is the geometry MY OWN PARENT'S ARRANGE just
  // wrote, which is what `fromArrange` reports.  It is NOT "any geometry
  // written while a pass is running somewhere in this process", which is the
  // question this used to ask -- detail::layoutPassActive() is a process-wide
  // flag, and the two answers differ for exactly the case that matters: a
  // container that positions its own children by hand from onGeometryChanged.
  // Those setGeometry calls ARE by hand, but they happen underneath the pass
  // that sized the container, so the old test threw every one of them away.
  // The children then reported preferred = 0x0 for the rest of their lives, and
  // the day that container was dropped into a layout they were arranged at
  // nothing.
  if (fromArrange) return;
  const Size s = geometry_.size();
  if (s.isEmpty()) return;  // nothing worth remembering yet
  naturalSize_ = s;
}

// M-1: layoutRect() is a DOOR, and it is the one the enumeration in section
// 11.4 did not have.
//
// It qualifies under P1 for the plainest possible reason -- it is a virtual
// call on a widget -- and it is protected rather than private precisely so that
// applications override it: its own declaration invites them to (a container
// that draws decoration of its own hands back the inside of that decoration).
// An override is application code, application code may destroy widgets, and
// the widget it is most likely to reach is the one it is a method of.
//
// GroupBox is the library's only override and it reads geometry and a string
// and nothing else, so nothing in the library can trigger this today.  "Today's
// only override" has never been an invariant in this family; it is the sentence
// that was written in front of every one of the five recurrences.
//
// WHY THE CHECK IS HERE and not left to the caller.  Everything after the door
// in THIS frame is a read through `this` -- layout_ twice -- and both of them
// run before control gets back to runLayoutIfAny.  The caller's guard covers
// the caller's frame; it cannot cover a read that already happened.  So the
// caller's check (which it needs anyway, for its own reads) is NECESSARY and
// NOT SUFFICIENT, and a comment here declaring liveness to be the caller's
// business would have been a comment that is false.
//
// WHY NOT PRE-READ THE MARGINS INSTEAD.  This is the one site in the family
// where pre-reading would actually work -- everything after the door is a read,
// and section 11.0's objection to pre-reading is that it cannot cover a WRITE.
// It is rejected because it changes the healthy path: layoutRect() is
// application code and is entitled to call setMargins() or setLayout(), and a
// hoisted read would then arrange with the margins from before the call.  That
// is a behaviour change with no defect pushing it, and it would ADD a cross-door
// tear of exactly the kind registered as REM3-RES-5 rather than remove one.  A
// cursor costs seven instructions on a path that is about to run a whole
// arrange.
//
// frameDegraded() once, here, per REM3-G8 -- and deliberately NOT a second time
// at the caller's check; see the note there.
Rect Widget::contentRect() const {
  const detail::DeathWatch self(this);
  const Rect r = layoutRect();
  if (!self.alive()) {
    // REM3-G1: `r` is legal because it is a local of THIS frame, copied out of
    // the call at the door -- the same standing ScrollArea::relayout's `h` has.
    // Nothing else here is: layout_ is a read of a freed Widget.
    //
    // The value is dead on arrival anyway.  runLayoutIfAny is the only caller
    // and it asks its own guard on the very next line, so no rectangle this
    // branch returns is ever consumed.  It is returned rather than fabricated
    // because fabricating one would be inventing an answer for an object that
    // no longer has one.
    detail::frameDegraded();
    return r;
  }
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
  bool refused = false;
  do {
    // Application code run by the previous round is entitled to take the layout
    // off this widget outright (adoptLayout(nullptr)).  There is then nothing
    // to arrange, and `running` below would be a null dereference.
    if (!layout_) break;
    layoutDirty_ = false;
    const Rect content = contentRect();
    // M-1's other half.  contentRect() calls layoutRect(), which is a protected
    // virtual an application may override -- a door, see the note on
    // contentRect -- and everything below this line reads a member: layout_ on
    // the next statement, then `*this` handed to arrangeFor.  `content` is
    // already a local copy, so it survives; nothing else here does.
    //
    // The guard for it was constructed four lines up and covers the whole
    // function, so closing this is one line.  That is not luck: LayoutGuard is
    // on the stack because arrange() is a door too, and a frame that already
    // holds a cursor pays nothing extra for a second question.
    //
    // NO frameDegraded() here, and it is a decision rather than an omission.
    // The counter exists because giving up is otherwise an ABSENCE -- a frame
    // returning void or a fabricated hint leaves no trace (Layout.hpp, next to
    // the counter).  This frame's give-up is not an absence: it is IN THE
    // RETURN VALUE.  That is the whole of the exception, now written into the
    // rule itself rather than only here, and this function is its only instance
    // in the library.
    //
    // Not "because setGeometry consumes it": that is one caller of three.
    // performLayout (below) and markLayoutDirty discard the bool, and they are
    // safe for an unrelated reason -- each ends immediately after this call, so
    // there is no member access after the door for the result to protect.  The
    // exemption rests on the value being VISIBLE to a caller, not on any caller
    // looking at it.
    //
    // Recording it as well would also make this check disagree with the
    // identical one after arrangeFor below, which has been silent since R2.
    if (!guard.alive()) return false;
    // Captured, not re-read: application code run from inside arrange() may
    // replace this widget's layout, and the result below belongs to the object
    // that PRODUCED it, not to whatever is installed by the time it returns.
    // adoptLayout parks the outgoing object, so this pointer stays valid until
    // the outermost pass unwinds.
    Layout* const running = layout_.get();
    const Widget* savedArrange = g_arrangeHost;
    g_arrangeHost = this;
    const LayoutOverflow result = running->arrangeFor(*this, content, refused);
    g_arrangeHost = savedArrange;
    // arrange() ran application code; this widget may be gone.  Nothing below
    // may touch a member, layout_ included -- it died with us.
    if (!guard.alive()) return false;
    // Nothing ran, so there is nothing to record: `result` is the PREVIOUS
    // pass's overflow, and the loop condition below would read a dirty flag
    // this function has already cleared.  Handled after the loop.
    if (refused) break;
    running->lastOverflow_ = result;
    // At most ONE re-run.  A layout that has not settled after seeing its own
    // output once will not settle on the third try either; it will just burn a
    // frame budget doing it.
  } while (layoutDirty_ && ++rounds < 2);

  if (refused) {
    // ADR-R2-04: the layout declined because one of its own measurements is on
    // the stack and refilling the buffers it is walking would free them under
    // it.  The REQUEST does not go away with the pass -- the dirty flag is put
    // back rather than cleared, and Layout::measureFor re-issues the pass as
    // soon as the measurement that was in the way returns.  Leaving the flag
    // clear here is what used to lose the pass outright: the widget kept the
    // geometry of the previous one and nothing anywhere remembered it was owed
    // a new one.  Not counted as non-convergence either; nothing diverged.
    layoutDirty_ = true;
    layoutRunning_ = false;
    return true;
  }
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
