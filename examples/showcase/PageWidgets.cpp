// 基础控件 —— the generic widget layer, and the manual test bed for focus
// traversal and the disabled-subtree cascade.
#include <cstdio>

#include "Pages.hpp"
#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/CheckBox.hpp"
#include "geeyoou/widget/GroupBox.hpp"
#include "geeyoou/widget/Label.hpp"
#include "geeyoou/widget/ProgressBar.hpp"
#include "geeyoou/widget/PushButton.hpp"
#include "geeyoou/widget/RadioButton.hpp"
#include "geeyoou/widget/Separator.hpp"
#include "geeyoou/widget/Slider.hpp"
#include "geeyoou/widget/SpinBox.hpp"
#include "geeyoou/widget/ToggleSwitch.hpp"

namespace showcase {

using namespace geeyoou;

namespace {
Label* caption(Widget* parent, float x, float y, float w, const char* s) {
  auto* l = parent->add<Label>();
  l->setGeometry({x, y, w, 20});
  l->setText(s);
  l->addStyleClass("caption");
  l->setPixelSize(11.0f);
  l->setAlign(HAlign::Left, VAlign::Middle);
  return l;
}
}  // namespace

Size buildWidgetsPage(Widget* content) {
  const Theme& th = Theme::current();

  auto* status = content->add<Label>();
  status->setGeometry({0, 470, 932, 26});
  status->addStyleClass("caption");
  status->setPixelSize(12.0f);
  status->setText("状态：就绪 · Tab / Shift+Tab 切换焦点，空格激活，方向键调值");
  auto say = [status](const std::string& s) { status->setText("状态：" + s); };

  // ---------------- 按钮与开关 ----------------
  auto* gButtons = content->add<GroupBox>();
  gButtons->setGeometry({0, 0, 300, 250});
  gButtons->setTitle("按钮与开关");

  auto* btnNormal = gButtons->add<PushButton>();
  btnNormal->setGeometry({14, 44, 128, 34});
  btnNormal->setText("普通按钮");

  auto* btnLatch = gButtons->add<PushButton>();
  btnLatch->setGeometry({154, 44, 128, 34});
  btnLatch->setText("手动/自动");
  btnLatch->setCheckable(true);
  btnLatch->setVariant(ButtonVariant::Warning);

  auto* btnDisabled = gButtons->add<PushButton>();
  btnDisabled->setGeometry({14, 88, 128, 34});
  btnDisabled->setText("已禁用");
  btnDisabled->setEnabled(false);

  auto* swPump = gButtons->add<ToggleSwitch>();
  swPump->setGeometry({154, 90, 132, 30});
  swPump->setText("进料泵");
  swPump->setChecked(true);

  auto* sep1 = gButtons->add<Separator>();
  sep1->setGeometry({14, 132, 272, 1});

  auto* cbInterlock = gButtons->add<CheckBox>();
  cbInterlock->setGeometry({14, 142, 272, 26});
  cbInterlock->setText("启用安全联锁");
  cbInterlock->setChecked(true);

  auto* cbLog = gButtons->add<CheckBox>();
  cbLog->setGeometry({14, 172, 272, 26});
  cbLog->setText("记录历史曲线");

  auto* cbOff = gButtons->add<CheckBox>();
  cbOff->setGeometry({14, 202, 272, 26});
  cbOff->setText("远程写入（无权限）");
  cbOff->setEnabled(false);

  // ---------------- 单选与进度 ----------------
  auto* gMode = content->add<GroupBox>();
  gMode->setGeometry({320, 0, 300, 250});
  gMode->setTitle("运行模式（单选组）");

  auto* rbStop = gMode->add<RadioButton>();
  rbStop->setGeometry({14, 44, 272, 26});
  rbStop->setText("停机");

  auto* rbManual = gMode->add<RadioButton>();
  rbManual->setGeometry({14, 74, 272, 26});
  rbManual->setText("手动");
  rbManual->setChecked(true);

  auto* rbAuto = gMode->add<RadioButton>();
  rbAuto->setGeometry({14, 104, 272, 26});
  rbAuto->setText("自动");

  auto* sep2 = gMode->add<Separator>();
  sep2->setGeometry({14, 140, 272, 1});

  caption(gMode, 14, 150, 272, "批次进度");
  auto* pbBatch = gMode->add<ProgressBar>();
  pbBatch->setGeometry({14, 176, 272, 20});
  pbBatch->setValue(40);

  auto* pbLevel = gMode->add<ProgressBar>();
  pbLevel->setGeometry({14, 204, 272, 20});
  pbLevel->setValue(72);
  pbLevel->setBarColor(th.ok);
  pbLevel->setText("料位 72%");

  // ---------------- 参数设定 ----------------
  auto* gParam = content->add<GroupBox>();
  gParam->setGeometry({640, 0, 292, 250});
  gParam->setTitle("参数设定");

  struct SpinSpec { const char* label; double lo, hi, val, step; int dec; const char* suffix; };
  const SpinSpec kSpins[] = {
      {"目标温度", 0, 300, 165.0, 0.5, 1, " °C"},
      {"压力上限", 0, 25, 8.5, 0.1, 2, " MPa"},
      {"重复次数", 1, 999, 12, 1, 0, ""},
  };
  for (int i = 0; i < 3; ++i) {
    caption(gParam, 14, 46.0f + float(i) * 40.0f, 120, kSpins[i].label);
    auto* sp = gParam->add<SpinBox>();
    sp->setGeometry({140, 46.0f + float(i) * 40.0f, 138, 30});
    sp->setRange(kSpins[i].lo, kSpins[i].hi);
    sp->setValue(kSpins[i].val);
    sp->setStep(kSpins[i].step);
    sp->setDecimals(kSpins[i].dec);
    sp->setSuffix(kSpins[i].suffix);
    const std::string label = kSpins[i].label;
    const int dec = kSpins[i].dec;
    sp->valueChanged.connect([say, label, dec](double v) {
      char buf[64];
      std::snprintf(buf, sizeof(buf), "%s = %.*f", label.c_str(), dec, v);
      say(buf);
    });
  }

  auto* sep3 = gParam->add<Separator>();
  sep3->setGeometry({14, 170, 264, 1});
  auto* hint = gParam->add<Label>();
  hint->setGeometry({14, 178, 264, 50});
  hint->setText("SpinBox：↑↓ 步进，PgUp/PgDn ×10，可直接键入数字");
  hint->addStyleClass("caption");
  hint->setPixelSize(11.0f);
  hint->setAlign(HAlign::Left, VAlign::Top);

  // ---------------- 滑块 ----------------
  auto* gSlider = content->add<GroupBox>();
  gSlider->setGeometry({0, 266, 620, 190});
  gSlider->setTitle("设定值滑块");

  caption(gSlider, 14, 46, 120, "搅拌转速");
  auto* sldSpeed = gSlider->add<Slider>();
  sldSpeed->setGeometry({140, 46, 340, 28});
  sldSpeed->setRange(0, 1500);
  sldSpeed->setStep(50);
  sldSpeed->setValue(600);
  sldSpeed->setTickCount(7);

  auto* lblSpeed = gSlider->add<Label>();
  lblSpeed->setGeometry({492, 46, 100, 28});
  lblSpeed->setText("600 rpm");
  lblSpeed->setAlign(HAlign::Right, VAlign::Middle);

  caption(gSlider, 14, 92, 120, "阀门开度");
  auto* sldFlow = gSlider->add<Slider>();
  sldFlow->setGeometry({140, 92, 340, 28});
  sldFlow->setRange(0, 100);
  sldFlow->setStep(1);
  sldFlow->setValue(35);
  sldFlow->setAccent(th.ok);

  auto* lblFlow = gSlider->add<Label>();
  lblFlow->setGeometry({492, 92, 100, 28});
  lblFlow->setText("35 %");
  lblFlow->setAlign(HAlign::Right, VAlign::Middle);

  caption(gSlider, 14, 136, 120, "（禁用示例）");
  auto* sldOff = gSlider->add<Slider>();
  sldOff->setGeometry({140, 136, 340, 28});
  sldOff->setValue(50);
  sldOff->setEnabled(false);

  // ---------------- 联锁 ----------------
  auto* gLock = content->add<GroupBox>();
  gLock->setGeometry({640, 266, 292, 190});
  gLock->setTitle("联锁：整组禁用");

  auto* cbEnableBlock = gLock->add<CheckBox>();
  cbEnableBlock->setGeometry({14, 44, 264, 26});
  cbEnableBlock->setText("允许修改下列参数");
  cbEnableBlock->setChecked(true);

  auto* gInner = gLock->add<GroupBox>();
  gInner->setGeometry({14, 78, 264, 96});

  auto* swA = gInner->add<ToggleSwitch>();
  swA->setGeometry({14, 12, 236, 28});
  swA->setText("旁路阀");

  auto* sldInner = gInner->add<Slider>();
  sldInner->setGeometry({14, 50, 236, 28});
  sldInner->setValue(40);

  // Disabling the container greys out and locks EVERY descendant.
  cbEnableBlock->toggled.connect([gInner](bool on) { gInner->setEnabled(on); });

  // ---------------- signals ----------------
  btnNormal->clicked.connect([say] { say("普通按钮被点击"); });
  btnLatch->toggled.connect([say](bool on) { say(on ? "已切到自动" : "已切到手动"); });
  swPump->toggled.connect([say](bool on) { say(on ? "进料泵 启动" : "进料泵 停止"); });
  cbInterlock->toggled.connect([say](bool on) { say(on ? "安全联锁 开" : "安全联锁 关"); });
  cbLog->toggled.connect([say](bool on) { say(on ? "开始记录曲线" : "停止记录曲线"); });
  rbStop->toggled.connect([say](bool on) { if (on) say("模式 → 停机"); });
  rbManual->toggled.connect([say](bool on) { if (on) say("模式 → 手动"); });
  rbAuto->toggled.connect([say](bool on) { if (on) say("模式 → 自动"); });

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

  return {940.0f, 510.0f};
}

}  // namespace showcase
