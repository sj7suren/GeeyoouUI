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
  // public API cannot produce -- setLayout constructs and adopts in one step.
  if (host_) host_->relayout();
}

// ------------------------------------------------------------ diagnostics ---
namespace detail {
namespace {
LayoutDiagnostics g_diagnostics;
}  // namespace

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

}  // namespace detail
}  // namespace geeyoou
