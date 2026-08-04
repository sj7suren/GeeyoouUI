#pragma once
#include "geeyoou/widget/Widget.hpp"

namespace geeyoou {

enum class Orientation { Horizontal, Vertical };

// A one-pixel rule.  Trivial, but having it as a widget keeps dividers out of
// every container's onPaint.
class Separator : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(Separator, Widget)

  void setOrientation(Orientation o);
  void setColor(Color c);

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;

 private:
  Orientation orientation_ = Orientation::Horizontal;
  bool colorSet_ = false;
  Color color_;
};

}  // namespace geeyoou
