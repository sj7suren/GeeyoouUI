#pragma once
//
// Layout: the strategy object a Widget hands its content rectangle to.
//
// A Widget owns at most one Layout (Widget::setLayout), and the Layout places
// that widget's DIRECT CHILDREN inside the rectangle it is given.  Nothing
// else: it does not create widgets, it does not reach past one level, and it
// does not run application code.  Everything that could re-enter the tree is
// deliberately outside it -- see the four mechanisms documented on
// Widget::runLayoutIfAny (docs/iterations/02-layout-engine.md).
//
// TWO STRUCTURAL RULES, both of them load-bearing:
//
//   1. A Layout MUST NOT hold a Widget* -- host() is the single exception, and
//      it is bound once and never rebound.  Child items are identified by their
//      INDEX in host()->children().  A wrong index puts a widget in the wrong
//      place: visible, and a test catches it.  A stale pointer is a
//      use-after-free: invisible, and it surfaces in a control room weeks
//      later.  This is worth the occasional index fix-up, and it is the first
//      thing to check in review of any Layout subclass.
//
//   2. measure() is a pure function of the tree.  It may not call setGeometry
//      and may not touch the host's geometry.  arrange() is the only writer.
//
#include <cstddef>
#include <cstdint>

#include "geeyoou/core/Types.hpp"

namespace geeyoou {

class Layout;
class Widget;

namespace detail {

// A layout whose host was destroyed, or which was replaced on its host, while
// its own arrange() was still on the stack.  Freeing it there would pull items_
// and the scratch buffers out from under that arrange; instead it is PARKED and
// released once no layout pass remains anywhere on the stack.  See the note
// above the park list in src/widget/Widget.cpp -- these two are its plumbing,
// and Widget is their only caller.
//
// parkLayout() takes ownership; the caller must have released it from whatever
// unique_ptr held it.
void parkLayout(Layout* l);
void releaseParkedLayouts();

}  // namespace detail

// The upper bound a widget uses to say "no limit".  A large finite number
// rather than infinity: sizes get added, subtracted and scaled all over a
// layout pass, and inf - inf is a NaN that then poisons every rectangle it
// touches with no way to trace where it came from.  1e7 logical pixels is
// ~5000 4K screens wide, so nothing real ever reaches it.
inline constexpr float kUnbounded = 1.0e7f;

// What a widget will accept along each axis.  `preferred` is what it wants,
// `min` what it cannot go below, `max` what it refuses to grow past.
struct SizeHint {
  Size min{0.0f, 0.0f};
  Size preferred{0.0f, 0.0f};
  Size max{kUnbounded, kUnbounded};
};

// How eagerly an item takes leftover space, per axis.  0 = not at all.
struct Stretch {
  std::uint16_t h = 0;
  std::uint16_t v = 0;
};

struct Margins {
  float left = 0.0f;
  float top = 0.0f;
  float right = 0.0f;
  float bottom = 0.0f;
};

// What did not fit.  REPORTED, never enforced: no assert, no exception, and --
// this is the important one -- no signal.  Emitting from here would run
// application code in the middle of a layout pass, which is precisely the
// re-entrancy that M1 and M2 exist to make impossible.  A caller that cares
// reads Widget::lastLayoutOverflow() after the pass has finished.
struct LayoutOverflow {
  float widthShort = 0.0f;   // > 0 = this many logical pixels still needed
  float heightShort = 0.0f;
  // Items that did not end up entirely inside the content rectangle.  Phrased
  // as "did not fit" rather than "got less than its minimum" because the
  // shortage strategy of every layout here is to place items at their MINIMUM
  // and let the tail run off the end: squeezing a control below the size it
  // declared it needs produces an unreadable widget, whereas a clipped one is
  // obviously wrong and the counter says so.
  int clippedCount = 0;

  bool any() const;
};

class Layout {
 public:
  virtual ~Layout() = default;

  Layout(const Layout&) = delete;
  Layout& operator=(const Layout&) = delete;

  // What the host would need to satisfy this layout.  Pure: no setGeometry, no
  // reads of the host's own geometry (that would be circular -- the geometry is
  // the result of the previous arrange).
  virtual SizeHint measure(const Widget& host) const = 0;

  // Places host's direct children inside `content`, which is in HOST-LOCAL
  // coordinates and already has the margins taken out -- and, for a host that
  // draws decoration of its own, its frame too (Widget::layoutRect; GroupBox
  // hands back the area inside its border and under its title rule).  It is
  // therefore NOT always at the origin.  Returns whatever did not fit.
  //
  // EVERY setGeometry in here runs application code, which may destroy the host
  // or replace this layout on it.  An implementation MUST therefore check
  // hostAlive() after every one of them and return immediately when it goes
  // false -- `host` is a dangling reference from that moment on.
  virtual LayoutOverflow arrange(Widget& host, const Rect& content) = 0;

  // Whether this layout still has a host to place children into.
  //
  // Goes false in exactly two places, both of them from inside this layout's
  // own arrange(): the host was destroyed, or the host was given a different
  // layout.  In both cases the object itself is kept alive until the outermost
  // pass unwinds -- the arrange on the stack is still reading its item list --
  // but the `host` reference it was handed is not, and the tree below it is no
  // longer this layout's business.
  bool hostAlive() const { return host_ != nullptr; }

  void setMargins(const Margins& m);
  const Margins& margins() const { return margins_; }

  void setSpacing(float px);
  float spacing() const { return spacing_; }

  // Bound once, by Widget::setLayout, and never rebound: a Layout cannot be
  // moved between widgets.  Null only between construction and adoption.
  Widget* host() const { return host_; }

  // Result of the most recent arrange().  Empty before the first one.
  const LayoutOverflow& lastOverflow() const { return lastOverflow_; }

 protected:
  Layout() = default;

  // "Something I depend on changed; re-run me."  Subclasses call this from
  // their own setters, exactly as setMargins/setSpacing do.
  void invalidate();

  // Hooks.  onChildAppended / onChildRemoved exist so a subclass that keeps
  // per-item state (stretch factors, grid cells) can keep it aligned with the
  // child vector -- `index` is the position the child occupied, so everything
  // at or after it has shifted down by one.
  virtual void onInvalidated() {}
  virtual void onChildAppended() {}
  virtual void onChildRemoved(std::size_t index) { (void)index; }

 private:
  friend class Widget;
  friend void detail::parkLayout(Layout*);
  friend void detail::releaseParkedLayouts();

  // The two entry points Widget uses, and the ONLY ones that are re-entrancy
  // safe.  measure() and arrange() share one set of mutable scratch buffers in
  // every subclass here -- that is what keeps a stable layout at zero
  // allocations per pass -- so a measurement that starts while this layout is
  // already measuring or arranging rewrites the numbers the outer pass is still
  // reading, and reallocates them out from under its pointers.
  //
  // Application code reaches that door routinely and legitimately: a child's
  // onGeometryChanged asking its container how big it wants to be is the whole
  // point of sizeHint(), and ScrollArea::relayout does exactly that.  So a
  // re-entrant measurement answers with the last completed one instead.  It is
  // the number the outer pass is working from anyway, and it terminates the
  // recursion; the alternative, measured on a 3x3 grid, was every cell but the
  // first landing on the same point.
  SizeHint measureFor(const Widget& host) const;
  LayoutOverflow arrangeFor(Widget& host, const Rect& content);

  Widget* host_ = nullptr;
  // Set only while this layout is destroyed-but-still-on-the-stack; see the
  // park list in Widget.cpp.  One pointer, and only on widgets that own a
  // layout at all.
  Layout* deferredNext_ = nullptr;
  Margins margins_;
  float spacing_ = 6.0f;
  // Lives here rather than in the Widget: it is the result of THIS object's
  // last arrange(), and 12 bytes on every widget in the tree -- the vast
  // majority of which will never own a layout -- buys nothing.  Widget
  // forwards lastLayoutOverflow() to it.
  LayoutOverflow lastOverflow_;
  // The answer a re-entrant measureFor() hands back, and the latch that decides
  // when it does.
  mutable SizeHint lastMeasure_;
  mutable bool buffersBusy_ = false;
};

namespace detail {

// How many widgets in this process currently own a Layout.
//
// Every layout hook in Widget tests this FIRST.  The library's own 32 controls
// and all six showcase pages position their children by hand, so for them the
// entire engine costs one load of a hot global and one perfectly predicted
// branch -- no parent-chain walks, no calls, nothing.  Exposed as a variable
// rather than behind a function so the test really is inlined at the call site
// in Widget::add, which is a template in the header.
extern std::size_t g_layoutHosts;

// Diagnostics for the two ways a pass can end badly.  Recorded, never fatal: an
// HMI that has been up for six weeks must not die because one panel's layout
// oscillates by half a pixel.  There is no logger in this library, so a counter
// the application (and the test suite) can read is the record.
struct LayoutDiagnostics {
  std::uint32_t notConverged = 0;    // arrange kept re-dirtying itself
  std::uint32_t depthExceeded = 0;   // nested past kMaxTreeDepth
  std::uint32_t reentered = 0;       // arrange() reached from inside its own measure()
  std::uint32_t indexClamped = 0;    // a grid row/column index past what a form can be
  const Widget* lastNotConvergedHost = nullptr;
  const Widget* lastDepthExceededHost = nullptr;
};

const LayoutDiagnostics& layoutDiagnostics();
void resetLayoutDiagnostics();
void layoutNotConverged(const Widget* host);
void layoutDepthExceeded(const Widget* host);
void layoutReentered();
void layoutIndexClamped();

// True while any layout pass is on the stack.  Content-driven invalidation
// consults it: during a pass the running pass will pick the dirt up itself, and
// starting a second one from underneath it is the re-entrancy M1 forbids.
bool layoutPassActive();

// The host of the innermost pass, or null.  Debug diagnostics only.
const Widget* currentLayoutHost();

}  // namespace detail
}  // namespace geeyoou
