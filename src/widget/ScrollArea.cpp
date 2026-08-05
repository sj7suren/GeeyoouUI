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

// --- E15: what this widget answers once its parts have been taken away -------
//
// REM3-RES-1's other half.  A guard gets ONE FRAME home safely and does not
// repair object state, so from E1 until E14 a ScrollArea whose content had been
// removed kept the freed pointer and died on the next repaint -- with no
// application call in between, which is what put this outside what any frame
// guard can reach.
//
// THE HOOK IS THREE LINES AND MAY BE NOTHING MORE (REM3-G9).  It runs from
// inside announceDetached's pre-order walk, over a tree that is half detached
// and has not been unlinked: an update(), a signal, a removal or any virtual
// call from here re-enters the door that walk just closed.  Comparing pointers
// is safe by construction -- `node`, this widget and the whole departing
// subtree are all alive at this instant, which is exactly why the broadcast is
// placed where it is.
//
// TWO TESTS, NOT ONE.  Removing the viewport announces the content too (it is a
// node of the same departing subtree, and the walk is per node), so both arms
// fire on that removal; removing the content alone fires only the first.
void ScrollArea::onDescendantDetached(Widget* node) {
  if (node == content_) content_ = nullptr;
  if (node == viewport_) viewport_ = nullptr;
}

// WHAT THE DEGRADED ANSWERS ARE, and why they are not a self-heal.
//
// Rebuilding the viewport and the content here would be easy and would be
// WRONG: the application took its own content out, and silently standing a
// different widget in its place turns "this crashes" into "content() quietly
// answers with something the application never put there" -- the same trade
// this whole remediation exists to refuse (ADR-R2-04's shape, one level up).
// So the degradation is PERMANENT, and it is the correct answer rather than a
// regrettable one.  Putting content back is a missing API, registered as
// ScrollArea::setContentWidget and deliberately out of this round.
//
// The table (section 11.12): content() -> nullptr, contentSize() -> {0,0},
// scrollOffset()/maxScroll() -> {0,0}, viewportSize() -> localRect().size(),
// both bars false, the four mutators return without writing any geometry,
// onPaint still draws the frame and no track or thumb, and no scroll input is
// accepted.
//
// MOST OF THAT FALLS OUT OF ONE GUARD rather than being written twice.  bars()
// answering "no bars" makes viewportSize() the whole local rect, which makes
// every bar and thumb rect empty, which makes onPaint's `bar` lambda return at
// its first line and maxScroll() {0,0} arithmetically.  Only the five entry
// points that DEREFERENCE a member need one of their own, and they are the five
// below.
void ScrollArea::setContentSize(Size size) {
  // E15.  Before the cursors, not after: the guards below are for a frame that
  // has something to guard, and DeathWatch on a null member is a caller bug by
  // its own contract (Widget.hpp) -- "a null member is the site's own null
  // check to make, not something a guard may impersonate".  This is that check.
  if (!hasParts()) return;

  // CP-C1 and CP-C2 (docs/iterations/02-layout-engine.md section 11.3).  TWO
  // doors in five lines, and everything after either of them is reached through
  // one of three pointers:
  //
  //   * relayout() goes through `this` and, inside, through both members;
  //   * scrollTo(scrollOffset()) dereferences viewport_ (scrollOffset) and
  //     content_ (maxScroll -> contentSize, ScrollArea.hpp);
  //   * update() goes through `this` alone.
  //
  // (Section 11.3 calls these three doors and statements :22, :24, :26 and :27,
  // from the HEAD it was written against.  They have moved down by the size of
  // these comments; the STATEMENTS are what the table means.)
  //
  // So three cursors, not one (REM3-G2): the application may destroy the
  // content or the viewport WITHOUT touching this ScrollArea -- content() is
  // public and the viewport is reachable through children() -- and a cursor
  // standing on `this` would report all is well while the very next line writes
  // into a freed block.
  //
  // The captured values are the other half of the same rule.  A cursor answers
  // "is the object I remembered still alive"; it cannot answer "is the member
  // still POINTING at it", and E14's onDescendantDetached hook is expected to
  // null these members out from inside a door (section 11.6, REM3-RES-7).  So
  // each check re-reads the member THROUGH `this` and compares, and a member
  // that changed degrades rather than being followed -- the new value has no
  // cursor on it, and this frame has no way to earn one now.
  Widget* const vp0 = viewport_;
  Widget* const ct0 = content_;
  const detail::DeathWatch self(this);
  const detail::DeathWatch vpw(viewport_);
  const detail::DeathWatch ctw(content_);

  content_->setGeometry({0.0f, 0.0f, std::max(0.0f, size.width),
                         std::max(0.0f, size.height)});
  // CP-C1, REM3-G3: immediately after the door, before any other statement.
  // The order is load-bearing, not stylistic: `this` first, because the two
  // member re-reads below dereference it; then the members, because a member
  // that was replaced makes its cursor answer a question about the wrong
  // object; then the cursors, which catch the case the re-reads cannot -- the
  // member still points where it did and the object it points at is gone.
  if (!self.alive() || viewport_ != vp0 || content_ != ct0 || !vpw.alive() ||
      !ctw.alive()) {
    detail::frameDegraded();  // REM3-G8: once per frame, and the frame ends here
    return;
  }
  relayout();
  // CP-C2.  relayout() is a door in its own right -- it holds three of them,
  // section 11.4 #3/#4/#5 -- and its own guards protect ITS frame, not this one.
  // Same five checks: `this` may have died in there, and so may either member.
  if (!self.alive() || viewport_ != vp0 || content_ != ct0 || !vpw.alive() ||
      !ctw.alive()) {
    detail::frameDegraded();
    return;
  }
  // Shrinking the content can leave the view scrolled past the new end.
  scrollTo(scrollOffset());
  // No CP-C3 after :26, and the reason is a contract rather than an accident:
  // scrollTo's only door is `scrolled.emit(...)` and the signal's host is
  // `this`, which D7 forbids a slot from destroying (Widget.hpp, core/
  // Signal.hpp).  Section 11.4 #9.  All that follows is update(), which reads
  // `this` and walks parent_ -- no member pointer.  Add a statement here that
  // touches viewport_ or content_ and the exemption stops covering it, because
  // D7 says nothing about the OTHER two pointers.
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

// {0,0} rather than the offset the viewport still happened to carry: an area
// with no content cannot be scrolled anywhere, so "where is it scrolled to" has
// exactly one honest answer.  Guarded on BOTH members, not just the one it
// dereferences -- an offset that survives its content is a number nothing can
// be done with, and scrollTo() below refuses to change it.
Point ScrollArea::scrollOffset() const {
  if (!hasParts()) return {0.0f, 0.0f};
  return viewport_->contentOffset();
}

Point ScrollArea::maxScroll() const {
  const Size vp = viewportSize();
  const Size cs = contentSize();
  return {std::max(0.0f, cs.width - vp.width), std::max(0.0f, cs.height - vp.height)};
}

void ScrollArea::scrollTo(Point offset) {
  // E15.  maxScroll() and scrollOffset() would both answer {0,0} on their own,
  // so the clamp below would already refuse to move -- but the write goes
  // through viewport_, and a method that reaches a member pointer states its
  // own precondition rather than inheriting one from two callees.
  if (!hasParts()) return;

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
  // E15.  "Reveal this rectangle" has no meaning without a content to reveal it
  // in; the alternative is scrolling an empty viewport to an arbitrary place.
  if (!hasParts()) return;

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
//
// BOTH BARS ARE DECIDED TOGETHER, in one function, because each one steals a
// strip from the other's axis.  Asking the two questions independently either
// recurses forever or answers one of them wrongly, and the version this
// replaces did the second: needHBar() subtracted a vertical bar's width
// UNCONDITIONALLY, so `content.width > width - kBar` was true for every content
// exactly as wide as the area -- which, since relayout() sizes content to at
// least the viewport, is the normal case.  Every scroll area in the
// application therefore carried a permanent 10px horizontal scroll range with
// nothing to scroll to.
//
// The refinement step terminates: only a horizontal bar that appeared on its
// own can bring a vertical one with it, and that question is asked once.
// Vertical still wins the tie, which is the right bias for lists.
void ScrollArea::bars(bool& vbar, bool& hbar) const {
  // E15, AND THE ONE GUARD THE PAINT PATH ACTUALLY NEEDS.  Everything drawn --
  // both tracks, both thumbs -- is derived from this answer, so "no parts, no
  // bars" here is what keeps onPaint free of null tests and what makes
  // viewportSize() below the whole local rect.  It is also the frame that
  // matters: the repaint after a removal needs no application call to happen,
  // which is what put this defect outside every frame guard.
  if (!hasParts()) {
    vbar = false;
    hbar = false;
    return;
  }

  const Rect r = localRect();
  const Size cs = contentSize();
  vbar = cs.height > r.height();
  hbar = cs.width > r.width() - (vbar ? kBar : 0.0f);
  if (hbar && !vbar) vbar = cs.height > r.height() - kBar;
}

bool ScrollArea::needVBar() const {
  bool v = false;
  bool h = false;
  bars(v, h);
  return v;
}

bool ScrollArea::needHBar() const {
  bool v = false;
  bool h = false;
  bars(v, h);
  return h;
}

Size ScrollArea::viewportSize() const {
  bool v = false;
  bool h = false;
  bars(v, h);
  const Rect r = localRect();
  return {std::max(0.0f, r.width() - (v ? kBar : 0.0f)),
          std::max(0.0f, r.height() - (h ? kBar : 0.0f))};
}

void ScrollArea::relayout() {
  // E15.  Both arms below write geometry THROUGH the two members -- the branch
  // test itself is `content_->layout()` -- so this is the precondition of the
  // whole function rather than of one path.  Returning here is also what makes
  // "the area kept the size it had" true after a removal: nothing is placed,
  // because there is nothing left to place.
  if (!hasParts()) return;

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
  //
  // ORDER OF EVALUATION IS THE WHOLE POINT HERE.  What the page NEEDS decides
  // the two bars, the bars decide the viewport, and the viewport and the need
  // together decide the content -- in that order.  Sizing the viewport first,
  // as this used to, asked bars() about the content size from BEFORE the resize
  // and then set the content to exactly the width that answer implied, which is
  // how a page that fits perfectly ended up permanently scrollable sideways.
  if (content_->layout()) {
    // CP-S1 and CP-S2 (section 11.3).  The guards live INSIDE this branch on
    // purpose: content with no layout takes the tail of the function, runs no
    // application code on the way, and pays nothing -- which is ADR-R2-01 held
    // to, not merely quoted.  Every ScrollArea in the library and the five
    // absolute-coordinate showcase pages take that other path.
    //
    // Three cursors and two captured values, for the same reason as
    // setContentSize above: the two setGeometry calls below WRITE through the
    // two members, and a write is what pre-reading can never cover.  (Section
    // 11.3 calls this door :158 and those two writes :164 and :165, from the
    // HEAD it was written against; the statements are what the table means.)
    Widget* const vp0 = viewport_;
    Widget* const ct0 = content_;
    const detail::DeathWatch self(this);
    const detail::DeathWatch vpw(viewport_);
    const detail::DeathWatch ctw(content_);

    const SizeHint h = content_->sizeHint();
    // CP-S1.  The door above runs the content's layout, which asks every child,
    // and one of those overrides is entitled to drop this whole scroll area --
    // which is exactly what a page does when a control on it rebuilds the page.
    // `h` is already a local copy, so it stays legal; nothing else does.
    if (!self.alive() || viewport_ != vp0 || content_ != ct0 || !vpw.alive() ||
        !ctw.alive()) {
      detail::frameDegraded();
      return;
    }
    const Rect r = localRect();
    const bool vbar = h.preferred.height > r.height();
    const bool hbar = h.preferred.width > r.width() - (vbar ? kBar : 0.0f);
    const Size vp{std::max(0.0f, r.width() - (vbar ? kBar : 0.0f)),
                  std::max(0.0f, r.height() - (hbar ? kBar : 0.0f))};
    viewport_->setGeometry({0.0f, 0.0f, vp.width, vp.height});
    // CP-S2 -- the door in SERIES with the one above, and the one a fix that
    // only guarded :158 would leave open.  The viewport is a plain Widget with
    // no layout today, so this call runs no application code TODAY; that is not
    // an invariant, because content()->parent() is reachable through the public
    // API and one setLayout<> on it makes this a door for real (section 11.4
    // #4).  It is also the one the reproducer's stack names: the free site is
    // this line and the use site is the next one.
    //
    // THREE checks, not five: `viewport_` is never touched again after this
    // line, so guarding it here would be asking a question with no consumer.
    // `content_` is touched -- read out of `this` and then written through --
    // so it keeps both of its checks.
    if (!self.alive() || content_ != ct0 || !ctw.alive()) {
      detail::frameDegraded();
      return;
    }
    content_->setGeometry({0.0f, 0.0f,
                           std::max(vp.width, h.preferred.width),
                           std::max(vp.height, h.preferred.height)});
    // A door with no check after it, deliberately: the next statement is the
    // return, so a check here would be dead code (section 11.3).  ADD ANYTHING
    // BELOW THIS LINE AND IT NEEDS A CHECK FIRST -- REM3-G3, and the checks
    // above are the shape to copy.
    return;
  }

  // Hand-placed content: its size is whatever setContentSize was told, so the
  // bars can be read straight off it.
  const Size vp = viewportSize();
  viewport_->setGeometry({0.0f, 0.0f, vp.width, vp.height});
  // The other unchecked door, and the same reason: it is the last statement of
  // the function.  No cursor is taken on this path at all, which is what keeps
  // the hand-placed-content case free of charge.  APPEND A STATEMENT HERE AND
  // YOU NEED BOTH -- a cursor in front of this call and a check behind it.
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
  // E15.  No content, no scrolling, and therefore NO ACCEPT: an area that
  // swallowed the wheel and then did nothing with it would stop the event
  // bubbling to whatever encloses it, so an operator's wheel would die on a
  // widget that has nothing to show.  Refusing lets the parent scroll instead.
  // The stale hover flags this leaves behind cannot be seen: both bar rects are
  // empty, so nothing that reads them is drawn.
  if (!hasParts()) return;

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
  // E15, same reason as onMouse: PageDown on an emptied area belongs to
  // whatever encloses it, not to a scroll area with nothing to scroll.
  if (!hasParts()) return;
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
