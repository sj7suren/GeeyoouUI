//
// Offscreen golden-image tests.
//
// Two baseline groups, and the split is load-bearing rather than tidy:
//
//   baseline/shape/ -- no text at all.  Blend2D rasterises the same geometry to
//                      the same bytes everywhere, so these run at ZERO pixel
//                      tolerance and a mismatch FAILS the build.
//   baseline/text/  -- contains text.  Glyph rasterisation depends on which
//                      font file the machine has and on the hinting the font
//                      itself carries, so a byte comparison across machines is
//                      not a defect signal.  These WARN and never fail.
//
// A single mixed group would force one of two bad outcomes: a tolerance loose
// enough to hide a real geometry regression, or a red build on every machine
// that has a different Windows font package installed.
//
// Rewriting: run with GEEYOOU_UPDATE_BASELINE=1.  That mode deliberately exits
// non-zero -- a rewritten baseline is a human review task, not a pass.
//
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

#include "framework/Test.hpp"
#include "geeyoou/render/Canvas.hpp"
#include "geeyoou/render/Icon.hpp"
#include "geeyoou/render/Offscreen.hpp"
#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/StyleSheet.hpp"
#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/Label.hpp"
#include "geeyoou/widget/ProgressBar.hpp"
#include "geeyoou/widget/PushButton.hpp"
#include "geeyoou/widget/Widget.hpp"

using geeyoou::ButtonVariant;
using geeyoou::Canvas;
using geeyoou::Color;
using geeyoou::Icon;
using geeyoou::Label;
using geeyoou::OffscreenImage;
using geeyoou::Painter;
using geeyoou::Point;
using geeyoou::ProgressBar;
using geeyoou::PushButton;
using geeyoou::Rect;
using geeyoou::Theme;
using geeyoou::Widget;

namespace {

enum class Gate { Hard, WarnOnly };

bool envFlagOn(const char* name) {
#ifdef _MSC_VER
  std::size_t len = 0;
  char buf[16] = {};
  if (getenv_s(&len, buf, sizeof(buf), name) != 0) return false;
  return len > 1 && buf[0] != '0';
#else
  const char* v = std::getenv(name);
  return v && v[0] != '\0' && v[0] != '0';
#endif
}

bool updateMode() {
  static const bool on = envFlagOn("GEEYOOU_UPDATE_BASELINE");
  return on;
}

// Everything a deterministic render has to pin down.  Called at the top of each
// case rather than once globally, so no case can be made to pass or fail by the
// case that happened to run before it.
void resetStyling() {
  Theme::current() = Theme();
  geeyoou::activeStyleSheet().clear();
  geeyoou::bumpStyleGeneration();  // clear() does not bump on its own
}

// The whole point of Offscreen: a detached widget tree renders through exactly
// the same Canvas -> Painter -> paintTree path a real window uses.  No Window,
// no message loop, no display.
OffscreenImage renderTree(Widget& root, int width, int height, Color background) {
  OffscreenImage img(width, height, 1.0f);
  // dpr is 1, so the physical dirty rect and the logical window rect are the
  // same numbers.  Anything else would need the two spelled separately.
  const Rect all(0.0f, 0.0f, float(width), float(height));

  Canvas canvas;
  if (!canvas.begin(img.surface(), all)) return OffscreenImage();
  Painter p = canvas.painter();
  p.fillRect(all, background);  // opaque: see the alpha check in compareGolden
  root.paintTree(p, all, all);
  canvas.end();
  return img;
}

std::string baselinePath(const char* group, const char* name) {
  return std::string(GEEYOOU_BASELINE_DIR) + "/" + group + "/" + name + ".png";
}

void compareGolden(geeyoou::test::Context& ctx, const char* file, int line,
                   const char* group, const char* name, const OffscreenImage& got,
                   Gate gate) {
  const std::string path = baselinePath(group, name);

  if (!got.valid()) {
    ctx.fail(file, line, std::string("离屏渲染失败：") + name);
    return;
  }
  // A translucent baseline cannot be a zero-tolerance gate: PNG stores straight
  // alpha, so a premultiplied buffer does not survive the round trip bit for
  // bit.  Caught here, loudly, rather than as an intermittent failure later.
  for (int y = 0; y < got.height(); ++y) {
    for (int x = 0; x < got.width(); ++x) {
      if ((got.pixel(x, y) >> 24) != 0xFFu) {
        ctx.fail(file, line,
                 std::string("基线必须完全不透明，但 ") + name + " 在 (" +
                     std::to_string(x) + "," + std::to_string(y) + ") 的 alpha 不是 255");
        return;
      }
    }
  }

  if (updateMode()) {
    std::error_code ec;
    std::filesystem::create_directories(
        std::filesystem::path(path).parent_path(), ec);
    if (!geeyoou::writePng(got, path.c_str())) {
      ctx.fail(file, line, "写入基线失败：" + path);
    } else {
      std::printf("    [基线] 已重写 %s\n", path.c_str());
    }
    return;
  }

  OffscreenImage want;
  if (!geeyoou::readPng(path.c_str(), want)) {
    const std::string msg = "缺少基线 " + path +
                            "（用 GEEYOOU_UPDATE_BASELINE=1 生成后人工核对）";
    if (gate == Gate::Hard) {
      ctx.fail(file, line, msg);
    } else {
      geeyoou::test::note("[warn] " + msg);
    }
    return;
  }

  std::string problem;
  if (want.width() != got.width() || want.height() != got.height()) {
    problem = "尺寸不符：基线 " + std::to_string(want.width()) + "x" +
              std::to_string(want.height()) + "，实际 " +
              std::to_string(got.width()) + "x" + std::to_string(got.height());
  } else {
    std::size_t differing = 0;
    int firstX = -1;
    int firstY = -1;
    for (int y = 0; y < got.height(); ++y) {
      for (int x = 0; x < got.width(); ++x) {
        if (got.pixel(x, y) == want.pixel(x, y)) continue;
        ++differing;
        if (firstX < 0) {
          firstX = x;
          firstY = y;
        }
      }
    }
    if (differing != 0) {
      char buf[192];
      std::snprintf(buf, sizeof(buf),
                    "%zu 个像素不符，首个在 (%d,%d)：基线 %08X，实际 %08X",
                    differing, firstX, firstY, want.pixel(firstX, firstY),
                    got.pixel(firstX, firstY));
      problem = buf;
    }
  }
  if (problem.empty()) return;

  // Dump what we actually drew next to the baseline so a human can look at the
  // two images instead of at a pixel coordinate.
  const std::string actual = path.substr(0, path.size() - 4) + ".actual.png";
  const bool dumped = geeyoou::writePng(got, actual.c_str());
  const std::string full = std::string(group) + "/" + name + "：" + problem +
                           (dumped ? ("，实际图已写入 " + actual) : std::string());

  if (gate == Gate::Hard) {
    ctx.fail(file, line, full);
  } else {
    // Text rasterisation is machine-dependent by nature; a diff here is a
    // prompt to look, not a defect.
    geeyoou::test::note("[warn] 文本基线不符 " + full);
  }
}

// ---------------------------------------------------------------------------
// Probe widgets.  These exist so a baseline can cover the Painter primitives
// and the icon set WITHOUT dragging a real widget's layout logic into the
// picture -- a change to PushButton's padding should not repaint the primitive
// baseline.
class PrimitivesProbe : public Widget {
 protected:
  void onPaint(Painter& p, const Rect&) override {
    p.fillRoundRect({8.0f, 8.0f, 80.0f, 40.0f}, 10.0f, Color::rgb(0x2F, 0xA8, 0xFF));
    p.strokeRoundRect({96.0f, 8.0f, 80.0f, 40.0f}, 10.0f, Color::rgb(0xFF, 0xB0, 0x20),
                      2.0f);
    p.fillCircle({28.0f, 80.0f}, 18.0f, Color::rgb(0x3E, 0xD1, 0x7A));
    p.strokeCircle({76.0f, 80.0f}, 18.0f, Color::rgb(0xFF, 0x4D, 0x5E), 3.0f);
    p.strokeArc({134.0f, 80.0f}, 22.0f, -210.0f, 240.0f, Color::rgb(0xE6, 0xEB, 0xF4),
                4.0f, true);
    p.fillArcRing({134.0f, 80.0f}, 15.0f, 8.0f, -210.0f, 120.0f,
                  Color::rgb(0x2F, 0xA8, 0xFF));
    const Point line[] = {{8.0f, 122.0f},  {40.0f, 142.0f},  {72.0f, 114.0f},
                          {104.0f, 138.0f}, {136.0f, 118.0f}, {168.0f, 134.0f}};
    p.strokePolyline(line, 6, Color::rgb(0x86, 0x94, 0xAD), 2.0f);
    p.fillTriangle({20.0f, 176.0f}, {60.0f, 176.0f}, {40.0f, 152.0f},
                   Color::rgb(0xFF, 0x4D, 0x5E));
    p.strokeLine({80.0f, 164.0f}, {176.0f, 164.0f}, Color::rgb(0x2C, 0x37, 0x4C), 1.0f);
    p.strokeLine({80.0f, 176.0f}, {176.0f, 176.0f}, Color::rgb(0x2C, 0x37, 0x4C), 3.0f);
  }
};

// A flat rectangle of one colour, filling its own local rect.  Deliberately
// featureless: the case it serves is about WHERE the paint lands, not what is
// drawn, and any decoration would only add pixels that a real regression could
// hide behind.
class ClipBox : public Widget {
 public:
  explicit ClipBox(Color c) : color_(c) {}

 protected:
  void onPaint(Painter& p, const Rect&) override { p.fillRect(localRect(), color_); }

 private:
  Color color_;
};

class IconStrip : public Widget {
 protected:
  void onPaint(Painter& p, const Rect&) override {
    static const Icon kIcons[] = {Icon::Search, Icon::Check,  Icon::Warning,
                                  Icon::Settings, Icon::Play, Icon::ChevronDown,
                                  Icon::Lock,   Icon::Refresh};
    float x = 6.0f;
    for (const Icon icon : kIcons) {
      drawIcon(p, icon, Rect(x, 6.0f, 28.0f, 28.0f), Color::rgb(0xE6, 0xEB, 0xF4));
      // Second row at half size: icons are authored in a 24x24 box and scaled,
      // so a size change is exactly where a bad transform would show up.
      drawIcon(p, icon, Rect(x + 7.0f, 40.0f, 14.0f, 14.0f),
               Color::rgb(0x2F, 0xA8, 0xFF));
      x += 34.0f;
    }
  }
};

}  // namespace

#define GOLDEN(group, name, img, gate) \
  compareGolden(ctx_, __FILE__, __LINE__, group, name, img, gate)

// ------------------------------------------------------------- shape group ---
GEEYOOU_TEST(golden, shape_primitives) {
  resetStyling();
  PrimitivesProbe probe;
  probe.setGeometry({0.0f, 0.0f, 184.0f, 192.0f});
  const OffscreenImage img =
      renderTree(probe, 184, 192, Color::rgb(0x12, 0x16, 0x1D));
  GOLDEN("shape", "primitives", img, Gate::Hard);
}

GEEYOOU_TEST(golden, shape_icons) {
  resetStyling();
  IconStrip strip;
  strip.setGeometry({0.0f, 0.0f, 278.0f, 60.0f});
  const OffscreenImage img =
      renderTree(strip, 278, 60, Color::rgb(0x12, 0x16, 0x1D));
  GOLDEN("shape", "icons", img, Gate::Hard);
}

GEEYOOU_TEST(golden, shape_clip_inheritance) {
  // Every other baseline in this group draws a root that fills the canvas with
  // children entirely inside it, and for that shape `mine.intersected(clip)` is
  // the identity -- so deleting the intersection from Widget::paintTree changes
  // not one pixel of any of them.  That is a hole in the visual gate, and this
  // scene is what closes it: the orange child OVERFLOWS its 100x100 parent by
  // 150 pixels in both directions, and only the inherited clip keeps it inside.
  //
  // Losing that clip is not cosmetic.  It is what stops a scrolled ListView row
  // from painting over the panel that contains it, and a dropdown row from
  // spilling across the screen behind it.
  resetStyling();
  Widget root;
  root.setGeometry({0.0f, 0.0f, 200.0f, 200.0f});

  ClipBox* panel = root.add<ClipBox>(Color::rgb(0x2F, 0xA8, 0xFF));
  panel->setGeometry({20.0f, 20.0f, 100.0f, 100.0f});

  // Runs off the parent's right and bottom edges: without the inherited clip it
  // would cover most of the canvas instead of a 50x50 corner.
  ClipBox* overflow = panel->add<ClipBox>(Color::rgb(0xFF, 0xB0, 0x20));
  overflow->setGeometry({50.0f, 50.0f, 200.0f, 200.0f});

  // A sibling that stays inside, so the baseline pins down the ordinary case in
  // the same picture -- an over-eager clip would show up here.
  ClipBox* inside = panel->add<ClipBox>(Color::rgb(0x3E, 0xD1, 0x7A));
  inside->setGeometry({10.0f, 10.0f, 25.0f, 25.0f});

  const OffscreenImage img =
      renderTree(root, 200, 200, Color::rgb(0x12, 0x16, 0x1D));
  GOLDEN("shape", "clip_inheritance", img, Gate::Hard);
}

GEEYOOU_TEST(golden, shape_button_variants) {
  resetStyling();
  Widget root;
  root.setGeometry({0.0f, 0.0f, 420.0f, 56.0f});

  // No text and no icon on purpose -- this baseline is about the fill, border
  // and corner radius of each variant, and text belongs in the other group.
  const ButtonVariant kVariants[] = {ButtonVariant::Default, ButtonVariant::Primary,
                                     ButtonVariant::Success, ButtonVariant::Danger};
  float x = 10.0f;
  for (const ButtonVariant v : kVariants) {
    PushButton* b = root.add<PushButton>();
    b->setVariant(v);
    b->setGeometry({x, 10.0f, 90.0f, 36.0f});
    x += 100.0f;
  }
  // The last one greys out: the disabled palette is its own code path.
  root.children().back()->setEnabled(false);

  const OffscreenImage img =
      renderTree(root, 420, 56, Color::rgb(0x12, 0x16, 0x1D));
  GOLDEN("shape", "button_variants", img, Gate::Hard);
}

GEEYOOU_TEST(golden, shape_progress_bars) {
  resetStyling();
  Widget root;
  root.setGeometry({0.0f, 0.0f, 260.0f, 96.0f});

  const double kValues[] = {0.0, 1.0, 35.0, 100.0};
  float y = 10.0f;
  for (const double value : kValues) {
    ProgressBar* bar = root.add<ProgressBar>();
    bar->setTextVisible(false);  // shape group: no glyphs
    bar->setValue(value);
    bar->setGeometry({12.0f, y, 236.0f, 14.0f});
    y += 22.0f;
  }
  // 1% is the case that used to render as a pinched rounded rect -- the clamp
  // in ProgressBar::onPaint is what this row is here to hold in place.

  const OffscreenImage img =
      renderTree(root, 260, 96, Color::rgb(0x12, 0x16, 0x1D));
  GOLDEN("shape", "progress_bars", img, Gate::Hard);
}

// -------------------------------------------------------------- text group ---
GEEYOOU_TEST(golden, text_label_and_button) {
  resetStyling();
  Widget root;
  root.setGeometry({0.0f, 0.0f, 300.0f, 96.0f});

  Label* label = root.add<Label>();
  label->setText("泵 P-101 运行中");
  label->setGeometry({12.0f, 8.0f, 276.0f, 24.0f});

  PushButton* button = root.add<PushButton>();
  button->setVariant(ButtonVariant::Primary);
  button->setText("确认");
  button->setGeometry({12.0f, 44.0f, 120.0f, 36.0f});

  ProgressBar* bar = root.add<ProgressBar>();
  bar->setValue(62.0);
  bar->setGeometry({148.0f, 52.0f, 140.0f, 20.0f});

  const OffscreenImage img =
      renderTree(root, 300, 96, Color::rgb(0x12, 0x16, 0x1D));
  // WARN ONLY: which font file this machine has decides these bytes.
  GOLDEN("text", "label_and_button", img, Gate::WarnOnly);
}

// ------------------------------------------------------- offscreen plumbing ---
GEEYOOU_TEST(golden, png_round_trip_is_lossless_for_opaque_pixels) {
  OffscreenImage img(7, 5, 1.0f);
  REQUIRE(img.valid());
  CHECK_EQ(img.stride(), std::intptr_t(28));
  CHECK_EQ(img.pixel(0, 0), 0u);  // resize() zero-fills

  for (int y = 0; y < 5; ++y) {
    for (int x = 0; x < 7; ++x) {
      img.pixels()[std::size_t(y) * 7u + std::size_t(x)] =
          0xFF000000u | std::uint32_t(x * 30) << 16 | std::uint32_t(y * 50) << 8 |
          std::uint32_t(x + y);
    }
  }

  std::error_code ec;
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path(ec) / "geeyoou_tests";
  std::filesystem::create_directories(dir, ec);
  const std::string path = (dir / "roundtrip.png").string();

  REQUIRE(geeyoou::writePng(img, path.c_str()));
  OffscreenImage back;
  REQUIRE(geeyoou::readPng(path.c_str(), back));

  CHECK_EQ(back.width(), 7);
  CHECK_EQ(back.height(), 5);
  std::size_t differing = 0;
  for (int y = 0; y < 5; ++y) {
    for (int x = 0; x < 7; ++x) {
      if (img.pixel(x, y) != back.pixel(x, y)) ++differing;
    }
  }
  CHECK_EQ(differing, std::size_t(0));

  // Failure is reported, never thrown -- an export button on a plant display
  // must not terminate the process because a directory is read-only.
  const std::string nowhere = (dir / "no_such_dir" / "x.png").string();
  CHECK(!geeyoou::writePng(img, nowhere.c_str()));
  OffscreenImage missing;
  CHECK(!geeyoou::readPng(nowhere.c_str(), missing));
  CHECK(!geeyoou::writePng(OffscreenImage(), path.c_str()));
  CHECK(!geeyoou::writePng(img, nullptr));
  CHECK(!geeyoou::readPng(nullptr, missing));

  std::filesystem::remove(path, ec);
}

GEEYOOU_TEST(golden, baseline_rewrite_mode_is_not_a_pass) {
  // Registered as a normal case so the banner appears exactly once, next to the
  // summary, instead of being buried in whichever golden case ran first.
  if (updateMode()) {
    geeyoou::test::invalidateRun(
        "基线已重写，本次运行不构成通过：请人工核对 tests/visual/baseline/ 下的图像后再提交");
  }
}
