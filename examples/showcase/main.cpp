//
// GeeyoouUI showcase -- the ONE entry point.
//
// An admin-console shell: pick a page from the left rail and it opens in the
// content area.  Every former standalone example now lives here as a page, so
// there is a single binary to build, ship and run.
//
// The window itself is a ShowcaseWindow, which derives from AppWindow -- the
// library's frameless window base class.  main() never constructs a bare
// Window: the chrome belongs to the window class, not to the startup code.
//
#include <cmath>
#include <cstdio>

#include "AppState.hpp"
#include "Pages.hpp"
#include "Shell.hpp"
#include "ShowcaseWindow.hpp"
#include "geeyoou/platform/Platform.hpp"
#include "geeyoou/render/Theme.hpp"

using namespace geeyoou;
using namespace showcase;

void AppState::startAcquisition() {
  if (running.load()) return;
  running.store(true);
  worker = std::thread([this] {
    double t = 0.0;
    while (running.load(std::memory_order_relaxed)) {
      const std::uint64_t ts = nowMs();
      // push() is the ONLY thing this thread may call on the UI side.  It never
      // touches a Widget -- docs/architecture.md section 3.11.
      hub.push(chFlow, 50.0 + 28.0 * std::sin(t * 0.7), ts);
      hub.push(chTemp, 120.0 + 45.0 * std::sin(t * 0.23 + 1.1), ts);
      hub.push(chPress, 5.0 + 3.2 * std::sin(t * 0.41 + 2.3), ts);
      t += 0.05;
      std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
  });
}

void AppState::stopAcquisition() {
  if (!running.exchange(false)) return;
  if (worker.joinable()) worker.join();
}

int main() {
  // DECLARATION ORDER IS LOAD-BEARING.
  //
  // The page builders below capture `&app`, and those lambdas live inside the
  // window's widget tree.  Destruction runs in reverse declaration order, so
  // `app` MUST be declared FIRST in order to be destroyed LAST -- otherwise the
  // tree outlives the state it points at, and teardown is a use-after-free.
  //
  // This used to be the other way round, patched by a manual
  // `app.alarmSink = nullptr;` before returning.  That patch is gone: getting
  // the order right removes the whole class of problem instead of one instance.
  AppState app;
  app.chFlow = app.hub.addChannel("进料流量", "m³/h");
  app.chTemp = app.hub.addChannel("釜内温度", "°C");
  app.chPress = app.hub.addChannel("系统压力", "MPa");
  app.startAcquisition();

  // Frameless, own title bar, own window buttons -- see ShowcaseWindow.cpp.
  // The shell inside it is sized by AppWindow::setContent, so nothing here has
  // to subscribe to resized() any more.
  ShowcaseWindow win;
  win.enableAnimations(30);

  Shell* shell = win.shell();

  shell->addPage("总览", "概览", "库的构成、分层规模与未实现清单", Icon::Info,
                 [&app](Widget* c) { return buildOverviewPage(c, app); });
  shell->addPage("", "窗口外壳", "无边框窗口、自绘标题栏与可配置属性",
                 Icon::WindowMaximize,
                 [&win](Widget* c) { return buildWindowPage(c, win); });
  shell->addPage("", "主题与皮肤", "皮肤注册表 / 主题色 / 类 QSS 选择器",
                 Icon::Sun,
                 [&win](Widget* c) { return buildThemePage(c, win); });
  shell->addPage("", "图标库", "全部内置与自定义图标 · 搜索 / 尺寸对比 / 用法",
                 Icon::Eye, [](Widget* c) { return buildIconsPage(c); });
  shell->addPage("演示画面", "HMI 监控", "仪表 / 指示灯 / 实时趋势", Icon::Play,
                 [&app](Widget* c) { return buildHmiPage(c, app); });
  shell->addPage("", "运维控制台", "报警列表 / 滚动表单 / 采集队列", Icon::Warning,
                 [&app](Widget* c) { return buildOpsPage(c, app); });
  shell->addPage("", "布局引擎", "拖窗口边缘看 stretch / min-max / 跨列的实时反应",
                 Icon::Copy, [](Widget* c) { return buildLayoutPage(c); });
  shell->addPage("控件族", "基础控件", "按钮 / 开关 / 单选 / 滑块 / 数值设定",
                 Icon::Check, [](Widget* c) { return buildWidgetsPage(c); });
  shell->addPage("", "输入与按钮", "文本输入族与按钮变体、图标按钮", Icon::Edit,
                 [](Widget* c) { return buildInputsPage(c); });
  shell->addPage("", "下拉选择", "单选 / 搜索 / 多选 / 树形 / 级联 / 菜单 / 日期",
                 Icon::ChevronDown, [](Widget* c) { return buildSelectsPage(c); });

  shell->showPage(0);

  // Draining runs on a timer of its own rather than on a page's ticker: the
  // queue must be emptied even while the operator is looking at a page that
  // does not display live data, or it silently backs up and starts dropping.
  int alarmTick = 0;
  bool pressWasHigh = false;
  bool tempWasHigh = false;
  // The id is deliberately dropped: everything this captures lives in main()'s
  // frame, which outlives the event loop, so there is nothing to stop it before.
  (void)platform().startTimer(100, [&] {
    app.hub.drain();
    ++alarmTick;

    char buf[96];
    const double press = app.hub.lastValue(app.chPress);
    const bool pressHigh = press >= 7.5;
    if (pressHigh && !pressWasHigh) {
      AlarmRecord r;
      r.timestampMs = nowMs();
      r.severity = AlarmSeverity::Critical;
      r.tag = "PI-201";
      r.message = "系统压力超过高限";
      std::snprintf(buf, sizeof(buf), "%.2f MPa", press);
      r.value = buf;
      app.raise(std::move(r));
    }
    pressWasHigh = pressHigh;

    const double temp = app.hub.lastValue(app.chTemp);
    const bool tempHigh = temp >= 150.0;
    if (tempHigh && !tempWasHigh) {
      AlarmRecord r;
      r.timestampMs = nowMs();
      r.severity = AlarmSeverity::High;
      r.tag = "TI-102";
      r.message = "釜内温度进入预警区";
      std::snprintf(buf, sizeof(buf), "%.1f °C", temp);
      r.value = buf;
      app.raise(std::move(r));
    }
    tempWasHigh = tempHigh;

    if (alarmTick % 37 == 0) {
      AlarmRecord r;
      r.timestampMs = nowMs();
      r.severity = AlarmSeverity::Low;
      r.tag = "SYS";
      r.message = "Modbus 从站响应超时，已重试";
      r.value = "-";
      app.raise(std::move(r));
    }
  });

  win.show();
  const int rc = platform().runEventLoop();

  // Stop the acquisition thread before anything unwinds -- it is the only
  // writer that outlives the event loop.  The alarm sink needs no manual
  // teardown any more: `win` is destroyed before `app`, so the widgets holding
  // that lambda are gone by the time `app` goes.
  app.stopAcquisition();
  return rc;
}
