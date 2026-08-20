// 反应釜监控 —— gauges, status lamps and a live trend, fed by the shared
// acquisition thread rather than by a simulation of its own.
#include <cstdio>
#include <memory>

#include "Pages.hpp"
#include "geeyoou/hmi/Gauge.hpp"
#include "geeyoou/hmi/StatusLed.hpp"
#include "geeyoou/hmi/TrendChart.hpp"
#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/GroupBox.hpp"
#include "geeyoou/widget/Label.hpp"
#include "geeyoou/widget/PushButton.hpp"
#include "i18n/I18n.hpp"

namespace showcase {

using namespace geeyoou;

Size buildHmiPage(Widget* content, AppState& app) {
  const Theme& th = Theme::current();

  auto* flowGauge = content->add<Gauge>();
  flowGauge->setGeometry({0, 0, 250, 214});
  flowGauge->setRange(0, 100);
  flowGauge->setBands(75, 90);
  flowGauge->setTitle(tr("进料流量"));
  flowGauge->setUnit("m³/h");

  auto* tempGauge = content->add<Gauge>();
  tempGauge->setGeometry({266, 0, 250, 214});
  tempGauge->setRange(0, 200);
  tempGauge->setBands(150, 180);
  tempGauge->setTitle(tr("釜内温度"));
  tempGauge->setUnit("°C");

  auto* pressGauge = content->add<Gauge>();
  pressGauge->setGeometry({532, 0, 250, 214});
  pressGauge->setRange(0, 10);
  pressGauge->setBands(7, 8.5);
  pressGauge->setTitle(tr("系统压力"));
  pressGauge->setUnit("MPa");

  auto* statusPanel = content->add<GroupBox>();
  statusPanel->setGeometry({798, 0, 274, 214});
  statusPanel->setTitle(tr("设备状态"));

  struct LedSpec { std::string caption; StatusLed::State init; };
  const LedSpec kLeds[] = {
      {tr("进料泵 P-101"), StatusLed::State::Ok},
      {tr("加热器 H-201"), StatusLed::State::Ok},
      {tr("泄压阀 V-303"), StatusLed::State::Off},
      {tr("超压报警"), StatusLed::State::Off},
      {tr("Modbus 通讯"), StatusLed::State::Ok},
  };
  StatusLed* leds[5];
  for (int i = 0; i < 5; ++i) {
    leds[i] = statusPanel->add<StatusLed>();
    leds[i]->setGeometry({14, 44.0f + float(i) * 32.0f, 246, 28});
    leds[i]->setCaption(kLeds[i].caption);
    leds[i]->setState(kLeds[i].init);
  }
  leds[3]->setBlinkOnAlarm(true);

  auto* chart = content->add<TrendChart>();
  chart->setGeometry({0, 230, 1072, 330});
  chart->setTitle(tr("实时趋势"));
  chart->setYRange(0, 200);
  chart->setGridDivisions(8, 4);
  chart->addChannel(tr("流量"), th.accent, 900);
  chart->addChannel(tr("温度"), th.warn, 900);
  chart->addChannel(tr("压力×20"), th.ok, 900);

  // Shared by the three lambdas below and destroyed with the last of them --
  // a raw new here would leak every time the page is rebuilt.
  auto running = std::make_shared<bool>(true);
  auto blinkDiv = std::make_shared<int>(0);

  auto* btnStart = content->add<PushButton>();
  btnStart->setGeometry({0, 576, 120, 38});
  btnStart->setText(tr("启动"));
  btnStart->setVariant(ButtonVariant::Success);

  auto* btnStop = content->add<PushButton>();
  btnStop->setGeometry({132, 576, 120, 38});
  btnStop->setText(tr("停止"));
  btnStop->setVariant(ButtonVariant::Danger);

  auto* status = content->add<Label>();
  status->setGeometry({268, 576, 500, 38});
  status->addStyleClass("caption");
  status->setPixelSize(12.0f);
  status->setText(tr("状态：采集中"));

  btnStart->clicked.connect([running, status, leds] {
    *running = true;
    status->setText(tr("状态：采集中"));
    leds[0]->setState(StatusLed::State::Ok);
    leds[4]->setState(StatusLed::State::Ok);
  });
  btnStop->clicked.connect([running, status, leds] {
    *running = false;
    status->setText(tr("状态：已停止（数据保持）"));
    leds[0]->setState(StatusLed::State::Off);
    leds[1]->setState(StatusLed::State::Off);
  });

  // The page reads the SHARED hub instead of running its own maths, so the
  // numbers here and on the ops page always agree.
  auto* ticker = content->add<Ticker>();
  ticker->setGeometry({0, 0, 0, 0});
  ticker->divisor = 2;  // 30 fps animation clock -> ~15 Hz refresh
  ticker->onTick = [&app, running, blinkDiv, flowGauge, tempGauge, pressGauge,
                    leds, chart] {
    if (++(*blinkDiv) % 4 == 0) leds[3]->tick();
    if (!*running) return;

    const double flow = app.hub.lastValue(app.chFlow);
    const double temp = app.hub.lastValue(app.chTemp);
    const double press = app.hub.lastValue(app.chPress);

    flowGauge->setValue(flow);
    tempGauge->setValue(temp);
    pressGauge->setValue(press);

    leds[1]->setState(temp >= 150.0 ? StatusLed::State::Warn : StatusLed::State::Ok);
    leds[2]->setState(press >= 7.0 ? StatusLed::State::Warn : StatusLed::State::Off);
    leds[3]->setState(press >= 8.0 ? StatusLed::State::Alarm : StatusLed::State::Off);

    const float samples[3] = {float(flow), float(temp), float(press * 20.0)};
    chart->pushAll(samples, 3);
  };

  return {1072.0f, 620.0f};
}

}  // namespace showcase
