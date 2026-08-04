#include "geeyoou/render/Icon.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <initializer_list>

#include "geeyoou/render/IconRegistry.hpp"
#include "geeyoou/render/Painter.hpp"

namespace geeyoou {
namespace {

// Authoring grid: everything below is written in 24x24 units and mapped into
// the destination box.
//
// The mapping itself now lives in IconCanvas (render/IconRegistry.hpp) so that
// externally registered icons are authored against the SAME grid as the
// built-ins.  This struct is a thin adapter over it, kept only so the 39 cases
// below can go on reading `g.at(...)` / `g.stroke` unchanged -- there is one
// source of truth for the geometry, not two.
struct Grid {
  const IconCanvas* c;
  float stroke;

  Point at(float ux, float uy) const { return c->at(ux, uy); }
  float len(float u) const { return c->len(u); }
};

void line(Painter& p, const Grid& g, float x1, float y1, float x2, float y2, Color c) {
  p.strokeLine(g.at(x1, y1), g.at(x2, y2), c, g.stroke);
}

void poly(Painter& p, const Grid& g, std::initializer_list<Point> unitPts, Color c) {
  Point buf[10];
  std::size_t n = 0;
  for (const Point& u : unitPts) {
    if (n >= 10) break;
    buf[n++] = g.at(u.x, u.y);
  }
  p.strokePolyline(buf, n, c, g.stroke);
}

void chevron(Painter& p, const Grid& g, float dx, float dy, Color c) {
  // dx/dy give the pointing direction; the two arms are perpendicular to it.
  const float cx = 12.0f, cy = 12.0f, r = 4.5f;
  const float tipX = cx + dx * r, tipY = cy + dy * r;
  const float ax = cx - dx * r + dy * r, ay = cy - dy * r + dx * r;
  const float bx = cx - dx * r - dy * r, by = cy - dy * r - dx * r;
  poly(p, g, {{ax, ay}, {tipX, tipY}, {bx, by}}, c);
}

}  // namespace

void drawIcon(Painter& p, Icon icon, const Rect& box, Color c, float strokeScale) {
  if (icon == Icon::None || box.isEmpty()) return;

  const IconCanvas canvas(p, box, c, strokeScale);

  // Registered icons come first and are dispatched by id.  An unknown id draws
  // NOTHING -- no placeholder box -- because on a plant display a stand-in
  // glyph that looks like a real symbol is worse than an empty space.
  if (std::uint16_t(icon) >= std::uint16_t(Icon::FirstCustom)) {
    icons().draw(icon, p, canvas);
    return;
  }

  const Grid g{&canvas, canvas.stroke()};

  switch (icon) {
    case Icon::Search:
      p.strokeCircle(g.at(10.5f, 10.5f), g.len(6.0f), c, g.stroke);
      line(p, g, 15.0f, 15.0f, 20.0f, 20.0f, c);
      break;

    case Icon::Close:
      line(p, g, 6.0f, 6.0f, 18.0f, 18.0f, c);
      line(p, g, 18.0f, 6.0f, 6.0f, 18.0f, c);
      break;

    case Icon::Eye:
      poly(p, g, {{2.5f, 12.0f}, {7.0f, 6.5f}, {17.0f, 6.5f}, {21.5f, 12.0f}}, c);
      poly(p, g, {{2.5f, 12.0f}, {7.0f, 17.5f}, {17.0f, 17.5f}, {21.5f, 12.0f}}, c);
      p.strokeCircle(g.at(12.0f, 12.0f), g.len(3.2f), c, g.stroke);
      break;

    case Icon::EyeOff:
      poly(p, g, {{2.5f, 12.0f}, {7.0f, 6.5f}, {17.0f, 6.5f}, {21.5f, 12.0f}}, c);
      poly(p, g, {{2.5f, 12.0f}, {7.0f, 17.5f}, {17.0f, 17.5f}, {21.5f, 12.0f}}, c);
      p.strokeCircle(g.at(12.0f, 12.0f), g.len(3.2f), c, g.stroke);
      line(p, g, 4.0f, 20.0f, 20.0f, 4.0f, c);  // the strike-through
      break;

    case Icon::Check:
      poly(p, g, {{5.0f, 12.5f}, {10.0f, 17.5f}, {19.0f, 6.5f}}, c);
      break;

    case Icon::Warning:
      poly(p, g, {{12.0f, 3.5f}, {22.0f, 20.0f}, {2.0f, 20.0f}, {12.0f, 3.5f}}, c);
      line(p, g, 12.0f, 9.5f, 12.0f, 14.5f, c);
      p.fillCircle(g.at(12.0f, 17.3f), g.len(1.0f), c);
      break;

    case Icon::Error:
      p.strokeCircle(g.at(12.0f, 12.0f), g.len(9.0f), c, g.stroke);
      line(p, g, 8.5f, 8.5f, 15.5f, 15.5f, c);
      line(p, g, 15.5f, 8.5f, 8.5f, 15.5f, c);
      break;

    case Icon::Info:
      p.strokeCircle(g.at(12.0f, 12.0f), g.len(9.0f), c, g.stroke);
      p.fillCircle(g.at(12.0f, 7.6f), g.len(1.0f), c);
      line(p, g, 12.0f, 11.0f, 12.0f, 17.0f, c);
      break;

    case Icon::Plus:
      line(p, g, 12.0f, 5.0f, 12.0f, 19.0f, c);
      line(p, g, 5.0f, 12.0f, 19.0f, 12.0f, c);
      break;

    case Icon::Minus:
      line(p, g, 5.0f, 12.0f, 19.0f, 12.0f, c);
      break;

    case Icon::ChevronUp:    chevron(p, g,  0.0f, -1.0f, c); break;
    case Icon::ChevronDown:  chevron(p, g,  0.0f,  1.0f, c); break;
    case Icon::ChevronLeft:  chevron(p, g, -1.0f,  0.0f, c); break;
    case Icon::ChevronRight: chevron(p, g,  1.0f,  0.0f, c); break;

    case Icon::Refresh:
      // Open arc plus an arrowhead: a full circle would read as "loading",
      // which is a different affordance.
      p.strokeArc(g.at(12.0f, 12.0f), g.len(7.5f), -40.0f, 280.0f, c, g.stroke, true);
      poly(p, g, {{15.5f, 2.0f}, {18.5f, 5.5f}, {14.0f, 7.0f}}, c);
      break;

    case Icon::Settings:
      p.strokeCircle(g.at(12.0f, 12.0f), g.len(3.4f), c, g.stroke);
      p.strokeCircle(g.at(12.0f, 12.0f), g.len(8.2f), c, g.stroke);
      for (int i = 0; i < 4; ++i) {
        const float a = float(i) * 90.0f + 45.0f;
        const float rad = a * 3.14159265f / 180.0f;
        const float ca = std::cos(rad), sa = std::sin(rad);
        p.strokeLine(g.at(12.0f + ca * 8.0f, 12.0f + sa * 8.0f),
                     g.at(12.0f + ca * 11.0f, 12.0f + sa * 11.0f), c, g.stroke);
      }
      break;

    case Icon::Play:
      p.fillTriangle(g.at(7.5f, 5.0f), g.at(7.5f, 19.0f), g.at(19.0f, 12.0f), c);
      break;

    case Icon::Pause:
      p.fillRect({g.at(7.5f, 5.5f), Size(g.len(3.0f), g.len(13.0f))}, c);
      p.fillRect({g.at(13.5f, 5.5f), Size(g.len(3.0f), g.len(13.0f))}, c);
      break;

    case Icon::Stop:
      p.fillRoundRect({g.at(6.5f, 6.5f), Size(g.len(11.0f), g.len(11.0f))},
                      g.len(1.5f), c);
      break;

    case Icon::Trash:
      line(p, g, 4.0f, 6.5f, 20.0f, 6.5f, c);
      poly(p, g, {{6.5f, 6.5f}, {7.5f, 20.5f}, {16.5f, 20.5f}, {17.5f, 6.5f}}, c);
      poly(p, g, {{9.0f, 6.5f}, {9.5f, 3.5f}, {14.5f, 3.5f}, {15.0f, 6.5f}}, c);
      break;

    case Icon::Save:
      poly(p, g, {{4.0f, 4.0f}, {17.0f, 4.0f}, {20.0f, 7.0f}, {20.0f, 20.0f},
                  {4.0f, 20.0f}, {4.0f, 4.0f}}, c);
      p.strokeRect({g.at(8.0f, 4.0f), Size(g.len(8.0f), g.len(5.5f))}, c, g.stroke);
      p.strokeRect({g.at(7.5f, 13.0f), Size(g.len(9.0f), g.len(7.0f))}, c, g.stroke);
      break;

    case Icon::Lock:
      p.strokeRoundRect({g.at(5.0f, 10.5f), Size(g.len(14.0f), g.len(10.0f))},
                        g.len(2.0f), c, g.stroke);
      p.strokeArc(g.at(12.0f, 10.5f), g.len(4.5f), 180.0f, 180.0f, c, g.stroke, true);
      break;

    case Icon::Unlock:
      p.strokeRoundRect({g.at(5.0f, 10.5f), Size(g.len(14.0f), g.len(10.0f))},
                        g.len(2.0f), c, g.stroke);
      p.strokeArc(g.at(17.0f, 10.5f), g.len(4.5f), 180.0f, 140.0f, c, g.stroke, true);
      break;

    case Icon::Filter:
      poly(p, g, {{3.0f, 5.0f}, {21.0f, 5.0f}, {14.0f, 13.0f}, {14.0f, 20.0f},
                  {10.0f, 17.5f}, {10.0f, 13.0f}, {3.0f, 5.0f}}, c);
      break;

    case Icon::Download:
      line(p, g, 12.0f, 3.5f, 12.0f, 15.0f, c);
      poly(p, g, {{7.0f, 10.5f}, {12.0f, 15.5f}, {17.0f, 10.5f}}, c);
      poly(p, g, {{4.0f, 16.0f}, {4.0f, 20.5f}, {20.0f, 20.5f}, {20.0f, 16.0f}}, c);
      break;

    case Icon::Upload:
      line(p, g, 12.0f, 15.5f, 12.0f, 4.0f, c);
      poly(p, g, {{7.0f, 8.5f}, {12.0f, 3.5f}, {17.0f, 8.5f}}, c);
      poly(p, g, {{4.0f, 16.0f}, {4.0f, 20.5f}, {20.0f, 20.5f}, {20.0f, 16.0f}}, c);
      break;

    case Icon::Edit:
      poly(p, g, {{4.0f, 20.0f}, {4.0f, 15.5f}, {15.5f, 4.0f}, {20.0f, 8.5f},
                  {8.5f, 20.0f}, {4.0f, 20.0f}}, c);
      line(p, g, 13.5f, 6.0f, 18.0f, 10.5f, c);
      break;

    case Icon::Copy:
      p.strokeRoundRect({g.at(8.0f, 8.0f), Size(g.len(12.0f), g.len(12.0f))},
                        g.len(2.0f), c, g.stroke);
      poly(p, g, {{16.0f, 4.0f}, {4.0f, 4.0f}, {4.0f, 16.0f}}, c);
      break;

    case Icon::Menu:
      line(p, g, 4.0f, 7.0f, 20.0f, 7.0f, c);
      line(p, g, 4.0f, 12.0f, 20.0f, 12.0f, c);
      line(p, g, 4.0f, 17.0f, 20.0f, 17.0f, c);
      break;

    // --- window chrome ------------------------------------------------------
    // Authored inside a 12-unit box (6..18) rather than the usual 3..21, so the
    // three glyphs read as one family at the ~10px they are actually drawn at.
    case Icon::WindowMinimize:
      line(p, g, 6.0f, 12.0f, 18.0f, 12.0f, c);
      break;

    case Icon::WindowMaximize:
      p.strokeRect({g.at(6.0f, 6.0f), Size(g.len(12.0f), g.len(12.0f))}, c, g.stroke);
      break;

    case Icon::WindowRestore:
      // Front sheet solid, back sheet only where it peeks out: drawing the back
      // one as a full square would show a line THROUGH the front one.
      p.strokeRect({g.at(5.0f, 9.0f), Size(g.len(10.0f), g.len(10.0f))}, c, g.stroke);
      poly(p, g, {{8.5f, 9.0f}, {8.5f, 5.0f}, {19.0f, 5.0f}, {19.0f, 15.5f},
                  {15.0f, 15.5f}}, c);
      break;

    // --- admin-console header -----------------------------------------------
    case Icon::User:
      p.strokeCircle(g.at(12.0f, 8.5f), g.len(3.9f), c, g.stroke);
      // Shoulders as the top half of a circle centred below the frame, which is
      // what keeps the silhouette readable when the avatar is only 16px wide.
      p.strokeArc(g.at(12.0f, 21.5f), g.len(7.4f), 180.0f, 180.0f, c, g.stroke, true);
      break;

    case Icon::Globe:
      p.strokeCircle(g.at(12.0f, 12.0f), g.len(9.0f), c, g.stroke);
      line(p, g, 3.0f, 12.0f, 21.0f, 12.0f, c);
      // The meridian is an ellipse, which the Painter facade has no primitive
      // for; two wide arcs struck from off-canvas centres give the same read.
      p.strokeArc(g.at(-4.0f, 12.0f), g.len(18.36f), -29.4f, 58.8f, c, g.stroke);
      p.strokeArc(g.at(28.0f, 12.0f), g.len(18.36f), 150.6f, 58.8f, c, g.stroke);
      break;

    case Icon::Bell:
      p.strokeArc(g.at(12.0f, 11.5f), g.len(7.0f), 180.0f, 180.0f, c, g.stroke);
      line(p, g, 5.0f, 11.5f, 5.0f, 17.5f, c);
      line(p, g, 19.0f, 11.5f, 19.0f, 17.5f, c);
      line(p, g, 3.5f, 17.5f, 20.5f, 17.5f, c);
      p.strokeArc(g.at(12.0f, 17.8f), g.len(2.4f), 0.0f, 180.0f, c, g.stroke, true);
      break;

    case Icon::Logout:
      poly(p, g, {{13.0f, 4.0f}, {5.0f, 4.0f}, {5.0f, 20.0f}, {13.0f, 20.0f}}, c);
      line(p, g, 10.0f, 12.0f, 20.5f, 12.0f, c);
      poly(p, g, {{16.5f, 8.0f}, {20.5f, 12.0f}, {16.5f, 16.0f}}, c);
      break;

    case Icon::Sun:
      p.strokeCircle(g.at(12.0f, 12.0f), g.len(4.4f), c, g.stroke);
      for (int i = 0; i < 8; ++i) {
        const float rad = float(i) * 45.0f * 3.14159265f / 180.0f;
        const float ca = std::cos(rad), sa = std::sin(rad);
        p.strokeLine(g.at(12.0f + ca * 7.2f, 12.0f + sa * 7.2f),
                     g.at(12.0f + ca * 9.8f, 12.0f + sa * 9.8f), c, g.stroke);
      }
      break;

    case Icon::Moon:
      // Crescent = one circle minus another.  With no boolean path ops in the
      // facade, the two arcs between the circles' intersection points ARE the
      // outline -- the angles below are those intersections, precomputed.
      p.strokeArc(g.at(12.0f, 12.0f), g.len(8.5f), 29.6f, 210.8f, c, g.stroke, true);
      p.strokeArc(g.at(17.0f, 7.0f), g.len(9.5f), 75.4f, 119.2f, c, g.stroke, true);
      break;

    // Listed rather than defaulted, so adding an enumerator without drawing it
    // is a compiler warning instead of a blank button someone finds in the field.
    case Icon::None:
    case Icon::FirstCustom:
      break;
  }
}

}  // namespace geeyoou
