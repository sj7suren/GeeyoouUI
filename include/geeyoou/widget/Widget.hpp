#pragma once
//
// Widget: the node type of the UI tree.
//
// Ownership: a parent owns its children through unique_ptr; children hold a raw
// back-pointer.  Non-visual objects (data channels, protocol parsers, worker
// threads) deliberately do NOT live in this tree -- unlike Qt, where everything
// inherits QObject.  See docs/architecture.md section 3.3.
//
#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "geeyoou/core/Event.hpp"
#include "geeyoou/core/Signal.hpp"
#include "geeyoou/core/Types.hpp"
#include "geeyoou/render/StyleSheet.hpp"
#include "geeyoou/widget/Layout.hpp"

namespace geeyoou {

class Painter;
class Window;

// Declares a widget's type name to the style engine, and keeps type selectors
// inheritance-aware: with this macro a `PushButton { }` rule also styles
// IconButton and MenuButton, exactly as QSS does with QPushButton.
//
// Put it in the PUBLIC section of every widget that should be addressable by
// name from a style sheet.  A class that omits it is still styleable through
// its base's type name, its .class and its #id -- it just has no name of its
// own.  No RTTI is involved: this is a virtual call and a string compare.
#define GEEYOOU_STYLE_TYPE(Self, Base)                                 \
  const char* styleType() const override { return #Self; }             \
  bool styleMatchesType(std::string_view n) const override {           \
    return n == #Self || Base::styleMatchesType(n);                    \
  }

// Whether and how a widget can take keyboard focus.
enum class FocusPolicy : std::uint8_t {
  None,   // never focusable (labels, separators, decorative panels)
  Click,  // focusable by clicking, but skipped by Tab traversal
  Tab,    // focusable by clicking AND reachable with Tab / Shift+Tab
};

class Widget : public StyleSubject {
 public:
  Widget() = default;
  // Out of line, and NOT defaulted: it has to cancel any in-flight event bubble
  // standing on this widget.  The removal API announces that on its way through
  // (Widget.cpp, announceDetached), but a widget can die without ever passing
  // through it -- a Window is a stack object, the unique_ptr takeChild() hands
  // back can simply be dropped, and any subclass destructor gets here too.  The
  // bubble is what would then read a freed parent pointer.
  ~Widget() override;

  Widget(const Widget&) = delete;
  Widget& operator=(const Widget&) = delete;

  // Constructs a child in place and returns a non-owning pointer to it.
  template <class T, class... Args>
  T* add(Args&&... args) {
    static_assert(std::is_base_of_v<Widget, T>, "T must derive from Widget");
    auto owned = std::make_unique<T>(std::forward<Args>(args)...);
    T* raw = owned.get();
    raw->parent_ = this;
    // Depth is maintained here because this is the ONLY place in the library
    // where a widget is linked to a parent.  The subtree case is not
    // hypothetical: a container builds its own children in its CONSTRUCTOR --
    // ScrollArea does, AppWindow does -- so those nodes were numbered from zero
    // a moment ago and have to be rebased.  Paid only by containers, and only
    // once each.
    assert(depth_ + 1 < int(kMaxTreeDepth) && "widget tree deeper than kMaxTreeDepth");
    raw->depth_ = std::uint16_t(depth_ + 1);
    if (!raw->children_.empty()) raw->rebaseSubtreeDepth();
    children_.push_back(std::move(owned));
    raw->update();
    // One load and one predicted branch when nobody in the process uses a
    // layout, which is every widget this library ships today.
    if (detail::g_layoutHosts != 0) childAppended();
    return raw;
  }

  // --- removing children ---------------------------------------------------
  //
  // Hands `child` back to the caller, who becomes its owner; the subtree under
  // it comes along and its parent() becomes null.  Returns nullptr when `child`
  // is not one of ours -- including the case where a handler run during the
  // removal got to it first.
  //
  // Before anything is unlinked, EVERY node of the departing subtree is
  // announced to the Window (Window::widgetDetached), which drops any focus,
  // hover, mouse-grab or popup pointer aimed into it.  Per node rather than
  // just at the root, because those pointers routinely name a grandchild: a
  // root-only notification would leave the window dereferencing freed memory on
  // the very next event.
  //
  // Legal from inside an event handler or a slot -- an in-flight event bubble
  // that is standing on a removed widget is cancelled rather than left walking
  // into freed memory.  The one thing a slot still may NOT do is remove the
  // object that owns the signal it is running inside: that is contract D7 (see
  // core/Signal.hpp), and nothing here can make destroying a Signal mid-emit
  // safe.
  //
  // That licence extends to THIS widget: a handler the announcement runs may
  // destroy the parent whose takeChild is in flight.  The call then returns
  // nullptr and touches nothing -- neither this widget nor `child`, which was
  // owned by it and went with it.  Nothing is logged, asserted or thrown; it is
  // the ADR-R2-04 answer, and it is the same nullptr the call already returns
  // when a handler got to `child` first.
  std::unique_ptr<Widget> takeChild(Widget* child);

  // takeChild() and let the result die: `child` and its subtree are destroyed.
  void removeChild(Widget* child);

  // Removes every child, last one first -- reverse order of construction, the
  // same order the compiler would have used.
  //
  // Same licence and the same degradation as takeChild: a handler that destroys
  // this widget mid-drain ends the loop where it stands.  The children it had
  // not reached yet are destroyed anyway -- by ~Widget, which owns them.
  void clearChildren();

  // --- geometry (logical pixels, relative to the parent) -------------------
  //
  // CALLING THIS RE-ENTERS APPLICATION CODE.  It runs onGeometryChanged() on
  // the widget it moves, and an override there may destroy widgets -- the one
  // whose method is doing the calling included (AppWindow::relayout emits
  // contentResized from inside one, and a slot is entitled to destroy widgets).
  // So if your frame reads ANYTHING of its own after this call returns, the
  // CALLER'S obligation stated in Layout.hpp -- above measure()/arrange(), and
  // it binds every caller, not only Layout subclasses -- applies to you.
  void setGeometry(const Rect& r);
  const Rect& geometry() const { return geometry_; }
  Rect localRect() const { return {0.0f, 0.0f, geometry_.width(), geometry_.height()}; }
  Rect windowRect() const;
  Point mapToWindow(Point local) const;

  // THIS RE-ENTERS APPLICATION CODE TOO, and it is the least obvious of the
  // four: the call looks like a flag being flipped.  A hidden item is an item a
  // layout no longer holds space for, so this marks the chain dirty and the
  // topmost dirty host runs a pass -- arrange, setGeometry, onGeometryChanged,
  // application code, which may destroy widgets including the one whose method
  // is doing the calling.  AppWindow::setHeaderVisible is exactly that frame:
  // it calls this and then calls relayout() through `this`.
  //
  // So the CALLER'S obligation stated in Layout.hpp -- above measure()/
  // arrange(), fourth kind of caller -- applies to any frame that reads
  // anything of its own after this call returns.
  void setVisible(bool on);
  bool isVisible() const { return visible_; }

  // --- layout ---------------------------------------------------------------
  //
  // OPTIONAL, and off by default.  A widget with no Layout keeps the absolute
  // positioning this library was built on (docs/architecture.md section 4), and
  // pays nothing at all for the engine's existence.
  //
  // Constructs the layout in place, binds it to this widget for good, and runs
  // it once.  Replacing an existing layout destroys the old one; the children
  // keep whatever geometry they last had until the new layout arranges them.
  template <class L, class... Args>
  L* setLayout(Args&&... args) {
    static_assert(std::is_base_of_v<Layout, L>, "L must derive from Layout");
    auto owned = std::make_unique<L>(std::forward<Args>(args)...);
    L* raw = owned.get();
    adoptLayout(std::move(owned));
    return raw;
  }
  Layout* layout() const { return layout_.get(); }

  // What this widget would like to be.
  //
  // A widget that OWNS a layout answers with what that layout needs: it is a
  // container, and a container's size is a statement about its contents.  Any
  // other answer makes nesting impossible -- a GroupBox holding a GridLayout,
  // placed in a page's BoxLayout, would report the size somebody once gave it
  // by hand, and the whole tree would end up sized by construction order rather
  // than by what is in it.
  //
  // A widget with NO layout answers with its NATURAL size -- the first non-empty
  // size it was ever given -- with no lower bound and no upper one.
  //
  // It deliberately does NOT read geometry(): a widget's geometry is the OUTPUT
  // of the previous arrange, so measuring from it would make the definition
  // circular.  A window dragged smaller step by step would then shrink its
  // children, measure the shrunk children, shrink them again, and never recover
  // when the window was dragged back out.  See ADR-R2-09.
  //
  // CALLING THIS RE-ENTERS APPLICATION CODE TOO, and that is easy to miss
  // because the call looks like a pure query.  It is not: an override is
  // application code, a container forwards the question to its layout and the
  // layout asks every child, and any one of those overrides may destroy widgets
  // -- the container that asked included.  A frame that reads its own members
  // after asking for a sizeHint() carries the CALLER'S obligation in Layout.hpp
  // (above measure()/arrange()), whether or not it is a Layout.
  virtual SizeHint sizeHint() const;

  // "What I want has changed" -- a Label whose text was replaced, a list that
  // gained rows.  Marks this widget's layout chain dirty and, unless a pass is
  // already running, re-runs the topmost dirty host once.
  void invalidateSizeHint();

  // Runs this widget's layout now.  No-op without one.
  //
  // NOT called relayout(): AppWindow, ScrollArea and the showcase's Shell each
  // already have a relayout() of their own, and ScrollArea's is private -- so a
  // base-class member of that name would be statically hidden by three of the
  // library's four containers and inaccessible through a fourth.  Renaming the
  // one method with no call sites is cheaper than renaming a published API.
  void performLayout();

  // What did not fit in the last pass.  All zeroes when there is no layout.
  const LayoutOverflow& lastLayoutOverflow() const;

  // --- enabled state ------------------------------------------------------
  // A disabled widget takes no input and neither do its descendants: disabling
  // a GroupBox greys out the whole parameter block, which is exactly what an
  // interlocked HMI screen needs.
  void setEnabled(bool on);
  bool isEnabled() const { return enabled_; }
  bool isEffectivelyEnabled() const;

  // --- focus --------------------------------------------------------------
  void setFocusPolicy(FocusPolicy p) { focusPolicy_ = p; }
  FocusPolicy focusPolicy() const { return focusPolicy_; }
  bool isFocusable() const;

  void setFocus();
  void clearFocus();
  bool hasFocus() const;

  // --- repaint ------------------------------------------------------------
  // Marks this widget (or a sub-region of it) dirty.  Cheap and safe to call
  // often -- the dirty region is coalesced and the actual repaint happens once
  // per platform paint cycle.
  void update();
  void update(const Rect& localRect);

  // --- tree ---------------------------------------------------------------
  Widget* parent() const { return parent_; }
  // Distance from the root, maintained by add<T>.  A root -- a Window, or any
  // widget that was never added to a parent -- is 0.  Bounded by kMaxTreeDepth,
  // which debug builds assert.
  std::uint16_t depth() const { return depth_; }
  Window* window();
  const std::vector<std::unique_ptr<Widget>>& children() const { return children_; }

  // --- style identity -----------------------------------------------------
  //
  // The three things a selector can address, mirroring QSS: the type name
  // (declared with GEEYOOU_STYLE_TYPE), an object name (`#startPump`) and any
  // number of style classes (`.danger`).  All optional -- a widget with none of
  // them is still styled by its type and by `*`.
  void setObjectName(std::string name);
  const std::string& objectName() const { return objectName_; }

  // Space-separated, replacing whatever was there: setStyleClasses("danger big")
  void setStyleClasses(std::string_view spaceSeparated);
  void addStyleClass(std::string cls);
  void removeStyleClass(std::string_view cls);
  bool hasStyleClass(std::string_view cls) const;
  const std::vector<std::string>& styleClasses() const { return classes_; }

  // Properties the active style sheet resolves for this widget in `state`.
  // Cached against the global style generation, so the cascade runs once per
  // widget per skin change rather than once per paint.
  const StyleProps& style(StyleState state = StyleState::None) const;

  // --- StyleSubject -------------------------------------------------------
  virtual const char* styleType() const { return "Widget"; }
  bool styleMatchesType(std::string_view n) const override { return n == "Widget"; }
  bool styleMatchesClass(std::string_view c) const override { return hasStyleClass(c); }
  const std::string& styleObjectName() const override { return objectName_; }
  const StyleSubject* styleParentSubject() const override { return parent_; }
  // What the base class can know on its own.  A widget that also tracks hover
  // or pressed passes those in through style(state) when it paints; this is the
  // fallback used when the widget appears as an ANCESTOR in a descendant rule.
  StyleState styleState() const override;

  // --- scrolling ----------------------------------------------------------
  // Children are laid out at (their geometry - contentOffset), so a container
  // scrolls its contents by moving this rather than by rewriting every child's
  // geometry.  ScrollArea is the intended user; nothing stops a custom
  // container from driving it directly.
  void setContentOffset(Point offset);
  Point contentOffset() const { return contentOffset_; }

  // --- internal dispatch (called by Window) -------------------------------
  // `clipInWindow` is the accumulated visible region of every ancestor.  It is
  // what actually clips a child to its parent -- widget bounds alone would let
  // a scrolled row draw straight over the container that owns it.
  void paintTree(Painter& p, const Rect& dirtyInWindow, const Rect& clipInWindow);
  Widget* hitTest(Point windowPos);
  void dispatchMouse(const MouseEvent& windowEvent);
  void dispatchKey(const KeyEvent& e);
  void collectFocusable(std::vector<Widget*>& out);
  void animationTickTree();

 protected:
  // `dirtyLocal` is the damaged region in this widget's own coordinate space.
  // The painter is already translated to the widget origin and clipped to its
  // bounds, so a widget can always draw as if it owned the whole surface.
  virtual void onPaint(Painter& p, const Rect& dirtyLocal) { (void)p; (void)dirtyLocal; }
  virtual void onMouse(const MouseEvent& e) { (void)e; }
  virtual void onKey(const KeyEvent& e) { (void)e; }
  virtual void onFocusChanged(bool focused) { (void)focused; update(); }
  virtual void onEnabledChanged() { update(); }
  // An override here IS the application code the rest of the library keeps
  // warning about -- AppWindow::onGeometryChanged calls relayout(), which emits
  // contentResized, and a slot may destroy widgets.  Two obligations, pointing
  // in opposite directions, and both of them are the CALLER'S contract in
  // Layout.hpp (above measure()/arrange(), fourth kind of caller):
  //
  //   * whoever CALLS this -- Widget::setGeometry does, from inside a frame
  //     that goes on to read visible_ -- must be able to tell afterwards that
  //     it still has something to come back to;
  //   * an override that itself calls setGeometry / setVisible / sizeHint and
  //     then reads its own members is a caller in exactly the same sense, and
  //     carries the same obligation.
  virtual void onGeometryChanged() {}

  // The rectangle this widget's Layout is arranged into, BEFORE the layout's
  // own margins are taken out of it.  The whole widget for almost everything.
  //
  // A container that draws decoration of its own overrides it and hands back
  // the inside of that decoration -- GroupBox returns the area under its title
  // rule.  The alternative is telling every call site the frame's dimensions
  // (setMargins({12, 34, 12, 12}) at every GroupBox in the application), which
  // puts a private constant of one widget into every file that uses it, and
  // draws the first row over the title on the day somebody forgets.  Margins
  // then compose on top: they stay what the AUTHOR asked for, and the frame
  // stays what the WIDGET knows.
  virtual Rect layoutRect() const { return localRect(); }

  // Called on every visible widget when Window::enableAnimations() is on.
  // Default is a no-op, and an animating widget must call update() ITSELF --
  // the tick alone never repaints anything, so an idle screen stays idle even
  // with the animation clock running.
  //
  // One timer owned by the Window, walked over the tree, rather than a timer
  // per widget: docs/architecture.md forbids widgets owning timers, and a
  // registry of animated widgets would need unregistration on destruction.
  virtual void onAnimationTick() {}

 private:
  virtual Window* asWindow() { return nullptr; }

  // --- layout internals -----------------------------------------------------
  void adoptLayout(std::unique_ptr<Layout> l);
  // Returns false when application code destroyed this widget during arrange();
  // the caller must then touch nothing else, `this` included.
  bool runLayoutIfAny();
  // layoutRect() with the layout's margins taken out, in this widget's own
  // coordinate space -- which is the space its children's geometry lives in.
  Rect contentRect() const;
  void markLayoutDirty();
  void childAppended();
  void childRemoved(std::size_t index);
  // `fromArrange` is "this geometry was written by my own parent's layout",
  // which is the one source a natural size may never come from.  Decided by
  // setGeometry, which is the only caller and the only place that can still
  // tell -- see the definition.
  void latchNaturalSize(bool fromArrange);
  void rebaseSubtreeDepth();

  Rect geometry_;
  Point contentOffset_;
  Widget* parent_ = nullptr;
  std::vector<std::unique_ptr<Widget>> children_;
  // Declared after children_ so it is DESTROYED BEFORE THEM.  A layout that
  // outlived the widgets it indexes would be holding stale positions during the
  // rest of teardown; this way it is simply gone first.
  std::unique_ptr<Layout> layout_;
  bool visible_ = true;
  bool enabled_ = true;
  FocusPolicy focusPolicy_ = FocusPolicy::None;
  // These four live here on purpose: they fit exactly in the padding that
  // already followed focusPolicy_, so the layout engine's flags cost zero
  // bytes per widget.  See the size budget assert in Widget.cpp.
  bool layoutRunning_ = false;   // M1: a pass is inside this host right now
  bool layoutDirty_ = false;     // something invalidated it since the last pass
  std::uint16_t depth_ = 0;      // distance from the root; maintained by add<T>

  std::string objectName_;
  std::vector<std::string> classes_;
  // One-entry cache.  A widget paints in one state per frame, so a single slot
  // hits essentially always -- a map would cost more than the cascade it saves.
  mutable StyleProps styleCache_;
  mutable StyleState styleCacheState_ = StyleState::None;
  mutable std::uint64_t styleCacheGen_ = 0;
  // Latched ONCE, from the first non-empty geometry this widget is ever given
  // (or when it is first taken into a layout, whichever happens first), and
  // never written again -- see sizeHint().
  Size naturalSize_;

  friend class Window;
};

namespace detail {

// --- a frame standing on a widget it does not own ----------------------------
//
// ONE in-flight stack frame that handed control to application code and is
// going to carry on touching the widget it was standing on when that code
// returns.  Application code is entitled to destroy that widget (contract D7 in
// core/Signal.hpp lets a slot destroy other objects), so the frame needs to be
// able to ask, in one pointer compare, whether it still has anything to come
// back to.
//
// Moved here from Widget.cpp's anonymous namespace UNCHANGED.  The reason it
// had to move: the frames that need it are no longer only in Widget.  A
// container's own sizeHint() and its own relayout() call into application code
// and then read themselves -- GroupBox and ScrollArea both do -- and those are
// widget SUBCLASSES, compiled in their own translation units.  A guard is a
// pure stack object, so its complete type has to be visible where the frame is
// written.  Three of the four lists stay private to Widget.cpp; only the one
// below needs external linkage.
//
// The mechanism's full argument -- why a cursor rather than pre-reading what
// the frame needs, and why one mechanism with four lists rather than four
// hand-rolled checks -- is in Widget.cpp, next to the three private lists.
struct LiveCursor {
  Widget* node = nullptr;
  LiveCursor* outer = nullptr;
};

// The list is a TEMPLATE parameter rather than a constructor argument, so each
// kind of frame gets a distinct type and cannot be threaded onto the wrong list
// by a typo.  A reference template argument may name an internal-linkage
// variable, which is what lets three of the four lists stay in Widget.cpp's
// anonymous namespace now that this template does not.
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

// Frames cancelled by DESTRUCTION and by nothing else -- "dead, not merely
// detached".
//
// Named after the cancellation POLICY, not after a door.  A takeChild has never
// called sizeHint() and a measurement has never detached anything; what those
// frames have in common is not where they are written but what must cancel
// them, and a detach must NOT -- takeChild hands its subtree back alive, and a
// frame that gave up on a widget which never died has degraded for nothing.
// The two lists that announceDetached cancels as well (g_bubbles, g_geometries)
// are named after their frame kind instead, because for those two the two
// namings agree.
//
// NOTHING reads this list in order to DECIDE anything.  alive() on the guards
// is the whole of it; deathWatchDepth() below reads the depth, but only tests
// read that and no branch in the library turns on it.  That is a PRECONDITION,
// not an observation: the day something needs to ask "is a frame in flight on
// X?" and then behave differently, this list SPLITS first.  The counter-example
// is already in the library -- see the three readers of g_layouts documented in
// Widget.cpp, each of which turns the answer into a different engine action.
//
// MAINTENANCE, and it is the whole reason for the name: this list is correct
// only while every frame on it wants the SAME cancellation policy.  A future
// site that needs "cancel on detach too" does not belong here -- it belongs on
// g_bubbles / g_geometries, or on a list of its own.
extern LiveCursor* g_deathWatch;

// The guard for that list, and the only one a widget subclass ever needs.
//
// PRIVATE inheritance plus one using-declaration: alive() is exported, node()
// and moveTo() deliberately are not.  That is what makes the const_cast below
// safe by CONSTRUCTION rather than by review -- a cursor whose pointer cannot
// be obtained cannot be dereferenced, and cancelOn only ever COMPARES it.  Nor
// is the widget a const object: const is a property of the path a const member
// function reached it by (GroupBox::sizeHint() is one), the object itself lives
// in its parent's child vector, and nothing here writes through the cast.
//
// A null widget is DEFINED as already dead: alive() is false from here on,
// never undefined and never a crash.  It is still a caller bug, because every
// site that guards a pointer is about to dereference it -- a null MEMBER is the
// site's own null check to make, not something a guard may impersonate.
class DeathWatch : private LiveGuard<g_deathWatch> {
 public:
  explicit DeathWatch(const Widget* w)
      : LiveGuard<g_deathWatch>(const_cast<Widget*>(w)) {
    assert(w && "a null here means the caller's own null check is missing");
  }

  using LiveGuard<g_deathWatch>::alive;
};

// How many death-watch frames are on the stack.  DIAGNOSTIC ONLY -- nothing in
// the library branches on it -- and it exists because a list that does not come
// back to zero means a guard outlived its own frame, which nothing else can
// observe.
std::size_t deathWatchDepth();

// A guarded frame found the tree moved under it and gave up.  Recorded, never
// fatal (ADR-R2-04): it is what lets a test assert a POSITIVE fact instead of
// "it did not crash".  The counter lives in LayoutDiagnostics (Layout.hpp),
// with the four the layout engine already keeps.
//
// ONE call per FRAME, not one per check, and it covers both reasons a frame
// bails out: a cancelled cursor, and a member pointer that changed across the
// door.  Named after neither of them on purpose.
void frameDegraded();

}  // namespace detail
}  // namespace geeyoou
