#pragma once
#include "AppState.hpp"
#include "Shell.hpp"

namespace showcase {

class ShowcaseWindow;

// Every page has the same shape: fill `content` with widgets, and return the
// size it needs so the hosting ScrollArea knows what to scroll.  No page
// touches the Window or the sidebar -- with one deliberate exception,
// buildWindowPage, whose whole subject IS the window chrome.
//
// HOW a page arrives at that size is up to the page, and both answers are
// supported for good (docs/architecture.md section 4):
//
//   * buildWidgetsPage / buildInputsPage / buildOpsPage give `content` a
//     BoxLayout or GridLayout and return content->sizeHint().preferred, so the
//     number is computed and the page reflows;
//   * the other five position their children at absolute coordinates and
//     return the design size those coordinates were drawn for.  The HMI page in
//     particular SHOULD: a pump drawn 40px left of a valve is process
//     semantics, not typography.
Size buildOverviewPage(Widget* content, AppState& app);
Size buildHmiPage(Widget* content, AppState& app);
Size buildIconsPage(Widget* content);
Size buildLayoutPage(Widget* content);
Size buildWidgetsPage(Widget* content);
Size buildInputsPage(Widget* content);
Size buildSelectsPage(Widget* content);
Size buildOpsPage(Widget* content, AppState& app);
Size buildWindowPage(Widget* content, ShowcaseWindow& win);
Size buildThemePage(Widget* content, ShowcaseWindow& win);

}  // namespace showcase
