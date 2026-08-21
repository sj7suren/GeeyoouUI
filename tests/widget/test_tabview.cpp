//
// TabView: one page visible at a time, and the switch notifies.
//
#include "framework/Test.hpp"
#include "geeyoou/widget/TabView.hpp"

using geeyoou::TabView;
using geeyoou::Widget;

GEEYOOU_TEST(tabview, first_added_tab_becomes_current_and_visible) {
  TabView tv;
  Widget* a = tv.addTab("A");
  CHECK_EQ(tv.currentIndex(), 0);
  CHECK_EQ(tv.tabCount(), 1);
  CHECK(a->isVisible());
}

GEEYOOU_TEST(tabview, switching_hides_the_old_page_and_shows_the_new) {
  TabView tv;
  Widget* a = tv.addTab("A");
  Widget* b = tv.addTab("B");
  // The second tab is added hidden; only the first shows.
  CHECK(a->isVisible());
  CHECK(!b->isVisible());

  int changed = -1;
  auto c = tv.currentChanged.connect([&](int i) { changed = i; });
  tv.setCurrentIndex(1);

  CHECK(!a->isVisible());
  CHECK(b->isVisible());
  CHECK_EQ(tv.currentIndex(), 1);
  CHECK_EQ(changed, 1);
  c.disconnect();
}

GEEYOOU_TEST(tabview, switching_to_the_current_tab_is_a_noop) {
  TabView tv;
  tv.addTab("A");
  tv.addTab("B");
  tv.setCurrentIndex(1);

  int calls = 0;
  auto c = tv.currentChanged.connect([&](int) { ++calls; });
  tv.setCurrentIndex(1);  // already there
  CHECK_EQ(calls, 0);
  c.disconnect();
}
