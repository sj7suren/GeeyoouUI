#include "geeyoou/render/VectorPath.hpp"

#include <cctype>
#include <cmath>
#include <cstdlib>

#include "render/VectorPathImpl.hpp"

namespace geeyoou {
namespace {
constexpr double kDeg2Rad = 3.14159265358979323846 / 180.0;
}  // namespace

VectorPath::VectorPath() : d_(std::make_unique<Impl>()) {}
VectorPath::~VectorPath() = default;

VectorPath::VectorPath(const VectorPath& o) : d_(std::make_unique<Impl>(*o.d_)) {}

VectorPath& VectorPath::operator=(const VectorPath& o) {
  if (this != &o) *d_ = *o.d_;
  return *this;
}

VectorPath::VectorPath(VectorPath&&) noexcept = default;
VectorPath& VectorPath::operator=(VectorPath&&) noexcept = default;

bool VectorPath::empty() const { return d_->path.is_empty(); }
void VectorPath::clear() { d_->path.clear(); }

void VectorPath::moveTo(Point p) { d_->path.move_to(p.x, p.y); }
void VectorPath::lineTo(Point p) { d_->path.line_to(p.x, p.y); }

void VectorPath::quadTo(Point c, Point to) {
  d_->path.quad_to(c.x, c.y, to.x, to.y);
}

void VectorPath::cubicTo(Point c1, Point c2, Point to) {
  d_->path.cubic_to(c1.x, c1.y, c2.x, c2.y, to.x, to.y);
}

void VectorPath::arcTo(Point c, float rx, float ry, float startDeg,
                       float sweepDeg, bool forceMoveTo) {
  d_->path.arc_to(c.x, c.y, rx, ry, startDeg * kDeg2Rad, sweepDeg * kDeg2Rad,
                  forceMoveTo);
}

void VectorPath::close() { d_->path.close(); }

Rect VectorPath::bounds() const {
  BLBox b{};
  if (d_->path.get_bounding_box(&b) != BL_SUCCESS) return {};
  return {float(b.x0), float(b.y0), float(b.x1 - b.x0), float(b.y1 - b.y0)};
}

// ---------------------------------------------------------------- SVG path ---
namespace {

// A hand-written scanner rather than a regex or a tokeniser class: SVG path
// data has no nesting, and the whole grammar is "a letter, then some numbers".
class SvgScanner {
 public:
  explicit SvgScanner(std::string_view s) : s_(s) {}

  void skipSeparators() {
    while (i_ < s_.size() &&
           (std::isspace(static_cast<unsigned char>(s_[i_])) || s_[i_] == ',')) {
      ++i_;
    }
  }

  bool atEnd() {
    skipSeparators();
    return i_ >= s_.size();
  }

  // A command letter, or 0 when the next token is a number (which means the
  // previous command repeats -- "L 1 2 3 4" is two line segments).
  char peekCommand() {
    skipSeparators();
    if (i_ >= s_.size()) return 0;
    const char c = s_[i_];
    return std::isalpha(static_cast<unsigned char>(c)) ? c : 0;
  }

  char takeCommand() {
    const char c = peekCommand();
    if (c) ++i_;
    return c;
  }

  // SVG allows numbers to run together when the sign disambiguates them --
  // "10-5" is two numbers, not one -- so this cannot just call strtod on the
  // rest of the string and trust it.
  bool number(double& out) {
    skipSeparators();
    if (i_ >= s_.size()) return false;
    const char* begin = s_.data() + i_;
    char* end = nullptr;
    const double v = std::strtod(begin, &end);
    if (end == begin) return false;
    i_ += std::size_t(end - begin);
    out = v;
    return true;
  }

  // A flag in an arc command is a single character, '0' or '1', and may be
  // written with NO separator before the next number ("a1 1 0 011 5").
  bool flag(bool& out) {
    skipSeparators();
    if (i_ >= s_.size()) return false;
    const char c = s_[i_];
    if (c != '0' && c != '1') return false;
    out = (c == '1');
    ++i_;
    return true;
  }

  std::size_t pos() const { return i_; }

 private:
  std::string_view s_;
  std::size_t i_ = 0;
};

}  // namespace

VectorPath VectorPath::fromSvg(std::string_view d, std::string* error) {
  VectorPath out;
  BLPath& p = out.d_->path;

  SvgScanner sc(d);
  char cmd = 0;
  BLPoint cur{0.0, 0.0};       // current point, absolute
  BLPoint start{0.0, 0.0};     // sub-path start, for Z
  bool haveCurrent = false;

  auto fail = [&](const char* what) {
    if (error && error->empty()) {
      *error = std::string(what) + "（偏移 " + std::to_string(sc.pos()) + "）";
    }
  };

  while (!sc.atEnd()) {
    if (const char c = sc.peekCommand()) {
      cmd = sc.takeCommand();
    } else if (cmd == 0) {
      fail("路径未以命令字母开始");
      return out;
    } else if (cmd == 'M') {
      cmd = 'L';  // extra pairs after a moveto are implicit linetos
    } else if (cmd == 'm') {
      cmd = 'l';
    }

    const bool rel = (cmd >= 'a' && cmd <= 'z');
    const double ox = rel ? cur.x : 0.0;
    const double oy = rel ? cur.y : 0.0;
    double a = 0, b = 0, c1 = 0, c2 = 0, c3 = 0, c4 = 0;

    switch (std::toupper(static_cast<unsigned char>(cmd))) {
      case 'M':
        if (!sc.number(a) || !sc.number(b)) { fail("M 需要两个数"); return out; }
        cur = {ox + a, oy + b};
        start = cur;
        p.move_to(cur);
        haveCurrent = true;
        break;

      case 'L':
        if (!sc.number(a) || !sc.number(b)) { fail("L 需要两个数"); return out; }
        cur = {ox + a, oy + b};
        p.line_to(cur);
        break;

      case 'H':
        if (!sc.number(a)) { fail("H 需要一个数"); return out; }
        cur.x = ox + a;
        p.line_to(cur);
        break;

      case 'V':
        if (!sc.number(a)) { fail("V 需要一个数"); return out; }
        cur.y = oy + a;
        p.line_to(cur);
        break;

      case 'C':
        if (!sc.number(a) || !sc.number(b) || !sc.number(c1) || !sc.number(c2) ||
            !sc.number(c3) || !sc.number(c4)) {
          fail("C 需要六个数");
          return out;
        }
        cur = {ox + c3, oy + c4};
        p.cubic_to(ox + a, oy + b, ox + c1, oy + c2, cur.x, cur.y);
        break;

      case 'S':
        if (!sc.number(a) || !sc.number(b) || !sc.number(c1) || !sc.number(c2)) {
          fail("S 需要四个数");
          return out;
        }
        cur = {ox + c1, oy + c2};
        // Blend2D reflects the previous control point itself, which is the
        // fiddly half of S and T.
        p.smooth_cubic_to(ox + a, oy + b, cur.x, cur.y);
        break;

      case 'Q':
        if (!sc.number(a) || !sc.number(b) || !sc.number(c1) || !sc.number(c2)) {
          fail("Q 需要四个数");
          return out;
        }
        cur = {ox + c1, oy + c2};
        p.quad_to(ox + a, oy + b, cur.x, cur.y);
        break;

      case 'T':
        if (!sc.number(a) || !sc.number(b)) { fail("T 需要两个数"); return out; }
        cur = {ox + a, oy + b};
        p.smooth_quad_to(cur.x, cur.y);
        break;

      case 'A': {
        bool largeArc = false, sweep = false;
        if (!sc.number(a) || !sc.number(b) || !sc.number(c1) ||
            !sc.flag(largeArc) || !sc.flag(sweep) || !sc.number(c2) ||
            !sc.number(c3)) {
          fail("A 需要 rx ry 旋转 大弧标志 方向标志 x y");
          return out;
        }
        cur = {ox + c2, oy + c3};
        // elliptic_arc_to takes SVG's endpoint parameterisation verbatim, so
        // there is no centre-parameterisation conversion to get wrong.
        p.elliptic_arc_to(a, b, c1 * kDeg2Rad, largeArc, sweep, cur.x, cur.y);
        break;
      }

      case 'Z':
        p.close();
        cur = start;
        break;

      default:
        fail("不支持的路径命令");
        return out;
    }
    (void)haveCurrent;
  }
  return out;
}

}  // namespace geeyoou
