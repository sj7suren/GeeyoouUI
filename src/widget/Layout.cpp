#include "geeyoou/widget/Layout.hpp"

#include "geeyoou/widget/Widget.hpp"

namespace geeyoou {

bool LayoutOverflow::any() const {
  return widthShort > 0.0f || heightShort > 0.0f || clippedCount > 0;
}

// ---------------------------------------------------------------- Layout ---
void Layout::setMargins(const Margins& m) {
  margins_ = m;
  invalidate();
}

// Not clamped at zero: a negative spacing (items overlapping by a hairline, so
// two adjacent borders read as one) is a legitimate thing to ask for, and the
// arrange implementations already have to cope with a content rectangle too
// small for their items anyway.
void Layout::setSpacing(float px) {
  spacing_ = px;
  invalidate();
}

void Layout::invalidate() {
  onInvalidated();
  // Null only for a Layout that was constructed but never adopted, which the
  // public API cannot produce -- setLayout constructs and adopts in one step --
  // or for one that has been parked because its host died mid-pass.
  if (host_) host_->performLayout();
}

namespace {

// How many Layout::measureFor frames are on the stack, anywhere in the process.
// The exact analogue of the g_layoutDepth that Widget.cpp's LayoutGuard keeps,
// and it exists for the same reason: a parked layout may only be freed once
// NOTHING is still reading it, and a measurement walks items_ and scratch_
// exactly as an arrange does.  Counting arranges alone was the gap -- a pure
// sizeHint(), which is what ScrollArea::relayout issues, is not a pass.
//
// TWO readers now, and the second one is M-2 in measureFor below: this is also
// the measuring half's depth CEILING.  It was kept correctly and read by the
// park list for a whole round without ever being compared against anything,
// which is the same asymmetry the guard family keeps producing -- the handrail
// was built for the arranging half and the measuring half got the bookkeeping
// without the rail.
//
// A plain static, not a thread_local: the widget tree is UI-thread-only by
// construction (docs/architecture.md section 3.4), the same ruling every other
// global in the engine is made under.
std::uint32_t g_measureDepth = 0;

// Restores the latch on the way out rather than at the end of the function, so
// a buffer that failed to grow cannot leave a layout permanently convinced it
// is still busy -- which would silently stop it arranging for the rest of the
// process's life.
class BusyLatch {
 public:
  explicit BusyLatch(bool& flag) : flag_(flag) { flag_ = true; }
  ~BusyLatch() { flag_ = false; }

  BusyLatch(const BusyLatch&) = delete;
  BusyLatch& operator=(const BusyLatch&) = delete;

 private:
  bool& flag_;
};

// One measurement frame.  Declared BEFORE the BusyLatch at every use site, so
// it is destroyed AFTER it: the latch writes to the layout and must run while
// the object is certainly alive, and this is what may free it.
class MeasureFrame {
 public:
  MeasureFrame() { ++g_measureDepth; }
  ~MeasureFrame() {
    --g_measureDepth;
    // The outermost measurement, with no arrange under it either: this is the
    // second of the park list's two drain points.  ~LayoutGuard is the first,
    // and neither alone is enough -- a host destroyed from inside a pure
    // measurement parks a layout that no subsequent pass may ever come along to
    // release.
    if (g_measureDepth == 0 && !detail::layoutPassActive()) {
      detail::releaseParkedLayouts();
    }
  }

  MeasureFrame(const MeasureFrame&) = delete;
  MeasureFrame& operator=(const MeasureFrame&) = delete;
};

}  // namespace

SizeHint Layout::measureFor(const Widget& host) const {
  if (buffersBusy_) return lastMeasure_;  // see the declaration

  // M-2, the measuring half's ceiling, and the mirror of the one
  // Widget::runLayoutIfAny keeps for arranging (M4: g_layoutDepth against
  // kMaxTreeDepth).  WHAT IT CATCHES that the latch above does not:
  //
  //   * a layout measuring ITSELF again, and a two-layout cycle A -> B -> A,
  //     are both stopped by buffersBusy_ -- control comes back to A and finds
  //     A's own latch set;
  //   * a CHAIN A -> B -> C -> ... -> N is not.  Every level is a different
  //     Layout object with its own latch, and no level is a layout PASS, so
  //     g_layoutDepth stays at zero throughout and M4 never sees it.  All it
  //     takes is an application sizeHint() override that asks a widget in
  //     another tree for its size hint -- which is a thing sizeHint() exists to
  //     let people do.  The end of that chain is an exhausted stack: a denial
  //     of service rather than a corrupted heap, but the same shape as the
  //     guard family in section 11 of docs/iterations/02-layout-engine.md --
  //     the rail was designed around the arranging half.
  //
  // RECORDED, NEVER FATAL (ADR-R2-04).  The answer is lastMeasure_ -- the same
  // answer the latch above gives, for the same reason: it is the number the
  // outer measurement is already working from, and handing it back terminates
  // the recursion instead of the process.  No abort, no throw; a control room
  // screen does not die because one panel asked a silly question.
  //
  // WHY THE TEST IS HERE, in front of the frame rather than inside it.  Two
  // properties have to hold, and this position is what makes both of them
  // trivial rather than argued:
  //
  //   * COUNT BALANCE.  A refused call never touches g_measureDepth at all, so
  //     there is no increment to pair with a decrement -- and MeasureFrame's
  //     destructor, which is the park list's SECOND drain point (see it below),
  //     keeps its exact meaning: this frame neither parked anything nor became
  //     the outermost measurement, and the frames still on the stack drain the
  //     list on their way out as they always did.  Testing inside
  //     MeasureFrame's constructor instead would make "did this frame count?" a
  //     question its destructor has to answer too, i.e. a second flag whose
  //     only job is to keep the two halves in step -- and a counter kept in two
  //     places is how this file got a park list with two drain points in the
  //     first place.
  //   * IT MUST RUN BEFORE THE RECURSION IT EXISTS TO STOP.  This is the
  //     decisive one, and the only one that makes the position load-bearing:
  //     move the test one line down, past measure(host), and the ceiling is not
  //     merely harder to argue about, it is FUNCTIONALLY DEAD.  measure() is
  //     what descends into the next link of the chain; by the time it returns,
  //     the whole runaway chain has already been walked and the stack has
  //     already been spent.  A ceiling tested afterwards records a fact about a
  //     recursion it did nothing to prevent.
  //
  // NOT READING A FREED OBJECT is a separate question, and the answer is NOT
  // "no door has been crossed in this frame yet".  That was the argument this
  // comment used to make, and it is wrong about the frame it describes: the
  // scope below already has TWO accesses through `this` AFTER a door --
  // `lastMeasure_ = measure(host)` writes one, `measured = lastMeasure_` reads
  // it back on the next line, and measure() is application code.  What keeps
  // those safe is the PARK LIST, not the absence of a door: inside the
  // MeasureFrame scope buffersBusy_ is true, so ~Widget and adoptLayout take
  // the parkLayout() branch instead of deleting this object, and
  // releaseParkedLayouts() refuses to drain while g_measureDepth != 0.  The
  // case is r2_remediation.a_box_survives_its_host_dying_inside_a_pure_measure.
  //
  // What a move past the door WOULD dangle is `host`, the reference -- parking
  // saves the Layout, nothing saves the Widget.  The test only takes its
  // ADDRESS (layoutDepthExceeded stores the pointer and never dereferences it),
  // so even that would not be a use-after-free.  Which is the point: the reason
  // to keep this test here is the recursion above, not a liveness argument.
  //
  // AFTER the latch, not before, so the existing behaviour is bit for bit what
  // it was: both branches return lastMeasure_ and only the record differs, and
  // a re-entrant measurement of the SAME layout is documented, expected and
  // self-terminating -- not a runaway chain, and it must not be counted as one.
  //
  // depthExceeded, the counter M4 already raises, rather than a sixth of its
  // own: it is the same fact ("nested past kMaxTreeDepth") and the same reader
  // wants it.  The host recorded is the one that was REFUSED, matching M4.
  if (g_measureDepth >= kMaxTreeDepth) {
    detail::layoutDepthExceeded(&host);
    return lastMeasure_;
  }

  // Taken by VALUE and returned from a local: by the time the scope below
  // closes, `this` may have been parked and freed -- the host can be destroyed
  // from a child's sizeHint(), and that is the whole of NEW-2.  Reading
  // lastMeasure_ after the frame has unwound would be the use-after-free the
  // parking exists to prevent.
  SizeHint measured;
  // Non-null only when a pass was refused below AND we are still hosted.
  Widget* deferred = nullptr;
  {
    const MeasureFrame frame;
    const BusyLatch latch(buffersBusy_);
    lastMeasure_ = measure(host);
    measured = lastMeasure_;
    if (arrangeDeferred_) {
      arrangeDeferred_ = false;
      // Null if the host died under us and parkLayout cleared it, which is
      // exactly when there is nothing left to re-run.
      deferred = host_;
    }
  }
  // The buffers are free again, so the pass that was refused while they were
  // busy can finally happen.  Outside the scope on purpose: performLayout()
  // arranges, and arranging takes the very latch that has just been dropped.
  if (deferred) deferred->performLayout();
  return measured;
}

LayoutOverflow Layout::arrangeFor(Widget& host, const Rect& content,
                                  bool& refused) {
  // The mirror case: a sizeHint() override that asks its own host to lay itself
  // out again.  M1 catches it whenever a pass is already running, but a plain
  // measure() outside one is not a pass, and refilling the buffers the
  // measurement is walking would free them under it.  Recorded and refused --
  // ADR-R2-04, a control room screen does not die over a layout that asked a
  // silly question -- and the caller keeps the geometry of the last good pass.
  //
  // REFUSED, not cancelled.  The request is remembered here and re-issued by
  // the measurement that was in the way, on its way out; the caller is told so
  // it can put back the dirty flag it cleared before asking.
  if (buffersBusy_) {
    detail::layoutReentered();
    arrangeDeferred_ = true;
    refused = true;
    return lastOverflow_;
  }
  refused = false;
  // Whatever was owed is about to be paid by this very pass.
  arrangeDeferred_ = false;
  const BusyLatch latch(buffersBusy_);
  return arrange(host, content);
}

// ------------------------------------------------------------ diagnostics ---
namespace detail {
namespace {
LayoutDiagnostics g_diagnostics;

// Intrusive, chained through Layout::deferredNext_: parking happens inside a
// destructor, and a destructor is the last place that should be asking for
// memory.
Layout* g_parked = nullptr;
}  // namespace

void parkLayout(Layout* l) {
  // Cleared, not kept: the `host` REFERENCE the running arrange was handed is
  // dangling too, and keeping the object alive does not fix that.  hostAlive()
  // going false is what stops that arrange at its next check.
  l->host_ = nullptr;
  l->deferredNext_ = g_parked;
  g_parked = l;
}

void releaseParkedLayouts() {
  // Called from ~LayoutGuard once no ARRANGE is left on the stack, which is
  // only half the question: a measurement of a parked layout is reading the
  // same items_ and the same scratch_, and freeing it here would pull both out
  // from under it.  The measurement drains the list itself on its way out.
  if (g_measureDepth != 0) return;
  while (Layout* l = g_parked) {
    g_parked = l->deferredNext_;
    delete l;  // ~Layout runs no application code, so this cannot re-enter
  }
}

std::size_t parkedLayoutCount() {
  std::size_t n = 0;
  for (const Layout* l = g_parked; l; l = l->deferredNext_) ++n;
  return n;
}

std::size_t g_layoutHosts = 0;

const LayoutDiagnostics& layoutDiagnostics() { return g_diagnostics; }

void resetLayoutDiagnostics() { g_diagnostics = LayoutDiagnostics{}; }

// Saturating rather than wrapping.  These counters are read by a human deciding
// whether a screen has a layout bug; a count that rolled over to 0 overnight
// would answer "no" to exactly the question they asked.
void layoutNotConverged(const Widget* host) {
  if (g_diagnostics.notConverged != 0xFFFFFFFFu) ++g_diagnostics.notConverged;
  g_diagnostics.lastNotConvergedHost = host;
}

void layoutDepthExceeded(const Widget* host) {
  if (g_diagnostics.depthExceeded != 0xFFFFFFFFu) ++g_diagnostics.depthExceeded;
  g_diagnostics.lastDepthExceededHost = host;
}

void layoutReentered() {
  if (g_diagnostics.reentered != 0xFFFFFFFFu) ++g_diagnostics.reentered;
}

void layoutIndexClamped() {
  if (g_diagnostics.indexClamped != 0xFFFFFFFFu) ++g_diagnostics.indexClamped;
}

// Raised by a guarded frame, not by the layout engine -- see Widget.hpp, where
// it is declared next to DeathWatch.  It lives HERE because the counter it
// increments does, and because a second diagnostics store would be a second
// place to reset (Widget.cpp's own note: a second hand-rolled copy is a second
// place to forget).
//
// Once per FRAME, never once per check: the counter answers "how many frames
// gave up", and a frame that asked its three questions and failed the first two
// is still one frame.  The call sites keep that true by writing it in the
// give-up branch, immediately before the return.
void frameDegraded() {
  if (g_diagnostics.framesDegraded != 0xFFFFFFFFu) ++g_diagnostics.framesDegraded;
}

}  // namespace detail
}  // namespace geeyoou
