// 窗口外壳 —— the window layer itself, driven live.
//
// Every control on this page writes straight into the running window's header.
// That is the point of the page: the chrome is not a fixed asset baked into the
// library, it is a widget with properties, and a commissioning engineer can
// match it to a customer's plant standard without touching GeeyoouUI's source.
#include <string>
#include <vector>

#include "Pages.hpp"
#include "ShowcaseWindow.hpp"
#include "geeyoou/render/Icon.hpp"
#include "geeyoou/render/IconRegistry.hpp"
#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/CheckBox.hpp"
#include "geeyoou/widget/ComboBox.hpp"
#include "geeyoou/widget/GroupBox.hpp"
#include "geeyoou/widget/Label.hpp"
#include "geeyoou/widget/LineEdit.hpp"
#include "geeyoou/widget/PushButton.hpp"
#include "geeyoou/widget/Slider.hpp"
#include "i18n/I18n.hpp"

namespace showcase {

using namespace geeyoou;

namespace {

Label* caption(Widget* parent, float x, float y, float w, std::string s) {
  auto* l = parent->add<Label>();
  l->setGeometry({x, y, w, 20});
  l->setText(s);
  l->addStyleClass("caption");
  l->setPixelSize(11.0f);
  l->setAlign(HAlign::Left, VAlign::Middle);
  return l;
}

struct Swatch {
  std::string name;
  Color color;
};

// Draws every icon the registry knows about, at three sizes, with its name.
//
// One widget rather than N labels + N icon buttons: the whole strip repaints as
// a unit, and it needs no state beyond what IconRegistry::all() already gives.
class IconStrip : public Widget {
 public:
  // `customOnly` keeps the strip to the registered pack; the built-ins are
  // already visible all over the rest of the showcase.
  void setCustomOnly(bool on) { customOnly_ = on; update(); }

 protected:
  void onPaint(Painter& p, const Rect&) override {
    const Theme& t = Theme::current();
    float x = 6.0f;
    for (const IconEntry& e : icons().all()) {
      if (customOnly_ && e.builtin) continue;
      if (x + kCell > localRect().width()) break;

      // 32 / 20 / 14 px from ONE definition -- the point of a vector icon is
      // that there is no per-DPI asset and no size-specific artwork.
      drawIcon(p, e.id, {x + 20.0f, 6.0f, 32.0f, 32.0f}, t.accent);
      drawIcon(p, e.id, {x + 12.0f, 44.0f, 20.0f, 20.0f}, t.text);
      drawIcon(p, e.id, {x + 40.0f, 47.0f, 14.0f, 14.0f}, t.textDim);
      p.drawText({x + kCell * 0.5f, 72.0f}, e.name, t.fontSmall, t.textDim,
                 HAlign::Center, VAlign::Top);
      x += kCell;
    }
  }

 private:
  static constexpr float kCell = 72.0f;
  bool customOnly_ = true;
};

}  // namespace

Size buildWindowPage(Widget* content, ShowcaseWindow& win) {
  WindowHeader* h = win.header();

  auto* log = content->add<Label>();
  log->setGeometry({0, 664, 940, 26});
  log->addStyleClass("caption");
  log->setPixelSize(12.0f);
  log->setText(tr("提示：按住标题栏空白处即可拖动窗口，双击可最大化 / 还原。"));
  auto say = [log](const std::string& s) { log->setText(s); };
  win.headerAction.connect([say](const std::string& s) { say(s); });

  // ------------------------------------------------------------ 标题栏度量 ---
  auto* gMetrics = content->add<GroupBox>();
  gMetrics->setGeometry({0, 0, 300, 236});
  gMetrics->setTitle(tr("标题栏度量"));

  caption(gMetrics, 14, 42, 272, tr("标题栏高度"));
  auto* sHeight = gMetrics->add<Slider>();
  sHeight->setGeometry({14, 64, 272, 30});
  sHeight->setRange(32, 88);
  sHeight->setValue(h->height());
  sHeight->setTickCount(8);
  sHeight->valueChanged.connect([h, say](double v) {
    h->setHeight(float(v));
    say(tr("标题栏高度 = ") + std::to_string(int(v)) + " px");
  });

  caption(gMetrics, 14, 100, 272, tr("窗口按钮宽度"));
  auto* sBtn = gMetrics->add<Slider>();
  sBtn->setGeometry({14, 122, 272, 30});
  sBtn->setRange(32, 72);
  sBtn->setValue(h->buttonWidth());
  sBtn->valueChanged.connect([h, say](double v) {
    h->setButtonWidth(float(v));
    say(tr("窗口按钮宽度 = ") + std::to_string(int(v)) + " px");
  });

  caption(gMetrics, 14, 158, 272, tr("左侧留白"));
  auto* sPad = gMetrics->add<Slider>();
  sPad->setGeometry({14, 180, 272, 30});
  sPad->setRange(0, 48);
  sPad->setValue(h->leadingPadding());
  sPad->valueChanged.connect([h](double v) { h->setLeadingPadding(float(v)); });

  // ------------------------------------------------------------ 标题与图标 ---
  auto* gBrand = content->add<GroupBox>();
  gBrand->setGeometry({320, 0, 300, 236});
  gBrand->setTitle(tr("标题与图标"));

  caption(gBrand, 14, 42, 272, tr("标题文本"));
  auto* eTitle = gBrand->add<LineEdit>();
  eTitle->setGeometry({14, 64, 272, 32});
  eTitle->setText(h->title());
  eTitle->textChanged.connect([h](const std::string& s) { h->setTitle(s); });

  caption(gBrand, 14, 102, 272, tr("副标题"));
  auto* eSub = gBrand->add<LineEdit>();
  eSub->setGeometry({14, 124, 272, 32});
  eSub->setText(tr("工控 HMI 控件库 · 演示工程"));
  eSub->textChanged.connect([h](const std::string& s) { h->setSubtitle(s); });

  caption(gBrand, 14, 162, 130, tr("图标"));
  // Note the last four: they are REGISTERED icons, looked up by name, and they
  // flow into setIcon() exactly like a built-in enumerator does.  That is the
  // whole point of IconRegistry handing back an `Icon` rather than a new type.
  const std::vector<std::pair<std::string, Icon>> kIcons = {
      {tr("设置齿轮"), Icon::Settings},        {tr("信息"), Icon::Info},
      {tr("报警"), Icon::Warning},             {tr("运行"), Icon::Play},
      {tr("锁定"), Icon::Lock},                {tr("全球"), Icon::Globe},
      {tr("泵（自定义）"), icons().find("pump")},
      {tr("阀（自定义）"), icons().find("valve")},
      {tr("罐（自定义）"), icons().find("tank")},
      {tr("闪电（SVG）"), icons().find("zap")},
      {tr("无图标"), Icon::None},
  };
  auto* cIcon = gBrand->add<ComboBox>();
  cIcon->setGeometry({14, 184, 150, 32});
  {
    std::vector<SelectItem> items;
    for (const auto& kv : kIcons) items.push_back(SelectItem(kv.first));
    cIcon->setItems(std::move(items));
  }
  cIcon->setCurrentIndex(0);
  cIcon->currentIndexChanged.connect([h, kIcons](int i) {
    if (i >= 0 && i < int(kIcons.size())) h->setIcon(kIcons[std::size_t(i)].second);
  });

  auto* cbBadge = gBrand->add<CheckBox>();
  cbBadge->setGeometry({176, 184, 110, 32});
  cbBadge->setText(tr("图标底板"));
  cbBadge->setChecked(true);
  cbBadge->toggled.connect([h](bool on) { h->setIconBadge(on); });

  // ------------------------------------------------------------------ 配色 ---
  auto* gColor = content->add<GroupBox>();
  gColor->setGeometry({640, 0, 300, 236});
  gColor->setTitle(tr("配色"));

  const std::vector<Swatch> kBg = {
      {tr("面板灰（默认）"), Theme::current().panel.lerp(Theme::current().background, 0.2f)},
      {tr("深空蓝"), Color::rgb(0x0E, 0x1B, 0x2E)},
      {tr("石墨黑"), Color::rgb(0x16, 0x16, 0x1A)},
      {tr("工业绿"), Color::rgb(0x0F, 0x24, 0x1C)},
      {tr("警示棕"), Color::rgb(0x2A, 0x1C, 0x10)},
  };
  caption(gColor, 14, 42, 272, tr("标题栏底色"));
  auto* cBg = gColor->add<ComboBox>();
  cBg->setGeometry({14, 64, 272, 32});
  {
    std::vector<SelectItem> items;
    for (const Swatch& s : kBg) items.push_back(SelectItem(s.name));
    cBg->setItems(std::move(items));
  }
  cBg->setCurrentIndex(0);
  cBg->currentIndexChanged.connect([h, kBg](int i) {
    if (i >= 0 && i < int(kBg.size())) h->setBackground(kBg[std::size_t(i)].color);
  });

  const std::vector<Swatch> kAccent = {
      {tr("仪表蓝（默认）"), Theme::current().accent},
      {tr("安全绿"), Theme::current().ok},
      {tr("警戒橙"), Theme::current().warn},
      {tr("报警红"), Theme::current().alarm},
  };
  caption(gColor, 14, 102, 272, tr("图标 / 强调色"));
  auto* cAccent = gColor->add<ComboBox>();
  cAccent->setGeometry({14, 124, 272, 32});
  {
    std::vector<SelectItem> items;
    for (const Swatch& s : kAccent) items.push_back(SelectItem(s.name));
    cAccent->setItems(std::move(items));
  }
  cAccent->setCurrentIndex(0);
  cAccent->currentIndexChanged.connect([h, &win, kAccent](int i) {
    if (i < 0 || i >= int(kAccent.size())) return;
    const Color c = kAccent[std::size_t(i)].color;
    h->setIconColor(c);
    win.accountMenu()->setAvatarColor(c);
  });

  auto* cbBorder = gColor->add<CheckBox>();
  cbBorder->setGeometry({14, 166, 272, 28});
  cbBorder->setText(tr("标题栏下沿分隔线"));
  cbBorder->setChecked(true);
  cbBorder->toggled.connect([h](bool on) { h->setBorderVisible(on); });

  auto* cbWinBorder = gColor->add<CheckBox>();
  cbWinBorder->setGeometry({14, 196, 272, 28});
  cbWinBorder->setText(tr("窗口外框线"));
  cbWinBorder->setChecked(true);
  cbWinBorder->toggled.connect([&win](bool on) { win.setBorderVisible(on); });

  // ------------------------------------------------------------ 窗口按钮 ---
  auto* gButtons = content->add<GroupBox>();
  gButtons->setGeometry({0, 256, 300, 196});
  gButtons->setTitle(tr("窗口按钮"));

  auto apply = [h](bool mi, bool ma, bool cl) {
    WindowButtons b;
    b.minimize = mi;
    b.maximize = ma;
    b.close = cl;
    h->setButtons(b);
  };
  auto* cbMin = gButtons->add<CheckBox>();
  auto* cbMax = gButtons->add<CheckBox>();
  auto* cbClose = gButtons->add<CheckBox>();
  cbMin->setGeometry({14, 44, 272, 28});
  cbMin->setText(tr("显示「最小化」"));
  cbMin->setChecked(true);
  cbMax->setGeometry({14, 76, 272, 28});
  cbMax->setText(tr("显示「最大化 / 还原」"));
  cbMax->setChecked(true);
  cbClose->setGeometry({14, 108, 272, 28});
  cbClose->setText(tr("显示「关闭」"));
  cbClose->setChecked(true);
  auto sync = [apply, cbMin, cbMax, cbClose](bool) {
    apply(cbMin->isChecked(), cbMax->isChecked(), cbClose->isChecked());
  };
  cbMin->toggled.connect(sync);
  cbMax->toggled.connect(sync);
  cbClose->toggled.connect(sync);

  auto* cbDrag = gButtons->add<CheckBox>();
  cbDrag->setGeometry({14, 146, 272, 28});
  cbDrag->setText(tr("标题栏空白处可拖动窗口"));
  cbDrag->setChecked(true);
  cbDrag->toggled.connect([h, say](bool on) {
    h->setDraggable(on);
    say(on ? tr("标题栏已可拖动") : tr("标题栏已锁定，窗口无法拖动"));
  });

  // -------------------------------------------------------------- 窗口命令 ---
  auto* gCmd = content->add<GroupBox>();
  gCmd->setGeometry({320, 256, 300, 196});
  gCmd->setTitle(tr("窗口命令"));

  caption(gCmd, 14, 42, 272,
          tr("与标题栏右上角的按钮是同一套 API（Window 提供）"));

  auto* bMin = gCmd->add<PushButton>();
  bMin->setGeometry({14, 68, 130, 34});
  bMin->setText(tr("最小化"));
  bMin->setIcon(Icon::WindowMinimize);
  bMin->clicked.connect([&win] { win.minimize(); });

  auto* bMax = gCmd->add<PushButton>();
  bMax->setGeometry({156, 68, 130, 34});
  bMax->setText(tr("最大化 / 还原"));
  bMax->setIcon(Icon::WindowMaximize);
  bMax->clicked.connect([&win] { win.toggleMaximize(); });

  auto* bHeader = gCmd->add<PushButton>();
  bHeader->setGeometry({14, 112, 130, 34});
  bHeader->setText(tr("隐藏标题栏"));
  bHeader->setCheckable(true);
  bHeader->toggled.connect([&win, say](bool on) {
    win.setHeaderVisible(!on);
    say(on ? tr("标题栏已隐藏（全屏画面模式）") : tr("标题栏已恢复"));
  });

  auto* bClose = gCmd->add<PushButton>();
  bClose->setGeometry({156, 112, 130, 34});
  bClose->setText(tr("关闭窗口"));
  bClose->setVariant(ButtonVariant::Danger);
  bClose->clicked.connect([&win] { win.close(); });

  // ---------------------------------------------------------------- 说明 ---
  auto* gAbout = content->add<GroupBox>();
  gAbout->setGeometry({640, 256, 300, 196});
  gAbout->setTitle(tr("这一层做了什么"));

  auto* about = gAbout->add<Label>();
  about->setGeometry({14, 40, 272, 148});
  about->setPixelSize(12.0f);
  about->addStyleClass("caption");
  about->setText(
      tr("AppWindow 是无边框窗口：Windows 不再绘制标题栏、\n"
      "边框和主题色，全部由 WindowHeader 用同一套\n"
      "Painter / Theme 画出来。\n\n"
      "但它仍是一个正常的顶层窗口——贴边分屏、最小化\n"
      "动画、Alt+Tab 缩略图、双击标题栏最大化都还在，\n"
      "因为拖动区是作为「窗口标题区」上报给系统的。"));

  // ------------------------------------------------------ 图标扩展 ---
  auto* gIcons = content->add<GroupBox>();
  gIcons->setGeometry({0, 470, 940, 190});
  gIcons->setTitle(tr("图标扩展（IconRegistry）—— 上面「图标」下拉里的自定义项就是这些"));

  caption(gIcons, 14, 40, 912,
          tr("前三个用 IconCanvas 代码绘制，其余用 SVG path 注册（marker 是填充式）。"
          "每个都以 32 / 20 / 14 px 三种尺寸绘制——同一份定义，无需按 DPI 出图。"));

  auto* strip = gIcons->add<IconStrip>();
  strip->setGeometry({14, 66, 912, 92});

  auto* iconNote = gIcons->add<Label>();
  iconNote->setGeometry({14, 158, 912, 20});
  iconNote->addStyleClass("caption");
  iconNote->setPixelSize(11.0f);
  {
    const auto& errs = icons().errors();
    iconNote->setText(
        errs.empty()
            ? (tr("已注册自定义图标 ") + std::to_string(icons().customCount()) +
               tr(" 个，SVG 路径全部解析通过；名字可直接 icons().find(\"pump\") 取回"))
            : (tr("SVG 解析有 ") + std::to_string(errs.size()) + tr(" 处问题：") + errs.front()));
  }

  return {940, 696};
}

}  // namespace showcase
