// 运维控制台 —— alarm list over ListView's pull model, a scrollable parameter
// form, and the live values coming off the shared acquisition thread.
#include <cstdio>
#include <memory>
#include <string>

#include "Pages.hpp"
#include "geeyoou/hmi/AlarmList.hpp"
#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/CheckBox.hpp"
#include "geeyoou/widget/GroupBox.hpp"
#include "geeyoou/widget/Label.hpp"
#include "geeyoou/widget/PushButton.hpp"
#include "geeyoou/widget/ScrollArea.hpp"
#include "geeyoou/widget/SpinBox.hpp"

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

Size buildOpsPage(Widget* content, AppState& app) {

  // ---------------- 实时值 ----------------
  auto* gLive = content->add<GroupBox>();
  gLive->setGeometry({0, 0, 340, 168});
  gLive->setTitle("实时值（来自采集线程）");

  Label* liveLabels[3];
  const char* names[3] = {"进料流量", "釜内温度", "系统压力"};
  for (int i = 0; i < 3; ++i) {
    caption(gLive, 14, 44.0f + float(i) * 34.0f, 110, names[i]);
    liveLabels[i] = gLive->add<Label>();
    liveLabels[i]->setGeometry({130, 40.0f + float(i) * 34.0f, 190, 28});
    liveLabels[i]->setAlign(HAlign::Right, VAlign::Middle);
    liveLabels[i]->setPixelSize(16.0f);
    liveLabels[i]->setText("--");
  }

  auto* queueStat = gLive->add<Label>();
  queueStat->setGeometry({14, 142, 306, 20});
  queueStat->addStyleClass("caption");
  queueStat->setPixelSize(11.0f);
  queueStat->setText("队列：0 待处理 / 0 丢弃");

  // ---------------- 报警列表 ----------------
  // Width chosen so the page's design size fits the content viewport at the
  // default window size; anything wider and the shell shows a horizontal
  // scrollbar, which is correct behaviour but ugly as a first impression.
  auto* gAlarm = content->add<GroupBox>();
  gAlarm->setGeometry({356, 0, 700, 340});
  gAlarm->setTitle("报警列表（ListView 拉取式模型）");

  auto* alarms = gAlarm->add<AlarmList>(/*capacity=*/2000);
  alarms->setGeometry({14, 44, 672, 246});

  auto* alarmStat = gAlarm->add<Label>();
  alarmStat->setGeometry({14, 300, 340, 24});
  alarmStat->addStyleClass("caption");
  alarmStat->setPixelSize(11.0f);
  alarmStat->setText("未确认 0 / 活动 0");

  auto* showAck = gAlarm->add<CheckBox>();
  showAck->setGeometry({368, 300, 120, 24});
  showAck->setText("显示已确认");
  showAck->setChecked(true);

  auto* ackBtn = gAlarm->add<PushButton>();
  ackBtn->setGeometry({498, 298, 88, 28});
  ackBtn->setText("确认");
  ackBtn->setVariant(ButtonVariant::Primary);

  auto* ackAllBtn = gAlarm->add<PushButton>();
  ackAllBtn->setGeometry({596, 298, 90, 28});
  ackAllBtn->setText("全部确认");

  ackBtn->clicked.connect([alarms] {
    if (const AlarmRecord* r = alarms->currentRecord()) alarms->acknowledge(r->id);
  });
  ackAllBtn->clicked.connect([alarms] { alarms->acknowledgeAll(); });
  showAck->toggled.connect([alarms](bool on) { alarms->setShowAcknowledged(on); });

  // Take over the alarm stream, and pick up anything raised before this page
  // was first opened so the list is never mysteriously empty.
  for (AlarmRecord& r : app.alarmBacklog) alarms->add(r);
  app.alarmBacklog.clear();
  app.alarmSink = [alarms](const AlarmRecord& r) { alarms->add(r); };

  // ---------------- 参数表单（ScrollArea） ----------------
  // A ScrollArea nested inside the page's own ScrollArea: the inner one scrolls
  // the parameter rows, the outer one scrolls the page.  They do not fight,
  // because the wheel is consumed by the deepest widget that handles it.
  auto* gForm = content->add<GroupBox>();
  gForm->setGeometry({0, 184, 340, 400});
  gForm->setTitle("参数表单（ScrollArea）");

  auto* scroll = gForm->add<ScrollArea>();
  scroll->setGeometry({14, 44, 312, 340});

  const int kParams = 20;
  scroll->setContentSize({280.0f, float(kParams) * 42.0f + 8.0f});
  for (int i = 0; i < kParams; ++i) {
    char name[64];
    std::snprintf(name, sizeof(name), "参数 P-%02d", i + 1);
    auto* lbl = scroll->content()->add<Label>();
    lbl->setGeometry({8.0f, 8.0f + float(i) * 42.0f, 120, 30});
    lbl->setText(name);
    lbl->addStyleClass("caption");
    lbl->setPixelSize(12.0f);

    auto* sp = scroll->content()->add<SpinBox>();
    sp->setGeometry({134.0f, 8.0f + float(i) * 42.0f, 140, 30});
    sp->setRange(0, 500);
    sp->setValue(double(i) * 7.5);
    sp->setDecimals(1);
  }

  // ---------------- refresh ----------------
  auto* ticker = content->add<Ticker>();
  ticker->setGeometry({0, 0, 0, 0});
  ticker->divisor = 3;  // ~10 Hz is plenty for numeric readouts
  ticker->onTick = [&app, liveLabels, queueStat, alarms, alarmStat] {
    char buf[96];
    for (int i = 0; i < 3; ++i) {
      std::snprintf(buf, sizeof(buf), "%.2f %s", app.hub.lastValue(i),
                    app.hub.channelUnit(i).c_str());
      liveLabels[i]->setText(buf);
    }
    std::snprintf(buf, sizeof(buf), "队列：%zu 待处理 / %zu 丢弃", app.hub.pending(),
                  app.hub.droppedCount());
    queueStat->setText(buf);
    std::snprintf(buf, sizeof(buf), "未确认 %d / 活动 %d",
                  alarms->unacknowledgedCount(), alarms->activeCount());
    alarmStat->setText(buf);
  };

  return {1056.0f, 596.0f};
}

}  // namespace showcase
