#pragma once
#include <string>

#include "geeyoou/widget/Widget.hpp"

namespace geeyoou {

// Titled container.  A visual and logical grouping: children may be placed in
// it by hand, and contentRect() gives the inset area so call sites do not
// hardcode the title height.
//
// Disabling a GroupBox disables every descendant, which is how an interlocked
// parameter block is expressed.
//
// It can also HOST a layout (setLayout<BoxLayout>()).  Then:
//   * the layout is arranged into contentRect() -- INSIDE the frame and below
//     the title rule -- because GroupBox overrides layoutRect().  A layout in a
//     GroupBox therefore needs no margins of its own, and the ones it does have
//     mean what they say rather than "frame plus what I wanted";
//   * sizeHint() reports what that layout needs, plus this frame.
class GroupBox : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(GroupBox, Widget)

  void setTitle(std::string utf8);
  const std::string& title() const { return title_; }

  Rect contentRect() const;

  SizeHint sizeHint() const override;

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;
  Rect layoutRect() const override { return contentRect(); }

 private:
  std::string title_;
};

}  // namespace geeyoou
