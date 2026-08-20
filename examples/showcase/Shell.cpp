#include "Shell.hpp"

#include <algorithm>

#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"
#include "i18n/I18n.hpp"

namespace showcase {
namespace {
using geeyoou::Color;
using geeyoou::HAlign;
using geeyoou::Key;
using geeyoou::MouseAction;
using geeyoou::MouseButton;
using geeyoou::Point;
using geeyoou::Theme;
using geeyoou::VAlign;
using geeyoou::drawIcon;

constexpr float kItemH = 40.0f;
constexpr float kSectionH = 30.0f;
constexpr float kRailTop = 62.0f;   // brand block
constexpr float kTitleH = 64.0f;
constexpr float kGap = 16.0f;
}  // namespace

// ================================================================ Sidebar ===
void Sidebar::setItems(std::vector<Item> items) {
  items_ = std::move(items);
  rebuildRows();
  update();
}

void Sidebar::rebuildRows() {
  rows_.clear();
  std::string lastSection;
  for (std::size_t i = 0; i < items_.size(); ++i) {
    if (!items_[i].section.empty() && items_[i].section != lastSection) {
      lastSection = items_[i].section;
      Row s;
      s.isSection = true;
      s.text = lastSection;
      rows_.push_back(std::move(s));
    }
    Row r;
    r.text = items_[i].title;
    r.icon = items_[i].icon;
    r.itemIndex = int(i);
    rows_.push_back(std::move(r));
  }
}

void Sidebar::setCurrent(int index) {
  if (current_ == index) return;
  current_ = index;
  update();
}

void Sidebar::setCollapsed(bool on) {
  if (collapsed_ == on) return;
  collapsed_ = on;
  update();
}

void Sidebar::setBrandVisible(bool on) {
  if (brand_ == on) return;
  brand_ = on;
  update();
}

float Sidebar::railTop() const { return brand_ ? kRailTop : 12.0f; }

float Sidebar::rowHeight(const Row& r) const {
  // Section captions vanish when collapsed -- a 60px rail has no room for
  // them, and a truncated caption reads as a rendering bug.
  if (r.isSection) return collapsed_ ? 8.0f : kSectionH;
  return kItemH;
}

float Sidebar::rowTop(int row) const {
  float y = railTop();
  for (int i = 0; i < row && i < int(rows_.size()); ++i) y += rowHeight(rows_[std::size_t(i)]);
  return y;
}

int Sidebar::rowAtY(float y) const {
  float top = railTop();
  for (std::size_t i = 0; i < rows_.size(); ++i) {
    const float h = rowHeight(rows_[i]);
    if (y >= top && y < top + h) return int(i);
    top += h;
  }
  return -1;
}

void Sidebar::onPaint(Painter& p, const Rect&) {
  const Theme& t = Theme::current();
  const Rect r = localRect();

  p.fillRect(r, t.panel.lerp(t.background, 0.35f));
  p.strokeLine({r.right() - 0.5f, 0.0f}, {r.right() - 0.5f, r.height()},
               t.panelBorder, 1.0f);

  // --- brand block ---
  if (brand_) {
    drawIcon(p, Icon::Settings, {16.0f, 16.0f, 28.0f, 28.0f}, t.accent);
    if (!collapsed_) {
      p.drawText({54.0f, 24.0f}, "GeeyoouUI", t.fontBody, t.text, HAlign::Left,
                 VAlign::Top);
      p.drawText({54.0f, 40.0f}, tr("控件库演示"), t.fontSmall, t.textDim,
                 HAlign::Left, VAlign::Top);
    }
    p.strokeLine({12.0f, kRailTop - 10.0f}, {r.width() - 12.0f, kRailTop - 10.0f},
                 t.panelBorder, 1.0f);
  }

  // --- nav rows ---
  for (std::size_t i = 0; i < rows_.size(); ++i) {
    const Row& row = rows_[i];
    const float y = rowTop(int(i));
    const float h = rowHeight(row);

    if (row.isSection) {
      if (!collapsed_) {
        p.drawText({18.0f, y + h * 0.5f}, row.text, t.fontSmall,
                   t.textDim.lerp(t.background, 0.25f), HAlign::Left, VAlign::Middle);
      }
      continue;
    }

    const bool active = (row.itemIndex == current_);
    const Rect box(6.0f, y + 2.0f, r.width() - 12.0f, h - 4.0f);

    if (active) {
      p.fillRoundRect(box, t.radius, t.accent.withAlpha(48));
      // Accent bar on the leading edge: on a wide rail the tint alone is easy
      // to lose, and this reads instantly even in peripheral vision.
      p.fillRoundRect({0.0f, y + 8.0f, 3.0f, h - 16.0f}, 1.5f, t.accent);
    } else if (int(i) == hovered_) {
      p.fillRoundRect(box, t.radius, t.panelBorder.withAlpha(90));
    }

    const Color fg = active ? t.text : t.textDim;
    drawIcon(p, row.icon, {16.0f, y, 22.0f, h}, active ? t.accent : fg);
    if (!collapsed_) {
      p.drawText({48.0f, y + h * 0.5f}, row.text, t.fontBody, fg, HAlign::Left,
                 VAlign::Middle);
    }
  }

  if (hasFocus()) {
    p.strokeRoundRect(r.deflated(1.5f), t.radius, t.focusRing.withAlpha(140), 1.0f);
  }
}

void Sidebar::onMouse(const geeyoou::MouseEvent& e) {
  switch (e.action) {
    case MouseAction::Leave:
      hovered_ = -1;
      update();
      e.accept();
      break;
    case MouseAction::Enter:
    case MouseAction::Move: {
      const int row = rowAtY(e.pos.y);
      const int h = (row >= 0 && !rows_[std::size_t(row)].isSection) ? row : -1;
      if (h != hovered_) { hovered_ = h; update(); }
      e.accept();
      break;
    }
    case MouseAction::Press: {
      if (e.button != MouseButton::Left) break;
      const int row = rowAtY(e.pos.y);
      if (row >= 0 && !rows_[std::size_t(row)].isSection) {
        activated.emit(rows_[std::size_t(row)].itemIndex);
      }
      e.accept();
      break;
    }
    default:
      break;
  }
}

void Sidebar::onKey(const geeyoou::KeyEvent& e) {
  if (!e.pressed || items_.empty()) return;
  if (e.key == Key::Up || e.key == Key::Down) {
    const int n = int(items_.size());
    int next = current_ + (e.key == Key::Down ? 1 : -1);
    if (next < 0) next = n - 1;
    if (next >= n) next = 0;
    activated.emit(next);
    e.accept();
  }
}

// =============================================================== TitleBar ===
void TitleBar::setTitle(std::string t, std::string subtitle) {
  title_ = std::move(t);
  subtitle_ = std::move(subtitle);
  update();
}

Rect TitleBar::toggleRect() const {
  return {12.0f, (localRect().height() - 32.0f) * 0.5f, 32.0f, 32.0f};
}

void TitleBar::onPaint(Painter& p, const Rect&) {
  const Theme& t = Theme::current();
  const Rect r = localRect();

  p.fillRect(r, t.background);
  p.strokeLine({0.0f, r.height() - 0.5f}, {r.width(), r.height() - 0.5f},
               t.panelBorder, 1.0f);

  const Rect tog = toggleRect();
  if (hoverToggle_) p.fillRoundRect(tog, 5.0f, t.panelBorder.withAlpha(110));
  drawIcon(p, Icon::Menu, tog.deflated(7.0f), hoverToggle_ ? t.text : t.textDim);

  p.drawText({56.0f, r.height() * 0.5f - 9.0f}, title_, 17.0f, t.text, HAlign::Left,
             VAlign::Middle);
  if (!subtitle_.empty()) {
    p.drawText({56.0f, r.height() * 0.5f + 11.0f}, subtitle_, t.fontSmall, t.textDim,
               HAlign::Left, VAlign::Middle);
  }
}

void TitleBar::onMouse(const geeyoou::MouseEvent& e) {
  switch (e.action) {
    case MouseAction::Leave:
      hoverToggle_ = false;
      update();
      e.accept();
      break;
    case MouseAction::Enter:
    case MouseAction::Move: {
      const bool h = toggleRect().contains(e.pos);
      if (h != hoverToggle_) { hoverToggle_ = h; update(); }
      e.accept();
      break;
    }
    case MouseAction::Press:
      if (e.button == MouseButton::Left && toggleRect().contains(e.pos)) {
        toggleRail.emit();
      }
      e.accept();
      break;
    default:
      break;
  }
}

// ================================================================== Shell ===
Shell::Shell() {
  sidebar_ = add<Sidebar>();
  titleBar_ = add<TitleBar>();
  pageArea_ = add<Widget>();

  sidebar_->activated.connect([this](int index) { showPage(index); });
  titleBar_->toggleRail.connect([this] {
    sidebar_->setCollapsed(!sidebar_->isCollapsed());
    relayout();
  });
}

void Shell::addPage(std::string section, std::string title, std::string subtitle,
                    Icon icon, PageBuilder builder) {
  Page pg;
  pg.section = std::move(section);
  pg.title = std::move(title);
  pg.subtitle = std::move(subtitle);
  pg.icon = icon;
  pg.builder = std::move(builder);
  pages_.push_back(std::move(pg));

  refreshNav();
}

// The sidebar's model is DERIVED from pages_ and rebuilt wholesale each time.
// Cheap for a handful of pages, and it removes any chance of the nav drifting
// out of sync with the page list -- or, now, out of sync with the language.
void Shell::refreshNav() {
  std::vector<Sidebar::Item> items;
  items.reserve(pages_.size());
  for (const Page& p : pages_) {
    Sidebar::Item it;
    // Translate here rather than at registration: pages_ holds keys, the
    // sidebar holds display text, and this is the seam between them.  An empty
    // section stays empty -- tr("") would be a lookup for a key nobody has.
    it.section = p.section.empty() ? std::string() : tr(p.section);
    it.title = tr(p.title);
    it.icon = p.icon;
    items.push_back(std::move(it));
  }
  sidebar_->setItems(std::move(items));
}

void Shell::showPage(int index) {
  if (index < 0 || index >= int(pages_.size()) || index == current_) return;

  if (current_ >= 0 && pages_[std::size_t(current_)].host) {
    // Hiding rather than destroying keeps page state (scroll position, typed
    // values) across navigation -- and animationTickTree skips invisible
    // subtrees, so a hidden page also stops doing periodic work.
    pages_[std::size_t(current_)].host->setVisible(false);
  }

  current_ = index;
  Page& pg = pages_[std::size_t(index)];
  if (!pg.host) {
    pg.host = pageArea_->add<ScrollArea>();
    pg.host->setFrameVisible(false);
    const Size design = pg.builder ? pg.builder(pg.host->content()) : Size{};
    pg.host->setContentSize(design);
  }
  pg.host->setVisible(true);
  sidebar_->setCurrent(index);
  titleBar_->setTitle(tr(pg.title), tr(pg.subtitle));
  relayout();
  update();
}

void Shell::rebuildPages() {
  // FIRST, before a single page widget dies: let the outside world drop the
  // pointers it holds into these trees.  Doing this after the destruction
  // would be the use-after-free this signal exists to prevent.
  pagesAboutToRebuild.emit();

  const int keep = current_ >= 0 ? current_ : 0;

  // Drop every built page.  clearChildren() destroys them last-first and tells
  // the Window about each node on the way out, so focus/hover/mouse-grab
  // pointers aimed into these trees are cleared rather than left dangling.
  for (Page& pg : pages_) pg.host = nullptr;
  pageArea_->clearChildren();

  // showPage() early-outs when the index is unchanged, and the page it would
  // skip no longer exists -- so forget which page we were on before asking for
  // it again.
  current_ = -1;

  refreshNav();
  showPage(keep);
}

void Shell::onGeometryChanged() { relayout(); }

void Shell::relayout() {
  const Rect r = localRect();
  const float rail = sidebar_->preferredWidth();

  sidebar_->setGeometry({0.0f, 0.0f, rail, r.height()});
  titleBar_->setGeometry({rail, 0.0f, std::max(0.0f, r.width() - rail), kTitleH});
  pageArea_->setGeometry({rail, kTitleH, std::max(0.0f, r.width() - rail),
                          std::max(0.0f, r.height() - kTitleH)});

  const Rect pa = pageArea_->localRect();
  for (Page& pg : pages_) {
    if (!pg.host) continue;
    pg.host->setGeometry({kGap, kGap, std::max(0.0f, pa.width() - kGap * 2.0f),
                          std::max(0.0f, pa.height() - kGap * 2.0f)});
  }
  update();
}

}  // namespace showcase
