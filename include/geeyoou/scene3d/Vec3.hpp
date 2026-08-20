#pragma once
//
// The 3D value types, and nothing else.
//
// Header-only, POD-shaped, no virtuals, no allocation: this is the layer the
// rest of scene3d/ does arithmetic in, and it has to be free.  A Vec3 is three
// floats and a Mat4 is sixteen, both trivially copyable, both passed by value
// where that is cheaper than a pointer.
//
// WHY THIS IS NOT IN core/Types.hpp, next to Point and Rect.  Everything above
// platform/ speaks 2D logical pixels (docs/architecture.md section 3.1), and
// that is a property worth keeping legible: a Point is a place on the screen,
// and a Vec3 is emphatically not.  A library where both live in the same header
// is one where somebody eventually assigns one to the other.  scene3d/ is the
// only place that needs these, so this is where they live.
//
// COLUMN-VECTOR CONVENTION, stated once so nothing below has to argue about it:
// a point is a column, transforms multiply on the LEFT (`M * v`), and composing
// reads right-to-left (`proj * view * model`).  Matrices are stored ROW-MAJOR --
// m[r][c] -- because that is the order they are written down in, and this file
// is read far more often than it is executed.
//
#include <cmath>

namespace geeyoou {

struct Vec3 {
  float x = 0.0f;
  float y = 0.0f;
  float z = 0.0f;

  constexpr Vec3() = default;
  constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

  constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
  constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
  constexpr Vec3 operator-() const { return {-x, -y, -z}; }
  constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
  constexpr Vec3& operator+=(const Vec3& o) {
    x += o.x;
    y += o.y;
    z += o.z;
    return *this;
  }

  constexpr float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
  constexpr Vec3 cross(const Vec3& o) const {
    return {y * o.z - z * o.y, z * o.x - x * o.z, x * o.y - y * o.x};
  }
  float length() const { return std::sqrt(dot(*this)); }

  // A ZERO VECTOR NORMALISES TO ZERO, not to NaN.  Degenerate triangles are
  // ordinary in a mesh that came out of a CAD exporter -- two coincident
  // vertices, a zero-area face -- and a NaN normal poisons the shade of a face
  // that would otherwise simply have been invisible.  Returning zero makes it
  // black-and-harmless instead of turning the whole surface into NaN soup.
  Vec3 normalized() const {
    const float len = length();
    return len > 1e-8f ? Vec3{x / len, y / len, z / len} : Vec3{};
  }
};

inline constexpr Vec3 operator*(float s, const Vec3& v) { return v * s; }

// Linear blend, for camera easing and colour ramps alike.
inline constexpr Vec3 lerp(const Vec3& a, const Vec3& b, float t) {
  return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

// An axis-aligned box, and the reason it exists: a camera has to be able to
// FRAME a model it has never seen.  Every mesh keeps one, and `frameAll()` on
// the view is nothing more than "put the eye far enough back to see this".
struct Bounds3 {
  Vec3 lo{1e30f, 1e30f, 1e30f};
  Vec3 hi{-1e30f, -1e30f, -1e30f};

  bool empty() const { return lo.x > hi.x || lo.y > hi.y || lo.z > hi.z; }

  // NOT `add`.  The door lint's P2 primitive list contains `add` -- it means
  // Widget::add<T> -- and the lint greps BY NAME, so a Bounds3::add would report
  // two false doors in a header that cannot reach application code at all.  The
  // same discipline the scene3d virtuals follow, applied one level down: do not
  // spend a name the checker already owns.
  void expand(const Vec3& p) {
    if (p.x < lo.x) lo.x = p.x;
    if (p.y < lo.y) lo.y = p.y;
    if (p.z < lo.z) lo.z = p.z;
    if (p.x > hi.x) hi.x = p.x;
    if (p.y > hi.y) hi.y = p.y;
    if (p.z > hi.z) hi.z = p.z;
  }

  void expand(const Bounds3& b) {
    if (b.empty()) return;
    expand(b.lo);
    expand(b.hi);
  }

  Vec3 center() const {
    return empty() ? Vec3{} : Vec3{(lo.x + hi.x) * 0.5f, (lo.y + hi.y) * 0.5f,
                                   (lo.z + hi.z) * 0.5f};
  }
  Vec3 extent() const { return empty() ? Vec3{} : hi - lo; }
  float radius() const {
    if (empty()) return 0.0f;
    return (extent() * 0.5f).length();
  }
};

// 4x4, row-major, column-vector convention.  See the file header.
struct Mat4 {
  float m[4][4] = {{1, 0, 0, 0}, {0, 1, 0, 0}, {0, 0, 1, 0}, {0, 0, 0, 1}};

  static constexpr Mat4 identity() { return {}; }

  static Mat4 translation(const Vec3& t) {
    Mat4 r;
    r.m[0][3] = t.x;
    r.m[1][3] = t.y;
    r.m[2][3] = t.z;
    return r;
  }

  static Mat4 scaling(const Vec3& s) {
    Mat4 r;
    r.m[0][0] = s.x;
    r.m[1][1] = s.y;
    r.m[2][2] = s.z;
    return r;
  }

  static Mat4 rotationX(float rad) {
    Mat4 r;
    const float c = std::cos(rad);
    const float s = std::sin(rad);
    r.m[1][1] = c;
    r.m[1][2] = -s;
    r.m[2][1] = s;
    r.m[2][2] = c;
    return r;
  }

  static Mat4 rotationY(float rad) {
    Mat4 r;
    const float c = std::cos(rad);
    const float s = std::sin(rad);
    r.m[0][0] = c;
    r.m[0][2] = s;
    r.m[2][0] = -s;
    r.m[2][2] = c;
    return r;
  }

  static Mat4 rotationZ(float rad) {
    Mat4 r;
    const float c = std::cos(rad);
    const float s = std::sin(rad);
    r.m[0][0] = c;
    r.m[0][1] = -s;
    r.m[1][0] = s;
    r.m[1][1] = c;
    return r;
  }

  // Right-handed look-at: the camera sits at `eye`, looks towards `target`, and
  // `up` only has to be roughly up -- it is re-orthogonalised here, which is what
  // stops the view from collapsing when an orbit passes near the pole.
  static Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
    const Vec3 f = (target - eye).normalized();   // forward
    Vec3 s = f.cross(up).normalized();            // right
    // Degenerate when the eye is directly above the target and `up` is parallel
    // to the view direction.  Pick any perpendicular rather than emitting a
    // matrix full of zeroes: an unusable camera is worse than a rolled one.
    if (s.length() < 1e-6f) s = f.cross(Vec3{0.0f, 0.0f, 1.0f}).normalized();
    const Vec3 u = s.cross(f);

    Mat4 r;
    r.m[0][0] = s.x;  r.m[0][1] = s.y;  r.m[0][2] = s.z;  r.m[0][3] = -s.dot(eye);
    r.m[1][0] = u.x;  r.m[1][1] = u.y;  r.m[1][2] = u.z;  r.m[1][3] = -u.dot(eye);
    r.m[2][0] = -f.x; r.m[2][1] = -f.y; r.m[2][2] = -f.z; r.m[2][3] = f.dot(eye);
    return r;
  }

  // Right-handed perspective, mapping the view volume to clip space with z in
  // [-1, 1].  `fovYRad` is the FULL vertical field of view.
  static Mat4 perspective(float fovYRad, float aspect, float zNear, float zFar) {
    const float t = 1.0f / std::tan(fovYRad * 0.5f);
    Mat4 r;
    r.m[0][0] = t / (aspect > 1e-6f ? aspect : 1.0f);
    r.m[1][1] = t;
    r.m[2][2] = (zFar + zNear) / (zNear - zFar);
    r.m[2][3] = (2.0f * zFar * zNear) / (zNear - zFar);
    r.m[3][2] = -1.0f;
    r.m[3][3] = 0.0f;
    return r;
  }

  Mat4 operator*(const Mat4& o) const {
    Mat4 r;
    for (int i = 0; i < 4; ++i) {
      for (int j = 0; j < 4; ++j) {
        r.m[i][j] = m[i][0] * o.m[0][j] + m[i][1] * o.m[1][j] +
                    m[i][2] * o.m[2][j] + m[i][3] * o.m[3][j];
      }
    }
    return r;
  }

  // A POINT: the translation column applies, and the w row is returned so the
  // caller can divide.  Kept separate from transformDirection precisely because
  // forgetting which one a normal needs is the classic 3D bug.
  Vec3 transformPoint(const Vec3& p, float* wOut = nullptr) const {
    const float x = m[0][0] * p.x + m[0][1] * p.y + m[0][2] * p.z + m[0][3];
    const float y = m[1][0] * p.x + m[1][1] * p.y + m[1][2] * p.z + m[1][3];
    const float z = m[2][0] * p.x + m[2][1] * p.y + m[2][2] * p.z + m[2][3];
    if (wOut) *wOut = m[3][0] * p.x + m[3][1] * p.y + m[3][2] * p.z + m[3][3];
    return {x, y, z};
  }

  // A DIRECTION: no translation.  Correct for normals only while the transform
  // has no non-uniform scale -- which is a constraint this library can simply
  // impose, because a plant model built from primitives is placed and rotated,
  // never squashed.  Stated here so the day somebody adds squashing, the reason
  // their lighting went wrong is written down.
  Vec3 transformDirection(const Vec3& v) const {
    return {m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z,
            m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z,
            m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z};
  }
};

}  // namespace geeyoou
