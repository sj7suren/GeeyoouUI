#pragma once
#include <string>

#include "geeyoou/widget/Widget.hpp"

namespace geeyoou {

// Titled container.  Purely a visual and logical grouping -- it does not lay
// its children out (v1 uses absolute coordinates), but contentRect() gives the
// inset area so call sites do not hardcode the title height.
//
// Disabling a GroupBox disables every descendant, which is how an interlocked
// parameter block is expressed.
class GroupBox : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(GroupBox, Widget)

  void setTitle(std::string utf8);
  const std::string& title() const { return title_; }

  Rect contentRect() const;

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;

 private:
  std::string title_;
};

}  // namespace geeyoou
