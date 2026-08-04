#pragma once
#include <string>
#include <vector>

#include "geeyoou/core/ConnectionScope.hpp"
#include "geeyoou/core/Signal.hpp"
#include "geeyoou/render/Icon.hpp"
#include "geeyoou/widget/PopupList.hpp"
#include "geeyoou/widget/Widget.hpp"

namespace geeyoou {

// Shared machinery for every dropdown: the closed field, the chevron, popup
// lifecycle, and the keyboard contract.  Subclasses supply a model and answer
// four questions -- what to show when closed, what rows to show when open, and
// what to do when a row is activated or toggled.
//
// Keyboard contract, identical across all of them:
//   closed : Enter / Space / Down / Alt+Down  -> open
//   open   : Up / Down    -> move highlight (skips headers and disabled rows)
//            Home / End   -> first / last selectable
//            Enter        -> activate
//            Esc / Tab    -> close
//            Alt+1..9     -> activate the row carrying that badge
//
// Alt+digit rather than a bare digit, because a bare digit is a legitimate
// search character in a tag name like "TI-101".
class SelectBase : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(SelectBase, Widget)

  SelectBase() { setFocusPolicy(FocusPolicy::Tab); }
  ~SelectBase() override;

  void setPlaceholder(std::string utf8);
  void setInvalid(bool on);
  bool isInvalid() const { return invalid_; }

  void setMaxVisibleRows(int n);
  // 0 = match the control's own width (the default).
  void setPopupWidth(float w);

  bool isOpen() const;
  // Virtual because not every dropdown's popup is a single list: Cascader puts
  // several columns side by side and DatePicker shows a calendar.  Those
  // override open()/close() and supply their own popup through
  // showCustomPopup(), while still inheriting the closed-field chrome, the
  // focus behaviour and the keyboard contract.
  virtual void open();
  virtual void close();

  Signal<bool> openStateChanged;

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;
  void onMouse(const MouseEvent& e) override;
  void onKey(const KeyEvent& e) override;
  void onFocusChanged(bool focused) override;
  void onEnabledChanged() override;

  // --- subclass contract ---------------------------------------------------
  virtual std::string displayText() const = 0;
  virtual bool hasValue() const = 0;
  // Only meaningful for the single-list dropdowns; Cascader and DatePicker
  // leave these at their defaults because their popup is not a PopupList.
  virtual std::vector<PopupRow> buildRows() { return {}; }
  virtual void onRowActivated(int /*row*/) {}
  virtual void onRowToggled(int /*row*/) {}
  virtual void onExpanderToggled(int /*row*/) {}

  // Multi-select stays open after a click; single-select closes.
  virtual bool closeOnActivate() const { return true; }
  // Search variants draw a caret after the field text while open.
  virtual bool showCaret() const { return false; }
  // Return true if the subclass consumed the key (e.g. search typing).
  virtual bool handleKeyWhileOpen(const KeyEvent& /*e*/) { return false; }
  virtual void onOpened() {}
  virtual void onClosed() {}

  // Rebuilds the row list and pushes it into the popup, preserving nothing --
  // callers that care about the highlight restore it afterwards.
  void refreshRows();
  PopupList* list() const { return popup_; }
  Rect fieldTextRect() const;

  // Opens an arbitrary widget as this control's popup, anchored to the field.
  // The widget must already be a child of the Window (create it with
  // window()->add<T>() and keep it hidden between uses).
  void showCustomPopup(Widget* popup);
  void hideCustomPopup(Widget* popup);

 private:
  void ensurePopup();

  PopupList* popup_ = nullptr;
  Widget* customPopup_ = nullptr;  // set when a subclass supplies its own
  std::string placeholder_;
  bool invalid_ = false;
  bool hovered_ = false;
  int maxVisibleRows_ = 9;
  float popupWidth_ = 0.0f;

  // Declared LAST so it is destroyed FIRST.  The slots ensurePopup() installs
  // capture `this` and are subscribed to a PopupList that belongs to the
  // WINDOW, so they outlive this control unless something drops them; taking
  // the popup down in the destructor closes it but leaves them connected.
  ConnectionScope conns_;
};

}  // namespace geeyoou
