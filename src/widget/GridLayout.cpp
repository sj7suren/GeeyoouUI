#include "geeyoou/widget/GridLayout.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <memory>

#include "geeyoou/widget/Widget.hpp"

namespace geeyoou {
namespace {

// Same tolerance, and the same reasoning, as BoxLayout's.
constexpr float kFit = 0.01f;

}  // namespace

// Clamped rather than rejected, and recorded rather than fatal: ADR-R2-04.  A
// screen that has been up for six weeks must not die because one call site
// computed a row number from a data field that arrived empty -- but a grid
// silently 131068 tracks wide is not an answer either, so the clamp is at a
// number a real form can reach and the counter says it happened.
// Recorded rather than asserted, deliberately: an assert would make this the
// one diagnostic in the layout engine that behaves differently in the two
// configurations the gate runs, and the counter is the thing a test -- and an
// operator reading layoutDiagnostics() after a six-week run -- can actually
// look at.
std::uint16_t GridLayout::toIndex(int v) {
  if (v >= 0 && v <= kMaxTrack) return std::uint16_t(v);
  detail::layoutIndexClamped();
  return v < 0 ? std::uint16_t(0) : std::uint16_t(kMaxTrack);
}

std::uint16_t GridLayout::toSpan(int v) {
  return v < 1 ? std::uint16_t(1) : toIndex(v);
}

// The clamp that actually bounds the grid.
//
// toIndex() and toSpan() bound a start and a length INDEPENDENTLY, which is not
// the same thing as bounding what the cell REACHES: row 4096 with rowSpan 4096
// passed both of them and then declared track 8191, so the worst case was twice
// the constant that documents it and every pass resized six vectors to 8192
// entries.  The bound that matters is the last track a cell touches, so that is
// the one clamped here -- kMaxTrack stays the largest index this class accepts,
// and trackCount() therefore never exceeds kMaxTrack + 1.
//
// The span gives way rather than the start: a cell asked for at row 4096 is at
// row 4096 in the caller's mental model, and moving it would put it somewhere
// nobody asked for, whereas shortening its span leaves it where it was asked
// for and merely smaller.
std::uint16_t GridLayout::clampSpan(std::uint16_t first, std::uint16_t span) {
  const std::size_t last = std::size_t(first) + span;
  if (last <= std::size_t(kMaxTrack) + 1) return span;
  detail::layoutIndexClamped();
  // At least 1: `first` is already <= kMaxTrack, so this cannot reach zero.
  return std::uint16_t(std::size_t(kMaxTrack) + 1 - first);
}

// ------------------------------------------------------------------ cells ---
void GridLayout::addWidget(Widget* child, int row, int col, int rowSpan,
                           int colSpan) {
  assert(child && "GridLayout::addWidget(nullptr)");
  assert(host() && "GridLayout must be adopted (Widget::setLayout) before use");
  if (!child || !host()) return;

  // Resolved to an INDEX and stored as one: ADR-R2-08.
  const std::vector<std::unique_ptr<Widget>>& kids = host()->children();
  std::size_t index = kids.size();
  for (std::size_t i = 0; i < kids.size(); ++i) {
    if (kids[i].get() == child) {
      index = i;
      break;
    }
  }
  assert(index < kids.size() && "GridLayout::addWidget: not a child of the host");
  assert(index < kNoChild && "more children than a GridLayout can address");
  if (index >= kids.size() || index >= kNoChild) return;

  Cell c;
  c.childIndex = std::uint16_t(index);
  c.row = toIndex(row);
  c.col = toIndex(col);
  c.rowSpan = clampSpan(c.row, toSpan(rowSpan));
  c.colSpan = clampSpan(c.col, toSpan(colSpan));
  cells_.push_back(c);
  invalidate();
}

void GridLayout::addRow(Widget* label, Widget* field) {
  const int row = int(trackCount(Axis::Rows));
  if (label) addWidget(label, row, 0);
  if (field) addWidget(field, row, 1);
  // The field column is the one that grows -- a form whose labels stretched and
  // whose inputs did not would be exactly backwards.  Only filled in when the
  // caller has not said otherwise: an explicit setColumnStretch wins.
  if (colStretch_.size() < 2 || colStretch_[1] == 0) setColumnStretch(1, 1);
}

void GridLayout::setColumnStretch(int col, std::uint16_t s) {
  const std::size_t c = toIndex(col);
  if (colStretch_.size() <= c) colStretch_.resize(c + 1, 0);
  colStretch_[c] = s;
  invalidate();
}

void GridLayout::setRowStretch(int row, std::uint16_t s) {
  const std::size_t r = toIndex(row);
  if (rowStretch_.size() <= r) rowStretch_.resize(r + 1, 0);
  rowStretch_[r] = s;
  invalidate();
}

void GridLayout::onChildRemoved(std::size_t index) {
  std::size_t out = 0;
  for (std::size_t i = 0; i < cells_.size(); ++i) {
    Cell c = cells_[i];
    if (std::size_t(c.childIndex) == index) continue;  // its widget is gone
    if (std::size_t(c.childIndex) > index) --c.childIndex;
    cells_[out++] = c;
  }
  cells_.resize(out);  // shrinking a vector never reallocates
}

Widget* GridLayout::cellWidget(const Widget& host, const Cell& c) const {
  const std::vector<std::unique_ptr<Widget>>& kids = host.children();
  if (std::size_t(c.childIndex) >= kids.size()) return nullptr;
  Widget* w = kids[c.childIndex].get();
  return (w && w->isVisible()) ? w : nullptr;
}

// The grid's shape is decided by what was DECLARED, not by what is currently
// visible: hiding one field must not collapse the column its neighbours are
// aligned to.  A stretch set past the last occupied track counts too -- saying
// "column 3 grows" is a statement that column 3 exists.
std::size_t GridLayout::trackCount(Axis axis) const {
  std::size_t n = (axis == Axis::Columns) ? colStretch_.size() : rowStretch_.size();
  for (const Cell& c : cells_) {
    const std::size_t end = (axis == Axis::Columns)
                                ? std::size_t(c.col) + c.colSpan
                                : std::size_t(c.row) + c.rowSpan;
    if (end > n) n = end;
  }
  return n;
}

// --------------------------------------------------------------- measuring ---
//
// ADR-R2-09's ascending-span pass.  See the header for why this cannot be a
// closed form and must not be an iteration.
void GridLayout::measureAxis(const Widget& host, Axis axis) const {
  const bool cols = (axis == Axis::Columns);
  std::vector<float>& mn = cols ? colMin_ : rowMin_;
  std::vector<float>& pf = cols ? colPref_ : rowPref_;

  const std::size_t n = trackCount(axis);
  // clear() then resize(): the capacity survives, so a grid of stable shape
  // allocates on its first pass and never again.
  mn.clear();
  mn.resize(n, 0.0f);
  pf.clear();
  pf.resize(n, 0.0f);

  // Batch order: every span-1 cell first, then every span-2, and so on.  A
  // plain sort rather than stable_sort -- stable_sort allocates a temporary
  // buffer, and the cell index in the comparison already makes the order total.
  spanOrder_.clear();
  spanOrder_.resize(cells_.size(), 0);
  for (std::size_t i = 0; i < cells_.size(); ++i) spanOrder_[i] = std::uint32_t(i);
  std::sort(spanOrder_.begin(), spanOrder_.end(),
            [this, cols](std::uint32_t a, std::uint32_t b) {
              const std::uint16_t sa = cols ? cells_[a].colSpan : cells_[a].rowSpan;
              const std::uint16_t sb = cols ? cells_[b].colSpan : cells_[b].rowSpan;
              return sa != sb ? sa < sb : a < b;
            });

  for (const std::uint32_t idx : spanOrder_) {
    // sizeHint() below is an application override, and one that removes a cell
    // leaves this order longer than the list it indexes.
    if (idx >= cells_.size()) continue;
    const Cell c = cells_[idx];  // by value, for the same reason
    const Widget* w = cellWidget(host, c);
    if (!w) continue;

    const SizeHint h = w->sizeHint();
    // An application override just ran, and it is entitled to remove the host
    // as well as a cell.  cellWidget() on the next turn of this loop would read
    // host.children() on freed memory; the track vectors below belong to this
    // object, which Widget parked, but the numbers in them are now about a tree
    // that no longer exists.  Same rule as BoxLayout::gather, same reason.
    if (!hostAlive()) return;
    const float needMin = cols ? h.min.width : h.min.height;
    const float needPref =
        (std::max)(needMin, cols ? h.preferred.width : h.preferred.height);

    const std::size_t first = cols ? c.col : c.row;
    const std::size_t span = cols ? c.colSpan : c.rowSpan;
    if (first + span > n) continue;  // cannot happen: trackCount covers it
    // The spacing BETWEEN the tracks a multi-track cell covers is space the
    // cell gets to use, so it counts towards what those tracks already supply.
    const float inner = float(span - 1) * spacing();

    float haveMin = inner;
    float havePref = inner;
    for (std::size_t k = 0; k < span; ++k) {
      haveMin += mn[first + k];
      havePref += pf[first + k];
    }
    // For span == 1 this is exactly "take the maximum over the cells in this
    // track"; for wider cells it is the ADR's share-out of the shortfall.
    if (needMin > haveMin) shareDeficit(mn, first, span, needMin - haveMin, axis);
    if (needPref > havePref) shareDeficit(pf, first, span, needPref - havePref, axis);
  }

  // A track can only reach its preferred size by passing through its minimum.
  // The two runs above are independent, so this is asserted rather than assumed.
  for (std::size_t i = 0; i < n; ++i) pf[i] = (std::max)(pf[i], mn[i]);
}

void GridLayout::shareDeficit(std::vector<float>& track, std::size_t first,
                              std::size_t span, float deficit, Axis axis) const {
  const std::vector<std::uint16_t>& st = stretchOf(axis);

  // The stretchable tracks are the ones the author said may grow, so they take
  // the shortfall.  Only when the cell covers none of them is it spread over
  // everything it covers -- somebody has to hold the space.
  std::size_t recipients = 0;
  for (std::size_t k = 0; k < span; ++k) {
    const std::size_t t = first + k;
    if (t < st.size() && st[t] > 0) ++recipients;
  }
  const bool useStretch = recipients > 0;
  if (!useStretch) recipients = span;
  if (recipients == 0) return;

  // Floored shares with the remainder on the LAST recipient: the same
  // determinism rule BoxLayout uses, and for the same reason -- a share that
  // rounded differently from one pass to the next would show up as a column
  // that twitches by a pixel while nothing is happening.
  const float share = std::floor(deficit / float(recipients));
  float handed = 0.0f;
  std::size_t last = first;
  for (std::size_t k = 0; k < span; ++k) {
    const std::size_t t = first + k;
    if (useStretch && !(t < st.size() && st[t] > 0)) continue;
    track[t] += share;
    handed += share;
    last = t;
  }
  track[last] += deficit - handed;
}

// ---------------------------------------------------------------- solving ---
//
// Identical in RULE to BoxLayout's distribution -- minimums first, then up to
// preferred, then the leftovers by stretch weight, floored with the remainder
// going to the largest weight (ties to the last) -- but not shared code with
// it: this one works on two parallel track vectors and needs no per-track
// maximum, and threading both shapes through one helper would cost a callback
// per track on the hot path to save thirty lines.
float GridLayout::spreadTracks(Axis axis, float free, bool toPreferred) const {
  const bool cols = (axis == Axis::Columns);
  std::vector<float>& size = cols ? colMin_ : rowMin_;
  const std::vector<float>& pref = cols ? colPref_ : rowPref_;
  const std::vector<std::uint16_t>& st = stretchOf(axis);
  const std::size_t n = size.size();
  if (free <= 0.0f) return 0.0f;

  float totalWeight = 0.0f;
  for (std::size_t i = 0; i < n; ++i) {
    const float w = toPreferred ? (pref[i] - size[i])
                               : (i < st.size() ? float(st[i]) : 0.0f);
    if (w > 0.0f) totalWeight += w;
  }
  if (totalWeight <= 0.0f) return 0.0f;

  float granted = 0.0f;
  std::size_t best = n;
  float bestWeight = 0.0f;
  for (std::size_t i = 0; i < n; ++i) {
    const float w = toPreferred ? (pref[i] - size[i])
                                : (i < st.size() ? float(st[i]) : 0.0f);
    if (w <= 0.0f) continue;
    // In the first round a track stops at its preferred size -- there may be
    // more free space than the tracks collectively want, and whatever is left
    // belongs to the stretch round, not to whoever happened to be measured
    // first.  The stretch round itself has no ceiling.
    const float headroom = toPreferred ? (pref[i] - size[i]) : free;
    float share = std::floor(free * w / totalWeight);
    if (share > headroom) share = headroom;
    size[i] += share;
    granted += share;
    if (w >= bestWeight) {  // >= : equal weights hand the remainder to the LAST
      bestWeight = w;
      best = i;
    }
  }

  // Never more than one pixel per participant, and it lands somewhere
  // predictable rather than being lost.
  float remainder = free - granted;
  if (remainder > 0.0f && best != n) {
    const float take =
        toPreferred ? (std::min)(remainder, pref[best] - size[best]) : remainder;
    if (take > 0.0f) {
      size[best] += take;
      granted += take;
      remainder -= take;
    }
  }
  // Only reachable in the first round, when the largest wanter was already
  // satisfied.  Backwards, so the tie-break still leans towards the last track.
  for (std::size_t i = n; i-- > 0 && remainder > 0.0f && toPreferred;) {
    const float take = (std::min)(remainder, pref[i] - size[i]);
    if (take <= 0.0f) continue;
    size[i] += take;
    granted += take;
    remainder -= take;
  }
  return granted;
}

// Returns how many pixels the axis is short, which is zero whenever the
// minimums fit.
float GridLayout::solveAxis(Axis axis, float avail) const {
  const bool cols = (axis == Axis::Columns);
  std::vector<float>& size = cols ? colMin_ : rowMin_;
  const std::size_t n = size.size();
  if (n == 0) return 0.0f;

  const float sep = float(n - 1) * spacing();
  float sumMin = 0.0f;
  for (const float v : size) sumMin += v;

  const float free = avail - sep - sumMin;
  // Too little even for the minimums: every track keeps its minimum and the
  // tail runs off the end.  Squeezing tracks below what their contents declared
  // they need produces a row of unreadable stumps; a clipped grid is at least
  // obviously wrong, and lastLayoutOverflow() says by how much.
  if (free <= 0.0f) return -free;

  const float used = spreadTracks(axis, free, /*toPreferred=*/true);
  spreadTracks(axis, free - used, /*toPreferred=*/false);
  return 0.0f;
}

SizeHint GridLayout::measure(const Widget& host) const {
  measureAxis(host, Axis::Columns);
  if (!hostAlive()) return {};  // a cell's sizeHint() took the host away
  measureAxis(host, Axis::Rows);
  if (!hostAlive()) return {};

  const Margins& m = margins();
  float minW = m.left + m.right;
  float prefW = minW;
  float minH = m.top + m.bottom;
  float prefH = minH;
  if (!colMin_.empty()) {
    const float sep = float(colMin_.size() - 1) * spacing();
    minW += sep;
    prefW += sep;
    for (const float v : colMin_) minW += v;
    for (const float v : colPref_) prefW += v;
  }
  if (!rowMin_.empty()) {
    const float sep = float(rowMin_.size() - 1) * spacing();
    minH += sep;
    prefH += sep;
    for (const float v : rowMin_) minH += v;
    for (const float v : rowPref_) prefH += v;
  }

  SizeHint out;
  out.min = Size{minW, minH};
  out.preferred = Size{prefW, prefH};
  return out;
}

// --------------------------------------------------------------- arranging ---
LayoutOverflow GridLayout::arrange(Widget& host, const Rect& content) {
  LayoutOverflow of;
  measureAxis(host, Axis::Columns);
  // Measuring is the first thing an arrange does and it runs every cell's
  // sizeHint(), so the host can already be gone before a single rectangle has
  // been written.  The check after setGeometry, at the bottom of this function,
  // is a whole measure pass too late to catch it.
  if (!hostAlive()) return {};
  measureAxis(host, Axis::Rows);
  if (!hostAlive()) return {};

  of.widthShort = solveAxis(Axis::Columns, content.width());
  of.heightShort = solveAxis(Axis::Rows, content.height());
  // colMin_ / rowMin_ now hold the SOLVED track sizes.

  // Their own buffers, not a second life for colPref_/rowPref_: see the header.
  colOff_.clear();
  colOff_.resize(colMin_.size(), 0.0f);
  rowOff_.clear();
  rowOff_.resize(rowMin_.size(), 0.0f);
  float x = 0.0f;
  for (std::size_t i = 0; i < colMin_.size(); ++i) {
    colOff_[i] = x;
    x += colMin_[i] + spacing();
  }
  float y = 0.0f;
  for (std::size_t i = 0; i < rowMin_.size(); ++i) {
    rowOff_[i] = y;
    y += rowMin_[i] + spacing();
  }

  // TWO bounds, the same pair and for the same reasons as BoxLayout::arrange:
  // setGeometry runs application code, a handler that removes a child shortens
  // this list through onChildRemoved, and one that adds a widget and calls
  // addWidget lengthens it past what the track vectors above were solved for.
  // `n` is the shape this pass measured; cells_.size() is what the list holds
  // right now.  Cells that arrived since are placed by the re-run their own
  // addWidget scheduled, not by guesswork off half-solved tracks.
  const std::size_t n = cells_.size();
  for (std::size_t i = 0; i < n && i < cells_.size(); ++i) {
    const Cell c = cells_[i];
    Widget* w = cellWidget(host, c);
    if (!w) continue;
    if (std::size_t(c.col) + c.colSpan > colMin_.size()) continue;
    if (std::size_t(c.row) + c.rowSpan > rowMin_.size()) continue;

    float cw = float(c.colSpan - 1) * spacing();
    for (std::size_t k = 0; k < c.colSpan; ++k) cw += colMin_[c.col + k];
    float ch = float(c.rowSpan - 1) * spacing();
    for (std::size_t k = 0; k < c.rowSpan; ++k) ch += rowMin_[c.row + k];

    const float cx = colOff_[c.col];
    const float cy = rowOff_[c.row];
    if (cx + cw > content.width() + kFit || cy + ch > content.height() + kFit) {
      ++of.clippedCount;
    }
    // The cell is filled, not aligned inside: R2 has no per-cell alignment API,
    // and inventing one here would be a rule nobody could see from the call site.
    w->setGeometry({content.x() + cx, content.y() + cy, cw, ch});
    // Application code just ran and may have destroyed the host or replaced
    // this layout on it.  `host`, cells_ and every track vector above belong to
    // that widget; nothing below may be read once this goes false.
    if (!hostAlive()) return {};
  }
  return of;
}

}  // namespace geeyoou
