#pragma once
#include <string>
#include <vector>

#include "geeyoou/render/Painter.hpp"
#include "geeyoou/widget/Widget.hpp"

namespace geeyoou {

class Label : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(Label, Widget)

  void setText(std::string utf8);
  const std::string& text() const { return text_; }

  // Both of these PIN the value: once set from code, neither the theme nor the
  // style sheet overrides it again.  Leave them alone to let a label follow the
  // active skin -- which is what a `.caption { color: @textDim }` rule needs.
  void setPixelSize(float px);
  void setColor(Color c);
  // Effective values after theme and style sheet are folded in.
  float pixelSize() const;
  Color color() const;
  void setAlign(HAlign h, VAlign v = VAlign::Middle);

  // Honours '\n' and soft-wraps at the widget's width.
  //
  // Off by default: the overwhelming majority of HMI labels are one short
  // string, and paying for a wrap pass on every one of them would be waste.
  // But a label that cannot render a newline is not a label -- without this,
  // "\n" ends up as a tofu box.
  void setWordWrap(bool on);
  bool wordWrap() const { return wrap_; }

  // Height the current text needs at the current width; useful for sizing a
  // wrapped label to its content.
  float heightForWidth(float width) const;
  float lineSpacing() const;

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;
  void onGeometryChanged() override;

 private:
  void rebuildLines(float width) const;

  std::string text_;
  // Both start UNSET rather than at a literal, so an unstyled label follows the
  // active theme.  Hard-coding the dark palette's text colour here meant every
  // label stayed pale when a light skin was applied -- invisible on white.
  float pixelSize_ = 0.0f;
  bool sizeSet_ = false;
  Color color_;
  bool colorSet_ = false;
  HAlign hAlign_ = HAlign::Left;
  VAlign vAlign_ = VAlign::Middle;
  bool wrap_ = false;

  // Cached wrap result. Mutable so heightForWidth() and onPaint() can both
  // refresh it without forcing callers to pre-compute a layout.
  mutable std::vector<std::string> lines_;
  mutable float wrappedFor_ = -1.0f;
  mutable std::uint64_t wrappedGen_ = 0;
};

}  // namespace geeyoou
