#pragma once
#include <string>
#include <vector>

#include "geeyoou/widget/PopupList.hpp"
#include "geeyoou/widget/PushButton.hpp"

namespace geeyoou {

// One entry of an action menu.  Unlike SelectItem this represents a COMMAND,
// not a value -- hence the shortcut hint and the absence of a "selected" state.
struct MenuItem {
  std::string text;
  std::string shortcutText;  // e.g. "Ctrl+S"; display only, not bound here
  std::string id;            // handed back on trigger; defaults to `text`
  Icon icon = Icon::None;
  bool enabled = true;
  bool separator = false;

  MenuItem() = default;
  MenuItem(std::string t) : text(std::move(t)) {}
  MenuItem(std::string t, std::string identifier)
      : text(std::move(t)), id(std::move(identifier)) {}
  MenuItem(std::string t, std::string identifier, Icon ic)
      : text(std::move(t)), id(std::move(identifier)), icon(ic) {}

  static MenuItem sep() {
    MenuItem m;
    m.separator = true;
    m.enabled = false;
    return m;
  }
};

// A button that drops an action menu.
//
// Derives from PushButton so the variant palette, icon layout, loading spinner
// and focus ring all come for free; only the click behaviour changes.
class MenuButton : public PushButton {
 public:
  GEEYOOU_STYLE_TYPE(MenuButton, PushButton)

  ~MenuButton() override;

  void setItems(std::vector<MenuItem> items);
  const std::vector<MenuItem>& items() const { return items_; }
  void setMenuWidth(float w);      // 0 = at least the button's width
  void setMaxVisibleRows(int n);

  bool isMenuOpen() const;
  void openMenu();
  void closeMenu();

  Signal<const std::string&> triggered;  // item id
  Signal<int> triggeredIndex;

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;
  void onMouse(const MouseEvent& e) override;
  void onKey(const KeyEvent& e) override;

  // True when a press at `p` should open the menu instead of firing clicked().
  // MenuButton says "anywhere"; SplitButton narrows it to the arrow strip.
  virtual bool inArrowZone(Point p) const;
  // Width reserved on the right for the chevron; 0 hides it.
  virtual float arrowWidth() const { return 0.0f; }

 private:
  void ensureMenu();
  void trigger(int row);

  std::vector<MenuItem> items_;
  PopupList* menu_ = nullptr;
  float menuWidth_ = 0.0f;
  int maxRows_ = 12;
};

// Primary action on the left, menu of alternatives behind the chevron.
class SplitButton : public MenuButton {
 public:
  GEEYOOU_STYLE_TYPE(SplitButton, MenuButton)

 protected:
  bool inArrowZone(Point p) const override;
  float arrowWidth() const override { return 26.0f; }
};

}  // namespace geeyoou
