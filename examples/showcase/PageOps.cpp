// 运维控制台 —— alarm list over ListView's pull model, a scrollable parameter
// form, and the live values coming off the shared acquisition thread.
//
// LAID OUT BY THE ENGINE (R2/T-11).  The alarm list is the interesting case:
// it used to be 700x340 because that is what fitted next to the other panels at
// the default window size.  It now asks for six rows and takes everything the
// row can spare, so a bigger window shows more alarms instead of more felt.
#include <cstdio>
#include <memory>
#include <string>

#include "Pages.hpp"
#include "geeyoou/hmi/AlarmList.hpp"
#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/BoxLayout.hpp"
#include "geeyoou/widget/CheckBox.hpp"
#include "geeyoou/widget/GridLayout.hpp"
#include "geeyoou/widget/GroupBox.hpp"
#include "geeyoou/widget/Label.hpp"
#include "geeyoou/widget/PushButton.hpp"
#include "geeyoou/widget/ScrollArea.hpp"
#include "geeyoou/widget/SpinBox.hpp"
#include "i18n/I18n.hpp"

namespace showcase {

using namespace geeyoou;

namespace {
constexpr float kPanelGap = 16.0f;
constexpr float kItemGap = 10.0f;

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

Widget* band(Widget* parent, BoxLayout* into, std::uint16_t stretch = 0) {
  Widget* w = parent->add<Widget>();
  into->addWidget(w, stretch);
  return w;
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

Size buildOpsPage(Widget* content, AppState& app) {
  BoxLayout* page = line(content, kPanelGap);

  Widget* leftCol = band(content, page, 1);
  BoxLayout* left = stack(leftCol, kPanelGap);

  // ---------------- 实时值 ----------------
  auto* gLive = leftCol->add<GroupBox>();
  gLive->setTitle(tr("实时值（来自采集线程）"));
  left->addWidget(gLive);
  auto* live = gLive->setLayout<GridLayout>();
  live->setSpacing(kItemGap);
  live->setColumnStretch(1, 1);  // the readouts take the width, the names do not

  Label* liveLabels[3];
  const std::string names[3] = {tr("进料流量"), tr("釜内温度"), tr("系统压力")};
  for (int i = 0; i < 3; ++i) {
    liveLabels[i] = gLive->add<Label>();
    liveLabels[i]->setAlign(HAlign::Right, VAlign::Middle);
    liveLabels[i]->setPixelSize(16.0f);
    liveLabels[i]->setText("--");
    live->addRow(rowCaption(gLive, names[i]), liveLabels[i]);
  }

  auto* queueStat = gLive->add<Label>();
  queueStat->addStyleClass("caption");
  queueStat->setPixelSize(11.0f);
  queueStat->setText(tr("队列：0 待处理 / 0 丢弃"));
  live->addWidget(queueStat, 3, 0, 1, 2);

  // ---------------- 参数表单（ScrollArea） ----------------
  // A ScrollArea nested inside the page's own ScrollArea: the inner one scrolls
  // the parameter rows, the outer one scrolls the page.  They do not fight,
  // because the wheel is consumed by the deepest widget that handles it.
  auto* gForm = leftCol->add<GroupBox>();
  gForm->setTitle(tr("参数表单（ScrollArea）"));
  left->addWidget(gForm, 1);
  BoxLayout* form = stack(gForm, kItemGap);

  auto* scroll = gForm->add<ScrollArea>();
  form->addWidget(scroll, 1);

  // The rows inside the scroll area are laid out too -- a grid, one row per
  // parameter.  Its extent is NOT handed to anybody: giving the content a
  // layout is the whole opt-in, and ScrollArea::relayout then sizes it from its
  // own sizeHint() on every resize.  This is the last setContentSize on any
  // migrated page (O3), and the reason it could go is that the number it passed
  // -- content()->sizeHint().preferred -- was computed exactly once, at build
  // time, and never again: a form whose rows grew later scrolled to where it
  // used to end.
  auto* rows = scroll->content()->setLayout<GridLayout>();
  rows->setSpacing(kItemGap);
  rows->setMargins({8.0f, 8.0f, 8.0f, 8.0f});
  const int kParams = 20;
  for (int i = 0; i < kParams; ++i) {
    char name[64];
    std::snprintf(name, sizeof(name), tr("参数 P-%02d").c_str(), i + 1);
    auto* lbl = scroll->content()->add<Label>();
    lbl->setText(name);
    lbl->addStyleClass("caption");
    lbl->setPixelSize(12.0f);

    auto* sp = scroll->content()->add<SpinBox>();
    sp->setRange(0, 500);
    sp->setValue(double(i) * 7.5);
    sp->setDecimals(1);
    rows->addRow(lbl, sp);
  }

  // ---------------- 报警列表 ----------------
  auto* gAlarm = content->add<GroupBox>();
  gAlarm->setTitle(tr("报警列表（ListView 拉取式模型）"));
  page->addWidget(gAlarm, 2);
  BoxLayout* alarmBox = stack(gAlarm, kItemGap);

  auto* alarms = gAlarm->add<AlarmList>(/*capacity=*/2000);
  alarmBox->addWidget(alarms, 1);

  Widget* alarmTools = band(gAlarm, alarmBox);
  BoxLayout* tools = line(alarmTools, kItemGap);

  auto* alarmStat = alarmTools->add<Label>();
  alarmStat->addStyleClass("caption");
  alarmStat->setPixelSize(11.0f);
  alarmStat->setText(tr("未确认 0 / 活动 0"));
  tools->addWidget(alarmStat, 1);

  auto* showAck = alarmTools->add<CheckBox>();
  showAck->setText(tr("显示已确认"));
  showAck->setChecked(true);
  tools->addWidget(showAck);

  auto* ackBtn = alarmTools->add<PushButton>();
  ackBtn->setText(tr("确认"));
  ackBtn->setVariant(ButtonVariant::Primary);
  tools->addWidget(ackBtn);

  auto* ackAllBtn = alarmTools->add<PushButton>();
  ackAllBtn->setText(tr("全部确认"));
  tools->addWidget(ackAllBtn);

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

  // ---------------- refresh ----------------
  // Not an item in any box: a Ticker draws nothing, and animationTickTree walks
  // the TREE rather than the layout, so a zero-sized child that no layout knows
  // about still ticks.
  auto* ticker = content->add<Ticker>();
  ticker->divisor = 3;  // ~10 Hz is plenty for numeric readouts
  ticker->onTick = [&app, liveLabels, queueStat, alarms, alarmStat] {
    char buf[96];
    for (int i = 0; i < 3; ++i) {
      std::snprintf(buf, sizeof(buf), "%.2f %s", app.hub.lastValue(i),
                    app.hub.channelUnit(i).c_str());
      liveLabels[i]->setText(buf);
    }
    std::snprintf(buf, sizeof(buf), tr("队列：%zu 待处理 / %zu 丢弃").c_str(), app.hub.pending(),
                  app.hub.droppedCount());
    queueStat->setText(buf);
    std::snprintf(buf, sizeof(buf), tr("未确认 %d / 活动 %d").c_str(),
                  alarms->unacknowledgedCount(), alarms->activeCount());
    alarmStat->setText(buf);
  };

  return content->sizeHint().preferred;
}

}  // namespace showcase
