// 布局引擎 —— 拖窗口边缘，看这一页怎么变。
//
// This page exists to be RESIZED.  Every panel on it answers a different
// question about what the layout engine does when the window changes size, and
// every swatch prints its own live width, so the effect is a number you can
// read rather than a shape you have to squint at.
//
// The last panel is the control group: identical content placed at absolute
// coordinates.  It does not move.  Putting the two side by side is the whole
// argument for R2 in one screen.
#include <cstdio>
#include <functional>
#include <string>

#include "Pages.hpp"
#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/BoxLayout.hpp"
#include "geeyoou/widget/GridLayout.hpp"
#include "geeyoou/widget/GroupBox.hpp"
#include "geeyoou/widget/Label.hpp"
#include "geeyoou/widget/LineEdit.hpp"
#include "geeyoou/widget/PushButton.hpp"
#include "geeyoou/widget/SpinBox.hpp"

namespace showcase {

using namespace geeyoou;

namespace {
constexpr float kBandGap = 14.0f;
constexpr float kItemGap = 10.0f;

// A coloured block that reports its own width.
//
// Printing the live number is the point: "the middle one grew twice as fast"
// is an assertion, and "142 / 284" is evidence.  Its size hint is settable, so
// one class covers every case this page wants to show.
class Swatch : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(Swatch, Widget)

  void set(std::string caption, Color fill, SizeHint hint) {
    caption_ = std::move(caption);
    fill_ = fill;
    hint_ = hint;
    invalidateSizeHint();
    update();
  }

  SizeHint sizeHint() const override { return hint_; }

 protected:
  void onPaint(Painter& p, const Rect&) override {
    const Theme& t = Theme::current();
    const Rect r = localRect();
    p.fillRoundRect(r, t.radius, fill_);

    char buf[32];
    std::snprintf(buf, sizeof(buf), "%d", int(r.width() + 0.5f));
    p.drawText({r.center().x, r.center().y - 8.0f}, caption_, t.fontSmall,
               t.onFilled, HAlign::Center, VAlign::Middle);
    p.drawText({r.center().x, r.center().y + 9.0f}, buf, 15.0f, t.onFilled,
               HAlign::Center, VAlign::Middle);
  }

 private:
  std::string caption_;
  Color fill_;
  SizeHint hint_;
};

// Fires whenever the page is re-laid out, which is what lets the readout strip
// at the top show live numbers instead of the ones from construction time.
//
// It has to sit INSIDE the page's layout to work at all: onGeometryChanged
// fires when THIS widget's rectangle changes, and a widget the layout never
// touches never gets one.  So it takes a full-width, zero-height slot -- it
// still gets a new width on every resize, and draws nothing.
class ResizeProbe : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(ResizeProbe, Widget)
  std::function<void()> onResized;

  SizeHint sizeHint() const override {
    return SizeHint{Size{0.0f, 0.0f}, Size{0.0f, 0.0f}, Size{kUnbounded, 0.0f}};
  }

 protected:
  void onGeometryChanged() override {
    if (onResized) onResized();
  }
};

Widget* band(Widget* parent, BoxLayout* into, std::uint16_t stretch = 0) {
  Widget* w = parent->add<Widget>();
  into->addWidget(w, stretch);
  return w;
}

BoxLayout* stack(Widget* host, float spacing) {
  auto* b = host->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
  b->setSpacing(spacing);
  return b;
}

BoxLayout* line(Widget* host, float spacing) {
  auto* b = host->setLayout<BoxLayout>(BoxLayout::Orientation::Horizontal);
  b->setSpacing(spacing);
  return b;
}

Label* note(Widget* parent, BoxLayout* into, const char* s) {
  auto* l = parent->add<Label>();
  l->setText(s);
  l->addStyleClass("caption");
  l->setPixelSize(11.0f);
  l->setAlign(HAlign::Left, VAlign::Middle);
  into->addWidget(l);
  return l;
}

// GroupBox reserves its border and title through layoutRect(); the margins here
// are the author's own breathing room on top of that.
GroupBox* panel(Widget* parent, BoxLayout* into, const char* title,
                std::uint16_t stretch = 0) {
  auto* g = parent->add<GroupBox>();
  g->setTitle(title);
  into->addWidget(g, stretch);
  return g;
}

SizeHint fixed(float w, float h) {
  return SizeHint{Size{w, h}, Size{w, h}, Size{w, h}};
}
SizeHint elastic(float minW, float prefW, float maxW, float h) {
  return SizeHint{Size{minW, h}, Size{prefW, h}, Size{maxW, h}};
}
}  // namespace

Size buildLayoutPage(Widget* content) {
  const Theme& th = Theme::current();
  BoxLayout* page = stack(content, kBandGap);

  // ------------------------------------------------------------- 实时读数 ---
  auto* readout = content->add<Label>();
  readout->setPixelSize(12.0f);
  readout->setAlign(HAlign::Left, VAlign::Middle);
  page->addWidget(readout);

  auto* probe = content->add<ResizeProbe>();
  page->addWidget(probe);  // in the layout, or it never sees a resize

  // ------------------------------------------- 1. BoxLayout 的 stretch 权重 ---
  GroupBox* gStretch = panel(content, page, "① BoxLayout —— 余量按 stretch 权重分");
  BoxLayout* stretchCol = stack(gStretch, kItemGap);
  note(gStretch, stretchCol,
       "四块的 preferred 都是 120。窗口变宽时，多出来的空间按 0 : 1 : 2 : 3 分 —— "
       "第一块永远不动，最后一块长得最快。");

  Widget* stretchRow = band(gStretch, stretchCol);
  BoxLayout* sr = line(stretchRow, kItemGap);
  const struct { const char* cap; std::uint16_t w; Color c; } kStretch[] = {
      {"stretch 0", 0, th.textDisabled},
      {"stretch 1", 1, th.accent},
      {"stretch 2", 2, th.ok},
      {"stretch 3", 3, th.warn},
  };
  for (const auto& s : kStretch) {
    auto* sw = stretchRow->add<Swatch>();
    sw->set(s.cap, s.c, elastic(60.0f, 120.0f, kUnbounded, 56.0f));
    sr->addWidget(sw, s.w);
  }

  // ------------------------------------------ 2. SizeHint 的 min/pref/max ---
  GroupBox* gHint = panel(content, page, "② SizeHint —— 缩小时谁先让，放大时谁先停");
  BoxLayout* hintCol = stack(gHint, kItemGap);
  note(gHint, hintCol,
       "把窗口拖窄：可伸缩的先让到 min，固定的一步不退。再拖宽："
       "有上限的到 240 就不长了，把余量让给旁边。");

  Widget* hintRow = band(gHint, hintCol);
  BoxLayout* hr = line(hintRow, kItemGap);
  auto* swFixed = hintRow->add<Swatch>();
  swFixed->set("固定 140", th.textDisabled, fixed(140.0f, 56.0f));
  hr->addWidget(swFixed, 0);

  auto* swElastic = hintRow->add<Swatch>();
  swElastic->set("min 80 / 无上限", th.accent,
                 elastic(80.0f, 160.0f, kUnbounded, 56.0f));
  hr->addWidget(swElastic, 1);

  auto* swCapped = hintRow->add<Swatch>();
  swCapped->set("上限 240", th.alarm, elastic(80.0f, 160.0f, 240.0f, 56.0f));
  hr->addWidget(swCapped, 1);

  // ------------------------------------------------- 3. GridLayout 跨列 ---
  GroupBox* gGrid = panel(content, page, "③ GridLayout —— 跨列与列对齐");
  auto* grid = gGrid->setLayout<GridLayout>();
  // Just breathing room -- GroupBox already reserves its border and title in
  // layoutRect(), so adding the title height here would count it twice.
  grid->setMargins({0.0f, 0.0f, 0.0f, 0.0f});
  grid->setSpacing(kItemGap);

  auto* spanCell = gGrid->add<Swatch>();
  spanCell->set("跨 3 列", th.accent.withAlpha(120),
                elastic(200.0f, 420.0f, kUnbounded, 34.0f));
  grid->addWidget(spanCell, 0, 0, 1, 3);

  const char* kCols[] = {"第 1 列", "第 2 列", "第 3 列"};
  for (int c = 0; c < 3; ++c) {
    auto* cell = gGrid->add<Swatch>();
    cell->set(kCols[c], th.panelBorder, elastic(70.0f, 120.0f, kUnbounded, 46.0f));
    grid->addWidget(cell, 1, c);
  }
  grid->setColumnStretch(0, 1);
  grid->setColumnStretch(1, 2);
  grid->setColumnStretch(2, 1);

  // ---------------------------------------------------- 4. addRow 表单 ---
  GroupBox* gForm = panel(content, page, "④ GridLayout::addRow —— 参数表单");
  auto* form = gForm->setLayout<GridLayout>();
  form->setMargins({0.0f, 0.0f, 0.0f, 0.0f});
  form->setSpacing(kItemGap);

  const char* kRows[] = {"设备位号", "工程描述", "Modbus 地址"};
  const char* kVals[] = {"TI-101", "反应釜内温度", "40001"};
  for (int i = 0; i < 3; ++i) {
    auto* lab = gForm->add<Label>();
    lab->setText(kRows[i]);
    lab->addStyleClass("caption");
    lab->setPixelSize(11.0f);
    lab->setAlign(HAlign::Left, VAlign::Middle);

    auto* ed = gForm->add<LineEdit>();
    ed->setText(kVals[i]);
    form->addRow(lab, ed);
  }
  // 标签列不跟着长，字段列吃掉全部余量 —— 这正是 addRow 默认给的形状。

  // -------------------------------------------------- 5. 对照组：绝对坐标 ---
  GroupBox* gAbs = panel(content, page, "⑤ 对照组 —— 绝对坐标（不会响应缩放）");
  BoxLayout* absCol = stack(gAbs, kItemGap);
  note(gAbs, absCol,
       "下面三块用 setGeometry 摆死。窗口怎么拉，它们纹丝不动 —— "
       "这不是 bug，组态画面（P&ID / 罐区）就该是这样，位置是工艺含义。");

  // Deliberately NOT given a layout: its children keep the coordinates below,
  // which is the entire point of the control group.
  //
  // Order matters here.  The host has to be sized BEFORE it joins the column,
  // because the base size hint latches naturalSize_ from the geometry it has at
  // that moment -- add it first and it latches zero width, the column honours
  // that faithfully, and paintTree clips the children away.  Cost me one round
  // of "why is that panel empty".
  auto* absHost = gAbs->add<Widget>();
  absHost->setGeometry({0.0f, 0.0f, 3.0f * 162.0f - 12.0f, 56.0f});
  for (int i = 0; i < 3; ++i) {
    auto* sw = absHost->add<Swatch>();
    sw->set("固定坐标", th.panel.lerp(th.text, 0.18f), fixed(150.0f, 48.0f));
    sw->setGeometry({float(i) * 162.0f, 4.0f, 150.0f, 48.0f});
  }
  absCol->addWidget(absHost);

  // ------------------------------------------------------------- 读数刷新 ---
  auto refresh = [readout, content, swFixed, swElastic, swCapped] {
    const Rect r = content->geometry();
    const LayoutOverflow& of = content->lastLayoutOverflow();
    char buf[256];
    std::snprintf(buf, sizeof(buf),
                  "内容区 %d x %d px    ·    固定 %d / 可伸缩 %d / 有上限 %d    ·    %s",
                  int(r.width() + 0.5f), int(r.height() + 0.5f),
                  int(swFixed->geometry().width() + 0.5f),
                  int(swElastic->geometry().width() + 0.5f),
                  int(swCapped->geometry().width() + 0.5f),
                  of.any() ? "空间不足：已按 min 截断（LayoutOverflow）" : "空间充足");
    readout->setText(buf);
  };
  probe->onResized = refresh;
  refresh();

  return content->sizeHint().preferred;
}

}  // namespace showcase
