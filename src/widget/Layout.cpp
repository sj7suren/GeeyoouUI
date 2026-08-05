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

}  // namespace

SizeHint Layout::measureFor(const Widget& host) const {
  if (buffersBusy_) return lastMeasure_;  // see the declaration
  const BusyLatch latch(buffersBusy_);
  lastMeasure_ = measure(host);
  return lastMeasure_;
}

LayoutOverflow Layout::arrangeFor(Widget& host, const Rect& content) {
  // The mirror case: a sizeHint() override that asks its own host to lay itself
  // out again.  M1 catches it whenever a pass is already running, but a plain
  // measure() outside one is not a pass, and refilling the buffers the
  // measurement is walking would free them under it.  Recorded and refused --
  // ADR-R2-04, a control room screen does not die over a layout that asked a
  // silly question -- and the caller keeps the geometry of the last good pass.
  if (buffersBusy_) {
    detail::layoutReentered();
    return lastOverflow_;
  }
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
  while (Layout* l = g_parked) {
    g_parked = l->deferredNext_;
    delete l;  // ~Layout runs no application code, so this cannot re-enter
  }
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
