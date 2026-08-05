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
    header_->setMaximized(on);
    // The outline is dropped while maximised, so the content area grows by the
    // border width -- that has to be re-laid out, not just repainted.
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

void AppWindow::relayout() {
  if (!header_ || !content_) return;

  const float b = borderWidth();
  const Rect r = localRect().deflated(b);
  const float hh = header_->isVisible() ? header_->height() : 0.0f;

  header_->setGeometry({r.x(), r.y(), std::max(0.0f, r.width()), hh});
  const Size cs{std::max(0.0f, r.width()), std::max(0.0f, r.height() - hh)};
  content_->setGeometry({r.x(), r.y() + hh, cs.width, cs.height});
  if (fill_) fill_->setGeometry({0.0f, 0.0f, cs.width, cs.height});

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
