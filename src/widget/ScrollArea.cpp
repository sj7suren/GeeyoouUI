#include "geeyoou/widget/ScrollArea.hpp"

#include <algorithm>

#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"

namespace geeyoou {
namespace {
constexpr float kBar = 10.0f;
constexpr float kMinThumb = 24.0f;
}  // namespace

ScrollArea::ScrollArea() {
  setFocusPolicy(FocusPolicy::Click);
  viewport_ = add<Widget>();
  content_ = viewport_->add<Widget>();
  content_->setGeometry({0.0f, 0.0f, 0.0f, 0.0f});
}

void ScrollArea::setContentSize(Size size) {
  content_->setGeometry({0.0f, 0.0f, std::max(0.0f, size.width),
                         std::max(0.0f, size.height)});
  relayout();
  // Shrinking the content can leave the view scrolled past the new end.
  scrollTo(scrollOffset());
  update();
}

// A VIEWPORT size, and deliberately not the content's.
//
// Reporting contentSize() here would defeat the widget: a scroll area exists
// because its content does not fit, so a hint of "as big as what I hold" asks
// the enclosing layout for exactly the room that would make the scrollbars
// unnecessary -- and, when it cannot have it, records the difference as an
// overflow that is not a fault.  It would also be circular the moment the
// content is itself laid out.
//
// So the numbers below are a default WINDOW: big enough to be usable, small
// enough to sit in a form, and meant to be overridden by giving the scroll area
// stretch in whatever box holds it.
SizeHint ScrollArea::sizeHint() const {
  SizeHint h;
  h.preferred = Size{320.0f, 200.0f};
  h.min = Size{4.0f * kBar, 4.0f * kBar};
  return h;
}

Point ScrollArea::scrollOffset() const { return viewport_->contentOffset(); }

Point ScrollArea::maxScroll() const {
  const Size vp = viewportSize();
  const Size cs = contentSize();
  return {std::max(0.0f, cs.width - vp.width), std::max(0.0f, cs.height - vp.height)};
}

void ScrollArea::scrollTo(Point offset) {
  const Point m = maxScroll();
  const Point clamped{std::clamp(offset.x, 0.0f, m.x),
                      std::clamp(offset.y, 0.0f, m.y)};
  const Point cur = scrollOffset();
  if (clamped.x == cur.x && clamped.y == cur.y) return;
  viewport_->setContentOffset(clamped);
  update();
  scrolled.emit(clamped);
}

void ScrollArea::scrollBy(float dx, float dy) {
  const Point c = scrollOffset();
  scrollTo({c.x + dx, c.y + dy});
}

void ScrollArea::ensureVisible(const Rect& r) {
  const Size vp = viewportSize();
  Point o = scrollOffset();
  if (r.top() < o.y) o.y = r.top();
  else if (r.bottom() > o.y + vp.height) o.y = r.bottom() - vp.height;
  if (r.left() < o.x) o.x = r.left();
  else if (r.right() > o.x + vp.width) o.x = r.right() - vp.width;
  scrollTo(o);
}

void ScrollArea::setScrollStep(float px) { step_ = std::max(1.0f, px); }

void ScrollArea::setFrameVisible(bool on) {
  frame_ = on;
  update();
}

void ScrollArea::onGeometryChanged() { relayout(); }

// ----------------------------------------------------------------- layout ---
bool ScrollArea::needVBar() const {
  return contentSize().height > localRect().height() - (needHBar() ? kBar : 0.0f);
}

bool ScrollArea::needHBar() const {
  // Deliberately asymmetric: the horizontal test does NOT consult needVBar(),
  // because the pair would recurse forever.  Vertical wins the tie, which is
  // the right bias for lists.
  return contentSize().width > localRect().width() - kBar;
}

Size ScrollArea::viewportSize() const {
  const Rect r = localRect();
  return {std::max(0.0f, r.width() - (needVBar() ? kBar : 0.0f)),
          std::max(0.0f, r.height() - (needHBar() ? kBar : 0.0f))};
}

void ScrollArea::relayout() {
  const Size vp = viewportSize();
  viewport_->setGeometry({0.0f, 0.0f, vp.width, vp.height});

  // A laid-out page grows with the window; a hand-placed one does not.
  //
  // Without this the layout engine stops at the page's edge: the controls
  // inside a page rearrange themselves perfectly, and then the page itself
  // stays pinned to whatever design size it was built at, so enlarging the
  // window still just adds margin -- the exact complaint R2 exists to fix.
  //
  // Strictly opt-in: content with no layout is left alone, which is why the
  // five pages still using absolute coordinates cannot be affected by this.
  // max() rather than assignment, so a page shorter than the viewport still
  // fills it instead of floating in the middle.
  if (content_->layout()) {
    const SizeHint h = content_->sizeHint();
    content_->setGeometry({0.0f, 0.0f,
                           std::max(vp.width, h.preferred.width),
                           std::max(vp.height, h.preferred.height)});
  }
}

Rect ScrollArea::vBarRect() const {
  const Rect r = localRect();
  if (!needVBar()) return {};
  return {r.right() - kBar, 0.0f, kBar, viewportSize().height};
}

Rect ScrollArea::hBarRect() const {
  const Rect r = localRect();
  if (!needHBar()) return {};
  return {0.0f, r.bottom() - kBar, viewportSize().width, kBar};
}

Rect ScrollArea::vThumbRect() const {
  const Rect bar = vBarRect();
  if (bar.isEmpty()) return {};
  const float cs = contentSize().height;
  const float vp = viewportSize().height;
  const float h = std::max(kMinThumb, bar.height() * (vp / cs));
  const float m = maxScroll().y;
  const float y = (m > 0.0f) ? (bar.height() - h) * (scrollOffset().y / m) : 0.0f;
  return {bar.x() + 2.0f, bar.y() + y, bar.width() - 4.0f, h};
}

Rect ScrollArea::hThumbRect() const {
  const Rect bar = hBarRect();
  if (bar.isEmpty()) return {};
  const float cs = contentSize().width;
  const float vp = viewportSize().width;
  const float w = std::max(kMinThumb, bar.width() * (vp / cs));
  const float m = maxScroll().x;
  const float x = (m > 0.0f) ? (bar.width() - w) * (scrollOffset().x / m) : 0.0f;
  return {bar.x() + x, bar.y() + 2.0f, w, bar.height() - 4.0f};
}

// ------------------------------------------------------------------ paint ---
void ScrollArea::onPaint(Painter& p, const Rect&) {
  const Theme& t = Theme::current();
  const Rect r = localRect();

  if (frame_) {
    p.fillRoundRect(r, t.radius, t.field);
    p.strokeRoundRect(r.deflated(0.5f), t.radius,
                      hasFocus() ? t.focusRing : t.panelBorder, 1.0f);
  }

  const auto bar = [&](const Rect& track, const Rect& thumb, bool hot) {
    if (track.isEmpty()) return;
    p.fillRoundRect(track.deflated(2.0f), 3.0f, t.track);
    p.fillRoundRect(thumb, 3.0f,
                    hot ? t.scrollbar.lerp(t.text, 0.3f) : t.scrollbar);
  };
  bar(vBarRect(), vThumbRect(), hoverV_ || drag_ == Drag::Vertical);
  bar(hBarRect(), hThumbRect(), hoverH_ || drag_ == Drag::Horizontal);
}

// ------------------------------------------------------------------ input ---
void ScrollArea::onMouse(const MouseEvent& e) {
  if (!isEffectivelyEnabled()) return;

  switch (e.action) {
    case MouseAction::Wheel:
      // Shift+wheel scrolls horizontally, the desktop convention.
      if (e.shift) scrollBy(-e.wheelDelta * step_, 0.0f);
      else scrollBy(0.0f, -e.wheelDelta * step_);
      e.accept();
      return;

    case MouseAction::Press:
      if (e.button != MouseButton::Left) break;
      if (vThumbRect().contains(e.pos)) {
        drag_ = Drag::Vertical;
        dragStart_ = e.pos.y;
        dragScrollStart_ = scrollOffset().y;
        e.accept();
        return;
      }
      if (hThumbRect().contains(e.pos)) {
        drag_ = Drag::Horizontal;
        dragStart_ = e.pos.x;
        dragScrollStart_ = scrollOffset().x;
        e.accept();
        return;
      }
      // Clicking the track pages towards the click, like every desktop
      // scrollbar -- jumping the thumb under the cursor loses the operator's
      // place in a long alarm list.
      if (vBarRect().contains(e.pos)) {
        scrollBy(0.0f, e.pos.y < vThumbRect().y() ? -viewportSize().height
                                                  : viewportSize().height);
        e.accept();
        return;
      }
      if (hBarRect().contains(e.pos)) {
        scrollBy(e.pos.x < hThumbRect().x() ? -viewportSize().width
                                            : viewportSize().width, 0.0f);
        e.accept();
        return;
      }
      break;

    case MouseAction::Move: {
      const bool hv = vBarRect().contains(e.pos);
      const bool hh = hBarRect().contains(e.pos);
      if (hv != hoverV_ || hh != hoverH_) {
        hoverV_ = hv;
        hoverH_ = hh;
        update();
      }
      if (drag_ == Drag::Vertical) {
        const Rect bar = vBarRect();
        const float span = bar.height() - vThumbRect().height();
        if (span > 0.0f) {
          const float k = maxScroll().y / span;
          scrollTo({scrollOffset().x, dragScrollStart_ + (e.pos.y - dragStart_) * k});
        }
        e.accept();
      } else if (drag_ == Drag::Horizontal) {
        const Rect bar = hBarRect();
        const float span = bar.width() - hThumbRect().width();
        if (span > 0.0f) {
          const float k = maxScroll().x / span;
          scrollTo({dragScrollStart_ + (e.pos.x - dragStart_) * k, scrollOffset().y});
        }
        e.accept();
      }
      return;
    }

    case MouseAction::Release:
      if (drag_ != Drag::None) {
        drag_ = Drag::None;
        update();
        e.accept();
      }
      return;

    case MouseAction::Leave:
      hoverV_ = hoverH_ = false;
      update();
      return;

    default:
      break;
  }
}

void ScrollArea::onKey(const KeyEvent& e) {
  if (!e.pressed) return;
  const Size vp = viewportSize();
  switch (e.key) {
    case Key::Up:       scrollBy(0.0f, -step_); e.accept(); break;
    case Key::Down:     scrollBy(0.0f, step_);  e.accept(); break;
    case Key::Left:     scrollBy(-step_, 0.0f); e.accept(); break;
    case Key::Right:    scrollBy(step_, 0.0f);  e.accept(); break;
    case Key::PageUp:   scrollBy(0.0f, -vp.height); e.accept(); break;
    case Key::PageDown: scrollBy(0.0f, vp.height);  e.accept(); break;
    case Key::Home:     scrollTo({scrollOffset().x, 0.0f}); e.accept(); break;
    case Key::End:      scrollTo({scrollOffset().x, maxScroll().y}); e.accept(); break;
    default: break;
  }
}

}  // namespace geeyoou
