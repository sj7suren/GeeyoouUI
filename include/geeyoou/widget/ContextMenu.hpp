#pragma once
//
// A right-click action menu, detached from any button.
//
// MenuButton drops the same menu from a button; a context menu drops it at the
// cursor, from a right-click on a table row, a trend point, a device symbol.
// The two share the machinery -- a PopupList parented to the Window, positioned
// by openPopup -- so this reuses it rather than growing a second menu widget.
//
// Hold one as a member of the widget that owns the right-click, set its items,
// and call popupAt() from onMouse when you see MouseButton::Right.  Its
// ConnectionScope cuts the popup subscription when the owner dies, so the menu
// (which lives on the Window, and outlives the owner) never calls back into a
// freed object -- the same lifetime rule MenuButton follows.
//
#include <string>
#include <vector>

#include "geeyoou/core/ConnectionScope.hpp"
#include "geeyoou/widget/MenuButton.hpp"  // for MenuItem
#include "geeyoou/widget/PopupList.hpp"

namespace geeyoou {

class Window;

class ContextMenu {
 public:
  ContextMenu() = default;
  ~ContextMenu();

  ContextMenu(const ContextMenu&) = delete;
  ContextMenu& operator=(const ContextMenu&) = delete;

  void setItems(std::vector<MenuItem> items);
  const std::vector<MenuItem>& items() const { return items_; }

  // Opens the menu on `w`, its top-left near `windowPos` (window coordinates,
  // e.g. a MouseEvent's windowPos).  A no-op if there are no items.
  void popupAt(Window* w, Point windowPos);
  void close();
  bool isOpen() const;

  Signal<const std::string&> triggered;   // item id (or text when id is empty)
  Signal<int> triggeredIndex;

 private:
  void ensureMenu(Window* w);
  void trigger(int row);

  std::vector<MenuItem> items_;
  PopupList* menu_ = nullptr;
  Window* window_ = nullptr;
  int maxRows_ = 14;

  // Declared LAST -> destroyed FIRST.  The menu lives on the Window and
  // outlives this object; the subscription must be cut before we go.
  ConnectionScope conns_;
};

}  // namespace geeyoou
