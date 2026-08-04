#include "PlantIcons.hpp"

#include "geeyoou/render/IconRegistry.hpp"
#include "geeyoou/render/Painter.hpp"

namespace showcase {

using namespace geeyoou;

void registerPlantIcons() {
  IconRegistry& reg = icons();
  if (reg.contains("pump")) return;  // already registered

  // ---------------------------------------------------------- 代码绘制 ---
  //
  // Authored in the same 24x24 grid as the built-ins, through the same
  // IconCanvas.  Nothing here knows what size it will be drawn at, so one
  // definition serves a 14px inline glyph and a 48px header mark.
  reg.add("pump",
          [](Painter&, const IconCanvas& g) {
            g.circle(12.0f, 11.0f, 6.5f);
            // Impeller: a triangle pointing at the discharge side.
            g.poly({{9.5f, 7.5f}, {16.5f, 11.0f}, {9.5f, 14.5f}, {9.5f, 7.5f}});
            g.line(9.0f, 17.4f, 9.0f, 20.0f);
            g.line(15.0f, 17.4f, 15.0f, 20.0f);
            g.line(5.5f, 20.0f, 18.5f, 20.0f);
          },
          "设备");

  reg.add("valve",
          [](Painter&, const IconCanvas& g) {
            // The two-triangle body every P&ID uses for a manual valve.
            g.poly({{3.0f, 6.5f}, {3.0f, 17.5f}, {12.0f, 12.0f}, {3.0f, 6.5f}});
            g.poly({{21.0f, 6.5f}, {21.0f, 17.5f}, {12.0f, 12.0f}, {21.0f, 6.5f}});
            g.line(12.0f, 12.0f, 12.0f, 5.0f);
            g.line(8.0f, 4.0f, 16.0f, 4.0f);  // handwheel
          },
          "设备");

  reg.add("tank",
          [](Painter&, const IconCanvas& g) {
            g.roundRect(5.0f, 3.0f, 14.0f, 18.0f, 6.0f);
            g.line(6.0f, 14.0f, 18.0f, 14.0f);  // level
          },
          "设备");

  // ------------------------------------------------------------ SVG 路径 ---
  //
  // The `d` attribute of a 24x24 <path>, exactly as an icon set ships it.  The
  // outline is parsed ONCE here and reused on every paint, which makes these
  // cheaper to draw than the hand-coded ones above.
  //
  // Between them these exercise the whole supported command set: M/m L/l H/h
  // V/v C/c S/s A/a Z.
  reg.addSvgPath("thermometer",
                 "M14 14.8V4a2 2 0 0 0-4 0v10.8a4 4 0 1 0 4 0z",
                 PathStyle::Stroke, 24.0f, "仪表");

  reg.addSvgPath("droplet", "M12 2.7l5.7 5.7a8 8 0 1 1-11.4 0z",
                 PathStyle::Stroke, 24.0f, "仪表");

  reg.addSvgPath("zap", "M13 2L3 14h9l-1 8L21 10h-9l1-8z", PathStyle::Stroke,
                 24.0f, "仪表");

  reg.addSvgPath("activity", "M22 12h-4l-3 9L9 3l-3 9H2", PathStyle::Stroke,
                 24.0f, "仪表");

  // Cubic + smooth-cubic, the two commands a hand-rolled parser usually gets
  // wrong -- Blend2D reflects the control point for `s` itself.
  reg.addSvgPath("flow", "M2 12c2-4 4-4 6 0s4 4 6 0 4-4 6 0", PathStyle::Stroke,
                 24.0f, "仪表");

  // A FILLED path, to prove the stroke/fill distinction actually matters:
  // rendering this one as a stroke would give a hairline outline, not a solid.
  reg.addSvgPath("marker", "M12 2a7 7 0 0 0-7 7c0 5.3 7 13 7 13s7-7.7 7-13a7 7 0 0 0-7-7z",
                 PathStyle::Fill, 24.0f, "仪表");
}

}  // namespace showcase
