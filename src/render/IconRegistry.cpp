#include "geeyoou/render/IconRegistry.hpp"

#include <algorithm>
#include <cmath>

#include "geeyoou/render/Painter.hpp"

namespace geeyoou {
namespace {

// Built-in names.  Kebab-case on purpose: it is what Lucide, Feather and Tabler
// use, so a name copied from one of those sets usually resolves here too --
// find("chevron-down") works without anybody having to learn our spelling.
struct BuiltinName {
  Icon id;
  const char* name;
  const char* category;
};

const BuiltinName kBuiltins[] = {
    {Icon::Search, "search", "通用"},
    {Icon::Close, "close", "通用"},
    {Icon::Eye, "eye", "通用"},
    {Icon::EyeOff, "eye-off", "通用"},
    {Icon::Check, "check", "通用"},
    {Icon::Warning, "warning", "状态"},
    {Icon::Error, "error", "状态"},
    {Icon::Info, "info", "状态"},
    {Icon::Plus, "plus", "通用"},
    {Icon::Minus, "minus", "通用"},
    {Icon::ChevronUp, "chevron-up", "箭头"},
    {Icon::ChevronDown, "chevron-down", "箭头"},
    {Icon::ChevronLeft, "chevron-left", "箭头"},
    {Icon::ChevronRight, "chevron-right", "箭头"},
    {Icon::Refresh, "refresh", "动作"},
    {Icon::Settings, "settings", "动作"},
    {Icon::Play, "play", "动作"},
    {Icon::Pause, "pause", "动作"},
    {Icon::Stop, "stop", "动作"},
    {Icon::Trash, "trash", "动作"},
    {Icon::Save, "save", "动作"},
    {Icon::Lock, "lock", "状态"},
    {Icon::Unlock, "unlock", "状态"},
    {Icon::Filter, "filter", "动作"},
    {Icon::Download, "download", "动作"},
    {Icon::Upload, "upload", "动作"},
    {Icon::Edit, "edit", "动作"},
    {Icon::Copy, "copy", "动作"},
    {Icon::Menu, "menu", "通用"},
    {Icon::WindowMinimize, "window-minimize", "窗口"},
    {Icon::WindowMaximize, "window-maximize", "窗口"},
    {Icon::WindowRestore, "window-restore", "窗口"},
    {Icon::User, "user", "账户"},
    {Icon::Globe, "globe", "账户"},
    {Icon::Bell, "bell", "账户"},
    {Icon::Logout, "logout", "账户"},
    {Icon::Sun, "sun", "账户"},
    {Icon::Moon, "moon", "账户"},
};

}  // namespace

std::string_view builtinIconName(Icon id) {
  for (const BuiltinName& b : kBuiltins) {
    if (b.id == id) return b.name;
  }
  return {};
}

Icon builtinIconFromName(std::string_view name) {
  for (const BuiltinName& b : kBuiltins) {
    if (name == b.name) return b.id;
  }
  return Icon::None;
}

// ------------------------------------------------------------- IconCanvas ---
IconCanvas::IconCanvas(Painter& p, const Rect& box, Color color,
                       float strokeScale)
    : p_(p), box_(box), color_(color) {
  const float side = std::min(box.width(), box.height());
  scale_ = side / 24.0f;
  x0_ = box.x() + (box.width() - side) * 0.5f;
  y0_ = box.y() + (box.height() - side) * 0.5f;
  // Never let a stroke drop below one physical pixel, or a 12px icon dissolves.
  stroke_ = std::max(1.0f, 1.8f * scale_ * strokeScale);
}

Point IconCanvas::at(float ux, float uy) const {
  return {x0_ + ux * scale_, y0_ + uy * scale_};
}

float IconCanvas::len(float u) const { return u * scale_; }

Rect IconCanvas::square() const {
  const float side = 24.0f * scale_;
  return {x0_, y0_, side, side};
}

void IconCanvas::line(float x1, float y1, float x2, float y2) const {
  p_.strokeLine(at(x1, y1), at(x2, y2), color_, stroke_);
}

void IconCanvas::poly(std::initializer_list<Point> unitPts) const {
  if (unitPts.size() < 2) return;
  std::vector<Point> buf;
  buf.reserve(unitPts.size());
  for (const Point& u : unitPts) buf.push_back(at(u.x, u.y));
  p_.strokePolyline(buf.data(), buf.size(), color_, stroke_);
}

void IconCanvas::circle(float cx, float cy, float r) const {
  p_.strokeCircle(at(cx, cy), len(r), color_, stroke_);
}

void IconCanvas::fillCircle(float cx, float cy, float r) const {
  p_.fillCircle(at(cx, cy), len(r), color_);
}

void IconCanvas::rect(float x, float y, float w, float h) const {
  p_.strokeRect({at(x, y), Size(len(w), len(h))}, color_, stroke_);
}

void IconCanvas::fillRect(float x, float y, float w, float h) const {
  p_.fillRect({at(x, y), Size(len(w), len(h))}, color_);
}

void IconCanvas::roundRect(float x, float y, float w, float h, float r) const {
  p_.strokeRoundRect({at(x, y), Size(len(w), len(h))}, len(r), color_, stroke_);
}

void IconCanvas::fillRoundRect(float x, float y, float w, float h, float r) const {
  p_.fillRoundRect({at(x, y), Size(len(w), len(h))}, len(r), color_);
}

void IconCanvas::arc(float cx, float cy, float r, float startDeg, float sweepDeg,
                     bool roundCap) const {
  p_.strokeArc(at(cx, cy), len(r), startDeg, sweepDeg, color_, stroke_, roundCap);
}

void IconCanvas::triangle(Point a, Point b, Point c) const {
  p_.fillTriangle(at(a.x, a.y), at(b.x, b.y), at(c.x, c.y), color_);
}

void IconCanvas::path(const VectorPath& vp, PathStyle style, float viewBox) const {
  if (vp.empty() || viewBox <= 0.0f) return;
  const float s = (24.0f * scale_) / viewBox;

  p_.save();
  p_.translate(x0_, y0_);
  p_.scale(s);
  if (style == PathStyle::Fill) {
    p_.fillPath(vp, color_);
  } else {
    // The context transform scales stroke width too, so divide it back out --
    // otherwise a 14px icon and a 48px icon come out at different weights,
    // which is the single most visible way an imported icon set looks wrong.
    p_.strokePath(vp, color_, stroke_ / s, true);
  }
  p_.restore();
}

// ----------------------------------------------------------- IconRegistry ---
IconRegistry& IconRegistry::instance() {
  static IconRegistry r;
  return r;
}

Icon IconRegistry::add(std::string name, IconDrawer draw, std::string category) {
  if (name.empty() || !draw) return Icon::None;

  for (Custom& c : custom_) {
    if (c.name == name) {
      // Keep the id: widgets are already holding it.  Replacing the drawer but
      // handing back a NEW id would leave every existing button blank.
      c.drawer = std::move(draw);
      if (!category.empty()) c.category = std::move(category);
      return c.id;
    }
  }

  Custom c;
  c.id = Icon(nextId_++);
  c.name = std::move(name);
  c.category = std::move(category);
  c.drawer = std::move(draw);
  const Icon id = c.id;
  custom_.push_back(std::move(c));
  return id;
}

Icon IconRegistry::addSvgPath(std::string name, std::string_view svgPathData,
                              PathStyle style, float viewBox,
                              std::string category) {
  std::string err;
  VectorPath path = VectorPath::fromSvg(svgPathData, &err);
  if (!err.empty()) {
    errors_.push_back("图标 \"" + name + "\"：" + err);
  }
  if (path.empty()) {
    if (err.empty()) errors_.push_back("图标 \"" + name + "\"：路径为空");
    return Icon::None;
  }

  // The parsed outline is captured by value and reused on every paint.  That is
  // what makes an SVG-sourced icon CHEAPER than a hand-coded one, which rebuilds
  // its geometry from scratch each time it is drawn.
  return add(std::move(name),
             [path = std::move(path), style, viewBox](Painter&,
                                                      const IconCanvas& g) {
               g.path(path, style, viewBox);
             },
             std::move(category));
}

bool IconRegistry::remove(std::string_view name) {
  for (auto it = custom_.begin(); it != custom_.end(); ++it) {
    if (it->name == name) {
      custom_.erase(it);
      return true;
    }
  }
  return false;
}

Icon IconRegistry::find(std::string_view name) const {
  for (const Custom& c : custom_) {
    if (c.name == name) return c.id;
  }
  // Custom names shadow built-ins on purpose: overriding "warning" with a
  // plant's own symbol is a legitimate thing to want.
  return builtinIconFromName(name);
}

std::string IconRegistry::name(Icon id) const {
  for (const Custom& c : custom_) {
    if (c.id == id) return c.name;
  }
  return std::string(builtinIconName(id));
}

bool IconRegistry::contains(std::string_view name) const {
  return find(name) != Icon::None;
}

std::vector<IconEntry> IconRegistry::all() const {
  std::vector<IconEntry> out;
  out.reserve(std::size(kBuiltins) + custom_.size());
  for (const BuiltinName& b : kBuiltins) {
    out.push_back({b.id, b.name, b.category, true});
  }
  for (const Custom& c : custom_) {
    out.push_back({c.id, c.name, c.category, false});
  }
  return out;
}

std::vector<std::string> IconRegistry::categories() const {
  std::vector<std::string> out;
  for (const IconEntry& e : all()) {
    if (e.category.empty()) continue;
    if (std::find(out.begin(), out.end(), e.category) == out.end()) {
      out.push_back(e.category);
    }
  }
  return out;
}

bool IconRegistry::draw(Icon id, Painter& p, const IconCanvas& canvas) const {
  for (const Custom& c : custom_) {
    if (c.id == id) {
      c.drawer(p, canvas);
      return true;
    }
  }
  return false;
}

}  // namespace geeyoou
