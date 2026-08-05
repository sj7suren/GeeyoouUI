#pragma once
//
// GridLayout: rows and columns, with spans.
//
// SPANS ARE THE WHOLE DESIGN PROBLEM HERE, and ADR-R2-10 is the ruling that
// shapes this class.  A cell with colSpan=3 does not tell you how wide any one
// of the three columns has to be: it states a requirement about their SUM, and
// how much of any shortfall each column should absorb only becomes answerable
// once the single-column cells have had their say.  There is no closed form.
//
// The answer is not a solver and not an iteration to a fixed point -- both of
// which can fail to terminate on a screen that has to stay up for months.  It
// is a SINGLE PASS IN ASCENDING SPAN ORDER:
//
//   * every span-1 cell sets its column's minimum and preferred size;
//   * then every span-2 cell asks whether the two columns it covers (plus the
//     spacing between them) already add up to what it needs, and if not shares
//     the difference out over them;
//   * then span-3, and so on.
//
// Each batch only ever reads what earlier batches settled, so the pass visits
// every cell once, terminates by construction, and gives the same answer every
// time.  The shortfall goes to the STRETCHABLE columns a cell covers -- those
// are the ones the author said may grow -- and only if it covers none of them
// is it spread over all of them.
//
// Items are addressed by CHILD INDEX, never by pointer: see Layout.hpp.
//
#include <cstddef>
#include <cstdint>
#include <vector>

#include "geeyoou/widget/Layout.hpp"

namespace geeyoou {

class Widget;

class GridLayout final : public Layout {
 public:
  GridLayout() = default;

  // `child` must ALREADY be a child of the host.  Cells may not overlap; the
  // class does not check that, and two cells claiming the same square simply
  // end up on top of each other.
  void addWidget(Widget* child, int row, int col, int rowSpan = 1, int colSpan = 1);

  // The label/field pair every parameter form is made of: appends a new row
  // with `label` in column 0 and `field` in column 1, and makes column 1 the
  // one that grows.  An explicit setColumnStretch(1, n) still wins -- this only
  // fills in a stretch of zero.
  void addRow(Widget* label, Widget* field);

  void setColumnStretch(int col, std::uint16_t s);
  void setRowStretch(int row, std::uint16_t s);

  // Derived from the cells, so both answer correctly before the first pass.
  std::size_t columnCount() const { return trackCount(Axis::Columns); }
  std::size_t rowCount() const { return trackCount(Axis::Rows); }

  SizeHint measure(const Widget& host) const override;
  LayoutOverflow arrange(Widget& host, const Rect& content) override;

 protected:
  void onChildRemoved(std::size_t index) override;

 private:
  static constexpr std::uint16_t kNoChild = 0xFFFF;

  struct Cell {
    std::uint16_t childIndex = kNoChild;
    std::uint16_t row = 0;
    std::uint16_t col = 0;
    std::uint16_t rowSpan = 1;
    std::uint16_t colSpan = 1;
  };

  enum class Axis { Columns, Rows };

  // Clamps a caller's row/column number, and a span, into what this class can
  // address.  Static because they answer for the type, not for a grid.
  static std::uint16_t toIndex(int v);
  static std::uint16_t toSpan(int v);

  Widget* cellWidget(const Widget& host, const Cell& c) const;

  // How many tracks one axis has: the furthest any cell reaches, or the
  // furthest a stretch factor was set on, whichever is greater.
  std::size_t trackCount(Axis axis) const;

  // Sizes every track on one axis from the cells, in ascending span order.
  // PURE: reads sizeHint(), writes no geometry.
  void measureAxis(const Widget& host, Axis axis) const;

  // Spreads `deficit` over the tracks [first, first+span) of `track`, favouring
  // the stretchable ones.
  void shareDeficit(std::vector<float>& track, std::size_t first,
                    std::size_t span, float deficit, Axis axis) const;

  // Grows the minimums towards the preferred sizes and then by stretch weight,
  // exactly as BoxLayout does, into `avail` pixels.  Leaves the solved sizes in
  // colMin_ / rowMin_ and returns how many pixels the axis is short.
  float solveAxis(Axis axis, float avail) const;

  // One round of that: returns how much of `free` it managed to hand out.
  float spreadTracks(Axis axis, float free, bool toPreferred) const;

  const std::vector<std::uint16_t>& stretchOf(Axis axis) const {
    return axis == Axis::Columns ? colStretch_ : rowStretch_;
  }

  // The largest row or column index this class accepts.  A grid here is a
  // parameter form or a mimic panel, not a spreadsheet, and every track costs a
  // float in six vectors on every pass -- so a typo'd setColumnStretch(100000)
  // used to make each pass resize them all to 131068 entries.  Anything past
  // this is clamped, asserted in Debug and counted in
  // detail::layoutDiagnostics().indexClamped.
  static constexpr int kMaxTrack = 4096;

  std::vector<Cell> cells_;
  // Per-track working set.  Cleared and refilled every pass, never shrunk --
  // the never-shrunk idiom from DataHub::scratch_ -- so a grid whose shape is
  // stable allocates once and then never again.  colMin_/rowMin_ carry the
  // running minimum on the way in and the SOLVED size on the way out.
  mutable std::vector<float> colMin_, colPref_, rowMin_, rowPref_;
  // The running origin of each track, filled by arrange().
  //
  // This used to be a second life for colPref_/rowPref_, on the grounds that
  // the preferred sizes had done their job by then and a fifth and sixth buffer
  // would be carried by every grid in the process for the length of one
  // function.  That was a bad trade: it aliased two meanings onto one buffer,
  // so any measure() that landed between the two -- and asking a container for
  // its size hint from inside a geometry change is ordinary application code --
  // turned every offset back into a preferred size, and every cell after that
  // point landed on top of the first.  Silently: no dirty flag is raised, so
  // the wrong geometry is the final picture.  Two vectors, 48 bytes per grid.
  mutable std::vector<float> colOff_, rowOff_;
  mutable std::vector<std::uint16_t> colStretch_, rowStretch_;
  // Cell indices sorted by span, ascending: the batch order ADR-R2-09 requires.
  // Rebuilt for each axis in turn, out of the same buffer.  32 bits, not 16: a
  // grid may legally hold more than 65535 cells, and an index that wrapped
  // would measure some of them twice and others never.
  mutable std::vector<std::uint32_t> spanOrder_;
};

}  // namespace geeyoou
