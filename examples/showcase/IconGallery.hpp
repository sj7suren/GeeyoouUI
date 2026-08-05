#pragma once
//
// The icon page's grid, lifted out of PageIcons.cpp into a header of its own.
//
// WHY IT MOVED, because "so a test can see it" is only half the reason and the
// weaker half.  IconGallery was a class in an anonymous namespace inside a page
// builder, which made three separate things impossible at once:
//
//   * no case could name the type, so every property of the widget -- what its
//     sizeHint() answers for an EMPTY result set, whether a fresh data set
//     clears the selection, what the pick callback hands out -- could only be
//     reached through a whole page, a ScrollArea and a synthesised mouse event,
//     which is a lot of scaffolding standing between an assertion and the thing
//     it asserts;
//   * the page builder was the only possible owner, so a second screen wanting
//     an icon picker would have copied it;
//   * the state matrix below (no match / one / all / a name longer than a cell)
//     is a property of the GRID, not of the page, and a case that had to build
//     the page to reach it would be asserting the page's shape by accident.
//
// Header-only on purpose: it is example code, it has one member function worth
// more than a screen (the flow), and a second translation unit in examples/
// would have to be added to two CMake targets by hand.
//
// ---------------------------------------------------------------------------
// THE TRAP THIS WIDGET IS BUILT AROUND (ADR-R2-09) -- kept with the code that
// obeys it rather than left behind in the page.
//
// A gallery's HEIGHT depends on its WIDTH: wider window, more columns, fewer
// rows.  That is what heightForWidth() solves, and R3 has not landed yet.  The
// tempting shortcut -- read geometry() in sizeHint() and divide -- is a circular
// definition: geometry is the OUTPUT of the previous arrange, so a window
// dragged narrower would shrink the gallery, measure the shrunk gallery, shrink
// it again, and never recover on the way back out.
//
// So the split is:
//
//   * sizeHint()  -- PURE.  A function of (how many icons matched the filter)
//                    and constants only.  It reports the height the flow needs
//                    at kHintCols columns, which is a LOWER BOUND on the column
//                    count this widget can ever be given, so the number it
//                    reports is never short -- at worst it leaves a row of slack
//                    on a very wide window.
//   * onPaint()   -- reads the REAL width and reflows.  Reading geometry while
//                    drawing is not circular; it is the only honest thing to do.
//
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "geeyoou/render/Icon.hpp"
#include "geeyoou/render/IconRegistry.hpp"
#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/Widget.hpp"

namespace showcase {

using geeyoou::Color;
using geeyoou::FocusPolicy;
using geeyoou::IconEntry;
using geeyoou::MouseAction;
using geeyoou::MouseButton;
using geeyoou::MouseEvent;
using geeyoou::Painter;
using geeyoou::Point;
using geeyoou::Rect;
using geeyoou::Size;
using geeyoou::SizeHint;
using geeyoou::Theme;
using geeyoou::Widget;

// --- the flow's constants, all in logical pixels ----------------------------
//
// `inline constexpr` rather than plain `constexpr`: a namespace-scope constexpr
// is internally linked, and an inline member function below that odr-uses one
// would then mean a different object in every translation unit.
inline constexpr float kCellMinW = 104.0f;  // a cell never gets narrower than this
inline constexpr float kCellH = 74.0f;      // icon (30) + gap + one line of name
inline constexpr float kCellGap = 10.0f;
inline constexpr float kRowGap = 10.0f;
inline constexpr float kPadX = 2.0f;  // the gallery's own left/right breathing room
inline constexpr float kGroupHeadH = 26.0f;
inline constexpr float kGroupGap = 16.0f;
// The "no match" placeholder's height, and the FLOOR the empty result set is
// measured at.  It is not decoration: a gallery that answered zero height for
// an empty filter would be given zero height by the column that holds it, the
// group box around it would close up to its own frame, and the message telling
// the operator that nothing matched would be drawn in a rectangle nobody can
// see.  Asserted in tests/widget/test_icon_gallery.cpp.
inline constexpr float kEmptyH = 96.0f;
// The conservative column count sizeHint() measures at.  Deliberately modest:
// it is also the page's preferred WIDTH (kHintCols cells + gaps), and a page
// wider than the viewport is a horizontal scrollbar nobody asked for.
inline constexpr int kHintCols = 8;
inline constexpr int kMinCols = 3;
inline constexpr int kMaxCols = 64;

namespace icondetail {

// ASCII-only, and that is on purpose: the byte-wise loop below is UTF-8 safe
// because every continuation byte is >= 0x80 and therefore left alone.
// std::tolower on a raw `char` is the classic way to get UB on a Chinese
// category name.
inline std::string asciiLower(std::string_view s) {
  std::string out(s);
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') c = char(c + ('a' - 'A'));
  }
  return out;
}

// Trims to fit, one codepoint at a time.  Names are ASCII today; the
// continuation-byte walk costs nothing and means a Chinese icon name added
// tomorrow does not get cut in half.
//
// TERMINATION, because this is the function a 300-character icon name lands in:
// every iteration removes at least one byte and the loop also stops on an empty
// string, so the worst case is "…" alone -- never a hang, and never a negative
// width handed to the shaper.
inline std::string elide(std::string_view s, float maxW, float px) {
  if (geeyoou::measureText(s, px).width <= maxW) return std::string(s);
  std::string out(s);
  while (!out.empty() &&
         geeyoou::measureText(out + "\xE2\x80\xA6", px).width > maxW) {
    std::size_t n = out.size();
    do { --n; } while (n > 0 && (static_cast<unsigned char>(out[n]) & 0xC0) == 0x80);
    out.resize(n);
  }
  return out + "\xE2\x80\xA6";
}

}  // namespace icondetail

// ============================================================== IconGallery ===
//
// One widget for the WHOLE grid -- not one per cell, and not one per category.
// 47 cells is 47 widgets, 47 size hints and 47 hit-test rectangles for content
// that never takes focus and never changes independently; drawing them is one
// loop over a vector.  Grouping headers are drawn in the same loop, so the
// slack sizeHint() over-reports is paid ONCE for the page rather than once per
// category.
//
// ⚠️ That decision is also why a page built around this widget has ELEVEN
// widgets on it and not fifty: the count is a consequence of the design, not a
// measure of how much the page does.  See the note on checkPage() in
// tests/widget/test_showcase_pages.cpp.
class IconGallery : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(IconGallery, Widget)

  IconGallery() { setFocusPolicy(FocusPolicy::None); }

  // BY VALUE, and that is the fix for L-2 rather than a style preference.
  //
  // This callback is invoked from select(), whose only source for the entry is
  // all_.  Handing out `const IconEntry&` meant handing out a reference INTO
  // that vector, and the first thing a picker's callback wants to do is show
  // the icon somewhere -- which is application code, which is entitled to call
  // setEntries() (a filter chip, a "reload plant icons" button), which is
  // `all_ = std::move(all)` and frees the buffer the reference points at.  The
  // callback then reads its own argument out of a freed block.
  //
  // The guarantee is in the TYPE, not in a comment at the call site: with
  // `void(IconEntry)` the copy happens when std::function's operator() builds
  // its parameter, before any of the target's body runs, so no future call site
  // can reintroduce the alias by forgetting to copy first.  An IconEntry is two
  // strings, an enum and a bool -- one copy per click, on the click path.
  //
  // tests/widget/test_icon_gallery.cpp holds the case that goes red (under
  // ASan) if this is ever turned back into a reference.
  std::function<void(IconEntry)> onPicked;

  // A NEW DATA SET, which is not the same event as a new filter -- see
  // setFilter below, and note that the two differ in exactly one line.
  void setEntries(std::vector<IconEntry> all) {
    all_ = std::move(all);
    // L-3.  The selection is an index INTO all_, so a new all_ invalidates it:
    // index 30 of the built-in set and index 30 of a two-icon plant set are not
    // the same icon, and nothing anywhere reconciles them.  Left stale, the
    // grid highlighted whichever cell happened to land on that index -- a wrong
    // cell, silently, with the preview still showing the icon from before.
    //
    // Cleared HERE and not in rebuild(): rebuild() is also what a filter change
    // runs, and a filter must not drop the selection (see setFilter).  The two
    // events are different and the one line that differs is this one.
    selected_ = -1;
    rebuild();
  }

  void setFilter(std::string_view f) {
    filter_ = icondetail::asciiLower(f);
    // A filter change moves the selection out of view rather than off the list:
    // the preview keeps showing whatever was picked, which is what you want
    // while typing a name you are about to copy.  all_ is untouched, so the
    // index still means what it meant.
    rebuild();
  }

  int total() const { return int(all_.size()); }
  int visibleCount() const { return shown_; }

  // -1 when nothing is picked.  This is the value paintCell() compares each
  // cell index against, so "what is highlighted" and "what this returns" are
  // the same fact by construction rather than by agreement.
  int selectedIndex() const { return selected_; }

  // BY VALUE for the same reason onPicked is: an accessor that handed back a
  // reference into all_ would put the L-2 shape back, one call site further out.
  IconEntry entryAt(int index) const {
    if (index < 0 || index >= total()) return IconEntry{};
    return all_[std::size_t(index)];
  }

  void select(int index) {
    if (index < 0 || index >= total()) return;
    selected_ = index;
    update();
    // The copy is made by std::function's parameter, before the target runs;
    // see onPicked.  Nothing is read out of `this` after the callback returns,
    // so a callback that destroys this widget is survivable here -- which is
    // more than the page around it can say (section 11.4 #25).
    if (onPicked) onPicked(all_[std::size_t(index)]);
  }

  // PURE.  Reads all_, filter_ and the constants above -- never geometry().
  SizeHint sizeHint() const override {
    const float h = heightForColumns(kHintCols);
    SizeHint s;
    s.min = Size{widthForColumns(kMinCols), h};
    s.preferred = Size{widthForColumns(kHintCols), h};
    // Height pinned on all three: the flow is as tall as it is, and a layout
    // stretching it would only add trailing whitespace inside the group box.
    s.max = Size{geeyoou::kUnbounded, h};
    return s;
  }

 protected:
  void onPaint(Painter& p, const Rect&) override {
    const Theme& t = Theme::current();
    const Rect r = localRect();

    if (groups_.empty()) {
      p.drawText({r.center().x, kEmptyH * 0.5f},
                 "没有匹配的图标 —— 试试 chevron / window / pump / 仪表",
                 t.fontBody, t.textDim, geeyoou::HAlign::Center,
                 geeyoou::VAlign::Middle);
      return;
    }

    walk(r.width(),
         [&](const Rect& head, const Group& g) { paintGroupHead(p, t, head, g); },
         [&](const Rect& cell, int index) { paintCell(p, t, cell, index); });
  }

  void onMouse(const MouseEvent& e) override {
    switch (e.action) {
      case MouseAction::Leave:
        if (hovered_ != -1) { hovered_ = -1; update(); }
        e.accept();
        break;
      case MouseAction::Enter:
      case MouseAction::Move: {
        const int hit = cellAt(e.pos);
        if (hit != hovered_) { hovered_ = hit; update(); }
        e.accept();
        break;
      }
      case MouseAction::Press: {
        if (e.button != MouseButton::Left) break;
        const int hit = cellAt(e.pos);
        if (hit >= 0) select(hit);
        e.accept();
        break;
      }
      default:
        break;
    }
  }

 private:
  struct Group {
    std::string title;
    std::vector<int> items;  // indices into all_
  };

  // --- the flow ------------------------------------------------------------
  //
  // ONE walk, two callbacks, used by both onPaint and hit-testing.  Writing the
  // arithmetic twice is how a gallery ends up drawing a cell one place and
  // clicking it another.
  //
  // Columns come from the width the caller passes in; cells then SHARE the
  // leftover evenly instead of leaving a ragged right edge, so the grid is
  // flush on both sides at every window width.  cellW is therefore always in
  // [kCellMinW, kCellMinW + (kCellMinW + kCellGap) / cols).
  template <class GroupFn, class CellFn>
  void walk(float width, GroupFn&& gf, CellFn&& cf) const {
    const int cols = columnsFor(width);
    const float usable = std::max(kCellMinW, width - 2.0f * kPadX);
    const float cellW = (usable - float(cols - 1) * kCellGap) / float(cols);

    float y = 0.0f;
    for (const Group& g : groups_) {
      gf(Rect{kPadX, y, usable, kGroupHeadH}, g);
      y += kGroupHeadH;
      const int n = int(g.items.size());
      for (int i = 0; i < n; ++i) {
        const int row = i / cols;
        const int col = i % cols;
        cf(Rect{kPadX + float(col) * (cellW + kCellGap),
                y + float(row) * (kCellH + kRowGap), cellW, kCellH},
           g.items[std::size_t(i)]);
      }
      const int rows = (n + cols - 1) / cols;
      if (rows > 0) y += float(rows) * (kCellH + kRowGap) - kRowGap;
      y += kGroupGap;
    }
  }

  int columnsFor(float width) const {
    const float usable = width - 2.0f * kPadX;
    const int c = int(std::floor((usable + kCellGap) / (kCellMinW + kCellGap)));
    return std::clamp(c, kMinCols, kMaxCols);
  }

  static float widthForColumns(int cols) {
    return float(cols) * kCellMinW + float(cols - 1) * kCellGap + 2.0f * kPadX;
  }

  // The same vertical accumulation as walk(), minus everything that needs a
  // width.  Kept separate because sizeHint() may not know a width at all.
  //
  // The empty branch returns kEmptyH rather than 0: see the constant.
  float heightForColumns(int cols) const {
    if (groups_.empty()) return kEmptyH;
    float y = 0.0f;
    for (const Group& g : groups_) {
      y += kGroupHeadH;
      const int rows = (int(g.items.size()) + cols - 1) / cols;
      if (rows > 0) y += float(rows) * (kCellH + kRowGap) - kRowGap;
      y += kGroupGap;
    }
    return y - kGroupGap;
  }

  int cellAt(Point pos) const {
    int hit = -1;
    walk(localRect().width(), [](const Rect&, const Group&) {},
         [&](const Rect& cell, int index) {
           if (cell.contains(pos)) hit = index;
         });
    return hit;
  }

  // --- model ---------------------------------------------------------------
  bool matches(const IconEntry& e) const {
    if (filter_.empty()) return true;
    if (icondetail::asciiLower(e.name).find(filter_) != std::string::npos) return true;
    // Categories are Chinese, so they are matched byte-wise against what the
    // IME produced -- lowering would be meaningless and asciiLower leaves the
    // multibyte sequences untouched anyway.
    return e.category.find(filter_) != std::string::npos;
  }

  void rebuild() {
    groups_.clear();
    shown_ = 0;
    for (int i = 0; i < int(all_.size()); ++i) {
      const IconEntry& e = all_[std::size_t(i)];
      if (!matches(e)) continue;
      const std::string cat = e.category.empty() ? std::string("未分类") : e.category;
      Group* g = nullptr;
      for (Group& x : groups_) {
        if (x.title == cat) { g = &x; break; }
      }
      if (!g) {
        groups_.push_back(Group{cat, {}});
        g = &groups_.back();
      }
      g->items.push_back(i);
      ++shown_;
    }
    // Hover is a pointer position, and the cell that was under the pointer has
    // just moved or gone.  The SELECTION is not touched here on purpose -- see
    // setEntries and setFilter, which is where that decision belongs.
    hovered_ = -1;
    invalidateSizeHint();
    update();
  }

  // --- painting ------------------------------------------------------------
  void paintGroupHead(Painter& p, const Theme& t, const Rect& r, const Group& g) const {
    const float cy = r.center().y;
    p.drawText({r.x(), cy}, g.title, t.fontBody, t.text, geeyoou::HAlign::Left,
               geeyoou::VAlign::Middle);

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d 个", int(g.items.size()));
    const float nx = r.x() + geeyoou::measureText(g.title, t.fontBody).width + 8.0f;
    p.drawText({nx, cy + 1.0f}, buf, t.fontSmall, t.textDim, geeyoou::HAlign::Left,
               geeyoou::VAlign::Middle);

    // A rule out to the right edge, so the grouping reads even when a category
    // holds a single icon.
    const float lx = nx + geeyoou::measureText(buf, t.fontSmall).width + 10.0f;
    if (lx < r.right()) {
      p.strokeLine({lx, std::floor(cy) + 0.5f}, {r.right(), std::floor(cy) + 0.5f},
                   t.panelBorder, 1.0f);
    }
  }

  void paintCell(Painter& p, const Theme& t, const Rect& r, int index) const {
    const IconEntry& e = all_[std::size_t(index)];
    const bool sel = (index == selected_);
    const bool hot = (index == hovered_);

    // Built-in vs custom is carried by the SURFACE, not by the icon colour: an
    // icon gallery whose artwork changes colour by provenance is judging the
    // artwork.  Every value below comes from the active Theme, so the
    // distinction survives a skin change instead of disappearing into a
    // hard-coded grey.
    Color bg = e.builtin ? t.panelBorder.withAlpha(46) : t.accent.withAlpha(34);
    if (hot) bg = e.builtin ? t.panelBorder.withAlpha(104) : t.accent.withAlpha(70);
    if (sel) bg = t.accent.withAlpha(78);
    p.fillRoundRect(r, t.radius, bg);

    if (sel) {
      p.strokeRoundRect(r.deflated(0.75f), t.radius, t.accent, 1.5f);
    } else if (!e.builtin) {
      p.strokeRoundRect(r.deflated(0.5f), t.radius, t.accent.withAlpha(120), 1.0f);
    }

    geeyoou::drawIcon(p, e.id, {r.center().x - 15.0f, r.y() + 12.0f, 30.0f, 30.0f},
                      sel ? t.accent : t.text);

    // The corner mark, for the case where the tint alone is ambiguous -- a
    // light skin with a pale accent, or a colour-blind operator.
    if (!e.builtin) p.fillCircle({r.right() - 8.5f, r.y() + 8.5f}, 3.0f, t.accent);

    p.drawText({r.center().x, r.y() + 56.0f},
               icondetail::elide(e.name, r.width() - 10.0f, t.fontSmall), t.fontSmall,
               sel ? t.text : t.textDim, geeyoou::HAlign::Center,
               geeyoou::VAlign::Middle);
  }

  std::vector<IconEntry> all_;
  std::vector<Group> groups_;
  std::string filter_;
  int shown_ = 0;
  int selected_ = -1;
  int hovered_ = -1;
};

}  // namespace showcase
