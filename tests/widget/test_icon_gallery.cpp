//
// IconGallery, as a widget rather than as part of a page.
//
// The page-level cases (tests/widget/test_showcase_pages.cpp) assert O1/O2/O3
// -- that the layout placed everything, computed the page size and grew it.
// They cannot assert any of what is below, and the difference is not one of
// convenience:
//
//   * L-2 is a use-after-free that only fires when the PICK CALLBACK replaces
//     the data set, which no page does today and every picker eventually will;
//   * L-3 is a wrong HIGHLIGHT, not a crash and not a wrong size -- a page-level
//     geometry assertion is structurally blind to it;
//   * the state matrix (no match / one / all / a name longer than a cell) is a
//     property of the GRID.  Reaching it through a page would mean typing into
//     a search box and asserting on the page's height, i.e. asserting the
//     gallery's contract through two layers that have contracts of their own.
//
// This is also the first runtime coverage IconGallery has ever had: until E12
// the whole of PageIcons.cpp was COMPILED into the test binary and never CALLED,
// so ASan instrumented it and then never executed one instruction of it.
//
#include <cstddef>
#include <string>
#include <vector>

#include "IconGallery.hpp"
#include "framework/Test.hpp"
#include "geeyoou/render/Canvas.hpp"
#include "geeyoou/render/Icon.hpp"
#include "geeyoou/render/IconRegistry.hpp"
#include "geeyoou/render/Offscreen.hpp"
#include "geeyoou/render/Painter.hpp"
#include "geeyoou/widget/BoxLayout.hpp"
#include "geeyoou/widget/Widget.hpp"

using geeyoou::BoxLayout;
using geeyoou::Canvas;
using geeyoou::Icon;
using geeyoou::IconEntry;
using geeyoou::OffscreenImage;
using geeyoou::Painter;
using geeyoou::Rect;
using geeyoou::Size;
using geeyoou::SizeHint;
using geeyoou::Widget;
using showcase::IconGallery;

namespace {

// A data set the case owns outright.  icons().all() is process-wide state that
// PlantIcons.cpp mutates, so a case that measured against it would be asserting
// about whichever cases ran before it.
std::vector<IconEntry> makeSet(int n, const char* prefix, const char* category) {
  std::vector<IconEntry> v;
  v.reserve(std::size_t(n));
  for (int i = 0; i < n; ++i) {
    IconEntry e;
    e.id = Icon::Search;
    e.name = std::string(prefix) + "-" + std::to_string(i);
    e.category = category;
    e.builtin = (i % 2 == 0);
    v.push_back(std::move(e));
  }
  return v;
}

// Paints the widget through the same Canvas -> Painter -> paintTree path a real
// window uses.  Nothing is compared against a baseline: what these cases need is
// for onPaint to actually RUN, so that the flow arithmetic, the elide loop and
// the empty-set placeholder are executed rather than merely compiled.
void renderOnce(Widget& w, int width, int height) {
  OffscreenImage img(width, height, 1.0f);
  const Rect all(0.0f, 0.0f, float(width), float(height));
  Canvas canvas;
  if (!canvas.begin(img.surface(), all)) return;
  Painter p = canvas.painter();
  p.fillRect(all, geeyoou::Color::rgb(0, 0, 0));
  w.paintTree(p, all, all);
  canvas.end();
}

}  // namespace

// ================================================================== L-2 ======
//
// The pick callback is handed an ENTRY, and it may do anything an application
// does with one -- including replacing the data set the gallery is showing.
//
// Before the fix, onPicked was `std::function<void(const IconEntry&)>` and
// select() called `onPicked(all_[index])`, so the callback's parameter was a
// reference INTO all_.  `setEntries()` is `all_ = std::move(all)`, which frees
// that buffer; every read of the argument after that line is a use-after-free.
// Both configurations without a sanitiser sail through it -- a freed
// std::string's pointer is still the right bits for a few microseconds -- so
// this case is written to be red on the ASAN LEG and is expected to pass in
// Release and Debug either way.  That is not a weakness of the case; it is the
// same property verify.bat's header states about the whole third leg.
GEEYOOU_TEST(icon_gallery, a_pick_survives_a_callback_that_replaces_the_data_set) {
  IconGallery g;
  g.setEntries(makeSet(12, "alpha", "组一"));

  // Read INSIDE the callback and after the data set has been replaced, which is
  // the exact ordering the defect needs.  Copied out into the case's own storage
  // so the assertions below are not themselves reading the thing under test.
  std::string seenName;
  std::string seenCategory;
  bool seenBuiltin = false;
  int calls = 0;

  // THE PARAMETER IS DECLARED `const IconEntry&` ON PURPOSE, and that is what
  // makes this case an experiment rather than a restatement of the fix.  A
  // lambda taking IconEntry BY VALUE would copy on entry whatever the
  // std::function's signature is, so it would be safe either way and would prove
  // nothing.  Written like this, the only thing standing between the body below
  // and a freed buffer is the type of onPicked: bound to `void(IconEntry)` the
  // reference names std::function's own parameter, which is already a copy;
  // bound to `void(const IconEntry&)` it names all_[7] itself.
  g.onPicked = [&g, &seenName, &seenCategory, &seenBuiltin,
                &calls](const IconEntry& e) {
    ++calls;
    // The whole point of the case: the buffer `e` used to alias is freed HERE.
    g.setEntries(makeSet(2, "beta", "组二"));
    seenName = e.name;
    seenCategory = e.category;
    seenBuiltin = e.builtin;
  };

  g.select(7);

  CHECK_EQ(calls, 1);
  // alpha-7, not beta-anything: the entry handed to the callback describes the
  // data set that was current when the pick happened, and it keeps describing it
  // after that set is gone.  A reference would have read whatever the freed
  // block held.
  CHECK(seenName == "alpha-7");
  CHECK(seenCategory == "组一");
  CHECK_EQ(seenBuiltin, false);  // index 7 is odd -> not builtin

  // ...and the replacement really did happen inside the callback, so the case is
  // testing the ordering it claims to.
  CHECK_EQ(g.total(), 2);
}

// ================================================================== L-3 ======
//
// A new DATA SET and a new FILTER are different events, and they were being
// handled by the same function.  rebuild() cleared the hover and nothing ever
// cleared the selection, so:
//
//   * setEntries() left `selected_` pointing at an index into the OLD vector.
//     Index 30 of the built-in set and index 30 of a two-icon plant set are not
//     the same icon; the grid highlighted whatever landed there, or nothing at
//     all when the new set was shorter, while the preview still showed the icon
//     from before.  A wrong highlight is not a crash, so nothing was ever going
//     to notice it except a person looking at the screen.
//   * setFilter() must NOT clear it -- the preview deliberately keeps showing
//     what you picked while you type the name you are about to copy.
//
// So the case asserts BOTH halves.  A fix that cleared the selection in
// rebuild() would pass the first half and fail the second, which is why the
// second half is here at all.
GEEYOOU_TEST(icon_gallery, a_new_data_set_drops_the_selection_and_a_new_filter_keeps_it) {
  IconGallery g;
  g.setEntries(makeSet(40, "alpha", "组一"));

  g.select(30);
  CHECK_EQ(g.selectedIndex(), 30);
  // The highlight and the selection are the same fact: paintCell compares each
  // cell's index against exactly this number.
  CHECK(g.entryAt(g.selectedIndex()).name == "alpha-30");

  // --- a filter KEEPS it ---------------------------------------------------
  g.setFilter("alpha-3");
  CHECK_EQ(g.selectedIndex(), 30);
  CHECK(g.entryAt(g.selectedIndex()).name == "alpha-30");
  CHECK_EQ(g.total(), 40);  // filtering does not remove entries, only hides them

  g.setFilter("");
  CHECK_EQ(g.selectedIndex(), 30);

  // --- a new data set DROPS it ---------------------------------------------
  //
  // Deliberately SHORTER than the old one and with different names, which is
  // what makes the stale index observable in both directions: 30 is out of range
  // for the new set, and if it were not, it would name a different icon.
  g.setEntries(makeSet(4, "beta", "组二"));
  CHECK_EQ(g.total(), 4);
  CHECK_EQ(g.selectedIndex(), -1);
  // The invariant the number above is standing for, stated so a future change
  // that re-maps rather than clears still has something true to satisfy: the
  // selection is either "none" or an index into the CURRENT set.
  CHECK(g.selectedIndex() < g.total());

  // And a pick in the new set lands on the new set.
  g.select(2);
  CHECK_EQ(g.selectedIndex(), 2);
  CHECK(g.entryAt(g.selectedIndex()).name == "beta-2");
}

// ========================================================== state matrix =====
//
// Four states, one property each, and one property they all share.
//
// THE SHARED PROPERTY IS THE IMPORTANT ONE: sizeHint().preferred.height must be
// strictly positive in every state.  This widget is a vertical item in a
// BoxLayout inside a GroupBox; a zero or negative height is handed straight to
// the column, the group box closes up to its own frame, and on the EMPTY state
// the casualty is the "no match" message itself -- the one thing on screen that
// tells the operator why the grid is blank.  Nothing had ever asserted it.
GEEYOOU_TEST(icon_gallery, the_state_matrix_never_collapses_the_box_that_holds_it) {
  // --- 1. the FULL set ------------------------------------------------------
  IconGallery g;
  g.setEntries(makeSet(47, "icon", "组一"));
  const SizeHint full = g.sizeHint();
  CHECK_EQ(g.visibleCount(), 47);
  CHECK(full.preferred.height > 0.0f);
  CHECK(full.preferred.width > 0.0f);
  CHECK(full.min.width > 0.0f);
  // The hint is PURE: same answer before and after the widget is given a
  // rectangle, which is ADR-R2-09 held rather than quoted.
  g.setGeometry({0.0f, 0.0f, 900.0f, full.preferred.height});
  CHECK_NEAR(g.sizeHint().preferred.height, full.preferred.height, 0.01f);
  renderOnce(g, 900, int(full.preferred.height) + 4);

  // --- 2. ONE match ---------------------------------------------------------
  //
  // Strictly shorter than the full set and still strictly positive: one row of
  // one cell, plus its group heading.
  g.setFilter("icon-31");
  CHECK_EQ(g.visibleCount(), 1);
  const SizeHint one = g.sizeHint();
  CHECK(one.preferred.height > 0.0f);
  CHECK(one.preferred.height < full.preferred.height);
  renderOnce(g, 900, int(one.preferred.height) + 4);

  // --- 3. NO match ----------------------------------------------------------
  //
  // The state nobody had tested.  The height must be the placeholder's, not
  // zero: see kEmptyH in IconGallery.hpp for why a zero here is a defect and
  // not merely a tight fit.
  g.setFilter("这个名字不在任何图标里");
  CHECK_EQ(g.visibleCount(), 0);
  const SizeHint none = g.sizeHint();
  CHECK(none.preferred.height > 0.0f);
  CHECK_NEAR(none.preferred.height, showcase::kEmptyH, 0.01f);
  // min/max agree with preferred on the height, so a column cannot squeeze the
  // placeholder out of existence either.
  CHECK_NEAR(none.min.height, none.preferred.height, 0.01f);
  CHECK_NEAR(none.max.height, none.preferred.height, 0.01f);
  CHECK(none.min.width > 0.0f);
  // ...and it survives being painted, which is where the placeholder text is.
  g.setGeometry({0.0f, 0.0f, 900.0f, none.preferred.height});
  renderOnce(g, 900, int(none.preferred.height) + 4);

  // --- 4. a NAME LONGER THAN THE CELL --------------------------------------
  //
  // 300 characters into a cell that is at most ~120 wide.  The elide loop is the
  // thing under test: it must terminate, it must not hand the shaper a negative
  // width, and it must not walk off the front of the string.  Painted at a
  // deliberately NARROW width so the loop runs to its worst case.
  std::vector<IconEntry> longNames;
  IconEntry big;
  big.id = Icon::Search;
  big.name = std::string(300, 'w');
  big.category = "组一";
  big.builtin = true;
  longNames.push_back(big);
  IconEntry cjk;
  cjk.id = Icon::Search;
  cjk.name = "这是一个非常非常长的中文图标名字用来把省略号的多字节回退路径走一遍";
  cjk.category = "组一";
  cjk.builtin = false;
  longNames.push_back(cjk);

  g.setFilter("");
  g.setEntries(std::move(longNames));
  const SizeHint wide = g.sizeHint();
  CHECK_EQ(g.visibleCount(), 2);
  CHECK(wide.preferred.height > 0.0f);
  // A 300-character name must not make the widget 300 characters wide: the hint
  // is a function of the COUNT and the constants, never of the content.
  CHECK_NEAR(wide.preferred.width, full.preferred.width, 0.01f);
  g.setGeometry({0.0f, 0.0f, 320.0f, wide.preferred.height});
  renderOnce(g, 320, int(wide.preferred.height) + 4);

  // --- and the whole matrix inside a real column ---------------------------
  //
  // The property the four states above exist to protect, asserted where it
  // actually bites: a gallery in a vertical BoxLayout must be given a non-empty
  // rectangle in EVERY state, the empty one included.
  Widget host;
  BoxLayout* col = host.setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
  IconGallery* inColumn = host.add<IconGallery>();
  col->addWidget(inColumn);
  inColumn->setEntries(makeSet(9, "icon", "组一"));
  host.setGeometry({0.0f, 0.0f, 900.0f, host.sizeHint().preferred.height});
  CHECK(inColumn->geometry().height() > 0.0f);

  inColumn->setFilter("没有任何东西匹配这个");
  host.setGeometry({0.0f, 0.0f, 900.0f, host.sizeHint().preferred.height});
  CHECK_EQ(inColumn->visibleCount(), 0);
  CHECK(inColumn->geometry().height() > 0.0f);
  CHECK_NEAR(inColumn->geometry().height(), showcase::kEmptyH, 0.01f);
}
