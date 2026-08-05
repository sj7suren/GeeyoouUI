// 图标库 —— 全部图标一屏平铺。
//
// The registry is the subject of this page: `icons().all()` hands back every
// icon the process knows about -- the built-ins compiled into Icon.cpp AND
// whatever PlantIcons.cpp registered at startup -- and nothing on this page
// knows which is which except by asking IconEntry::builtin.  That is the point:
// a custom icon is an `Icon` value like any other, so a picker written against
// the registry keeps working when a plant adds its own P&ID symbols.
//
// ---------------------------------------------------------------------------
// THE ONE TRAP THIS PAGE IS BUILT AROUND (ADR-R2-09)
//
// A gallery's HEIGHT depends on its WIDTH: wider window, more columns, fewer
// rows.  That is what heightForWidth() solves, and R3 has not landed yet.  The
// tempting shortcut -- read geometry() in sizeHint() and divide -- is a
// circular definition: geometry is the OUTPUT of the previous arrange, so a
// window dragged narrower would shrink the gallery, measure the shrunk gallery,
// shrink it again, and never recover on the way back out.  Widget::sizeHint()
// says so in as many words, and Debug builds latch naturalSize_ once precisely
// to stop it.
//
// So the split here is:
//
//   * sizeHint()  -- PURE.  A function of (how many icons matched the filter)
//                    and constants only.  It reports the height the flow needs
//                    at kHintCols columns, which is a LOWER BOUND on the column
//                    count this widget can ever be given (see below), so the
//                    number it reports is never short -- at worst it leaves a
//                    row of slack on a very wide window.
//   * onPaint()   -- reads the REAL width and reflows.  Reading geometry while
//                    drawing is not circular; it is the only honest thing to do.
//
// Why kHintCols is a floor on the actual column count, structurally:
// BoxLayout gives a vertical item the full CROSS-axis width, clamped to that
// item's own min/max (BoxLayout::arrange).  Our max width is kUnbounded, so we
// get whatever the column has.  ScrollArea::relayout sizes laid-out content to
// max(viewport, content->sizeHint().preferred), and that preferred width is at
// least ours plus the frames around us.  Actual width >= preferred width =>
// actual columns >= kHintCols => actual rows <= hinted rows.  Over-report, never
// under-report.  The cost is bottom slack on a maximised 4K window; the cost of
// the other direction is a clipped last row, which is a defect.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "Pages.hpp"
#include "geeyoou/render/Icon.hpp"
#include "geeyoou/render/IconRegistry.hpp"
#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/BoxLayout.hpp"
#include "geeyoou/widget/GroupBox.hpp"
#include "geeyoou/widget/Label.hpp"
#include "geeyoou/widget/ScrollArea.hpp"
#include "geeyoou/widget/SearchBox.hpp"

namespace showcase {

using namespace geeyoou;

namespace {

// --- the flow's constants, all in logical pixels ---------------------------
constexpr float kBandGap = 14.0f;   // between the page's panels
constexpr float kCellMinW = 104.0f;  // a cell never gets narrower than this
constexpr float kCellH = 74.0f;      // icon (30) + gap + one line of name
constexpr float kCellGap = 10.0f;
constexpr float kRowGap = 10.0f;
constexpr float kPadX = 2.0f;        // the gallery's own left/right breathing room
constexpr float kGroupHeadH = 26.0f;
constexpr float kGroupGap = 16.0f;
constexpr float kEmptyH = 96.0f;     // "no match" placeholder
// The conservative column count sizeHint() measures at.  Deliberately modest:
// it is also the page's preferred WIDTH (kHintCols cells + gaps), and a page
// wider than the viewport is a horizontal scrollbar nobody asked for.
constexpr int kHintCols = 8;
constexpr int kMinCols = 3;
constexpr int kMaxCols = 64;
constexpr float kPreviewH = 104.0f;
constexpr float kPreviewSizes[] = {16.0f, 24.0f, 32.0f, 48.0f};

// ASCII-only, and that is on purpose: the byte-wise loop below is UTF-8 safe
// because every continuation byte is >= 0x80 and therefore left alone.
// std::tolower on a raw `char` is the classic way to get UB on a Chinese
// category name.
std::string asciiLower(std::string_view s) {
  std::string out(s);
  for (char& c : out) {
    if (c >= 'A' && c <= 'Z') c = char(c + ('a' - 'A'));
  }
  return out;
}

// "chevron-down" -> "ChevronDown".  The built-in names are kebab-case exactly
// so a name copied from Lucide resolves (IconRegistry.cpp), which means the
// enumerator spelling is recoverable from the name and this page does not need
// a second table that can drift out of sync with the first.
std::string enumName(std::string_view kebab) {
  std::string out;
  out.reserve(kebab.size());
  bool upper = true;
  for (char c : kebab) {
    if (c == '-') { upper = true; continue; }
    out.push_back(upper && c >= 'a' && c <= 'z' ? char(c - ('a' - 'A')) : c);
    upper = false;
  }
  return out;
}

// How you write this icon in code.  The whole reason the page shows it: a
// gallery you cannot copy a name out of is a poster, not a tool.
std::string usageExpr(const IconEntry& e) {
  if (e.builtin) return "Icon::" + enumName(e.name);
  return "icons().find(\"" + e.name + "\")";
}

// Trims to fit, one codepoint at a time.  Names are ASCII today; the
// continuation-byte walk costs nothing and means a Chinese icon name added
// tomorrow does not get cut in half.
std::string elide(std::string_view s, float maxW, float px) {
  if (measureText(s, px).width <= maxW) return std::string(s);
  std::string out(s);
  while (!out.empty() && measureText(out + "\xE2\x80\xA6", px).width > maxW) {
    std::size_t n = out.size();
    do { --n; } while (n > 0 && (static_cast<unsigned char>(out[n]) & 0xC0) == 0x80);
    out.resize(n);
  }
  return out + "\xE2\x80\xA6";
}

// The ScrollArea this page is hosted in (Shell.cpp builds one per page and
// hands us its content()).  Found by the STYLE type test rather than by
// dynamic_cast: it is a virtual call and a string compare, it is inheritance
// aware, and this library is built with no RTTI requirement.
ScrollArea* enclosingScrollArea(Widget* w) {
  for (Widget* p = w ? w->parent() : nullptr; p; p = p->parent()) {
    if (p->styleMatchesType("ScrollArea")) return static_cast<ScrollArea*>(p);
  }
  return nullptr;
}

// ============================================================== IconGallery ===
//
// One widget for the WHOLE grid -- not one per cell, and not one per category.
// 47 cells is 47 widgets, 47 size hints and 47 hit-test rectangles for content
// that never takes focus and never changes independently; drawing them is one
// loop over a vector.  Grouping headers are drawn in the same loop, so the
// slack sizeHint() over-reports is paid ONCE for the page rather than once per
// category.
class IconGallery : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(IconGallery, Widget)

  IconGallery() { setFocusPolicy(FocusPolicy::None); }

  std::function<void(const IconEntry&)> onPicked;

  void setEntries(std::vector<IconEntry> all) {
    all_ = std::move(all);
    rebuild();
  }
  void setFilter(std::string_view f) {
    filter_ = asciiLower(f);
    // A filter change moves the selection out of view rather than off the list:
    // the preview keeps showing whatever was picked, which is what you want
    // while typing a name you are about to copy.
    rebuild();
  }

  int total() const { return int(all_.size()); }
  int visibleCount() const { return shown_; }

  void select(int index) {
    if (index < 0 || index >= total()) return;
    selected_ = index;
    update();
    if (onPicked) onPicked(all_[std::size_t(index)]);
  }

  // PURE.  Reads all_, filter_ and the constants above -- never geometry().
  // See the header comment: this is the whole reason the page is shaped this
  // way.
  SizeHint sizeHint() const override {
    const float h = heightForColumns(kHintCols);
    SizeHint s;
    s.min = Size{widthForColumns(kMinCols), h};
    s.preferred = Size{widthForColumns(kHintCols), h};
    // Height pinned on all three: the flow is as tall as it is, and a layout
    // stretching it would only add trailing whitespace inside the group box.
    s.max = Size{kUnbounded, h};
    return s;
  }

 protected:
  void onPaint(Painter& p, const Rect&) override {
    const Theme& t = Theme::current();
    const Rect r = localRect();

    if (groups_.empty()) {
      p.drawText({r.center().x, kEmptyH * 0.5f},
                 "没有匹配的图标 —— 试试 chevron / window / pump / 仪表",
                 t.fontBody, t.textDim, HAlign::Center, VAlign::Middle);
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
    if (asciiLower(e.name).find(filter_) != std::string::npos) return true;
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
    hovered_ = -1;
    invalidateSizeHint();
    update();
  }

  // --- painting ------------------------------------------------------------
  void paintGroupHead(Painter& p, const Theme& t, const Rect& r, const Group& g) const {
    const float cy = r.center().y;
    p.drawText({r.x(), cy}, g.title, t.fontBody, t.text, HAlign::Left, VAlign::Middle);

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d 个", int(g.items.size()));
    const float nx = r.x() + measureText(g.title, t.fontBody).width + 8.0f;
    p.drawText({nx, cy + 1.0f}, buf, t.fontSmall, t.textDim, HAlign::Left, VAlign::Middle);

    // A rule out to the right edge, so the grouping reads even when a category
    // holds a single icon.
    const float lx = nx + measureText(buf, t.fontSmall).width + 10.0f;
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

    drawIcon(p, e.id, {r.center().x - 15.0f, r.y() + 12.0f, 30.0f, 30.0f},
             sel ? t.accent : t.text);

    // The corner mark, for the case where the tint alone is ambiguous -- a
    // light skin with a pale accent, or a colour-blind operator.
    if (!e.builtin) p.fillCircle({r.right() - 8.5f, r.y() + 8.5f}, 3.0f, t.accent);

    p.drawText({r.center().x, r.y() + 56.0f},
               elide(e.name, r.width() - 10.0f, t.fontSmall), t.fontSmall,
               sel ? t.text : t.textDim, HAlign::Center, VAlign::Middle);
  }

  std::vector<IconEntry> all_;
  std::vector<Group> groups_;
  std::string filter_;
  int shown_ = 0;
  int selected_ = -1;
  int hovered_ = -1;
};

// ============================================================== IconPreview ===
//
// The property a vector icon has and a bitmap does not: ONE definition, drawn
// at four sizes side by side, all from the same `Icon` handle.  Nothing here
// scales an image -- drawIcon re-runs the 24x24 authoring grid into each box.
class IconPreview : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(IconPreview, Widget)

  void setEntry(const IconEntry& e) {
    entry_ = e;
    has_ = true;
    update();
  }

  SizeHint sizeHint() const override {
    SizeHint s;
    s.min = Size{430.0f, kPreviewH};
    s.preferred = Size{700.0f, kPreviewH};
    s.max = Size{kUnbounded, kPreviewH};
    return s;
  }

 protected:
  void onPaint(Painter& p, const Rect&) override {
    const Theme& t = Theme::current();
    const Rect r = localRect();
    if (!has_) {
      p.drawText({r.center().x, r.center().y}, "点击下面任意图标", t.fontBody,
                 t.textDim, HAlign::Center, VAlign::Middle);
      return;
    }

    // --- the four sizes, sitting on one baseline -----------------------------
    const float base = 14.0f + 48.0f;  // bottom edge every box is aligned to
    float x = 6.0f;
    for (float s : kPreviewSizes) {
      const float slot = std::max(s, 36.0f);
      const Rect box{x + (slot - s) * 0.5f, base - s, s, s};
      p.fillRoundRect(box.deflated(-3.0f), 4.0f, t.panelBorder.withAlpha(60));
      drawIcon(p, entry_.id, box, t.text);

      char cap[16];
      std::snprintf(cap, sizeof(cap), "%d px", int(s));
      p.drawText({x + slot * 0.5f, base + 16.0f}, cap, t.fontSmall, t.textDim,
                 HAlign::Center, VAlign::Middle);
      x += slot + 16.0f;
    }

    const float sep = x + 2.0f;
    p.strokeLine({std::floor(sep) + 0.5f, 10.0f},
                 {std::floor(sep) + 0.5f, r.height() - 10.0f}, t.panelBorder, 1.0f);

    // --- identity + how to write it ------------------------------------------
    const float tx = sep + 18.0f;
    const float avail = std::max(60.0f, r.width() - tx - 8.0f);
    p.drawText({tx, 18.0f}, elide(entry_.name, avail, 17.0f), 17.0f, t.text,
               HAlign::Left, VAlign::Middle);

    const std::string meta = (entry_.builtin ? std::string("内置 · Icon.hpp")
                                             : std::string("自定义 · IconRegistry")) +
                             "    分类：" + (entry_.category.empty() ? "—" : entry_.category);
    p.drawText({tx, 40.0f}, elide(meta, avail, t.fontSmall), t.fontSmall,
               entry_.builtin ? t.textDim : t.accent, HAlign::Left, VAlign::Middle);

    const std::string expr = usageExpr(entry_);
    codeLine(p, t, tx, 63.0f, avail, "btn->setIcon(" + expr + ");");
    codeLine(p, t, tx, 85.0f, avail, "drawIcon(p, " + expr + ", box, t.text);");
  }

 private:
  // A snippet on its own field-coloured chip, so it reads as code rather than
  // as prose.  t.field is the same token every text input uses, which is what
  // keeps it legible under a light skin.
  static void codeLine(Painter& p, const Theme& t, float x, float cy, float avail,
                       const std::string& code) {
    const std::string s = elide(code, avail - 16.0f, 12.0f);
    const float w = measureText(s, 12.0f).width + 16.0f;
    p.fillRoundRect({x, cy - 10.0f, w, 20.0f}, 4.0f, t.field);
    p.drawText({x + 8.0f, cy}, s, 12.0f, t.text, HAlign::Left, VAlign::Middle);
  }

  IconEntry entry_;
  bool has_ = false;
};

// A search field with a width of its own.  Six lines instead of letting the
// row's stretch decide: a 500px search box over a 100px grid cell looks like a
// bug, and LineEdit's own preferred width is sized for a form field.
class SearchField : public SearchBox {
 public:
  GEEYOOU_STYLE_TYPE(SearchField, SearchBox)

  SizeHint sizeHint() const override {
    SizeHint s = SearchBox::sizeHint();
    s.min.width = 220.0f;
    s.preferred.width = 320.0f;
    s.max.width = 320.0f;
    return s;
  }
};

// --- small page-building helpers (same shape as PageLayout.cpp) -------------
BoxLayout* stack(Widget* host, float spacing) {
  auto* b = host->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
  b->setSpacing(spacing);
  return b;
}

Label* note(Widget* parent, BoxLayout* into, const std::string& s) {
  auto* l = parent->add<Label>();
  l->setText(s);
  // `.caption` rather than setColor(): the sheet resolves it to @textDim, so it
  // re-colours itself when the skin changes.  A literal here is the classic
  // reason a light skin comes out unreadable.
  l->addStyleClass("caption");
  l->setPixelSize(11.0f);
  l->setAlign(HAlign::Left, VAlign::Middle);
  into->addWidget(l);
  return l;
}

GroupBox* panel(Widget* parent, BoxLayout* into, const char* title) {
  auto* g = parent->add<GroupBox>();
  g->setTitle(title);
  into->addWidget(g);
  return g;
}

}  // namespace

Size buildIconsPage(Widget* content) {
  BoxLayout* page = stack(content, kBandGap);

  const std::vector<IconEntry> all = icons().all();
  int builtins = 0;
  for (const IconEntry& e : all) {
    if (e.builtin) ++builtins;
  }
  const int customs = int(all.size()) - builtins;
  const int categories = int(icons().categories().size());

  // ------------------------------------------------------------- 顶部统计 ---
  auto* stats = content->add<Label>();
  stats->setPixelSize(13.0f);
  stats->setAlign(HAlign::Left, VAlign::Middle);
  {
    char buf[192];
    std::snprintf(buf, sizeof(buf),
                  "共 %d 个图标   ·   内置 %d（Icon.hpp）   ·   自定义 %d"
                  "（IconRegistry 运行时注册）   ·   %d 个分类",
                  int(all.size()), builtins, customs, categories);
    stats->setText(buf);
  }
  page->addWidget(stats);

  // --------------------------------------------------------------- 搜索行 ---
  auto* row = content->add<Widget>();
  auto* rowBox = row->setLayout<BoxLayout>(BoxLayout::Orientation::Horizontal);
  rowBox->setSpacing(12.0f);

  auto* search = row->add<SearchField>();
  search->setPlaceholder("按名字或分类搜索：chevron / window / pump / 仪表");
  rowBox->addWidget(search);

  auto* matched = row->add<Label>();
  matched->addStyleClass("caption");
  matched->setPixelSize(12.0f);
  matched->setAlign(HAlign::Left, VAlign::Middle);
  rowBox->addWidget(matched);
  rowBox->addStretch();
  page->addWidget(row);

  // ----------------------------------------------------- 选中图标的多尺寸 ---
  GroupBox* gPreview = panel(content, page, "选中的图标 —— 一份定义，任意尺寸");
  BoxLayout* previewCol = stack(gPreview, 8.0f);
  note(gPreview, previewCol,
       "同一个 Icon 句柄画四遍。矢量图标没有 @2x 资源，也没有 DPI 变体 —— "
       "16px 的行内标记和 48px 的表头标记来自同一份 24x24 授权网格。");
  auto* preview = gPreview->add<IconPreview>();
  previewCol->addWidget(preview);

  // ------------------------------------------------------------- 图标画廊 ---
  GroupBox* gGallery = panel(content, page, "图标画廊 —— icons().all() 的全部内容");
  BoxLayout* galleryCol = stack(gGallery, 10.0f);
  note(gGallery, galleryCol,
       "蓝底 + 右上角 ● 的是自定义图标（examples/showcase/PlantIcons.cpp 启动时注册），"
       "其余是内置。点任意一格看它的多尺寸预览与写法。");
  auto* gallery = gGallery->add<IconGallery>();
  galleryCol->addWidget(gallery);

  // --------------------------------------------------------------- 状态栏 ---
  auto* status = content->add<Label>();
  status->addStyleClass("caption");
  status->setPixelSize(12.0f);
  status->setAlign(HAlign::Left, VAlign::Middle);
  page->addWidget(status);

  // ------------------------------------------------------------------ 接线 ---
  gallery->onPicked = [preview, status](const IconEntry& e) {
    preview->setEntry(e);
    status->setText("已选择 " + e.name + "（" +
                    (e.builtin ? "内置" : "自定义") + " · " +
                    (e.category.empty() ? "未分类" : e.category) +
                    "）   用法：" + usageExpr(e) +
                    (e.builtin ? "   或 icons().find(\"" + e.name + "\")" : ""));
  };
  gallery->setEntries(all);

  const auto refreshCount = [gallery, matched] {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "显示 %d / %d", gallery->visibleCount(),
                  gallery->total());
    matched->setText(buf);
  };

  search->textChanged.connect([gallery, content, refreshCount](const std::string& s) {
    gallery->setFilter(s);
    refreshCount();
    // Filtering changes how TALL this page is, and a ScrollArea only re-measures
    // laid-out content when its own geometry moves or when it is told the
    // content size did (ScrollArea::relayout is reached from onGeometryChanged
    // and from setContentSize).  Without this the grid re-flows correctly and
    // the scrollbar keeps describing the old page.  The value passed is
    // recomputed by relayout() anyway -- what matters is the call.
    if (ScrollArea* sa = enclosingScrollArea(content)) {
      sa->setContentSize(content->sizeHint().preferred);
    }
  });

  if (!all.empty()) gallery->select(0);
  refreshCount();

  return content->sizeHint().preferred;
}

}  // namespace showcase
