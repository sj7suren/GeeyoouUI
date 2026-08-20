#include "ReactorScreen.hpp"

#include <cmath>
#include <vector>

#include "geeyoou/hmi/Gauge.hpp"
#include "geeyoou/hmi/StatusLed.hpp"
#include "geeyoou/hmi/TrendChart.hpp"
#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/GroupBox.hpp"
#include "geeyoou/widget/Label.hpp"
#include "geeyoou/widget/PushButton.hpp"

namespace tutorial {
namespace {

using namespace geeyoou;

// 三个仪表的规格。写成表而不是三段复制粘贴，是因为教程第二步的重点就是
// "同一个控件换参数用三次"。
struct GaugeSpec {
  const char* title;
  const char* unit;
  double lo, hi;      // 量程
  double warn, alarm; // 预警 / 报警阈值
  double demo;        // 静态配图用的示数
};

const GaugeSpec kGauges[3] = {
    {"进料流量", "m³/h", 0, 100, 75, 90, 74.9},
    {"釜内温度", "°C", 0, 200, 150, 180, 163.4},
    {"系统压力", "MPa", 0, 10, 7, 8.5, 6.1},
};

Gauge* addGauge(Widget* parent, const GaugeSpec& s, float x, float y) {
  auto* g = parent->add<Gauge>();
  g->setGeometry({x, y, 250, 214});
  g->setRange(s.lo, s.hi);
  g->setBands(s.warn, s.alarm);
  g->setTitle(s.title);
  g->setUnit(s.unit);
  g->setValue(s.demo);
  return g;
}

// 设备状态面板：一个 GroupBox 装五盏灯。
GroupBox* addStatusPanel(Widget* parent, float x, float y) {
  auto* panel = parent->add<GroupBox>();
  panel->setGeometry({x, y, 274, 214});
  panel->setTitle("设备状态");

  struct LedSpec {
    const char* caption;
    StatusLed::State state;
  };
  // 这些初始状态和 kGauges 里的示数是对得上的：温度 163.4 已经越过 150 的
  // 预警线，所以加热器那盏灯是 Warn 而不是 Ok。
  //
  // 看起来是小事，但配图必须自洽 —— 教程下一节要讲"温度超限时灯自己变黄",
  // 而配图上却是绿的，读者只会怀疑自己读错了。
  const LedSpec kLeds[5] = {
      {"进料泵 P-101", StatusLed::State::Ok},
      {"加热器 H-201", StatusLed::State::Warn},
      {"泄压阀 V-303", StatusLed::State::Off},
      {"超压报警", StatusLed::State::Off},
      {"Modbus 通讯", StatusLed::State::Ok},
  };
  for (int i = 0; i < 5; ++i) {
    auto* led = panel->add<StatusLed>();
    led->setGeometry({14, 44.0f + float(i) * 32.0f, 246, 28});
    led->setCaption(kLeds[i].caption);
    led->setState(kLeds[i].state);
  }
  return panel;
}

TrendChart* addTrend(Widget* parent, float x, float y, float w) {
  const Theme& th = Theme::current();
  auto* chart = parent->add<TrendChart>();
  chart->setGeometry({x, y, w, 330});
  chart->setTitle("实时趋势");
  chart->setYRange(0, 200);
  chart->setGridDivisions(8, 4);
  chart->addChannel("流量", th.accent, 900);
  chart->addChannel("温度", th.warn, 900);
  chart->addChannel("压力×20", th.ok, 900);
  return chart;
}

// 往趋势图里灌一段正弦，好让静态配图上有曲线可看。
void seedTrend(TrendChart* chart, int samples) {
  for (int i = 0; i < samples; ++i) {
    const double t = double(i) * 0.05;
    const float v[3] = {
        float(50.0 + 28.0 * std::sin(t * 0.7)),
        float(120.0 + 45.0 * std::sin(t * 0.23 + 1.1)),
        float((5.0 + 3.2 * std::sin(t * 0.41 + 2.3)) * 20.0),
    };
    chart->pushAll(v, 3);
  }
}

// step4 之后，这些指针按加入顺序固定，animate() 靠位置找它们。
//
// 教程正文里的做法是把指针捕获进 lambda（更常规）；这里之所以按 children()
// 的位置取，是因为 shots.cpp 和 tutorial.cpp 各自建树，谁也没有对方的指针。
Gauge* gaugeAt(Widget* content, int i) {
  return static_cast<Gauge*>(content->children()[std::size_t(i)].get());
}

}  // namespace

Size buildStep1(Widget* content) {
  addGauge(content, kGauges[0], 0, 0);
  return {250.0f, 214.0f};
}

Size buildStep2(Widget* content) {
  for (int i = 0; i < 3; ++i) {
    addGauge(content, kGauges[i], float(i) * 266.0f, 0);
  }
  return {782.0f, 214.0f};
}

Size buildStep3(Widget* content) {
  for (int i = 0; i < 3; ++i) {
    addGauge(content, kGauges[i], float(i) * 266.0f, 0);
  }
  addStatusPanel(content, 798, 0);
  return {1072.0f, 214.0f};
}

Size buildStep4(Widget* content) {
  for (int i = 0; i < 3; ++i) {
    addGauge(content, kGauges[i], float(i) * 266.0f, 0);
  }
  addStatusPanel(content, 798, 0);

  TrendChart* chart = addTrend(content, 0, 230, 1072);
  seedTrend(chart, 700);

  auto* start = content->add<PushButton>();
  start->setGeometry({0, 576, 120, 38});
  start->setText("启动");
  start->setVariant(ButtonVariant::Success);

  auto* stop = content->add<PushButton>();
  stop->setGeometry({132, 576, 120, 38});
  stop->setText("停止");
  stop->setVariant(ButtonVariant::Danger);

  auto* status = content->add<Label>();
  status->setGeometry({268, 576, 500, 38});
  status->addStyleClass("caption");
  status->setPixelSize(12.0f);
  status->setText("状态：采集中");

  return {1072.0f, 620.0f};
}

void animate(Widget* content, double t) {
  const double flow = 50.0 + 28.0 * std::sin(t * 0.7);
  const double temp = 120.0 + 45.0 * std::sin(t * 0.23 + 1.1);
  const double press = 5.0 + 3.2 * std::sin(t * 0.41 + 2.3);

  gaugeAt(content, 0)->setValue(flow);
  gaugeAt(content, 1)->setValue(temp);
  gaugeAt(content, 2)->setValue(press);

  // 状态面板是第 4 个孩子，趋势图是第 5 个。
  auto* panel = static_cast<GroupBox*>(content->children()[3].get());
  auto* led1 = static_cast<StatusLed*>(panel->children()[1].get());
  auto* led2 = static_cast<StatusLed*>(panel->children()[2].get());
  auto* led3 = static_cast<StatusLed*>(panel->children()[3].get());
  led1->setState(temp >= 150.0 ? StatusLed::State::Warn : StatusLed::State::Ok);
  led2->setState(press >= 7.0 ? StatusLed::State::Warn : StatusLed::State::Off);
  led3->setState(press >= 8.0 ? StatusLed::State::Alarm : StatusLed::State::Off);

  auto* chart = static_cast<TrendChart*>(content->children()[4].get());
  const float v[3] = {float(flow), float(temp), float(press * 20.0)};
  chart->pushAll(v, 3);
}

}  // namespace tutorial
