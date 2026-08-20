#pragma once
//
// 教程《30 分钟做一个反应釜监控画面》的四个步骤。
//
// 每一步只往 `content` 里塞控件并返回这一步的设计尺寸 —— 它们不知道自己被画到
// 哪里去。这一点是刻意的，也是整个教程配图机制的基础：
//
//   * tutorial.cpp   把最后一步塞进 AppWindow，是读者真正跑起来的程序；
//   * shots.cpp      把每一步离屏渲染成 PNG，是教程里的配图。
//
// 同一份代码同时是"能跑的示例"和"文档里的图"，所以图不可能和代码对不上 ——
// 教程配图最常见的腐烂方式就是代码改了而截图没跟着改。
//
#include "geeyoou/core/Types.hpp"
#include "geeyoou/widget/Widget.hpp"

namespace tutorial {

using geeyoou::Size;
using geeyoou::Widget;

// 教程里出现的四张图，按顺序。
Size buildStep1(Widget* content);  // 一个仪表
Size buildStep2(Widget* content);  // 三个仪表并排
Size buildStep3(Widget* content);  // + 设备状态面板
Size buildStep4(Widget* content);  // + 实时趋势 + 操作按钮（完整画面）

// 给 step4 建好的画面接上一个假数据源，让它自己动起来。
//
// 教程正文里用的是真的采集线程 + DataHub；这里用一个正弦波，是为了让
// 「跑起来能看见东西」这件事不依赖任何硬件。
void animate(Widget* content, double t);

}  // namespace tutorial
