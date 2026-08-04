#include "geeyoou/widget/MenuButton.hpp"

#include <algorithm>

#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/Window.hpp"

namespace geeyoou {

MenuButton::~MenuButton() {
  // Before anything else: closing the menu does not unsubscribe from it, and
  // the slot below captures `this`.
  conns_.clear();

  // The menu is parented to the Window, so it survives us unless taken down.
  if (menu_ && menu_->isVisible()) {
    if (Window* w = menu_->window()) w->closePopup();
  }
}

void MenuButton::setItems(std::vector<MenuItem> items) {
  items_ = std::move(items);
  if (isMenuOpen()) closeMenu();
}

void MenuButton::setMenuWidth(float w) { menuWidth_ = std::max(0.0f, w); }

void MenuButton::setMaxVisibleRows(int n) {
  maxRows_ = std::max(1, n);
  if (menu_) menu_->setMaxVisibleRows(maxRows_);
}

bool MenuButton::isMenuOpen() const { return menu_ && menu_->isVisible(); }

bool MenuButton::inArrowZone(Point) const { return true; }

bool SplitButton::inArrowZone(Point p) const {
  return p.x >= localRect().right() - arrowWidth();
}

void MenuButton::ensureMenu() {
  if (menu_) return;
  Window* w = window();
  if (!w) return;
  menu_ = w->add<PopupList>();
  menu_->setMaxVisibleRows(maxRows_);
  // Owned by conns_: the menu belongs to the Window and survives us.
  conns_ += menu_->rowActivated.connect([this](int row) {
    trigger(row);
    closeMenu();
  });
}

void MenuButton::openMenu() {
  if (!isEffectivelyEnabled() || isLoading() || isMenuOpen() || items_.empty()) return;
  ensureMenu();
  if (!menu_) return;

  std::vector<PopupRow> rows;
  rows.reserve(items_.size());
  for (std::size_t i = 0; i < items_.size(); ++i) {
    const MenuItem& m = items_[i];
    PopupRow r;
    r.text = m.text;
    r.shortcutText = m.shortcutText;
    r.modelIndex = int(i);
    r.icon = m.icon;
    r.enabled = m.enabled;
    r.separator = m.separator;
    rows.push_back(std::move(r));
  }
  menu_->setRows(std::move(rows));

  const float w = menuWidth_ > 0.0f ? menuWidth_ : std::max(160.0f, geometry().width());
  menu_->setGeometry({0.0f, 0.0f, w, menu_->preferredHeight()});
  window()->openPopup(menu_, windowRect());
  menu_->highlightFirstSelectable();
  update();
}

void MenuButton::closeMenu() {
  if (!isMenuOpen()) return;
  if (Window* w = window()) w->closePopup();
  update();
}

void MenuButton::trigger(int row) {
  const auto& rows = menu_->rows();
  if (row < 0 || row >= int(rows.size())) return;
  const int mi = rows[std::size_t(row)].modelIndex;
  if (mi < 0 || mi >= int(items_.size())) return;
  const MenuItem& m = items_[std::size_t(mi)];
  triggeredIndex.emit(mi);
  triggered.emit(m.id.empty() ? m.text : m.id);
}

void MenuButton::onPaint(Painter& p, const Rect& dirty) {
  PushButton::onPaint(p, dirty);
  const float aw = arrowWidth();
  if (aw <= 0.0f) return;

  const Theme& t = Theme::current();
  const Rect r = localRect();
  const Palette pal = palette();
  // A hairline divides the primary action from the menu affordance, so it is
  // obvious the two halves do different things.
  p.strokeLine({r.right() - aw, r.y() + 6.0f}, {r.right() - aw, r.bottom() - 6.0f},
               pal.label.withAlpha(90), 1.0f);
  drawIcon(p, isMenuOpen() ? Icon::ChevronUp : Icon::ChevronDown,
           {r.right() - aw, r.y(), aw, r.height()}, pal.label, 0.9f);
  (void)t;
}

void MenuButton::onMouse(const MouseEvent& e) {
  if (e.action == MouseAction::Press && e.button == MouseButton::Left &&
      isEffectivelyEnabled() && !isLoading() && inArrowZone(e.pos)) {
    if (isMenuOpen()) closeMenu(); else openMenu();
    e.accept();
    return;
  }
  PushButton::onMouse(e);
}

void MenuButton::onKey(const KeyEvent& e) {
  if (!e.pressed) return;

  if (isMenuOpen()) {
    switch (e.key) {
      case Key::Up:     menu_->moveHighlight(-1); e.accept(); return;
      case Key::Down:   menu_->moveHighlight(+1); e.accept(); return;
      case Key::Home:   menu_->setHighlighted(-1, false); menu_->moveHighlight(+1); e.accept(); return;
      case Key::End:    menu_->setHighlighted(int(menu_->rows().size()), false);
                        menu_->moveHighlight(-1); e.accept(); return;
      case Key::Enter:  menu_->activateHighlighted(); e.accept(); return;
      case Key::Escape: closeMenu(); e.accept(); return;
      default: break;
    }
    int digit = 0;
    if (e.alt && keyToDigit(e.key, digit) && digit >= 1) {
      const auto& rows = menu_->rows();
      int nth = 0;
      for (std::size_t i = 0; i < rows.size(); ++i) {
        if (rows[i].separator || !rows[i].enabled) continue;
        if (++nth == digit) { trigger(int(i)); closeMenu(); break; }
      }
      e.accept();
      return;
    }
    return;
  }

  if (e.key == Key::Down || (e.key == Key::Space && arrowWidth() <= 0.0f)) {
    openMenu();
    e.accept();
    return;
  }
  PushButton::onKey(e);
}

}  // namespace geeyoou
