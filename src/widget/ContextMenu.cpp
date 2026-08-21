#include "geeyoou/widget/ContextMenu.hpp"

#include <algorithm>

#include "geeyoou/widget/Window.hpp"

namespace geeyoou {

ContextMenu::~ContextMenu() {
  // conns_ (declared last) has already cut the subscription by the time we get
  // here; nothing else to do -- the PopupList belongs to the Window.
}

void ContextMenu::setItems(std::vector<MenuItem> items) {
  items_ = std::move(items);
}

void ContextMenu::ensureMenu(Window* w) {
  if (menu_ && window_ == w) return;
  window_ = w;
  menu_ = w->add<PopupList>();
  menu_->setMaxVisibleRows(maxRows_);
  // CLOSE FIRST, dispatch second -- the same load-bearing order MenuButton
  // documents: trigger() emits, an emit runs application code, and "the
  // operator picked an entry" is exactly when an app tears down the screen this
  // menu was invoked from.  With the emit last, this lambda has nothing left to
  // touch; the other way round, close() would run on a freed subscriber.  D7 is
  // not in play -- rowActivated belongs to the Window's PopupList, not to us.
  conns_ += menu_->rowActivated.connect([this](int row) {
    close();
    trigger(row);
  });
}

void ContextMenu::popupAt(Window* w, Point windowPos) {
  if (!w || items_.empty()) return;
  ensureMenu(w);
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

  const float wdt = 200.0f;
  menu_->setGeometry({0.0f, 0.0f, wdt, menu_->preferredHeight()});
  // A zero-size anchor at the cursor makes openPopup place the menu just below
  // the click, flipping above it near the bottom edge -- exactly a context
  // menu's behaviour, and the flip logic already lives in openPopup.
  window_->openPopup(menu_, {windowPos.x, windowPos.y, 0.0f, 0.0f});
  menu_->highlightFirstSelectable();
}

void ContextMenu::close() {
  if (!isOpen()) return;
  if (window_) window_->closePopup();
}

bool ContextMenu::isOpen() const {
  return window_ && menu_ && window_->popup() == menu_;
}

void ContextMenu::trigger(int row) {
  const auto& rows = menu_->rows();
  if (row < 0 || row >= int(rows.size())) return;
  const int mi = rows[std::size_t(row)].modelIndex;
  if (mi < 0 || mi >= int(items_.size())) return;
  // By value before the first emit: a triggeredIndex slot may call setItems()
  // (relabel on a language switch / permission change), reallocating items_.
  const MenuItem& m = items_[std::size_t(mi)];
  const std::string id = m.id.empty() ? m.text : m.id;
  triggeredIndex.emit(mi);
  triggered.emit(id);
}

}  // namespace geeyoou
