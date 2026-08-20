#pragma once
//
// Admin-console shell: a fixed left navigation rail, a title bar, and a content
// area that hosts one page at a time.
//
// Each page lives inside its OWN ScrollArea.  That is what makes the whole
// thing resizable without a layout engine: a page lays itself out at a fixed
// design size, and if the window is smaller the operator scrolls.  Bigger
// windows simply leave margin.  docs/architecture.md section 4 explains why v1
// has no layout engine; this is the pattern that makes its absence liveable.
//
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "geeyoou/core/Signal.hpp"
#include "geeyoou/render/Icon.hpp"
#include "geeyoou/widget/ScrollArea.hpp"
#include "geeyoou/widget/Widget.hpp"

namespace showcase {

using geeyoou::Icon;
using geeyoou::Painter;
using geeyoou::Rect;
using geeyoou::ScrollArea;
using geeyoou::Signal;
using geeyoou::Size;
using geeyoou::Widget;

// Builds a page's contents into `content` and returns the design size it needs.
using PageBuilder = std::function<Size(Widget* content)>;

// Left navigation rail.
class Sidebar : public Widget {
 public:
  struct Item {
    std::string section;  // non-empty starts a new group above this item
    std::string title;
    Icon icon = Icon::None;
  };

  Sidebar() { setFocusPolicy(geeyoou::FocusPolicy::Tab); }

  void setItems(std::vector<Item> items);
  void setCurrent(int index);
  int current() const { return current_; }

  void setCollapsed(bool on);
  bool isCollapsed() const { return collapsed_; }

  // The rail's own logo block.  Turned OFF when the shell lives inside an
  // AppWindow, whose header already carries the product mark -- two brand
  // blocks stacked in one corner read as a rendering mistake.
  void setBrandVisible(bool on);
  bool isBrandVisible() const { return brand_; }
  static float expandedWidth() { return 212.0f; }
  static float collapsedWidth() { return 60.0f; }
  float preferredWidth() const {
    return collapsed_ ? collapsedWidth() : expandedWidth();
  }

  Signal<int> activated;

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;
  void onMouse(const geeyoou::MouseEvent& e) override;
  void onKey(const geeyoou::KeyEvent& e) override;

 private:
  struct Row {
    bool isSection = false;
    std::string text;
    Icon icon = Icon::None;
    int itemIndex = -1;
  };

  void rebuildRows();
  int rowAtY(float y) const;
  float rowHeight(const Row& r) const;
  float rowTop(int row) const;
  float railTop() const;

  std::vector<Item> items_;
  std::vector<Row> rows_;
  int current_ = -1;
  int hovered_ = -1;
  bool collapsed_ = false;
  bool brand_ = true;
};

// Title strip above the content: page name, subtitle, and the rail toggle.
class TitleBar : public Widget {
 public:
  void setTitle(std::string t, std::string subtitle);
  Signal<> toggleRail;

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;
  void onMouse(const geeyoou::MouseEvent& e) override;

 private:
  Rect toggleRect() const;
  std::string title_;
  std::string subtitle_;
  bool hoverToggle_ = false;
};

// The whole frame: sidebar + title bar + page host.
class Shell : public Widget {
 public:
  Shell();

  // `builder` runs the first time the page is shown, not at registration --
  // an unopened page costs nothing but its nav entry.
  //
  // `section`, `title` and `subtitle` are the CHINESE ORIGINALS, i.e. the
  // translation keys -- not display text.  The shell runs them through tr()
  // every time it draws the nav or the title strip, which is what lets
  // rebuildPages() re-label everything without the caller re-registering.
  void addPage(std::string section, std::string title, std::string subtitle,
               Icon icon, PageBuilder builder);
  void showPage(int index);
  int currentPage() const { return current_; }

  // Destroys every built page and builds the current one again.
  //
  // This is how a language change takes effect.  Nothing cheaper works: the
  // page builders bake their strings into Labels at construction time, so
  // there is no "re-translate the existing tree" pass to run -- the tree IS
  // the translation.  The cost is that page-local state (scroll offset, typed
  // values, which row was selected) goes with it, which is the honest
  // consequence of rebuilding and not worth hiding behind partial restores.
  void rebuildPages();

  // Emitted by rebuildPages() BEFORE any page is destroyed.
  //
  // This exists for the subscriptions that point INTO a page from outside it.
  // Two of them exist in this showcase -- AppState::alarmSink, which holds a
  // widget from the ops page, and ShowcaseWindow::headerAction, which pages
  // connect their activity logs to -- and both were harmless only for as long
  // as pages were never destroyed.  The moment rebuilding became possible they
  // turned into real use-after-frees, so the owners cut them here.
  Signal<> pagesAboutToRebuild;

  Sidebar* sidebar() { return sidebar_; }
  TitleBar* titleBar() { return titleBar_; }

 protected:
  void onGeometryChanged() override;

 private:
  struct Page {
    // Chinese originals = translation keys.  See addPage().
    std::string section;
    std::string title;
    std::string subtitle;
    Icon icon = Icon::None;
    PageBuilder builder;
    ScrollArea* host = nullptr;  // created lazily on first show
  };

  // Rebuilds the sidebar model from pages_, translating as it goes.
  void refreshNav();
  void relayout();

  Sidebar* sidebar_ = nullptr;
  TitleBar* titleBar_ = nullptr;
  Widget* pageArea_ = nullptr;
  std::vector<Page> pages_;
  int current_ = -1;
};

}  // namespace showcase
