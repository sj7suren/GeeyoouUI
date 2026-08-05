//
// Geometry baseline: the four load-bearing relayout() implementations.
//
// AppWindow, WindowHeader, ScrollArea and Shell between them place every pixel
// of the six showcase pages, and until now NOT ONE of the 88 existing cases
// touched them -- the suite would have stayed green while the whole shell
// collapsed into a corner.  That gap is the reason this file exists, and it is
// written BEFORE the layout engine touches Widget::setGeometry: these cases are
// the safety net for making that call idempotent (docs/iterations/02-layout-
// engine.md, T-02), not a description of the engine.
//
// So the assertions here are deliberately about EXISTING behaviour and nothing
// else.  Every number is one a showcase page depends on today.
//
#include <string>
#include <vector>

#include "framework/Test.hpp"
#include "geeyoou/core/ConnectionScope.hpp"
#include "geeyoou/platform/Platform.hpp"
#include "geeyoou/widget/AppWindow.hpp"
#include "geeyoou/widget/ScrollArea.hpp"
#include "geeyoou/widget/WindowHeader.hpp"
#include "showcase/Shell.hpp"

using geeyoou::AppWindow;
using geeyoou::ConnectionScope;
using geeyoou::Rect;
using geeyoou::ScrollArea;
using geeyoou::Size;
using geeyoou::Widget;
using geeyoou::WindowHeader;
using geeyoou::WindowOptions;

namespace {
constexpr float kEps = 0.0005f;

// A framed AppWindow: borderWidth() is 0 for it by the same branch that zeroes
// it while maximised, so it pins that arm of the formula without needing a
// window on screen.
WindowOptions framedOptions() {
  WindowOptions o;
  o.frameless = false;
  o.resizable = true;
  o.minSize = {320.0f, 240.0f};
  return o;
}
}  // namespace

// Four CHECK_NEARs, because a Rect comparison that failed would otherwise print
// "<?>" and leave the reader guessing which edge moved.
#define CHECK_RECT(r, X, Y, W, H)                                            \
  do {                                                                        \
    const ::geeyoou::Rect gy_rect_ = (r);                                     \
    CHECK_NEAR(gy_rect_.x(), (X), kEps);                                      \
    CHECK_NEAR(gy_rect_.y(), (Y), kEps);                                      \
    CHECK_NEAR(gy_rect_.width(), (W), kEps);                                  \
    CHECK_NEAR(gy_rect_.height(), (H), kEps);                                 \
  } while (0)

// ============================================================== AppWindow ===
//
// relayout() splits the client area into header + content, both inset by
// borderWidth().  Every application window in the library is this shape.
GEEYOOU_TEST(geom_baseline, appwindow_splits_client_area_into_header_and_content) {
  AppWindow win("geeyoou geometry baseline", 800, 600);
  REQUIRE(win.header() != nullptr);
  REQUIRE(win.content() != nullptr);

  // Frameless, border visible, not maximised => a 1px inset on all four sides.
  const float hh = win.header()->height();
  CHECK_NEAR(hh, 44.0f, kEps);  // the default the showcase is drawn against

  CHECK_RECT(win.header()->geometry(), 1.0f, 1.0f, 798.0f, hh);
  CHECK_RECT(win.content()->geometry(), 1.0f, 1.0f + hh, 798.0f, 598.0f - hh);

  // setContent<T> keeps its widget filling the content area, in CONTENT-local
  // coordinates -- it is a child of content_, not a sibling.
  Widget* fill = win.setContent<Widget>();
  REQUIRE(fill != nullptr);
  CHECK_RECT(fill->geometry(), 0.0f, 0.0f, 798.0f, 598.0f - hh);

  // contentResized carries exactly the content area's size.
  Size announced{-1.0f, -1.0f};
  ConnectionScope conns;
  conns += win.contentResized.connect([&](Size s) { announced = s; });
  win.relayout();
  CHECK_NEAR(announced.width, 798.0f, kEps);
  CHECK_NEAR(announced.height, 598.0f - hh, kEps);

  // A taller bar pushes the content down and shortens it by the same amount.
  win.header()->setHeight(56.0f);
  CHECK_RECT(win.header()->geometry(), 1.0f, 1.0f, 798.0f, 56.0f);
  CHECK_RECT(win.content()->geometry(), 1.0f, 57.0f, 798.0f, 542.0f);
  CHECK_RECT(fill->geometry(), 0.0f, 0.0f, 798.0f, 542.0f);

  // A hidden header still gets a rect, of zero height, and the content takes
  // the whole inset area.
  win.setHeaderVisible(false);
  CHECK_RECT(win.header()->geometry(), 1.0f, 1.0f, 798.0f, 0.0f);
  CHECK_RECT(win.content()->geometry(), 1.0f, 1.0f, 798.0f, 598.0f);
}

GEEYOOU_TEST(geom_baseline, appwindow_border_width_decides_the_inset) {
  AppWindow win("geeyoou geometry baseline", 800, 600);
  const float hh = win.header()->height();

  // Turning the outline off is the OTHER way to reach borderWidth() == 0 -- the
  // same arm the maximised window takes -- and children move flush to the edge.
  win.setBorderVisible(false);
  CHECK(!win.isBorderVisible());
  CHECK_RECT(win.header()->geometry(), 0.0f, 0.0f, 800.0f, hh);
  CHECK_RECT(win.content()->geometry(), 0.0f, hh, 800.0f, 600.0f - hh);

  win.setBorderVisible(true);
  CHECK_RECT(win.header()->geometry(), 1.0f, 1.0f, 798.0f, hh);

  // A FRAMED window never draws the hairline: the OS already drew one.
  AppWindow framed("geeyoou geometry baseline (framed)", 800, 600, framedOptions());
  const float fh = framed.header()->height();
  CHECK(framed.isBorderVisible());  // the property is on; borderWidth() is not
  CHECK_RECT(framed.header()->geometry(), 0.0f, 0.0f, 800.0f, fh);
  CHECK_RECT(framed.content()->geometry(), 0.0f, fh, 800.0f, 600.0f - fh);
}

GEEYOOU_TEST(geom_baseline, appwindow_maximised_drops_the_border_inset) {
  // The only case in this file that puts a window on screen, and briefly: the
  // maximised inset is decided by IsZoomed(), which nothing but a real
  // ShowWindow() can turn on.  Assertions are relative to the client size the
  // window manager hands back, so the case says the same thing on every monitor.
  AppWindow win("geeyoou geometry baseline (maximised)", 800, 600);
  const float hh = win.header()->height();

  win.maximize();
  REQUIRE(win.isMaximized());
  // AppWindow keeps the glyph in step -- restore, not maximise, while zoomed.
  CHECK(win.header()->isMaximized());

  const Rect client = win.localRect();
  CHECK(client.width() > 0.0f);
  CHECK(client.height() > 0.0f);

  CHECK_RECT(win.header()->geometry(), 0.0f, 0.0f, client.width(), hh);
  CHECK_RECT(win.content()->geometry(), 0.0f, hh, client.width(),
             client.height() - hh);

  win.restore();
  REQUIRE(!win.isMaximized());
  CHECK(!win.header()->isMaximized());
  // Restored: the hairline is back, so both children are inset by 1 again.
  const Rect back = win.localRect();
  CHECK_RECT(win.header()->geometry(), 1.0f, 1.0f, back.width() - 2.0f, hh);
  CHECK_RECT(win.content()->geometry(), 1.0f, 1.0f + hh, back.width() - 2.0f,
             back.height() - 2.0f - hh);
}

// =========================================================== WindowHeader ===
//
// relayoutItems() right-aligns the trailing GROUP against the inboard edge of
// the window buttons.  Aligning the group rather than each item is what pins the
// account menu to the corner as the window resizes.
GEEYOOU_TEST(geom_baseline, header_right_aligns_the_trailing_group) {
  WindowHeader header;
  header.setGeometry({0.0f, 0.0f, 800.0f, 44.0f});

  // Defaults the showcase relies on: three window commands at 46px each.
  const float buttonW = header.buttonWidth();
  CHECK_NEAR(buttonW, 46.0f, kEps);
  const float commands = 3.0f * buttonW;      // minimise + maximise + close
  const float padRight = header.trailingPadding();
  CHECK_NEAR(padRight, 8.0f, kEps);

  const float ih = header.itemHeight();
  CHECK_NEAR(ih, 30.0f, kEps);                // clamp(44 - 14, 22, 40)
  const float y = (44.0f - ih) * 0.5f;

  Widget* a = header.addTrailingItem<Widget>(100.0f);
  Widget* b = header.addTrailingItem<Widget>(80.0f);
  REQUIRE(a != nullptr);
  REQUIRE(b != nullptr);

  // gap 6 between items, none before the first.
  const float right = 800.0f - commands - padRight;   // 654
  const float total = 100.0f + 6.0f + 80.0f;          // 186
  CHECK_RECT(a->geometry(), right - total, y, 100.0f, ih);
  CHECK_RECT(b->geometry(), right - total + 106.0f, y, 80.0f, ih);
  // The last item ends exactly on the inboard edge of the window buttons.
  CHECK_NEAR(b->geometry().right(), right, kEps);

  // An explicit gap widens only the join it precedes.
  header.addTrailingGap(12.0f);
  Widget* c = header.addTrailingItem<Widget>(40.0f);
  REQUIRE(c != nullptr);
  const float total3 = 100.0f + 6.0f + 80.0f + 18.0f + 40.0f;  // 244
  CHECK_RECT(a->geometry(), right - total3, y, 100.0f, ih);
  CHECK_NEAR(c->geometry().right(), right, kEps);

  // Widening the bar moves the whole group, and only the group's ORIGIN moves:
  // widths and the gaps between them are untouched.
  header.setGeometry({0.0f, 0.0f, 1000.0f, 44.0f});
  const float right2 = 1000.0f - commands - padRight;  // 854
  CHECK_RECT(a->geometry(), right2 - total3, y, 100.0f, ih);
  CHECK_NEAR(c->geometry().right(), right2, kEps);

  // Dropping the window commands hands their strip back to the group.
  header.setButtons({false, false, false});
  CHECK_NEAR(c->geometry().right(), 1000.0f - padRight, kEps);
}

// ============================================================= ScrollArea ===
//
// The viewport is sized to EXCLUDE the scrollbar strips -- that is what keeps
// the bars from being painted under the content.  10px per bar, and the
// vertical test wins the tie (ScrollArea.cpp, needHBar).
GEEYOOU_TEST(geom_baseline, scrollarea_viewport_excludes_the_scrollbars) {
  ScrollArea sa;
  sa.setGeometry({0.0f, 0.0f, 200.0f, 100.0f});
  REQUIRE(!sa.children().empty());
  const Widget* viewport = sa.children()[0].get();
  REQUIRE(viewport != nullptr);
  REQUIRE(sa.content() != nullptr);
  CHECK_EQ(sa.content()->parent(), viewport);

  // Content that fits: no bars, viewport is the whole area.
  sa.setContentSize({50.0f, 50.0f});
  CHECK_RECT(viewport->geometry(), 0.0f, 0.0f, 200.0f, 100.0f);
  CHECK_RECT(sa.content()->geometry(), 0.0f, 0.0f, 50.0f, 50.0f);

  // Too tall: a vertical bar, 10px off the width.
  sa.setContentSize({50.0f, 500.0f});
  CHECK_RECT(viewport->geometry(), 0.0f, 0.0f, 190.0f, 100.0f);

  // Too wide: a horizontal bar, 10px off the height.
  sa.setContentSize({500.0f, 50.0f});
  CHECK_RECT(viewport->geometry(), 0.0f, 0.0f, 200.0f, 90.0f);

  // Both: both strips are reserved.
  sa.setContentSize({500.0f, 500.0f});
  CHECK_RECT(viewport->geometry(), 0.0f, 0.0f, 190.0f, 90.0f);

  // Resizing the area re-derives the viewport through onGeometryChanged.
  sa.setGeometry({0.0f, 0.0f, 600.0f, 600.0f});
  CHECK_RECT(viewport->geometry(), 0.0f, 0.0f, 600.0f, 600.0f);

  // Scrolling is the viewport's contentOffset, and shrinking the content
  // re-clamps it rather than leaving the view past the end.
  sa.setGeometry({0.0f, 0.0f, 200.0f, 100.0f});
  sa.setContentSize({50.0f, 500.0f});
  sa.scrollTo({0.0f, 1000.0f});
  CHECK_NEAR(sa.scrollOffset().y, 400.0f, kEps);  // 500 - 100
  CHECK_NEAR(viewport->contentOffset().y, 400.0f, kEps);
  sa.setContentSize({50.0f, 150.0f});
  CHECK_NEAR(sa.scrollOffset().y, 50.0f, kEps);   // 150 - 100
}

// ================================================================== Shell ===
//
// The showcase frame: a fixed rail on the left, a title strip across the top of
// what is left, and the page host under it -- with every page inset by kGap.
GEEYOOU_TEST(geom_baseline, shell_lays_out_rail_title_and_page_area) {
  showcase::Shell shell;
  REQUIRE(shell.sidebar() != nullptr);
  REQUIRE(shell.titleBar() != nullptr);

  bool built = false;
  shell.addPage("演示", "总览", "副标题", geeyoou::Icon::None,
                [&](Widget* content) {
                  built = true;
                  content->add<Widget>()->setGeometry({0.0f, 0.0f, 10.0f, 10.0f});
                  return Size{900.0f, 1200.0f};
                });
  shell.showPage(0);
  CHECK(built);
  CHECK_EQ(shell.currentPage(), 0);

  shell.setGeometry({0.0f, 0.0f, 1000.0f, 700.0f});

  const float rail = showcase::Sidebar::expandedWidth();
  CHECK_NEAR(rail, 212.0f, kEps);
  const float titleH = 64.0f;
  const float gap = 16.0f;

  CHECK_RECT(shell.sidebar()->geometry(), 0.0f, 0.0f, rail, 700.0f);
  CHECK_RECT(shell.titleBar()->geometry(), rail, 0.0f, 1000.0f - rail, titleH);

  // pageArea is the third child; the page host is inset inside it by kGap.
  REQUIRE(shell.children().size() >= std::size_t(3));
  const Widget* pageArea = shell.children()[2].get();
  CHECK_RECT(pageArea->geometry(), rail, titleH, 1000.0f - rail, 700.0f - titleH);

  REQUIRE(!pageArea->children().empty());
  const Widget* host = pageArea->children()[0].get();
  CHECK_RECT(host->geometry(), gap, gap, 1000.0f - rail - gap * 2.0f,
             700.0f - titleH - gap * 2.0f);
  CHECK(host->isVisible());

  // Collapsing the rail (the title bar's hamburger) re-lays everything out
  // against the narrow rail width.
  shell.titleBar()->toggleRail.emit();
  CHECK(shell.sidebar()->isCollapsed());
  const float narrow = showcase::Sidebar::collapsedWidth();
  CHECK_NEAR(narrow, 60.0f, kEps);
  CHECK_RECT(shell.sidebar()->geometry(), 0.0f, 0.0f, narrow, 700.0f);
  CHECK_RECT(shell.titleBar()->geometry(), narrow, 0.0f, 1000.0f - narrow, titleH);
  CHECK_RECT(pageArea->geometry(), narrow, titleH, 1000.0f - narrow,
             700.0f - titleH);
  CHECK_RECT(host->geometry(), gap, gap, 1000.0f - narrow - gap * 2.0f,
             700.0f - titleH - gap * 2.0f);

  // Resizing the shell walks the same path through onGeometryChanged.
  shell.setGeometry({0.0f, 0.0f, 640.0f, 480.0f});
  CHECK_RECT(shell.sidebar()->geometry(), 0.0f, 0.0f, narrow, 480.0f);
  CHECK_RECT(pageArea->geometry(), narrow, titleH, 640.0f - narrow,
             480.0f - titleH);
  CHECK_RECT(host->geometry(), gap, gap, 640.0f - narrow - gap * 2.0f,
             480.0f - titleH - gap * 2.0f);
}
