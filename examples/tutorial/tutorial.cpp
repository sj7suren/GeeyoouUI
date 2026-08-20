//
// 《30 分钟做一个反应釜监控画面》的成品程序。
//
// 教程正文一步步解释这里的每一段；这个文件是它们拼起来之后的样子，可以直接
// 编译运行：
//
//     build\bin\tutorial.exe
//
#include <cmath>

#include "ReactorScreen.hpp"
#include "geeyoou/platform/Platform.hpp"
#include "geeyoou/widget/AppWindow.hpp"

using namespace geeyoou;

int main() {
  // 无边框窗口 + 自绘标题栏。Windows 不再画标题栏、边框和主题色，
  // 但贴边分屏、双击最大化、Alt+Tab 全都还在。
  AppWindow win("反应釜监控", 1140, 700);
  win.header()->setIcon(Icon::Settings);
  win.header()->setTitle("反应釜监控");
  win.header()->setSubtitle("2# 反应釜 · 在线");

  // 30 fps 的动画时钟。仪表的指针、报警灯的闪烁、趋势曲线都靠它推进。
  win.enableAnimations(30);

  auto* content = win.content()->add<Widget>();
  content->setGeometry({24, 24, 1072, 620});
  tutorial::buildStep4(content);

  // 教程正文里这里接的是真的采集线程 + DataHub；成品程序用一个正弦波，
  // 好让它不依赖任何硬件就能跑起来。
  double t = 0.0;
  (void)platform().startTimer(66, [content, &t] {
    t += 0.05;
    tutorial::animate(content, t);
  });

  win.show();
  return platform().runEventLoop();
}
