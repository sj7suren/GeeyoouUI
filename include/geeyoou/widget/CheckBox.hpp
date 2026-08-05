#pragma once
#include <string>

#include "geeyoou/core/Signal.hpp"
#include "geeyoou/widget/Widget.hpp"

namespace geeyoou {

class CheckBox : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(CheckBox, Widget)

  CheckBox() { setFocusPolicy(FocusPolicy::Tab); }

  void setText(std::string utf8);
  const std::string& text() const { return text_; }

  void setChecked(bool on);
  bool isChecked() const { return checked_; }
  void toggle() { setChecked(!checked_); }

  Signal<bool> toggled;

  SizeHint sizeHint() const override;

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;
  void onMouse(const MouseEvent& e) override;
  void onKey(const KeyEvent& e) override;

 private:
  std::string text_;
  bool checked_ = false;
  bool hovered_ = false;
  bool pressed_ = false;
};

}  // namespace geeyoou
