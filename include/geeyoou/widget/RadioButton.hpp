#pragma once
#include <string>

#include "geeyoou/core/Signal.hpp"
#include "geeyoou/widget/Widget.hpp"

namespace geeyoou {

// Mutually exclusive selector.
//
// Grouping is implicit: radios sharing a parent AND a group id are exclusive.
// Qt uses the same parent-based rule, and it beats an explicit RadioGroup
// object because the exclusive set is almost always "the ones inside this
// GroupBox" -- which the tree already encodes.
class RadioButton : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(RadioButton, Widget)

  RadioButton() { setFocusPolicy(FocusPolicy::Tab); }

  void setText(std::string utf8);
  const std::string& text() const { return text_; }

  void setGroup(int id) { group_ = id; }
  int group() const { return group_; }

  void setChecked(bool on);
  bool isChecked() const { return checked_; }

  SizeHint sizeHint() const override;

  Signal<bool> toggled;

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;
  void onMouse(const MouseEvent& e) override;
  void onKey(const KeyEvent& e) override;

 private:
  void uncheckSiblings();

  std::string text_;
  int group_ = 0;
  bool checked_ = false;
  bool hovered_ = false;
};

}  // namespace geeyoou
