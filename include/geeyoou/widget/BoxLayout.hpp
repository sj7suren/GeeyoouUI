#pragma once
//
// BoxLayout: one row or one column.
//
// The two things worth knowing before reading the implementation:
//
//   1. SPACING GOES BETWEEN TWO ADJACENT WIDGETS, and nowhere else.  A spacer
//      (addSpacing / addStretch) neither takes spacing on its own sides nor
//      passes it through, so addSpacing(20) is twenty pixels of gap rather than
//      twenty plus two helpings of spacing().  Hidden widgets drop out of the
//      chain the same way, taking their gap with them.
//
//   2. THE DISTRIBUTION IS EXACT AND DETERMINISTIC.  Space left over after
//      every item has its preferred size is split by stretch weight; the shares
//      are floored to whole pixels and the REMAINDER goes to the item with the
//      largest weight, ties broken towards the last of them.  Two arranges with
//      the same numbers therefore produce byte-identical geometry -- no item
//      ever gains and loses a pixel as a neighbour is nudged, which on a screen
//      that is redrawn thirty times a second reads as a shimmer.
//
// Items are addressed by CHILD INDEX, never by pointer -- see the structural
// rules at the top of Layout.hpp.
//
#include <cstddef>
#include <cstdint>
#include <vector>

#include "geeyoou/widget/Layout.hpp"

namespace geeyoou {

class Widget;

class BoxLayout final : public Layout {
 public:
  enum class Orientation : std::uint8_t { Horizontal, Vertical };

  explicit BoxLayout(Orientation o);

  // `child` must ALREADY be a child of the host: this class positions the
  // host's children, it does not adopt widgets.  Anything else is ignored (and
  // asserted in debug builds), because the alternative -- silently keeping a
  // pointer to a widget the host does not own -- is the use-after-free that
  // ADR-R2-08 exists to prevent.
  //
  // `stretch` is how eagerly the item takes leftover space along the box's own
  // axis.  0 (the default) means "stay at your preferred size".
  void addWidget(Widget* child, std::uint16_t stretch = 0);

  // A fixed gap with no widget in it.
  void addSpacing(float px);

  // A gap that eats leftover space.  `addStretch()` before the first widget
  // pushes the row to the far end; one at each end centres it.
  void addStretch(std::uint16_t weight = 1);

  // How many items (widgets AND spacers) this box holds.
  std::size_t itemCount() const { return items_.size(); }

 protected:
  // Protected, like the base declarations: ask the HOST (Widget::sizeHint,
  // Widget::performLayout) rather than calling these, so the re-entrancy latch
  // in Layout::measureFor/arrangeFor is in the path.  See Layout.hpp.
  SizeHint measure(const Widget& host) const override;
  LayoutOverflow arrange(Widget& host, const Rect& content) override;

  void onChildRemoved(std::size_t index) override;

 private:
  static constexpr std::uint16_t kNoChild = 0xFFFF;

  struct Item {
    std::uint16_t childIndex = kNoChild;  // kNoChild = a spacer
    Stretch stretch;                      // only the box's own axis is read
    float fixed = 0.0f;                   // addSpacing's pixels
  };

  // Per-item working set, six floats each, laid out flat rather than as a
  // vector<struct> so there is exactly ONE buffer to keep warm -- see the
  // never-shrunk idiom on DataHub::scratch_.  arrange() and measure() only ever
  // clear() and resize() it, so after the first pass at a given item count they
  // allocate nothing at all.
  //
  // ONE buffer shared by the two of them is only safe because Layout::measureFor
  // refuses to re-enter a layout that is already measuring or arranging.  It
  // holds exactly the item count of the pass that filled it and not one float
  // more, so both loops that walk it bound themselves by ITS length as well as
  // by items_.size(): a handler that appends an item mid-pass moves the second
  // and not the first.
  //
  //   [0] size along the box's axis, grown in place by spread()
  //   [1] stretch weight
  //   [2] maximum along the axis
  //   [3] preferred along the axis
  //   [4] minimum across the axis
  //   [5] maximum across the axis
  //
  // The cross-axis pair is carried here so the placement loop does not have to
  // ask every child for its sizeHint() a second time -- for a Label that is a
  // full text measurement.
  static constexpr std::size_t kStride = 6;

  std::uint16_t weightOf(const Item& it) const {
    return o_ == Orientation::Horizontal ? it.stretch.h : it.stretch.v;
  }
  float mainOf(const Size& s) const {
    return o_ == Orientation::Horizontal ? s.width : s.height;
  }
  float crossOf(const Size& s) const {
    return o_ == Orientation::Horizontal ? s.height : s.width;
  }

  // Fills scratch_ from the tree and returns the totals the caller needs.
  // PURE: reads sizeHint(), writes no geometry.
  struct Totals {
    float min = 0.0f;      // sum of item minimums along the main axis
    float preferred = 0.0f;
    float separators = 0.0f;  // spacing() between adjacent widgets
    float crossMin = 0.0f;
    float crossPreferred = 0.0f;
  };
  Totals gather(const Widget& host) const;

  // The live, VISIBLE widget an item names, or null when the item is a spacer,
  // the widget is hidden, or the index no longer resolves.  Re-reads
  // host.children() on every call, on purpose.
  Widget* itemWidget(const Widget& host, const Item& it) const;

  // Grows the sizes in scratch_ from their minimums to fill `avail`.
  void distribute(const Totals& t, float avail) const;

  // One round of that: returns how much of `free` it managed to hand out.
  float spread(float free, bool toPreferred) const;

  std::vector<Item> items_;
  mutable std::vector<float> scratch_;  // reused across passes; never shrunk
  Orientation o_;
};

}  // namespace geeyoou
