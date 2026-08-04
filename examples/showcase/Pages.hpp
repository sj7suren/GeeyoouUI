#pragma once
#include "AppState.hpp"
#include "Shell.hpp"

namespace showcase {

class ShowcaseWindow;

// Every page has the same shape: fill `content` with widgets laid out at a
// fixed design size, and return that size so the hosting ScrollArea knows what
// to scroll.  No page touches the Window or the sidebar -- with one deliberate
// exception, buildWindowPage, whose whole subject IS the window chrome.
Size buildOverviewPage(Widget* content, AppState& app);
Size buildHmiPage(Widget* content, AppState& app);
Size buildWidgetsPage(Widget* content);
Size buildInputsPage(Widget* content);
Size buildSelectsPage(Widget* content);
Size buildOpsPage(Widget* content, AppState& app);
Size buildWindowPage(Widget* content, ShowcaseWindow& win);
Size buildThemePage(Widget* content, ShowcaseWindow& win);

}  // namespace showcase
