#pragma once
#include <string>
#include <vector>

#include "geeyoou/core/Signal.hpp"
#include "geeyoou/widget/Widget.hpp"

namespace geeyoou {

// A tab strip over a stack of pages, showing one page at a time.
//
// The pattern every configuration screen reaches for: group a wall of
// parameters into tabs so the operator sees one concern at a time.  Each page
// is an ordinary Widget you fill however you like; TabView owns them, shows the
// current one and hides the rest -- and because a hidden subtree is skipped by
// animationTickTree, a page that is not on screen also stops any periodic work
// it was doing, for free.
//
// It does NOT scroll a page that is too tall -- drop a ScrollArea in as the
// page if you need that, exactly as the showcase shell does.
class TabView : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(TabView, Widget)

  TabView();

  // Adds a tab and returns its (empty) page.  Fill the page with your widgets.
  Widget* addTab(std::string title);

  void setCurrentIndex(int index);
  int currentIndex() const { return current_; }
  int tabCount() const { return int(tabs_.size()); }

  // Fired after the visible page changes, with the new index.
  Signal<int> currentChanged;

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;
  void onMouse(const MouseEvent& e) override;
  void onGeometryChanged() override;

 private:
  struct Tab {
    std::string title;
    Widget* page = nullptr;
    float x = 0.0f;      // computed tab-strip geometry, for hit testing
    float width = 0.0f;
  };

  void layoutTabs();
  void layoutPages();
  int tabAtX(float x) const;

  std::vector<Tab> tabs_;
  int current_ = -1;
  int hovered_ = -1;
};

}  // namespace geeyoou
