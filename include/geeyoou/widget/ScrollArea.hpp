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
  //
  // ⚠️ MAY RETURN nullptr, and that is a change to this contract (E15).  If the
  // application removes the content -- or the viewport that holds it -- from
  // the tree, this scroll area is told (Widget::onDescendantDetached), it drops
  // the pointer, and it answers nullptr from then on.  Before E15 it kept the
  // dangling pointer and the next repaint was a use-after-free, so the check
  // this now asks of callers is not a new cost: it replaces a crash.
  //
  // PERMANENTLY nullptr, on purpose.  Nothing rebuilds the content, and nothing
  // will: an application that took its own content away and then found content()
  // answering with a DIFFERENT widget than the one it put there would have
  // traded "it crashes" for "it quietly answers wrong", which is the worse of
  // the two.  Putting content back is a missing API (setContentWidget),
  // registered and deliberately not in this round -- not something this getter
  // should improvise.
  //
  // Everything else on this class degrades to match: see the definitions in
  // ScrollArea.cpp and the table in docs/iterations/02-layout-engine.md
  // section 11.12.
  Widget* content() { return content_; }

  void setContentSize(Size size);
  // {0,0} once the content is gone -- an area with nothing in it has nothing to
  // scroll over, which is also what makes every bar and thumb below empty
  // without a second null test in any of them.
  Size contentSize() const {
    return content_ ? content_->geometry().size() : Size{0.0f, 0.0f};
  }

  Point scrollOffset() const;
  void scrollTo(Point offset);
  void scrollBy(float dx, float dy);
  // Scrolls the minimum distance needed to reveal `rect` (content coordinates).
  void ensureVisible(const Rect& rect);

  void setScrollStep(float px);
  void setFrameVisible(bool on);

  SizeHint sizeHint() const override;

  Signal<Point> scrolled;

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;
  void onMouse(const MouseEvent& e) override;
  void onKey(const KeyEvent& e) override;
  void onGeometryChanged() override;
  // E15/REM3-RES-1.  Nulls viewport_ / content_ when either leaves the tree,
  // and does NOTHING else -- REM3-G9: this runs in the middle of a pre-order
  // walk over a half-detached tree, so no update(), no signal, no removal, no
  // virtual call.  The degradation the null-out implies is in ScrollArea.cpp.
  void onDescendantDetached(Widget* node) override;

 private:
  enum class Drag { None, Vertical, Horizontal };

  // Is this scroll area still whole?  ONE predicate rather than a null test per
  // method, because the two members are not independently useful: the content
  // lives inside the viewport, so losing the viewport loses both, and an area
  // that has lost either one has nothing to scroll and nothing to scroll it in.
  // Taking the viewport announces the content as well (it is a node of the same
  // departing subtree), so the two are cleared by the same removal anyway.
  bool hasParts() const { return viewport_ != nullptr && content_ != nullptr; }

  // Which scrollbars the current content needs.  One function for the pair:
  // each bar takes a strip out of the other's axis, so they cannot be decided
  // independently.  See the note above the definition.
  void bars(bool& vbar, bool& hbar) const;
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
