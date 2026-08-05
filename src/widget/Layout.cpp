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

}  // namespace detail
}  // namespace geeyoou
