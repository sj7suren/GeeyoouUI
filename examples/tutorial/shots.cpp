//
// 把教程的每一步离屏渲染成 PNG。
//
// 没有窗口，没有消息循环，没有显示器 —— 走的是和真实窗口完全相同的
// Canvas -> Painter -> paintTree 路径（见 render/Offscreen.hpp 的说明）。
// 所以这些图不是"示意图"，就是那份代码画出来的像素。
//
// 用法：  tutorial_shots <输出目录>
//
#include <cstdio>
#include <string>

#include "ReactorScreen.hpp"
#include "geeyoou/render/Canvas.hpp"
#include "geeyoou/render/Offscreen.hpp"
#include "geeyoou/render/Offscreen.hpp"
#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Skin.hpp"
#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/Widget.hpp"

using namespace geeyoou;

namespace {

// 四周留白，这样控件不会贴着图片边缘 —— 教程配图贴边会显得像截错了。
constexpr float kPad = 24.0f;

bool shoot(const char* name, Size (*build)(Widget*), const char* outDir) {
  Widget root;
  const Size design = build(&root);

  const int w = int(design.width + kPad * 2.0f);
  const int h = int(design.height + kPad * 2.0f);

  // 控件是按 (0,0) 起始摆的，整体挪进留白里。
  for (const auto& child : root.children()) {
    const Rect g = child->geometry();
    child->setGeometry({g.x() + kPad, g.y() + kPad, g.width(), g.height()});
  }

  root.setGeometry({0, 0, float(w), float(h)});
  root.performLayout();

  OffscreenImage img(w, h, 1.0f);
  const Rect all(0.0f, 0.0f, float(w), float(h));

  Canvas canvas;
  if (!canvas.begin(img.surface(), all)) {
    std::printf("  [fail] canvas.begin for %s\n", name);
    return false;
  }
  Painter p = canvas.painter();
  p.fillRect(all, Theme::current().background);
  root.paintTree(p, all, all);
  canvas.end();

  const std::string path = std::string(outDir) + "/" + name + ".png";
  if (!writePng(img, path.c_str())) {
    std::printf("  [fail] writePng %s\n", path.c_str());
    return false;
  }
  std::printf("  %-28s %4d x %4d\n", (std::string(name) + ".png").c_str(), w, h);
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  const char* outDir = argc > 1 ? argv[1] : ".";

  struct Shot {
    const char* name;
    Size (*build)(Widget*);
  };
  const Shot kShots[] = {
      {"step1-gauge", &tutorial::buildStep1},
      {"step2-three-gauges", &tutorial::buildStep2},
      {"step3-status-panel", &tutorial::buildStep3},
      {"step4-full-screen", &tutorial::buildStep4},
  };

  // 深浅两套皮肤各出一份。教程正文用深色，换肤那一节用浅色 —— 而且这本身
  // 就是"一个颜色带动整套配色"最省口舌的证明。
  struct SkinRun {
    const char* skin;
    const char* suffix;
  };
  const SkinRun kSkins[] = {{"dark", ""}, {"light", "-light"}};

  int failed = 0;
  for (const SkinRun& s : kSkins) {
    if (!skins().apply(s.skin)) {
      std::printf("[warn] no such skin: %s\n", s.skin);
      continue;
    }
    std::printf("skin %s:\n", s.skin);
    for (const Shot& shot : kShots) {
      const std::string name = std::string(shot.name) + s.suffix;
      if (!shoot(name.c_str(), shot.build, outDir)) ++failed;
    }
  }

  std::printf("%s\n", failed ? "FAILED" : "ok");
  return failed ? 1 : 0;
}
