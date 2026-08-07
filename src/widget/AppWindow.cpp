#include "geeyoou/widget/AppWindow.hpp"

#include <algorithm>

#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Skin.hpp"
#include "geeyoou/render/Theme.hpp"

namespace geeyoou {

WindowOptions AppWindow::framelessDefaults() {
  WindowOptions o;
  o.frameless = true;
  o.resizable = true;
  o.minSize = {640.0f, 420.0f};
  return o;
}

AppWindow::AppWindow(const std::string& title, int logicalWidth,
                     int logicalHeight, const WindowOptions& options)
    : Window(title, logicalWidth, logicalHeight, options) {
  setBackground(Theme::current().background);

  header_ = add<WindowHeader>();
  content_ = add<Widget>();

  header_->setTitle(title);
  header_->minimizeRequested.connect([this] { minimize(); });
  header_->maximizeRequested.connect([this] { toggleMaximize(); });
  header_->closeRequested.connect([this] { close(); });
  // A property that changes the bar's height moves the content area with it.
  header_->metricsChanged.connect([this] { relayout(); });

  maximizedChanged.connect([this](bool on) {
    // header_ MAY BE NULL here since E15, and this slot runs on every single
    // maximise/restore, so an unguarded store would be a crash an application
    // reaches by pressing one button.  It is the one place E15 made reachable
    // and did not follow: before it, header_ was set in this constructor and
    // never written again.
    //
    // NO SELF-HEAL, same rule as onDescendantDetached and setContent<T>: an
    // application that took the title bar out of the tree does not get a new
    // one grown under it.  The state simply stops being maintained.
    if (header_) header_->setMaximized(on);
    // The outline is dropped while maximised, so the content area grows by the
    // border width -- that has to be re-laid out, not just repainted.  Both of
    // these stay UNCONDITIONAL: the border width changes whether or not a
    // header is present, relayout() has tested for the null members since the
    // day it was written, and update() touches none of them.
    relayout();
    update();
  });

  // The window backdrop is a stored colour rather than a per-paint theme
  // lookup, so unlike every widget it has to be re-read when the skin changes.
  conns_ += skins().changed.connect([this] {
    setBackground(Theme::current().background);
    update();
  });

  relayout();
}

// Out of line rather than defaulted in the header: the header would then need
// the complete definition of everything the members hold.
AppWindow::~AppWindow() = default;

// ----------------------------------------------------------------- layout ---
float AppWindow::borderWidth() const {
  // No outline on a framed window (the OS draws one) and none while maximised
  // (there is no edge to mark, and the line would eat a row of pixels against
  // the screen edge).
  if (!borderVisible_ || !isFrameless() || isMaximized()) return 0.0f;
  return 1.0f;
}

// Row #16 of section 11.4's table, the highest-graded one in it, and the
// function the comment above GeometryGuard in Widget.cpp has been holding up as
// the example all along: "onGeometryChanged runs APPLICATION code:
// AppWindow::relayout emits contentResized from inside one, and a slot is
// entitled to destroy widgets -- this one included."  That comment bought
// setGeometry's OWN frame a cursor.  This frame sits underneath it and had
// none, on a path that runs on every resize.
//
// THREE DOORS AND THREE CHECKPOINTS.  Each setGeometry below can reach
// application code, and what follows each one decides what its check contains:
//
//   * header_->setGeometry -- WindowHeader::onGeometryChanged relayouts its
//     trailing items, and any of those is an application widget.  Everything
//     after it is reached through `this`, content_ or fill_.
//   * content_->setGeometry -- a plain Widget today, which is NOT an invariant:
//     content() is public and one setLayout<> on it makes this a door for real.
//     After it, fill_ and `this`.
//   * fill_->setGeometry -- the application's own content widget, so this one
//     is application code by construction.  After it, only `this`.
//
// The emit is a door too and is NOT dangerous: contentResized belongs to this
// window, so contract D7 forbids a slot from destroying it, and update() on the
// next line touches nothing else.  Section 11.4 #17, and it is the one place in
// this file where D7 does real work.
//
// WHICH POINTERS GET CURSORS.  header_ does not, correcting the guard list in
// the table's #16 row: it is dereferenced at the door and never again, so a
// cursor on it would answer a question with no consumer.  content_ does -- it
// is non-null by this function's first line, so the plain constructor applies.
// fill_ takes the MayBeNull form, because a window with no content widget yet
// is an ordinary window and the frame lays it out under `if (fill_)`.
//
// WHY THE MEMBER RE-READS ARE THE LOAD-BEARING HALF HERE, unlike everywhere
// else in this family: since E15 this class OWNS a onDescendantDetached that
// nulls all three members, and the broadcast runs BEFORE anything is freed.  So
// the reachable failure is a NULL dereference rather than a dangling one -- the
// reproducer reddens on all three legs, not just the ASan one.  The cursors are
// defence in depth and are worth their five instructions anyway, because they
// are the half that does not depend on that hook being correct.
void AppWindow::relayout() {
  if (!header_ || !content_) return;

  const float b = borderWidth();
  const Rect r = localRect().deflated(b);
  const float hh = header_->isVisible() ? header_->height() : 0.0f;

  // Pre-door captures and cursors.  In front of the FIRST door, not in front of
  // the statement that uses them: a cursor registered after the object died
  // would read alive() forever, which is the mistake registered against
  // announceDetached in section 11.11.
  Widget* const ct0 = content_;
  Widget* const fl0 = fill_;
  const detail::DeathWatch self(this);
  const detail::DeathWatch ctw(content_);
  const detail::DeathWatch flw(fill_, detail::DeathWatch::MayBeNull{});

  header_->setGeometry({r.x(), r.y(), std::max(0.0f, r.width()), hh});
  // REM3-G3, immediately after door one.  `this` first because the two re-reads
  // dereference it; then the members, because a member that moved makes its own
  // cursor answer a question about the wrong object; then the cursors.
  if (!self.alive() || content_ != ct0 || fill_ != fl0 || !ctw.alive() ||
      (fl0 && !flw.alive())) {
    detail::frameDegraded();  // REM3-G8: once per frame, and the frame ends here
    return;
  }
  const Size cs{std::max(0.0f, r.width()), std::max(0.0f, r.height() - hh)};
  content_->setGeometry({r.x(), r.y() + hh, cs.width, cs.height});
  // Door two.  THREE checks, not five: content_ is never touched again after
  // this line, so it keeps neither its re-read nor its cursor -- the same
  // reasoning CP-S2 is written under in ScrollArea::relayout.
  if (!self.alive() || fill_ != fl0 || (fl0 && !flw.alive())) {
    detail::frameDegraded();
    return;
  }
  if (fill_) fill_->setGeometry({0.0f, 0.0f, cs.width, cs.height});
  // Door three.  Only `this` is left: the emit reads a member of this window
  // and update() walks its parent chain, and neither content_ nor fill_ is
  // named again.
  if (!self.alive()) {
    detail::frameDegraded();
    return;
  }

  contentResized.emit(cs);
  update();
}

void AppWindow::onGeometryChanged() { relayout(); }

// E15/REM3-RES-1.  Three assignments, and NOT ONE LINE ANYWHERE ELSE IN THIS
// FILE -- which is the strongest evidence this round produced that the approach
// is right rather than merely working.
//
// relayout() above opens with `if (!header_ || !content_) return;` and places
// the fill under `if (fill_)`.  Those three tests have been in the source since
// the class was written and NO LINE OF CODE COULD SATISFY THEM: nothing in the
// library could null a container's cached member, so they were dead code that
// looked like defensive programming.  The contract admitted this state years
// before the implementation could produce it.  All E15 does is produce it.
// setHeaderVisible, isHeaderVisible and hitZoneAt were already written the same
// way and are likewise untouched.
//
// The one thing that was NOT already written is setContent<T>(), which
// dereferenced content_ straight away; its null test is in AppWindow.hpp.
//
// REM3-G9 in full: three pointer comparisons and three stores.  No update(), no
// signal, no relayout() -- this runs inside announceDetached's walk over a
// half-detached tree, and relayout() alone would emit contentResized into
// application code from in there.  The repaint is not lost: whoever removed the
// widget went through Widget::takeChild, which repaints the vacated area before
// it announces anything.
//
// fill_ FIRST, because it is the innermost of the three and the walk announces
// outermost-first: removing content_ announces content_ and then fill_ (its
// child), so both arms fire on that one removal and the order they are written
// in makes no difference to the outcome.  Written inside-out anyway, so it
// reads in the same direction as the tree.
void AppWindow::onDescendantDetached(Widget* node) {
  if (node == fill_) fill_ = nullptr;
  if (node == content_) content_ = nullptr;
  if (node == header_) header_ = nullptr;
}

void AppWindow::setHeaderVisible(bool on) {
  if (!header_ || header_->isVisible() == on) return;
  header_->setVisible(on);
  relayout();
}

bool AppWindow::isHeaderVisible() const {
  return header_ && header_->isVisible();
}

void AppWindow::setBorderVisible(bool on) {
  if (borderVisible_ == on) return;
  borderVisible_ = on;
  relayout();
}

void AppWindow::setBorderColor(Color c) {
  borderColor_ = c;
  hasBorderColor_ = true;
  update();
}

// ------------------------------------------------------------------ paint ---
void AppWindow::onPaint(Painter& p, const Rect&) {
  const float b = borderWidth();
  if (b <= 0.0f) return;
  // Children are inset by exactly this much in relayout(), so the outline is
  // drawn first and still cannot be painted over -- no second paint pass and no
  // overlay hook needed.
  const Theme& t = Theme::current();
  p.strokeRect(localRect().deflated(b * 0.5f),
               hasBorderColor_ ? borderColor_ : t.panelBorder, b);
}

// -------------------------------------------------------------- hit zones ---
HitZone AppWindow::hitZoneAt(Point windowPos) {
  if (!header_ || !header_->isVisible()) return HitZone::Client;

  // An open drop-down is painted on top of everything, so it can overhang the
  // header.  If the cursor is on it, it is content -- reporting Caption there
  // would make the top rows of a menu undismissable and drag the window
  // instead.
  if (Widget* pop = popup(); pop && pop->isVisible() && pop->hitTest(windowPos)) {
    return HitZone::Client;
  }

  if (!header_->windowRect().contains(windowPos)) return HitZone::Client;

  // Anything the header hosts -- an avatar menu, a language switcher, a search
  // box -- must receive its own clicks.  Bare header background does not, and
  // that is what the operator grabs to move the window.
  Widget* hit = header_->hitTest(windowPos);
  if (hit && hit != header_) return HitZone::Client;

  const Rect hr = header_->windowRect();
  return header_->hitZone({windowPos.x - hr.x(), windowPos.y - hr.y()});
}

}  // namespace geeyoou
