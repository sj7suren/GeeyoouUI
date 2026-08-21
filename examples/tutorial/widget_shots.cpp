//
// Offscreen gallery for the new widgets -- a visual check that "compiles" and
// "renders correctly" are two different claims.  No window, no message loop:
// the same Canvas -> Painter -> paintTree path a real window uses.
//
//     widget_shots <output-dir>
//
#include <cstdio>
#include <string>

#include "geeyoou/hmi/Bargraph.hpp"
#include "geeyoou/render/Canvas.hpp"
#include "geeyoou/render/Offscreen.hpp"
#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Skin.hpp"
#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/Dialog.hpp"
#include "geeyoou/widget/GroupBox.hpp"
#include "geeyoou/widget/Label.hpp"
#include "geeyoou/widget/NumericKeypad.hpp"
#include "geeyoou/widget/PushButton.hpp"
#include "geeyoou/widget/TabView.hpp"

using namespace geeyoou;

namespace {

bool render(Widget& root, int w, int h, const std::string& path) {
  root.setGeometry({0, 0, float(w), float(h)});
  root.performLayout();
  OffscreenImage img(w, h, 1.0f);
  const Rect all(0.0f, 0.0f, float(w), float(h));
  Canvas canvas;
  if (!canvas.begin(img.surface(), all)) return false;
  Painter p = canvas.painter();
  p.fillRect(all, Theme::current().background);
  root.paintTree(p, all, all);
  canvas.end();
  const bool ok = writePng(img, path.c_str());
  std::printf("  %-24s %s\n", path.c_str(), ok ? "ok" : "FAIL");
  return ok;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string dir = argc > 1 ? argv[1] : ".";
  skins().apply("dark");

  // --- Bargraphs: normal, warning, alarm ---------------------------------
  {
    Widget root;
    struct Spec { const char* title; const char* unit; double lo, hi, warn, alarm, v; };
    const Spec specs[] = {
        {"进料流量", "m³/h", 0, 100, 75, 90, 62},
        {"釜内温度", "°C", 0, 200, 150, 180, 163},
        {"系统压力", "MPa", 0, 10, 7, 8.5, 8.8},
    };
    for (int i = 0; i < 3; ++i) {
      auto* b = root.add<Bargraph>();
      b->setGeometry({float(i) * 130.0f + 10.0f, 10.0f, 120.0f, 260.0f});
      b->setRange(specs[i].lo, specs[i].hi);
      b->setBands(specs[i].warn, specs[i].alarm);
      b->setTitle(specs[i].title);
      b->setUnit(specs[i].unit);
      b->setValue(specs[i].v);
    }
    render(root, 410, 280, dir + "/w-bargraph.png");
  }

  // --- TabView with three tabs -------------------------------------------
  {
    Widget root;
    auto* tv = root.add<TabView>();
    tv->setGeometry({10, 10, 460, 220});
    Widget* p0 = tv->addTab("进料");
    tv->addTab("反应");
    tv->addTab("公用工程");
    auto* g = p0->add<GroupBox>();
    g->setGeometry({16, 56, 428, 140});
    g->setTitle("进料参数");
    auto* l = g->add<Label>();
    l->setGeometry({16, 36, 380, 20});
    l->setText("当前显示第一个标签页的内容");
    render(root, 480, 240, dir + "/w-tabview.png");
  }

  // --- NumericKeypad ------------------------------------------------------
  {
    Widget root;
    auto* k = root.add<NumericKeypad>();
    k->setGeometry({10, 10, 300, 400});
    k->setPrompt("目标温度");
    k->setUnit("°C");
    k->setValue(165);
    render(root, 320, 420, dir + "/w-keypad.png");
  }

  // --- Dialog panel (scrim + panel + buttons) ----------------------------
  {
    Widget root;
    auto* dlg = root.add<Dialog>();
    dlg->setGeometry({0, 0, 640, 400});
    dlg->setTitle("确认操作");
    dlg->setPanelSize({420, 200});
    auto* msg = dlg->body()->add<Label>();
    msg->setGeometry({0, 0, 380, 60});
    msg->setText("确定要停止进料泵 P-101 吗？此操作会中断当前批次。");
    msg->setWordWrap(true);
    dlg->addButton("取消", 0, ButtonVariant::Default, false);
    dlg->addButton("停止", 1, ButtonVariant::Danger, true);
    render(root, 640, 400, dir + "/w-dialog.png");
  }

  std::printf("done\n");
  return 0;
}
