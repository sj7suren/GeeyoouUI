// 基础控件 —— the generic widget layer, and the manual test bed for focus
// traversal and the disabled-subtree cascade.
//
// LAID OUT BY THE ENGINE (R2/T-11).  There is not one coordinate in this file:
// the page is a column of two bands, each band a line of panels, and each panel
// a stack of controls.  Sizes come from the controls' own sizeHint(), so a
// button whose caption is translated, or a theme with a larger body font, moves
// everything around it instead of overlapping it.
//
// PageSelects.cpp next door is still absolute, on purpose.  Both work, and both
// go on working -- see docs/architecture.md section 4.
#include <cstdio>

#include "Pages.hpp"
#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/BoxLayout.hpp"
#include "geeyoou/widget/CheckBox.hpp"
#include "geeyoou/widget/GridLayout.hpp"
#include "geeyoou/widget/GroupBox.hpp"
#include "geeyoou/widget/Label.hpp"
#include "geeyoou/widget/ProgressBar.hpp"
#include "geeyoou/widget/PushButton.hpp"
#include "geeyoou/widget/RadioButton.hpp"
#include "geeyoou/widget/Separator.hpp"
#include "geeyoou/widget/Slider.hpp"
#include "geeyoou/widget/SpinBox.hpp"
#include "geeyoou/widget/ToggleSwitch.hpp"
#include "i18n/I18n.hpp"

namespace showcase {

using namespace geeyoou;

namespace {
constexpr float kBandGap = 16.0f;   // between the page's bands
constexpr float kPanelGap = 20.0f;  // between panels on one band
constexpr float kItemGap = 10.0f;   // between controls inside a panel

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

// A bare Widget that exists only to be the host of a nested box: a BoxLayout
// places its host's DIRECT children, so a line inside a column needs a node of
// its own to hang off.  Costs one widget and no geometry.
Widget* band(Widget* parent, BoxLayout* into, std::uint16_t stretch = 0) {
  Widget* w = parent->add<Widget>();
  into->addWidget(w, stretch);
  return w;
}

Label* caption(Widget* parent, BoxLayout* into, std::string s) {
  auto* l = parent->add<Label>();
  l->setText(s);
  l->addStyleClass("caption");
  l->setPixelSize(11.0f);
  l->setAlign(HAlign::Left, VAlign::Middle);
  into->addWidget(l);
  return l;
}

Label* rowCaption(Widget* parent, std::string s) {
  auto* l = parent->add<Label>();
  l->setText(s);
  l->addStyleClass("caption");
  l->setPixelSize(11.0f);
  l->setAlign(HAlign::Left, VAlign::Middle);
  return l;
}
}  // namespace

Size buildWidgetsPage(Widget* content) {
  const Theme& th = Theme::current();
  BoxLayout* page = stack(content, kBandGap);

  Widget* upper = band(content, page);
  BoxLayout* upperRow = line(upper, kPanelGap);
  Widget* lower = band(content, page);
  BoxLayout* lowerRow = line(lower, kPanelGap);

  // ---------------- 按钮与开关 ----------------
  auto* gButtons = upper->add<GroupBox>();
  gButtons->setTitle(tr("按钮与开关"));
  upperRow->addWidget(gButtons, 1);
  BoxLayout* buttons = stack(gButtons, kItemGap);

  Widget* btnRow1 = band(gButtons, buttons);
  BoxLayout* btnRow1L = line(btnRow1, kItemGap);
  auto* btnNormal = btnRow1->add<PushButton>();
  btnNormal->setText(tr("普通按钮"));
  btnRow1L->addWidget(btnNormal, 1);

  auto* btnLatch = btnRow1->add<PushButton>();
  btnLatch->setText(tr("手动/自动"));
  btnLatch->setCheckable(true);
  btnLatch->setVariant(ButtonVariant::Warning);
  btnRow1L->addWidget(btnLatch, 1);

  Widget* btnRow2 = band(gButtons, buttons);
  BoxLayout* btnRow2L = line(btnRow2, kItemGap);
  auto* btnDisabled = btnRow2->add<PushButton>();
  btnDisabled->setText(tr("已禁用"));
  btnDisabled->setEnabled(false);
  btnRow2L->addWidget(btnDisabled, 1);

  auto* swPump = btnRow2->add<ToggleSwitch>();
  swPump->setText(tr("进料泵"));
  swPump->setChecked(true);
  btnRow2L->addWidget(swPump, 1);

  auto* sep1 = gButtons->add<Separator>();
  buttons->addWidget(sep1);

  auto* cbInterlock = gButtons->add<CheckBox>();
  cbInterlock->setText(tr("启用安全联锁"));
  cbInterlock->setChecked(true);
  buttons->addWidget(cbInterlock);

  auto* cbLog = gButtons->add<CheckBox>();
  cbLog->setText(tr("记录历史曲线"));
  buttons->addWidget(cbLog);

  auto* cbOff = gButtons->add<CheckBox>();
  cbOff->setText(tr("远程写入（无权限）"));
  cbOff->setEnabled(false);
  buttons->addWidget(cbOff);
  // Everything above keeps its preferred height and the slack collects at the
  // bottom, which is what "top-aligned" means to a box.
  buttons->addStretch();

  // ---------------- 单选与进度 ----------------
  auto* gMode = upper->add<GroupBox>();
  gMode->setTitle(tr("运行模式（单选组）"));
  upperRow->addWidget(gMode, 1);
  BoxLayout* mode = stack(gMode, kItemGap);

  auto* rbStop = gMode->add<RadioButton>();
  rbStop->setText(tr("停机"));
  mode->addWidget(rbStop);

  auto* rbManual = gMode->add<RadioButton>();
  rbManual->setText(tr("手动"));
  rbManual->setChecked(true);
  mode->addWidget(rbManual);

  auto* rbAuto = gMode->add<RadioButton>();
  rbAuto->setText(tr("自动"));
  mode->addWidget(rbAuto);

  auto* sep2 = gMode->add<Separator>();
  mode->addWidget(sep2);

  caption(gMode, mode, tr("批次进度"));
  auto* pbBatch = gMode->add<ProgressBar>();
  pbBatch->setValue(40);
  mode->addWidget(pbBatch);

  auto* pbLevel = gMode->add<ProgressBar>();
  pbLevel->setValue(72);
  pbLevel->setBarColor(th.ok);
  pbLevel->setText(tr("料位 72%"));
  mode->addWidget(pbLevel);
  mode->addStretch();

  // ---------------- 参数设定 ----------------
  // A grid rather than a box: label/field pairs are exactly what addRow is for,
  // and column 1 is the one that grows, so every SpinBox lines up.
  auto* gParam = upper->add<GroupBox>();
  gParam->setTitle(tr("参数设定"));
  upperRow->addWidget(gParam, 1);
  auto* param = gParam->setLayout<GridLayout>();
  param->setSpacing(kItemGap);

  struct SpinSpec {
    std::string label;
    double lo, hi, val, step;
    int dec;
    const char* suffix;  // " °C" etc -- a unit, not prose
  };
  const SpinSpec kSpins[] = {
      {tr("目标温度"), 0, 300, 165.0, 0.5, 1, " °C"},
      {tr("压力上限"), 0, 25, 8.5, 0.1, 2, " MPa"},
      {tr("重复次数"), 1, 999, 12, 1, 0, ""},
  };

  auto* status = content->add<Label>();  // declared early: the spin boxes report to it
  status->addStyleClass("caption");
  status->setPixelSize(12.0f);
  status->setText(tr("状态：就绪 · Tab / Shift+Tab 切换焦点，空格激活，方向键调值"));
  auto say = [status](const std::string& s) { status->setText(tr("状态：") + s); };

  for (int i = 0; i < 3; ++i) {
    auto* sp = gParam->add<SpinBox>();
    sp->setRange(kSpins[i].lo, kSpins[i].hi);
    sp->setValue(kSpins[i].val);
    sp->setStep(kSpins[i].step);
    sp->setDecimals(kSpins[i].dec);
    sp->setSuffix(kSpins[i].suffix);
    param->addRow(rowCaption(gParam, kSpins[i].label), sp);

    const std::string label = kSpins[i].label;
    const int dec = kSpins[i].dec;
    sp->valueChanged.connect([say, label, dec](double v) {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%s = %.*f", label.c_str(), dec, v);
      say(buf);
    });
  }

  auto* sep3 = gParam->add<Separator>();
  param->addWidget(sep3, 3, 0, 1, 2);

  auto* hint = gParam->add<Label>();
  hint->setText(tr("SpinBox：↑↓ 步进，PgUp/PgDn ×10，可直接键入数字"));
  hint->addStyleClass("caption");
  hint->setPixelSize(11.0f);
  hint->setAlign(HAlign::Left, VAlign::Top);
  hint->setWordWrap(true);  // min width 0: it yields the column instead of forcing it
  param->addWidget(hint, 4, 0, 1, 2);

  // ---------------- 滑块 ----------------
  auto* gSlider = lower->add<GroupBox>();
  gSlider->setTitle(tr("设定值滑块"));
  lowerRow->addWidget(gSlider, 2);
  auto* sliders = gSlider->setLayout<GridLayout>();
  sliders->setSpacing(kItemGap);
  sliders->setColumnStretch(1, 1);  // the tracks take the width, the labels do not

  auto* sldSpeed = gSlider->add<Slider>();
  sldSpeed->setRange(0, 1500);
  sldSpeed->setStep(50);
  sldSpeed->setValue(600);
  sldSpeed->setTickCount(7);
  auto* lblSpeed = gSlider->add<Label>();
  lblSpeed->setText("600 rpm");
  lblSpeed->setAlign(HAlign::Right, VAlign::Middle);
  sliders->addWidget(rowCaption(gSlider, tr("搅拌转速")), 0, 0);
  sliders->addWidget(sldSpeed, 0, 1);
  sliders->addWidget(lblSpeed, 0, 2);

  auto* sldFlow = gSlider->add<Slider>();
  sldFlow->setRange(0, 100);
  sldFlow->setStep(1);
  sldFlow->setValue(35);
  sldFlow->setAccent(th.ok);
  auto* lblFlow = gSlider->add<Label>();
  lblFlow->setText("35 %");
  lblFlow->setAlign(HAlign::Right, VAlign::Middle);
  sliders->addWidget(rowCaption(gSlider, tr("阀门开度")), 1, 0);
  sliders->addWidget(sldFlow, 1, 1);
  sliders->addWidget(lblFlow, 1, 2);

  auto* sldOff = gSlider->add<Slider>();
  sldOff->setValue(50);
  sldOff->setEnabled(false);
  sliders->addWidget(rowCaption(gSlider, tr("（禁用示例）")), 2, 0);
  sliders->addWidget(sldOff, 2, 1);

  // ---------------- 联锁 ----------------
  auto* gLock = lower->add<GroupBox>();
  gLock->setTitle(tr("联锁：整组禁用"));
  lowerRow->addWidget(gLock, 1);
  BoxLayout* lock = stack(gLock, kItemGap);

  auto* cbEnableBlock = gLock->add<CheckBox>();
  cbEnableBlock->setText(tr("允许修改下列参数"));
  cbEnableBlock->setChecked(true);
  lock->addWidget(cbEnableBlock);

  // An untitled GroupBox with a layout of its own: two levels of nesting, which
  // only works because a container's sizeHint() is its layout's measure().
  auto* gInner = gLock->add<GroupBox>();
  lock->addWidget(gInner);
  BoxLayout* inner = stack(gInner, kItemGap);

  auto* swA = gInner->add<ToggleSwitch>();
  swA->setText(tr("旁路阀"));
  inner->addWidget(swA);

  auto* sldInner = gInner->add<Slider>();
  sldInner->setValue(40);
  inner->addWidget(sldInner);
  lock->addStretch();

  // Disabling the container greys out and locks EVERY descendant.
  cbEnableBlock->toggled.connect([gInner](bool on) { gInner->setEnabled(on); });

  // ---------------- 状态行 ----------------
  page->addWidget(status);

  // ---------------- signals ----------------
  btnNormal->clicked.connect([say] { say(tr("普通按钮被点击")); });
  btnLatch->toggled.connect([say](bool on) { say(on ? tr("已切到自动") : tr("已切到手动")); });
  swPump->toggled.connect([say](bool on) { say(on ? tr("进料泵 启动") : tr("进料泵 停止")); });
  cbInterlock->toggled.connect([say](bool on) { say(on ? tr("安全联锁 开") : tr("安全联锁 关")); });
  cbLog->toggled.connect([say](bool on) { say(on ? tr("开始记录曲线") : tr("停止记录曲线")); });
  rbStop->toggled.connect([say](bool on) { if (on) say(tr("模式 → 停机")); });
  rbManual->toggled.connect([say](bool on) { if (on) say(tr("模式 → 手动")); });
  rbAuto->toggled.connect([say](bool on) { if (on) say(tr("模式 → 自动")); });

  sldSpeed->valueChanged.connect([lblSpeed, pbBatch](double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.0f rpm", v);
    lblSpeed->setText(buf);
    pbBatch->setValue(v / 15.0);
  });
  sldFlow->valueChanged.connect([lblFlow](double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.0f %%", v);
    lblFlow->setText(buf);
  });

  // The page's own size, computed from what is in it rather than typed in.  The
  // shell hands this to the ScrollArea as the scrollable extent.
  return content->sizeHint().preferred;
}

}  // namespace showcase
