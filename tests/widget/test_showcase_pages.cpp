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
// The three page builders are compiled into the test binary the same way and
// for the same reason Shell.cpp already is: they are not library code, but a
// geometry regression in them is otherwise only visible by looking at a running
// application.  The other five pages are deliberately NOT here -- they are
// still absolute, and that they still work is the point.
//
#include <cstddef>
#include <vector>

#include "Pages.hpp"
#include "framework/Test.hpp"
#include "geeyoou/widget/Widget.hpp"

using geeyoou::Rect;
using geeyoou::Size;
using geeyoou::Widget;

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
void checkPage(geeyoou::test::Context& ctx_, Widget& content, const Size& design,
               std::size_t expectedUnplaced) {
  // O2 first: the builder returned what the layout asked for.
  CHECK(design.width > 0.0f);
  CHECK(design.height > 0.0f);
  CHECK_NEAR(design.width, content.sizeHint().preferred.width, kEps);
  CHECK_NEAR(design.height, content.sizeHint().preferred.height, kEps);

  // O1: at the size it asked for, everything is placed, inside its parent, and
  // nothing overflowed.
  content.setGeometry({0.0f, 0.0f, design.width, design.height});
  const PageCheck atDesign = inspect(content);
  CHECK(atDesign.widgets > 30);
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

}  // namespace

GEEYOOU_TEST(showcase_pages, the_widgets_page_is_placed_sized_and_grown_by_its_layout) {
  Widget content;
  const Size design = showcase::buildWidgetsPage(&content);
  checkPage(ctx_, content, design, /*expectedUnplaced=*/0);
}

GEEYOOU_TEST(showcase_pages, the_inputs_page_is_placed_sized_and_grown_by_its_layout) {
  Widget content;
  const Size design = showcase::buildInputsPage(&content);
  checkPage(ctx_, content, design, /*expectedUnplaced=*/0);
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
  checkPage(ctx_, content, design, /*expectedUnplaced=*/1);

  CHECK(app.alarmSink != nullptr);
}
