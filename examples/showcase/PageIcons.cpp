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
// THE ONE TRAP THIS PAGE IS BUILT AROUND (ADR-R2-09) lives with the widget that
// obeys it -- see the header comment on IconGallery.hpp.  What belongs HERE is
// the half of the argument that is about the PAGE:
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

#include "IconGallery.hpp"
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
#include "i18n/I18n.hpp"

namespace showcase {

using namespace geeyoou;

namespace {

// --- the page's own constants ----------------------------------------------
//
// The GRID's constants (cell size, gaps, column counts, the empty-set floor)
// moved to IconGallery.hpp with the widget that uses them.  What is left here
// is what the page decides.
constexpr float kBandGap = 14.0f;  // between the page's panels
constexpr float kPreviewH = 104.0f;
constexpr float kPreviewSizes[] = {16.0f, 24.0f, 32.0f, 48.0f};

using showcase::icondetail::elide;

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
      p.drawText({r.center().x, r.center().y}, tr("点击下面任意图标"), t.fontBody,
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

    const std::string meta = (entry_.builtin ? std::string(tr("内置 · Icon.hpp"))
                                             : std::string(tr("自定义 · IconRegistry"))) +
                             tr("    分类：") + (entry_.category.empty() ? "—" : entry_.category);
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

GroupBox* panel(Widget* parent, BoxLayout* into, std::string title) {
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
                  tr("共 %d 个图标   ·   内置 %d（Icon.hpp）   ·   自定义 %d"
                  "（IconRegistry 运行时注册）   ·   %d 个分类").c_str(),
                  int(all.size()), builtins, customs, categories);
    stats->setText(buf);
  }
  page->addWidget(stats);

  // --------------------------------------------------------------- 搜索行 ---
  auto* row = content->add<Widget>();
  auto* rowBox = row->setLayout<BoxLayout>(BoxLayout::Orientation::Horizontal);
  rowBox->setSpacing(12.0f);

  auto* search = row->add<SearchField>();
  search->setPlaceholder(tr("按名字或分类搜索：chevron / window / pump / 仪表"));
  rowBox->addWidget(search);

  auto* matched = row->add<Label>();
  matched->addStyleClass("caption");
  matched->setPixelSize(12.0f);
  matched->setAlign(HAlign::Left, VAlign::Middle);
  rowBox->addWidget(matched);
  rowBox->addStretch();
  page->addWidget(row);

  // ----------------------------------------------------- 选中图标的多尺寸 ---
  GroupBox* gPreview = panel(content, page, tr("选中的图标 —— 一份定义，任意尺寸"));
  BoxLayout* previewCol = stack(gPreview, 8.0f);
  note(gPreview, previewCol,
       tr("同一个 Icon 句柄画四遍。矢量图标没有 @2x 资源，也没有 DPI 变体 —— "
       "16px 的行内标记和 48px 的表头标记来自同一份 24x24 授权网格。"));
  auto* preview = gPreview->add<IconPreview>();
  previewCol->addWidget(preview);

  // ------------------------------------------------------------- 图标画廊 ---
  GroupBox* gGallery = panel(content, page, tr("图标画廊 —— icons().all() 的全部内容"));
  BoxLayout* galleryCol = stack(gGallery, 10.0f);
  note(gGallery, galleryCol,
       tr("蓝底 + 右上角 ● 的是自定义图标（examples/showcase/PlantIcons.cpp 启动时注册），"
       "其余是内置。点任意一格看它的多尺寸预览与写法。"));
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
    status->setText(tr("已选择 ") + e.name + tr("（") +
                    (e.builtin ? tr("内置") : tr("自定义")) + " · " +
                    (e.category.empty() ? tr("未分类") : e.category) +
                    tr("）   用法：") + usageExpr(e) +
                    (e.builtin ? tr("   或 icons().find(\"") + e.name + "\")" : ""));
  };
  gallery->setEntries(all);

  const auto refreshCount = [gallery, matched] {
    char buf[64];
    std::snprintf(buf, sizeof(buf), tr("显示 %d / %d").c_str(), gallery->visibleCount(),
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
    // the scrollbar keeps describing the old page.
    //
    // L-4: THE HINT IS TAKEN FIRST, AND THEN THE HOST IS RESOLVED.  The order is
    // the whole fix.  content->sizeHint() is a door -- it runs this page's own
    // hints, the gallery's included, and anything an application hangs off them
    // -- so a ScrollArea* found in FRONT of it is a pointer captured before
    // application code ran and used after it returned, which is precisely the
    // shape section 11.4 #25 registers this line under.  Resolved AFTERWARDS,
    // the walk up content->parent() reads the tree as it is NOW; there is no
    // stale pointer left to follow.
    //
    // And the value handed over is no longer incidental.  It is the height the
    // page has AFTER the filter, measured after the re-flow, so the scrollbar
    // describes the new page even if relayout() were ever to stop recomputing
    // it -- what used to be "the call is what matters" is now an argument that
    // does not depend on the callee.
    const Size need = content->sizeHint().preferred;
    if (ScrollArea* sa = enclosingScrollArea(content)) sa->setContentSize(need);
  });

  if (!all.empty()) gallery->select(0);
  refreshCount();

  return content->sizeHint().preferred;
}

}  // namespace showcase
