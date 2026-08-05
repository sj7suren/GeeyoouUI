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
// its own arrange() OR its own measure() was still on the stack.  Freeing it
// there would pull items_ and the scratch buffers out from under that call;
// instead it is PARKED and released once nothing is reading it any more.  See
// the note above the park list in src/widget/Widget.cpp -- these are its
// plumbing, and Widget is parkLayout's only caller.
//
// "Nothing is reading it" is TWO counters, not one: the arrange depth Widget's
// LayoutGuard keeps, and the measure depth Layout::measureFor keeps.  A pure
// measurement -- ScrollArea::relayout asking its content for a size hint -- is
// not a layout pass, and a park list drained on the arrange count alone would
// free the layout the measurement is standing in.  releaseParkedLayouts()
// therefore refuses while a measurement is on the stack, and measureFor calls
// it again on its own way out.
//
// parkLayout() takes ownership; the caller must have released it from whatever
// unique_ptr held it.
void parkLayout(Layout* l);
void releaseParkedLayouts();

// How many layouts are parked right now.  Diagnostic: the park list is a
// deferred-free queue, and a queue that only ever grows is a leak -- which is
// what the soak harness (tests/widget/test_layout_soak.cpp) samples it for.
std::size_t parkedLayoutCount();

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

  // --- what a subclass implements ---------------------------------------------
  //
  // PROTECTED, not public, and that is a safety property rather than tidiness.
  // The two of them share one set of scratch buffers in every subclass here --
  // that is what keeps a stable layout at zero allocations per pass -- and the
  // latch that stops one from refilling the buffers the other is walking lives
  // in measureFor/arrangeFor below.  Public, they were reachable as
  // `w->layout()->measure(*w)` straight past that latch, which is a
  // heap-use-after-free from ordinary application code and no amount of
  // checking inside the subclass can reach it.  Ask the WIDGET instead:
  // Widget::sizeHint() and Widget::performLayout() are the doors, and they go
  // through the latch.
  //
  // RE-ENTRANCY IS THE WHOLE CONTRACT OF BOTH OF THEM.  Every call into the
  // tree runs application code, and application code may do anything the public
  // API allows -- destroy the host, replace this layout on it, add or remove
  // children.  There are TWO such calls, not one:
  //
  //   * setGeometry(), from arrange();
  //   * sizeHint(), from arrange() AND from measure() -- an application-supplied
  //     override that is as entitled to remove a widget as any handler is, and
  //     removing the host is one of the widgets it may remove.
  //
  // So the rule is not "check hostAlive() after every setGeometry".  It is:
  // AFTER EVERY CALL THAT RE-ENTERS APPLICATION CODE, sizeHint() INCLUDED,
  // check that what the rest of the frame is going to touch is still there, and
  // give up immediately when it is not.
  //
  // WHOM THIS BINDS -- and this is the half that was wrong rather than merely
  // narrow.  It used to read "an implementation of Layout".  But crossing into
  // application code is a property of the CALL, so the obligation belongs to
  // whoever MAKES the call, not to whoever happens to implement this interface.
  // FOUR kinds of caller, and only the first one is a Layout:
  //
  //   1. a Layout implementation, between its own calls.  BoxLayout::gather and
  //      GridLayout::measureAxis shipped with no check at all under the narrower
  //      wording, which is why it was widened once already;
  //   2. a container's own sizeHint(), which forwards the question to its layout
  //      -- application code, by the second bullet above -- and then goes on
  //      composing an answer out of its own members (GroupBox does);
  //   3. a container's own geometry maintenance: relayout(), setContentSize()
  //      and the like, which measure a child and then write geometry THROUGH
  //      THEIR OWN MEMBER POINTERS (ScrollArea, AppWindow, WindowHeader).  Note
  //      what is at stake here and not in 1-2: those are WRITES into memory the
  //      frame no longer owns, and pre-reading cannot cover a write;
  //   4. anyone who calls a protected virtual geometry hook -- setVisible(),
  //      performLayout(), onGeometryChanged() -- and then reads itself.
  //
  // Cases 2-4 have no hostAlive() to ask, because they are not layouts.  They
  // ask geeyoou::detail::DeathWatch (Widget.hpp) instead: the same one-pointer
  // question, spelled for a frame that is standing on a WIDGET rather than
  // holding a Layout.
  //
  // WHAT "GIVE UP" MEANS, because the answer is not "clean up" -- there is
  // nothing left to clean up, and unwinding is not available either (ADR-R2-04:
  // record, never abort):
  //
  //   * touch NOTHING reachable through what died -- no member, no virtual
  //     call, no geometry(), no localRect(), no style(), no layout().  For a
  //     Layout that is `host`, which is a dangling REFERENCE from that moment
  //     on; this object itself survives, because Widget parks it, and that buys
  //     the chance to notice and nothing else;
  //   * a frame returning void returns immediately.  A frame returning a value
  //     may build it ONLY out of locals captured before the call and out of
  //     constants -- never out of a fresh read;
  //   * call detail::frameDegraded() once, per FRAME that gives up rather than
  //     per check, so that giving up is a recorded fact instead of an absence.
  //
  // Widget::sizeHint() and Widget::setGeometry() carry a pointer back to this
  // paragraph: the next person to be bitten by it will be reading Widget.hpp,
  // not this file.  The library does not yet honour the contract at every one
  // of its own call sites; the enumeration of which sites, and what is planned
  // for each, is in docs/iterations/02-layout-engine.md section 11.4.

  // What the host would need to satisfy this layout.  Pure: no setGeometry, no
  // reads of the host's own geometry (that would be circular -- the geometry is
  // the result of the previous arrange).
  virtual SizeHint measure(const Widget& host) const = 0;

  // Places host's direct children inside `content`, which is in HOST-LOCAL
  // coordinates and already has the margins taken out -- and, for a host that
  // draws decoration of its own, its frame too (Widget::layoutRect; GroupBox
  // hands back the area inside its border and under its title rule).  It is
  // therefore NOT always at the origin.  Returns whatever did not fit.
  virtual LayoutOverflow arrange(Widget& host, const Rect& content) = 0;

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
  friend std::size_t detail::parkedLayoutCount();

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

  // `refused` reports the re-entrant case ABOVE back to the caller, because the
  // geometry handed back when it happens is the LAST pass's, not this one's --
  // indistinguishable from a pass that ran and changed nothing.  Widget::
  // runLayoutIfAny clears the dirty flag before it gets here, so without this
  // out-parameter the request that raised the flag was simply swallowed: the
  // pass never ran, nothing remembered that it was owed, and the widget kept
  // the size of the previous one for good.  A bool rather than an optional so
  // the ordinary path stays a plain return of a 12-byte struct.
  LayoutOverflow arrangeFor(Widget& host, const Rect& content, bool& refused);

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
  // when it does.  buffersBusy_ is read by Widget too: a host destroyed while
  // one of its own measurements is on the stack has to park this object exactly
  // as a host destroyed mid-arrange does, and layoutRunning_ -- which is about
  // arranging -- is false in that case.
  mutable SizeHint lastMeasure_;
  mutable bool buffersBusy_ = false;
  // An arrangeFor() that was refused while the buffers were busy.  The refusal
  // is not a cancellation: whoever asked still wants the pass, so the
  // measurement that was in the way re-runs it on its way out.
  mutable bool arrangeDeferred_ = false;
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
  // A frame found the tree moved under it and gave up: the caller's contract
  // above measure()/arrange().  Counted here with the other four because it is
  // the same kind of fact and the same reader wants it, but raised by
  // detail::frameDegraded(), which is declared in Widget.hpp -- next to the
  // guard that decides it, and where the frames that raise it are written.
  std::uint32_t framesDegraded = 0;
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
