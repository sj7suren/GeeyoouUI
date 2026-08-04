#pragma once
#include "geeyoou/core/Signal.hpp"
#include "geeyoou/widget/Widget.hpp"

namespace geeyoou {

// Scrolling container.
//
// Three-level structure -- ScrollArea > viewport > content:
//   * ScrollArea owns the frame and draws the scrollbars in reserved strips;
//   * the viewport is sized to exclude those strips and carries the scroll
//     offset, so it is what actually clips the content;
//   * content is a plain Widget of arbitrary size that callers fill.
//
// The middle layer exists so scrollbars are never painted under the content:
// a widget paints itself before its children, so drawing the bars in
// ScrollArea::onPaint with a full-bleed content child would bury them.
class ScrollArea : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(ScrollArea, Widget)

  ScrollArea();

  // Add your children to this.  Its geometry IS the scrollable extent.
  Widget* content() { return content_; }

  void setContentSize(Size size);
  Size contentSize() const { return content_->geometry().size(); }

  Point scrollOffset() const;
  void scrollTo(Point offset);
  void scrollBy(float dx, float dy);
  // Scrolls the minimum distance needed to reveal `rect` (content coordinates).
  void ensureVisible(const Rect& rect);

  void setScrollStep(float px);
  void setFrameVisible(bool on);

  Signal<Point> scrolled;

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;
  void onMouse(const MouseEvent& e) override;
  void onKey(const KeyEvent& e) override;
  void onGeometryChanged() override;

 private:
  enum class Drag { None, Vertical, Horizontal };

  bool needVBar() const;
  bool needHBar() const;
  Size viewportSize() const;
  Rect vBarRect() const;
  Rect hBarRect() const;
  Rect vThumbRect() const;
  Rect hThumbRect() const;
  Point maxScroll() const;
  void relayout();

  Widget* viewport_ = nullptr;
  Widget* content_ = nullptr;
  float step_ = 40.0f;
  bool frame_ = true;
  Drag drag_ = Drag::None;
  float dragStart_ = 0.0f;
  float dragScrollStart_ = 0.0f;
  bool hoverV_ = false;
  bool hoverH_ = false;
};

}  // namespace geeyoou
