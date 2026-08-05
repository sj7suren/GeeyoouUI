#include "geeyoou/widget/BoxLayout.hpp"

#include <cassert>
#include <cmath>
#include <memory>

#include "geeyoou/widget/Widget.hpp"

namespace geeyoou {
namespace {

// How far past the content edge an item may sit before it counts as clipped.
// A hundredth of a logical pixel: big enough to absorb the accumulated error of
// a chain of float additions, far too small to hide a real overlap.
constexpr float kFit = 0.01f;

float clampf(float v, float lo, float hi) {
  if (hi < lo) hi = lo;  // a hint with max < min is the caller's bug, not a crash
  return v < lo ? lo : (v > hi ? hi : v);
}

}  // namespace

BoxLayout::BoxLayout(Orientation o) : o_(o) {}

// ------------------------------------------------------------------ items ---
void BoxLayout::addWidget(Widget* child, std::uint16_t stretch) {
  assert(child && "BoxLayout::addWidget(nullptr)");
  assert(host() && "BoxLayout must be adopted (Widget::setLayout) before use");
  if (!child || !host()) return;

  // Resolved to an INDEX here and stored as one: ADR-R2-08.  A widget that is
  // not one of the host's children has no index, and inventing one -- or
  // keeping the pointer "just this once" -- is how a layout outlives what it
  // points at.
  const std::vector<std::unique_ptr<Widget>>& kids = host()->children();
  std::size_t index = kids.size();
  for (std::size_t i = 0; i < kids.size(); ++i) {
    if (kids[i].get() == child) {
      index = i;
      break;
    }
  }
  assert(index < kids.size() && "BoxLayout::addWidget: not a child of the host");
  assert(index < kNoChild && "more children than a BoxLayout can address");
  if (index >= kids.size() || index >= kNoChild) return;

  Item it;
  it.childIndex = std::uint16_t(index);
  if (o_ == Orientation::Horizontal) {
    it.stretch.h = stretch;
  } else {
    it.stretch.v = stretch;
  }
  items_.push_back(it);
  invalidate();
}

void BoxLayout::addSpacing(float px) {
  Item it;
  it.fixed = px;
  items_.push_back(it);
  invalidate();
}

void BoxLayout::addStretch(std::uint16_t weight) {
  Item it;
  if (o_ == Orientation::Horizontal) {
    it.stretch.h = weight;
  } else {
    it.stretch.v = weight;
  }
  items_.push_back(it);
  invalidate();
}

// The child vector closed up over `index`, so every item above it now names the
// wrong widget.  Fixing this here is the whole price of index addressing, and
// it is paid in one place.
void BoxLayout::onChildRemoved(std::size_t index) {
  std::size_t out = 0;
  for (std::size_t i = 0; i < items_.size(); ++i) {
    Item it = items_[i];
    if (it.childIndex != kNoChild) {
      if (std::size_t(it.childIndex) == index) continue;  // its widget is gone
      if (std::size_t(it.childIndex) > index) --it.childIndex;
    }
    items_[out++] = it;
  }
  items_.resize(out);  // shrinking a vector never reallocates
}

Widget* BoxLayout::itemWidget(const Widget& host, const Item& it) const {
  if (it.childIndex == kNoChild) return nullptr;
  // Re-read every time: a child's onGeometryChanged runs application code, and
  // application code may remove widgets.  Same discipline as announceDetached.
  const std::vector<std::unique_ptr<Widget>>& kids = host.children();
  if (std::size_t(it.childIndex) >= kids.size()) return nullptr;
  Widget* c = kids[it.childIndex].get();
  // A hidden widget leaves the chain entirely -- it takes no space AND no
  // spacing, so hiding the middle of a row closes the gap instead of leaving a
  // double-width hole where it used to be.
  return (c && c->isVisible()) ? c : nullptr;
}

// ---------------------------------------------------------------- measuring ---
BoxLayout::Totals BoxLayout::gather(const Widget& host) const {
  Totals t;
  const std::size_t n = items_.size();
  // clear() + resize() rather than assign(): the capacity survives, so a box
  // whose item count is stable allocates on its first pass and never again.
  scratch_.clear();
  scratch_.resize(n * kStride, 0.0f);

  bool prevWidget = false;
  // Two bounds, because sizeHint() below is an application-supplied override
  // and may do anything the public API allows -- including removing a child,
  // which shortens items_ through onChildRemoved.  `n` is what scratch_ was
  // sized for; items_.size() is what the list holds right now.
  //
  // And a child is not the only thing it may remove: the same override can take
  // the HOST away, which is why the hostAlive() check below is here at all.
  // The measuring half of a pass runs application code exactly as the arranging
  // half does -- see the contract on Layout::measure -- and this loop used to
  // have no check in it, which made every gather() reachable from a hostile
  // sizeHint() a use-after-free on host.children().
  for (std::size_t i = 0; i < n && i < items_.size(); ++i) {
    // BY VALUE.  A reference into items_ does not survive the same handler
    // appending to it, and eight bytes is not worth a dangling one.
    const Item it = items_[i];
    float* track = &scratch_[i * kStride];

    if (it.childIndex == kNoChild) {
      // A spacer: `fixed` is its floor, its weight decides how much of the
      // leftover it takes, and it has no cross-axis opinion at all.
      track[0] = it.fixed;
      track[1] = float(weightOf(it));
      track[2] = kUnbounded;
      track[3] = it.fixed;
      track[4] = 0.0f;
      track[5] = kUnbounded;
      t.min += it.fixed;
      t.preferred += it.fixed;
      prevWidget = false;
      continue;
    }

    const Widget* c = itemWidget(host, it);
    if (!c) {           // removed under us, or hidden
      track[2] = kUnbounded;
      track[5] = kUnbounded;
      continue;         // contributes nothing, not even a separator
    }

    const SizeHint h = c->sizeHint();
    // sizeHint() is an application override and has just run.  If it took the
    // host with it, `host` is dangling and so is everything this loop reaches
    // through it -- items_ and scratch_ survive only because Widget parked this
    // object, and the totals half-built here mean nothing anyway.
    if (!hostAlive()) return {};
    // Re-taken AFTER the call: `track` above is only still valid because
    // Layout::measureFor refuses to re-enter this function and refill scratch_,
    // and a pointer whose validity depends on a latch two files away is not one
    // to keep across a virtual call.
    track = &scratch_[i * kStride];
    const float mn = mainOf(h.min);
    const float pf = (std::max)(mainOf(h.preferred), mn);
    const float mx = (std::max)(mainOf(h.max), pf);
    track[0] = mn;
    track[1] = float(weightOf(it));
    track[2] = mx;
    track[3] = pf;
    track[4] = crossOf(h.min);
    track[5] = (std::max)(crossOf(h.max), crossOf(h.min));

    t.min += mn;
    t.preferred += pf;
    t.crossMin = (std::max)(t.crossMin, crossOf(h.min));
    t.crossPreferred =
        (std::max)(t.crossPreferred, (std::max)(crossOf(h.preferred), crossOf(h.min)));
    if (prevWidget) t.separators += spacing();
    prevWidget = true;
  }
  return t;
}

// Hands out `free` pixels among the tracks in scratch_.
//
// `toPreferred` picks which of the two rounds this is: the first grows every
// item from its minimum towards its preferred size (weighted by how much it
// still wants), the second splits whatever is left over by stretch weight.
//
// The shares are FLOORED and the remainder is handed to the largest weight,
// ties going to the last of them.  That is the whole determinism story: the
// same numbers in always produce the same pixels out, so an item cannot gain a
// pixel one frame and lose it the next as a neighbour is nudged.
float BoxLayout::spread(float free, bool toPreferred) const {
  // The number of TRACKS, taken from the buffer being indexed rather than from
  // items_ -- gather() sized it to the item count of this pass, and by the time
  // distribute() runs, a sizeHint() override may already have moved that count.
  const std::size_t n = scratch_.size() / kStride;
  if (free <= 0.0f) return 0.0f;

  float totalWeight = 0.0f;
  for (std::size_t i = 0; i < n; ++i) {
    const float* track = &scratch_[i * kStride];
    const float w = toPreferred ? (track[3] - track[0]) : track[1];
    const float cap = toPreferred ? track[3] : track[2];
    if (w > 0.0f && cap > track[0]) totalWeight += w;
  }
  if (totalWeight <= 0.0f) return 0.0f;

  float granted = 0.0f;
  std::size_t best = n;
  float bestWeight = 0.0f;
  for (std::size_t i = 0; i < n; ++i) {
    float* track = &scratch_[i * kStride];
    const float w = toPreferred ? (track[3] - track[0]) : track[1];
    const float cap = toPreferred ? track[3] : track[2];
    const float headroom = cap - track[0];
    if (w <= 0.0f || headroom <= 0.0f) continue;
    float share = std::floor(free * w / totalWeight);
    if (share > headroom) share = headroom;
    track[0] += share;
    granted += share;
    // >= rather than >, so equal weights hand the remainder to the LAST item.
    if (w >= bestWeight) {
      bestWeight = w;
      best = i;
    }
  }

  float remainder = free - granted;
  if (remainder > 0.0f && best != n) {
    float* track = &scratch_[best * kStride];
    const float cap = toPreferred ? track[3] : track[2];
    const float take = (std::min)(remainder, cap - track[0]);
    if (take > 0.0f) {
      track[0] += take;
      granted += take;
      remainder -= take;
    }
  }
  // The winner was already full.  Spill backwards -- still "towards the last
  // item" -- rather than leaving pixels unspent while something can still hold
  // them.  Stops for good once every participant is at its cap, which is the
  // honest answer: that space cannot be used.
  for (std::size_t i = n; i-- > 0 && remainder > 0.0f;) {
    float* track = &scratch_[i * kStride];
    const float w = toPreferred ? (track[3] - track[0]) : track[1];
    const float cap = toPreferred ? track[3] : track[2];
    if (w <= 0.0f) continue;
    const float take = (std::min)(remainder, cap - track[0]);
    if (take <= 0.0f) continue;
    track[0] += take;
    granted += take;
    remainder -= take;
  }
  return granted;
}

void BoxLayout::distribute(const Totals& t, float avail) const {
  const float free = avail - t.separators - t.min;
  if (free <= 0.0f) return;  // too little for even the minimums: see arrange()
  const float used = spread(free, /*toPreferred=*/true);
  spread(free - used, /*toPreferred=*/false);
}

SizeHint BoxLayout::measure(const Widget& host) const {
  const Totals t = gather(host);
  // gather() ran every child's sizeHint(); one of them may have destroyed the
  // host.  Nothing below reads it, but a measurement of a tree that no longer
  // exists is not a number anybody should act on either.
  if (!hostAlive()) return {};
  const Margins& m = margins();
  const float mw = m.left + m.right;
  const float mh = m.top + m.bottom;

  const float mainMin = t.min + t.separators;
  const float mainPref = t.preferred + t.separators;

  SizeHint out;
  if (o_ == Orientation::Horizontal) {
    out.min = Size{mainMin + mw, t.crossMin + mh};
    out.preferred = Size{mainPref + mw, t.crossPreferred + mh};
  } else {
    out.min = Size{t.crossMin + mw, mainMin + mh};
    out.preferred = Size{t.crossPreferred + mw, mainPref + mh};
  }
  // No maximum.  A box whose items all cap out could report one, but the moment
  // a single stretchable item is present the answer is "unbounded" again, and a
  // maximum that flips between two very different numbers as items are added is
  // worse than one that is honestly absent.
  return out;
}

// ---------------------------------------------------------------- arranging ---
LayoutOverflow BoxLayout::arrange(Widget& host, const Rect& content) {
  LayoutOverflow of;
  const Totals t = gather(host);
  // The FIRST thing an arrange does is measure, and measuring runs application
  // code (every child's sizeHint()).  Without this the placement loop below
  // walks straight into host.children() on a freed widget -- the check at the
  // bottom of that loop is one setGeometry too late to help.
  if (!hostAlive()) return {};

  const float avail = mainOf(content.size());
  const float crossAvail = crossOf(content.size());
  distribute(t, avail);

  const bool horizontal = (o_ == Orientation::Horizontal);
  float& mainShort = horizontal ? of.widthShort : of.heightShort;
  float& crossShort = horizontal ? of.heightShort : of.widthShort;
  const float need = t.min + t.separators;
  if (need > avail) mainShort = need - avail;
  if (t.crossMin > crossAvail) crossShort = t.crossMin - crossAvail;

  const float crossStart = horizontal ? content.y() : content.x();
  float pos = horizontal ? content.x() : content.y();
  const float end = pos + avail;

  // TWO bounds, and both of them are load-bearing.  setGeometry below runs
  // application code, and that code may move the item count in EITHER
  // direction through the ordinary public API:
  //
  //   * it shrinks when a handler removes a child (onChildRemoved compacts
  //     items_), which is what items_.size() catches;
  //   * it GROWS when a handler does host->add<T>() + box->addWidget(), which
  //     is what `n` catches -- scratch_ was sized for the count this pass
  //     started with and holds no spare room at all, so indexing it by an item
  //     that arrived since is a heap-buffer-overflow.
  //
  // The second half is what the previous note here got wrong: "scratch_ keeps
  // the length it was given, so the indices stay in range" is only true while
  // the list is shrinking.  Items that arrive mid-pass are simply not placed
  // this round; the dirty flag their addWidget raised has already scheduled the
  // re-run that places them, and the same is true of whatever shifted down.
  const std::size_t n = scratch_.size() / kStride;
  bool prevWidget = false;
  for (std::size_t i = 0; i < n && i < items_.size(); ++i) {
    const Item it = items_[i];  // by value: setGeometry may reallocate items_
    const float* track = &scratch_[i * kStride];
    const float size = track[0];

    if (it.childIndex == kNoChild) {  // a spacer takes room and nothing else
      pos += size;
      prevWidget = false;
      continue;
    }
    Widget* c = itemWidget(host, it);
    if (!c) continue;  // hidden, or removed by a handler earlier in this pass

    if (prevWidget) pos += spacing();
    const float cross = clampf(crossAvail, track[4], track[5]);

    // "Did not fit", on either axis.  The cross size is clamped UP to the
    // item's minimum, so the test there is against the room available rather
    // than against what the item ended up with -- an item that is 80 wide in a
    // 50 wide panel has not been squeezed, it has been cut off.
    if (pos + size > end + kFit || track[4] > crossAvail + kFit) ++of.clippedCount;

    c->setGeometry(horizontal ? Rect{pos, crossStart, size, cross}
                              : Rect{crossStart, pos, cross, size});
    // The setGeometry above may have destroyed the host, or replaced this
    // layout on it.  `host` is a dangling reference from here on and so is
    // every member -- the object itself only outlives the host because Widget
    // parks it, which buys the chance to notice, not the right to carry on.
    if (!hostAlive()) return {};
    pos += size;
    prevWidget = true;
  }
  return of;
}

}  // namespace geeyoou
