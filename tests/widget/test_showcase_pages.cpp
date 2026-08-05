//
// The three showcase pages T-11 migrated, as a test rather than as a screenshot.
//
// This is the acceptance evidence for R2's product goals, so it asserts the
// goals themselves rather than any particular arrangement of pixels:
//
//   O1  no coordinates.  Not directly testable, but its consequence is: every
//       widget on the page has a non-empty geometry that NOBODY WROTE.  If the
//       layout did not place it, it is still at (0,0,0,0), and this file counts
//       those.
//   O2  the page's size is COMPUTED.  The builder returns what its layout asked
//       for, so `design == content.sizeHint().preferred` -- and the day somebody
//       types a number back in, that stops being true.
//   O3  the content GROWS.  Hand the page more room and the panels get bigger,
//       instead of the room turning into margin.  This is the half of the
//       showcase's "resizable without a layout engine" trick that R2 replaces.
//
// The page builders are compiled into the test binary the same way and for the
// same reason Shell.cpp already is: they are not library code, but a geometry
// regression in them is otherwise only visible by looking at a running
// application.  tests/CMakeLists.txt states the rule -- every page that hands
// its content to the layout engine is compiled in -- and that is now FIVE
// pages, not the three this file asserts against.
//
// That gap is now CLOSED: E12 adds the Icons and Layout pages below, so all
// five laid-out pages are built, measured, grown and shrunk by a case, and ASan
// runs their instructions rather than merely instrumenting them.  The remaining
// five pages (Overview, Hmi, Selects, Window, Theme) are absolute-positioned and
// deliberately outside all of this -- that they still work without a layout is
// the point.
//
// The icons page also brings CHAIN cases of its own, at the bottom of this file:
// its filter callback and its pick callback each run library code across a door,
// and neither is reachable from O1/O2/O3.
//
#include <cstddef>
#include <cstdio>
#include <string>
#include <vector>

#include "IconGallery.hpp"
#include "Pages.hpp"
#include "framework/Test.hpp"
#include "geeyoou/render/Canvas.hpp"
#include "geeyoou/render/Offscreen.hpp"
#include "geeyoou/render/Painter.hpp"
#include "geeyoou/widget/Label.hpp"
#include "geeyoou/widget/LineEdit.hpp"
#include "geeyoou/widget/ScrollArea.hpp"
#include "geeyoou/widget/Widget.hpp"

using geeyoou::Label;
using geeyoou::LineEdit;
using geeyoou::Rect;
using geeyoou::ScrollArea;
using geeyoou::Size;
using geeyoou::Widget;
using showcase::IconGallery;

namespace {
constexpr float kEps = 0.01f;

// Every descendant, because the pages nest four deep: page > band > panel >
// row > control.
void collect(Widget* w, std::vector<Widget*>& out) {
  for (const auto& c : w->children()) {
    out.push_back(c.get());
    collect(c.get(), out);
  }
}

struct PageCheck {
  std::size_t widgets = 0;
  std::size_t unplaced = 0;  // still at (0,0,0,0): nothing ever sized it
  std::size_t outside = 0;   // sticking out of its own parent
  float placedWidth = 0.0f;  // O3's one number, summed over the whole subtree
};

PageCheck inspect(Widget& content) {
  PageCheck r;
  std::vector<Widget*> all;
  collect(&content, all);
  r.widgets = all.size();
  for (Widget* w : all) {
    const Rect g = w->geometry();
    if (g.size().isEmpty()) ++r.unplaced;
    r.placedWidth += g.width();
    Widget* p = w->parent();
    // One pixel of slack for float accumulation; a real overlap is tens.
    if (p && p->geometry().width() > 0.0f &&
        g.right() > p->geometry().width() + 1.0f) {
      ++r.outside;
    }
  }
  return r;
}

// The three checks every migrated page has to pass, so a page that starts
// failing says which of O1/O2/O3 it broke rather than which line number.
//
// `minWidgets` is the page's OWN floor and is passed in rather than shared.
// It used to be one hard-coded `> 30` for every page, and that was an assumption
// about what a page LOOKS LIKE wearing the clothes of an assumption about what a
// page must SATISFY.  The icons page is the counter-example that makes the
// difference concrete: it builds ELEVEN widgets, because its 47 gallery cells
// are drawn by ONE widget on purpose (see IconGallery.hpp) -- so a page that is
// denser than any other here would have failed a check meant to catch a page
// that built nothing.
//
// What the number is actually protecting is stated as its own assertion below,
// and it is the part `> 30` was reaching for: the builder BUILT something and
// the layout PLACED it.  A builder that returned early satisfies "unplaced == 0",
// "outside == 0" and "no overflow" trivially.
void checkPage(geeyoou::test::Context& ctx_, Widget& content, const Size& design,
               std::size_t expectedUnplaced, std::size_t minWidgets) {
  // O2 first: the builder returned what the layout asked for.
  CHECK(design.width > 0.0f);
  CHECK(design.height > 0.0f);
  CHECK_NEAR(design.width, content.sizeHint().preferred.width, kEps);
  CHECK_NEAR(design.height, content.sizeHint().preferred.height, kEps);

  // O1: at the size it asked for, everything is placed, inside its parent, and
  // nothing overflowed.
  content.setGeometry({0.0f, 0.0f, design.width, design.height});
  const PageCheck atDesign = inspect(content);
  // The property, first: something was built, and the layout gave it room.
  CHECK(atDesign.widgets > atDesign.unplaced);
  CHECK(atDesign.placedWidth > 0.0f);
  // ...and then the page's own floor, which is a fact about THIS page.
  CHECK(atDesign.widgets >= minWidgets);
  CHECK_EQ(atDesign.unplaced, expectedUnplaced);
  CHECK_EQ(atDesign.outside, std::size_t(0));
  CHECK(!content.lastLayoutOverflow().any());

  // O3: more room means bigger panels, not a wider margin.
  content.setGeometry({0.0f, 0.0f, design.width * 1.6f, design.height * 1.3f});
  const PageCheck grown = inspect(content);
  CHECK(grown.placedWidth > atDesign.placedWidth);
  CHECK_EQ(grown.outside, std::size_t(0));

  // ...and back down again, without the collapse ADR-R2-09 exists to prevent.
  // The hints never read the geometry a previous pass handed out, so shrinking
  // and re-growing lands on exactly the same numbers rather than ratcheting
  // down a few pixels per resize.
  content.setGeometry({0.0f, 0.0f, design.width, design.height});
  CHECK_NEAR(inspect(content).placedWidth, atDesign.placedWidth, kEps);
}

// Finds the one descendant whose style TYPE matches, which is how this file
// reaches a widget a page built without the page having to hand it back.  The
// same virtual-call-and-string-compare route PageIcons.cpp itself uses to find
// its enclosing ScrollArea, and it needs no RTTI.
Widget* findByStyleType(Widget& root, const char* type) {
  std::vector<Widget*> all;
  collect(&root, all);
  for (Widget* w : all) {
    if (w->styleMatchesType(type)) return w;
  }
  return nullptr;
}

// Every Label under `root`, in tree order.  Used instead of "the last child of
// content" so the assertions below say what they mean -- "some label on this
// page says X" -- rather than pinning a position in the builder.
std::vector<Label*> labelsIn(Widget& root) {
  std::vector<Widget*> all;
  collect(&root, all);
  std::vector<Label*> out;
  for (Widget* w : all) {
    if (w->styleMatchesType("Label")) out.push_back(static_cast<Label*>(w));
  }
  return out;
}

// Paints the page, once, through the same Canvas -> Painter -> paintTree path a
// window uses.  Nothing is compared against a baseline -- that is test_golden's
// job and it needs a stable picture.  What this is for is the gap the whole
// suite had: a laid-out page was MEASURED by five cases and PAINTED by none, so
// every onPaint on it was compiled, instrumented by ASan, and never executed.
// A page whose paint reads an index the layout no longer has would have been
// green here and broken on screen.
void paintOnce(Widget& content, int width, int height) {
  geeyoou::OffscreenImage img(width, height, 1.0f);
  const Rect all(0.0f, 0.0f, float(width), float(height));
  geeyoou::Canvas canvas;
  if (!canvas.begin(img.surface(), all)) return;
  geeyoou::Painter p = canvas.painter();
  p.fillRect(all, geeyoou::Color::rgb(18, 20, 24));
  content.paintTree(p, all, all);
  canvas.end();
}

std::size_t labelsStartingWith(Widget& root, const std::string& prefix) {
  std::size_t n = 0;
  for (Label* l : labelsIn(root)) {
    if (l->text().rfind(prefix, 0) == 0) ++n;
  }
  return n;
}

}  // namespace

GEEYOOU_TEST(showcase_pages, the_widgets_page_is_placed_sized_and_grown_by_its_layout) {
  Widget content;
  const Size design = showcase::buildWidgetsPage(&content);
  checkPage(ctx_, content, design, /*expectedUnplaced=*/0, /*minWidgets=*/30);
}

GEEYOOU_TEST(showcase_pages, the_inputs_page_is_placed_sized_and_grown_by_its_layout) {
  Widget content;
  const Size design = showcase::buildInputsPage(&content);
  checkPage(ctx_, content, design, /*expectedUnplaced=*/0, /*minWidgets=*/30);
}

GEEYOOU_TEST(showcase_pages, the_ops_page_is_placed_sized_and_grown_by_its_layout) {
  // The one page with live data behind it.  No acquisition thread is started --
  // the builder only drains the (empty) alarm backlog and installs a sink.
  showcase::AppState app;
  Widget content;
  const Size design = showcase::buildOpsPage(&content, app);

  // One widget on this page is deliberately in no layout and has no size: the
  // Ticker, which draws nothing and exists to receive animation ticks.  Stated
  // as a number so that a SECOND widget going missing is a failure.
  checkPage(ctx_, content, design, /*expectedUnplaced=*/1, /*minWidgets=*/30);

  CHECK(app.alarmSink != nullptr);
}

// ============================================================ E12: two more ==
//
// The two pages that shipped outside every gate.  Same three goals, same
// function, and the only thing either of them needs of its own is an honest
// floor for how many widgets it builds.

GEEYOOU_TEST(showcase_pages, the_layout_page_is_placed_sized_and_grown_by_its_layout) {
  Widget content;
  const Size design = showcase::buildLayoutPage(&content);

  // The page whose SUBJECT is the layout engine: swatches in a row that stretch,
  // a fixed one that does not, a capped one that stops -- plus one deliberately
  // hand-placed panel demonstrating that absolute coordinates still work.
  //
  // ONE widget is deliberately "unplaced" by inspect()'s definition, and it is
  // the same kind of exception the Ops page's Ticker is: the ResizeProbe asks
  // for a full-width, ZERO-HEIGHT slot, because it exists to receive
  // onGeometryChanged and to draw nothing.  An empty rectangle is exactly what
  // it asked for, so the number is stated rather than the definition loosened --
  // and the two lines below say WHICH widget it is, so a second one going empty
  // is a failure rather than a silent swap.
  checkPage(ctx_, content, design, /*expectedUnplaced=*/1, /*minWidgets=*/20);

  Widget* probe = findByStyleType(content, "ResizeProbe");
  CHECK(probe != nullptr);
  if (probe) {
    CHECK(probe->geometry().width() > 0.0f);   // the layout DID reach it...
    CHECK_EQ(probe->geometry().height(), 0.0f);  // ...and it asked for no height
  }
}

GEEYOOU_TEST(showcase_pages, the_icons_page_is_placed_sized_and_grown_by_its_layout) {
  Widget content;
  const Size design = showcase::buildIconsPage(&content);

  // ELEVEN widgets, and that number is the reason checkPage takes a floor per
  // page instead of sharing one: the gallery draws 47 cells with a single
  // widget, so the densest page in the showcase is also the one with the fewest
  // objects in it.  A floor of 8 leaves room for the search box's own internals
  // to change without this case having an opinion about them.
  checkPage(ctx_, content, design, /*expectedUnplaced=*/0, /*minWidgets=*/8);

  // The page built with no enclosing ScrollArea, which is the case the filter
  // callback has to survive: enclosingScrollArea() returns null and the chain
  // must simply do nothing rather than dereference it.  Asserted here because
  // buildIconsPage() ends by calling gallery->select(0), which runs the pick
  // chain, and this case is the one that runs it without a host.
  CHECK_EQ(labelsStartingWith(content, "已选择 "), std::size_t(1));

  // ...and it PAINTS.  The gallery's flow, the elide loop over every real
  // registry name, the preview's four boxes and its two code chips all run here
  // and nowhere else in the suite.  Twice: at the design size, and at a width
  // narrow enough to force the cells down to kCellMinW, which is where the
  // elide loop does its work.
  content.setGeometry({0.0f, 0.0f, design.width, design.height});
  paintOnce(content, int(design.width), int(design.height));
  content.setGeometry({0.0f, 0.0f, 380.0f, design.height});
  paintOnce(content, 380, int(design.height));
}

// ================================================= the icons page's chains ===
//
// Two callbacks the O1/O2/O3 checks cannot see, because neither of them changes
// a rectangle that a static inspection of the built page would notice.
//
// Both are built inside a REAL ScrollArea rather than on a bare Widget: the
// filter chain's whole subject is `enclosingScrollArea(content)`, and a case
// that ran it with no host would be asserting that a null check works.

namespace {

// The same host relationship Shell.cpp builds: a ScrollArea per page, the page
// living in its content().
struct HostedIconsPage {
  Widget root;
  ScrollArea* area = nullptr;
  Widget* content = nullptr;
  Size design;

  HostedIconsPage() {
    root.setGeometry({0.0f, 0.0f, 1000.0f, 700.0f});
    area = root.add<ScrollArea>();
    area->setGeometry({0.0f, 0.0f, 900.0f, 620.0f});
    content = area->content();
    design = showcase::buildIconsPage(content);
    area->setContentSize(design);
  }
};

}  // namespace

GEEYOOU_TEST(showcase_pages, filtering_the_icons_page_retells_the_scroll_area_how_tall_it_is) {
  HostedIconsPage page;
  CHECK(page.content != nullptr);

  IconGallery* gallery =
      static_cast<IconGallery*>(findByStyleType(*page.content, "IconGallery"));
  Widget* searchWidget = findByStyleType(*page.content, "SearchField");
  CHECK(gallery != nullptr);
  CHECK(searchWidget != nullptr);
  if (!gallery || !searchWidget) return;

  const int everything = gallery->visibleCount();
  CHECK(everything > 0);
  const float tallHeight = page.area->contentSize().height;
  CHECK(tallHeight > 0.0f);

  // Typing into the search box is the whole chain: LineEdit::setText emits
  // textChanged -> the page's slot calls gallery->setFilter, refreshes the
  // count label, measures the page and calls sa->setContentSize.  Nothing here
  // reaches into the page's internals to shortcut it.
  static_cast<LineEdit*>(searchWidget)->setText("chevron");

  const int filtered = gallery->visibleCount();
  CHECK(filtered > 0);           // "chevron-down" and friends are built in
  CHECK(filtered < everything);  // ...and the filter really removed something

  // L-4's observable half.  The scroll area must now describe the SHORTER page.
  // Before E12 nothing asserted it, and the bug the fix was aimed at -- a host
  // pointer resolved in front of the measuring door -- would have left this
  // number describing the unfiltered page (or worse).
  const float shortHeight = page.area->contentSize().height;
  CHECK(shortHeight > 0.0f);
  CHECK(shortHeight < tallHeight);

  // The count readout followed too, which is the other half of the same slot.
  char want[64];
  std::snprintf(want, sizeof(want), "显示 %d / %d", filtered, gallery->total());
  CHECK_EQ(labelsStartingWith(*page.content, want), std::size_t(1));

  // ...and clearing it goes all the way back to the number it started at, with
  // no ratcheting: the gallery's hint is pure, so the round trip is exact.
  static_cast<LineEdit*>(searchWidget)->setText("");
  CHECK_EQ(gallery->visibleCount(), everything);
  CHECK_NEAR(page.area->contentSize().height, tallHeight, kEps);
}

GEEYOOU_TEST(showcase_pages, picking_an_icon_updates_the_preview_and_the_status_line) {
  HostedIconsPage page;
  IconGallery* gallery =
      static_cast<IconGallery*>(findByStyleType(*page.content, "IconGallery"));
  CHECK(gallery != nullptr);
  if (!gallery) return;

  // buildIconsPage ends with select(0), so the chain has already run once and
  // exactly one label carries the status line.
  CHECK_EQ(labelsStartingWith(*page.content, "已选择 "), std::size_t(1));
  CHECK_EQ(gallery->selectedIndex(), 0);

  const int last = gallery->total() - 1;
  CHECK(last > 0);
  gallery->select(last);

  CHECK_EQ(gallery->selectedIndex(), last);
  // The status line names the icon that was picked -- which is the assertion
  // that the ENTRY reached the callback intact, not just that some callback ran.
  const std::string want = "已选择 " + gallery->entryAt(last).name + "（";
  CHECK_EQ(labelsStartingWith(*page.content, want), std::size_t(1));
  // Still exactly one status line: the slot replaced the text rather than the
  // page growing a second label.
  CHECK_EQ(labelsStartingWith(*page.content, "已选择 "), std::size_t(1));

  // The preview widget exists and was told about it.  Its content is drawn
  // rather than stored as text, so what is asserted here is the wiring: a page
  // whose preview was never added would have failed to find it.
  CHECK(findByStyleType(*page.content, "IconPreview") != nullptr);
}
