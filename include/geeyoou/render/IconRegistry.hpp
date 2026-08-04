#pragma once
//
// Collecting and extending icons.
//
// The built-in set in Icon.hpp covers generic UI affordances.  What it cannot
// cover is the DOMAIN: a batching plant wants a pump, a valve, a reactor and a
// conveyor, and no general-purpose icon set will ever ship the exact schematic
// symbol a customer's P&ID standard calls for.
//
// Two ways in, both producing an ordinary `Icon` value:
//
//   1. A drawing callback, in the same 24x24 authoring grid the built-ins use.
//      Best for domain symbols you are inventing.
//
//        icons().add("pump", [](Painter&, const IconCanvas& g) {
//          g.circle(12, 12, 7);
//          g.poly({{12, 5}, {19, 12}, {12, 19}});
//        }, "设备");
//
//   2. SVG path data -- the `d` attribute of an <svg><path>.  Best for adopting
//      an existing set.  Lucide, Feather, Tabler and Material are all authored
//      on a 24x24 grid with 2-unit strokes, which is EXACTLY this library's
//      authoring grid, so their paths drop straight in.
//
//        icons().addSvgPath("valve", "M4 12h4l4-6 4 12 4-6h4");
//
// Because both return an `Icon`, every existing API keeps working unchanged:
//
//        btn->setIcon(icons().find("pump"));
//
// Names also make the built-ins addressable ("warning", "chevron-down"), which
// is what a config file, a settings screen or an icon picker needs.
//
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "geeyoou/core/Types.hpp"
#include "geeyoou/render/Icon.hpp"
#include "geeyoou/render/VectorPath.hpp"

namespace geeyoou {

class Painter;

// The 24x24 authoring grid, made public.
//
// This used to be a private helper inside Icon.cpp, which is precisely why
// icons could not be extended from outside: an external author would have had
// to re-derive the unit -> device mapping, the stroke weight and the
// centre-the-largest-square-that-fits rule by hand, and get all three right.
class IconCanvas {
 public:
  IconCanvas(Painter& p, const Rect& box, Color color, float strokeScale);

  // --- the mapping ---------------------------------------------------------
  Point at(float ux, float uy) const;  // 24x24 authoring units -> device
  float len(float u) const;            // a length in units -> device
  float unitScale() const { return scale_; }
  // Device stroke width, already clamped so a small icon never dissolves below
  // one physical pixel.
  float stroke() const { return stroke_; }
  Color color() const { return color_; }
  Painter& painter() const { return p_; }
  // The square actually drawn into, after centring inside the caller's box.
  Rect square() const;

  // --- primitives, all in authoring units ---------------------------------
  void line(float x1, float y1, float x2, float y2) const;
  void poly(std::initializer_list<Point> unitPts) const;
  void circle(float cx, float cy, float r) const;
  void fillCircle(float cx, float cy, float r) const;
  void rect(float x, float y, float w, float h) const;
  void fillRect(float x, float y, float w, float h) const;
  void roundRect(float x, float y, float w, float h, float r) const;
  void fillRoundRect(float x, float y, float w, float h, float r) const;
  void arc(float cx, float cy, float r, float startDeg, float sweepDeg,
           bool roundCap = false) const;
  void triangle(Point a, Point b, Point c) const;  // filled

  // Draws an outline authored in a `viewBox` x `viewBox` box, mapped onto this
  // canvas.  Stroke width is compensated for the scale, so an SVG icon comes
  // out at the same visual weight as a hand-coded one.
  void path(const VectorPath& p, PathStyle style, float viewBox = 24.0f) const;

 private:
  Painter& p_;
  Rect box_;
  float x0_, y0_, scale_, stroke_;
  Color color_;
};

using IconDrawer = std::function<void(Painter&, const IconCanvas&)>;

struct IconEntry {
  Icon id = Icon::None;
  std::string name;
  std::string category;
  bool builtin = false;
};

class IconRegistry {
 public:
  static IconRegistry& instance();

  // --- registering ---------------------------------------------------------
  //
  // Re-registering an existing name REPLACES its drawing but keeps its id, so
  // a widget that already holds the handle picks up the new artwork instead of
  // silently going blank.
  Icon add(std::string name, IconDrawer draw, std::string category = {});

  // `viewBox` is the side of the square the path was authored in; 24 for every
  // mainstream icon set.  Parse failures register nothing and are reported
  // through errors() -- icon data is content, and bad content must not throw.
  Icon addSvgPath(std::string name, std::string_view svgPathData,
                  PathStyle style = PathStyle::Stroke, float viewBox = 24.0f,
                  std::string category = {});

  bool remove(std::string_view name);

  // --- looking up ----------------------------------------------------------
  Icon find(std::string_view name) const;  // Icon::None when unknown
  std::string name(Icon id) const;         // "" when unknown; built-ins included
  bool contains(std::string_view name) const;

  // --- collecting ----------------------------------------------------------
  // Everything registered, built-ins first, for a picker or a gallery.
  std::vector<IconEntry> all() const;
  std::vector<std::string> categories() const;
  std::size_t customCount() const { return custom_.size(); }

  const std::vector<std::string>& errors() const { return errors_; }
  void clearErrors() { errors_.clear(); }

  // Called by drawIcon for ids >= Icon::FirstCustom.  False when unregistered.
  bool draw(Icon id, Painter& p, const IconCanvas& canvas) const;

 private:
  IconRegistry() = default;

  struct Custom {
    Icon id = Icon::None;
    std::string name;
    std::string category;
    IconDrawer drawer;
  };

  std::vector<Custom> custom_;
  std::vector<std::string> errors_;
  std::uint16_t nextId_ = std::uint16_t(Icon::FirstCustom);
};

inline IconRegistry& icons() { return IconRegistry::instance(); }

// Name of a built-in enumerator ("warning", "chevron-down"), or "" if `id` is
// not a built-in.  Kebab-case, matching the naming most icon sets use, so a
// name written against Lucide usually resolves against the built-ins too.
std::string_view builtinIconName(Icon id);
Icon builtinIconFromName(std::string_view name);

}  // namespace geeyoou
