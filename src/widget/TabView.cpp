#include "geeyoou/widget/TabView.hpp"

#include <algorithm>

#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"

namespace geeyoou {
namespace {
constexpr float kBarH = 40.0f;      // tab strip height
constexpr float kTabPadX = 18.0f;   // horizontal padding inside a tab
constexpr float kTabMinW = 72.0f;
}  // namespace

TabView::TabView() { setFocusPolicy(FocusPolicy::Tab); }

Widget* TabView::addTab(std::string title) {
  auto* page = add<Widget>();
  page->setVisible(false);
  tabs_.push_back({std::move(title), page, 0.0f, 0.0f});
  layoutTabs();
  if (current_ < 0) setCurrentIndex(0);
  update();
  return page;
}

void TabView::setCurrentIndex(int index) {
  if (index < 0 || index >= int(tabs_.size()) || index == current_) return;
  if (current_ >= 0 && tabs_[std::size_t(current_)].page) {
    tabs_[std::size_t(current_)].page->setVisible(false);
  }
  current_ = index;
  Tab& t = tabs_[std::size_t(index)];
  if (t.page) {
    t.page->setVisible(true);
    // Size the page to the content area now that it is the visible one.
    const Rect r = localRect();
    t.page->setGeometry({0.0f, kBarH, r.width(), std::max(0.0f, r.height() - kBarH)});
  }
  update();
  // Tail: a slot may rebuild the tabs, so notify last (section 11.4).
  currentChanged.emit(index);
}

void TabView::layoutTabs() {
  const Theme& t = Theme::current();
  float x = 0.0f;
  for (Tab& tab : tabs_) {
    const Size ts = measureText(tab.title, t.fontBody);
    tab.x = x;
    tab.width = std::max(kTabMinW, ts.width + 2.0f * kTabPadX);
    x += tab.width;
  }
}

void TabView::layoutPages() {
  const Rect r = localRect();
  const Rect area{0.0f, kBarH, r.width(), std::max(0.0f, r.height() - kBarH)};
  for (Tab& tab : tabs_) {
    if (tab.page) tab.page->setGeometry(area);
  }
}

void TabView::onGeometryChanged() {
  layoutTabs();
  layoutPages();
}

int TabView::tabAtX(float x) const {
  for (std::size_t i = 0; i < tabs_.size(); ++i) {
    if (x >= tabs_[i].x && x < tabs_[i].x + tabs_[i].width) return int(i);
  }
  return -1;
}

void TabView::onPaint(Painter& p, const Rect&) {
  const Theme& t = Theme::current();
  const Rect r = localRect();

  // Strip background + a rule along the bottom of the bar.
  p.fillRect({0.0f, 0.0f, r.width(), kBarH}, t.panel.lerp(t.background, 0.3f));
  p.strokeLine({0.0f, kBarH - 0.5f}, {r.width(), kBarH - 0.5f}, t.panelBorder, 1.0f);

  for (std::size_t i = 0; i < tabs_.size(); ++i) {
    const Tab& tab = tabs_[i];
    const bool active = (int(i) == current_);
    const Rect box{tab.x, 0.0f, tab.width, kBarH};

    if (active) {
      p.fillRect(box, t.panel);
      // Accent underline marks the active tab.
      p.fillRect({tab.x, kBarH - 2.0f, tab.width, 2.0f}, t.accent);
    } else if (int(i) == hovered_) {
      p.fillRect(box, t.panelBorder.withAlpha(70));
    }

    const Color fg = active ? t.text : t.textDim;
    p.drawText({tab.x + tab.width * 0.5f, kBarH * 0.5f}, tab.title, t.fontBody,
               fg, HAlign::Center, VAlign::Middle);
  }
}

void TabView::onMouse(const MouseEvent& e) {
  switch (e.action) {
    case MouseAction::Leave:
      if (hovered_ != -1) {
        hovered_ = -1;
        update();
      }
      e.accept();
      break;
    case MouseAction::Enter:
    case MouseAction::Move: {
      const int h = (e.pos.y < kBarH) ? tabAtX(e.pos.x) : -1;
      if (h != hovered_) {
        hovered_ = h;
        update();
      }
      e.accept();
      break;
    }
    case MouseAction::Press: {
      if (e.button != MouseButton::Left || e.pos.y >= kBarH) break;
      const int i = tabAtX(e.pos.x);
      if (i >= 0) setCurrentIndex(i);
      e.accept();
      break;
    }
    default:
      break;
  }
}

}  // namespace geeyoou
