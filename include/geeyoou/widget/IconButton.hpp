#pragma once
#include "geeyoou/widget/PushButton.hpp"

namespace geeyoou {

// Square, icon-only button for toolbars and inline actions.
//
// Subclasses PushButton so variants, checkable latching, loading and focus
// behaviour are all inherited; it only changes the layout (centre the glyph,
// no label) and the default shape.
class IconButton : public PushButton {
 public:
  GEEYOOU_STYLE_TYPE(IconButton, PushButton)

  IconButton() {
    setVariant(ButtonVariant::Ghost);
  }

  // Icon size as a fraction of the shorter side.  0.55 keeps a comfortable
  // tap target around a 24px glyph in a 44px button.
  void setIconScale(float f);
  float iconScale() const { return iconScale_; }

  // Round instead of rounded-rect -- reads better for a floating action.
  void setCircular(bool on);

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;

 private:
  float iconScale_ = 0.55f;
  bool circular_ = false;
};

}  // namespace geeyoou
