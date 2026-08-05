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
//
// It can HOST a layout (setLayout<BoxLayout>()), and then sizeHint() reports
// what that layout needs plus this frame.  One thing it does not do for you:
// the rectangle the layout is handed is localRect() minus the LAYOUT's margins,
// not contentRect() -- so a layout inside a GroupBox wants
// setMargins({12, 34, 12, 12}) or its first row is drawn across the title.  See
// docs/iterations/02-layout-engine.md.
class GroupBox : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(GroupBox, Widget)

  void setTitle(std::string utf8);
  const std::string& title() const { return title_; }

  Rect contentRect() const;

  SizeHint sizeHint() const override;

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;

 private:
  std::string title_;
};

}  // namespace geeyoou
