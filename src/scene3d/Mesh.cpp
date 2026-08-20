#include "geeyoou/scene3d/Mesh.hpp"

#include <cmath>

namespace geeyoou {
namespace {
constexpr float kPi = 3.14159265358979323846f;

int clampSegments(int n) {
  // Three is the fewest that closes; beyond 64 the facets are smaller than a
  // pixel at any HMI size and the only thing growing is the triangle budget.
  if (n < 3) return 3;
  if (n > 64) return 64;
  return n;
}
}  // namespace

Mesh::Mesh(const Vec3* vertices, std::size_t vertexCount, const Face* faces,
           std::size_t faceCount, const Body* bodies, std::size_t bodyCount)
    : vertices_(vertices),
      vertexCount_(vertexCount),
      faces_(faces),
      faceCount_(faceCount),
      bodies_(bodies),
      bodyCount_(bodyCount) {
  // One pass, at construction.  Doing it lazily would mean a const accessor that
  // mutates, and doing it per frame would mean walking every vertex of a static
  // model sixty times a second to learn something that cannot have changed.
  if (!vertices_) return;
  for (std::size_t i = 0; i < vertexCount_; ++i) bounds_.expand(vertices_[i]);
}

// ================================================================ builder ===
void MeshBuilder::clear() {
  vertices_.clear();
  faces_.clear();
  bodies_.clear();
  currentPart_ = kNoPart;
  bodyFaceBegin_ = 0;
  bodyVertexBegin_ = 0;
}

void MeshBuilder::reserve(std::size_t vertices, std::size_t faces) {
  vertices_.reserve(vertices);
  faces_.reserve(faces);
}

Mesh MeshBuilder::mesh() const {
  return Mesh(vertices_.data(), vertices_.size(), faces_.data(), faces_.size(),
              bodies_.data(), bodies_.size());
}

void MeshBuilder::beginBody(PartId part) {
  currentPart_ = part;
  bodyFaceBegin_ = std::uint32_t(faces_.size());
  bodyVertexBegin_ = std::uint32_t(vertices_.size());
}

// The sorting sphere is computed from the vertices this body actually added,
// not from a bounding box handed in by the caller: a caller who got it wrong
// would produce a body that sorts into the wrong place and nothing would say so.
void MeshBuilder::endBody() {
  Body b;
  b.faceBegin = bodyFaceBegin_;
  b.faceEnd = std::uint32_t(faces_.size());
  b.part = currentPart_;
  if (b.faceEnd == b.faceBegin) return;  // nothing was emitted; do not record it

  Bounds3 bb;
  for (std::size_t i = bodyVertexBegin_; i < vertices_.size(); ++i) {
    bb.expand(vertices_[i]);
  }
  b.center = bb.center();
  b.radius = bb.radius();
  bodies_.push_back(b);
}

// An orthonormal basis with Z along `axis`.  The seed is chosen away from the
// axis so the cross product cannot degenerate -- picking a fixed "up" and
// hoping is exactly how a pipe drawn straight up comes out as a flat sliver.
void MeshBuilder::basisFor(const Vec3& axis, Vec3& outU, Vec3& outV) {
  const Vec3 n = axis.normalized();
  const Vec3 seed =
      (std::fabs(n.y) < 0.9f) ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
  outU = n.cross(seed).normalized();
  outV = n.cross(outU).normalized();
}

// Winding is counter-clockwise as seen from OUTSIDE, everywhere in this file.
// The renderer culls by screen-space signed area, so a body wound the other way
// would be drawn inside-out -- visible as a shape that looks hollow and lights
// backwards.  There is no runtime check; consistency here is the check.
void MeshBuilder::addBox(const Vec3& center, const Vec3& size, PartId part) {
  beginBody(part);
  const Vec3 h = size * 0.5f;
  const std::uint32_t v0 = std::uint32_t(vertices_.size());

  vertices_.push_back({center.x - h.x, center.y - h.y, center.z - h.z});  // 0
  vertices_.push_back({center.x + h.x, center.y - h.y, center.z - h.z});  // 1
  vertices_.push_back({center.x + h.x, center.y + h.y, center.z - h.z});  // 2
  vertices_.push_back({center.x - h.x, center.y + h.y, center.z - h.z});  // 3
  vertices_.push_back({center.x - h.x, center.y - h.y, center.z + h.z});  // 4
  vertices_.push_back({center.x + h.x, center.y - h.y, center.z + h.z});  // 5
  vertices_.push_back({center.x + h.x, center.y + h.y, center.z + h.z});  // 6
  vertices_.push_back({center.x - h.x, center.y + h.y, center.z + h.z});  // 7

  auto quad = [&](std::uint32_t a, std::uint32_t b, std::uint32_t c,
                  std::uint32_t d) {
    faces_.push_back({v0 + a, v0 + b, v0 + c, part});
    faces_.push_back({v0 + a, v0 + c, v0 + d, part});
  };
  quad(4, 5, 6, 7);  // +Z
  quad(1, 0, 3, 2);  // -Z
  quad(0, 4, 7, 3);  // -X
  quad(5, 1, 2, 6);  // +X
  quad(3, 7, 6, 2);  // +Y
  quad(0, 1, 5, 4);  // -Y
  endBody();
}

// The shared body of every round primitive: two rings, a wall between them, and
// a fan cap at each end.
void MeshBuilder::addTube(const Vec3& from, const Vec3& to, float radius,
                          int segments) {
  const int n = clampSegments(segments);
  const Vec3 axis = to - from;
  if (axis.length() < 1e-6f || radius <= 0.0f) return;

  Vec3 u, v;
  basisFor(axis, u, v);

  const std::uint32_t base = std::uint32_t(vertices_.size());
  for (int i = 0; i < n; ++i) {
    const float a = 2.0f * kPi * float(i) / float(n);
    const Vec3 off = u * (std::cos(a) * radius) + v * (std::sin(a) * radius);
    vertices_.push_back(from + off);
  }
  for (int i = 0; i < n; ++i) {
    const float a = 2.0f * kPi * float(i) / float(n);
    const Vec3 off = u * (std::cos(a) * radius) + v * (std::sin(a) * radius);
    vertices_.push_back(to + off);
  }
  const std::uint32_t capA = std::uint32_t(vertices_.size());
  vertices_.push_back(from);
  const std::uint32_t capB = capA + 1;
  vertices_.push_back(to);

  for (int i = 0; i < n; ++i) {
    const std::uint32_t i0 = base + std::uint32_t(i);
    const std::uint32_t i1 = base + std::uint32_t((i + 1) % n);
    const std::uint32_t j0 = i0 + std::uint32_t(n);
    const std::uint32_t j1 = i1 + std::uint32_t(n);
    // Wound so the normal points AWAY from the axis.  Getting this backwards
    // does not make the body vanish -- the renderer culls by signed area, so an
    // inverted wall draws its FAR side and the object looks like something you
    // can see into.  tests/widget/test_scene3d.cpp checks every face of every
    // primitive against its body centre, which is how all of these were found.
    faces_.push_back({i0, j1, j0, currentPart_});
    faces_.push_back({i0, i1, j1, currentPart_});
    faces_.push_back({capA, i1, i0, currentPart_});  // bottom, outward is -axis
    faces_.push_back({capB, j0, j1, currentPart_});  // top, outward is +axis
  }
}

void MeshBuilder::addCylinder(const Vec3& base, float radius, float height,
                              PartId part, int segments) {
  beginBody(part);
  addTube(base, base + Vec3{0.0f, height, 0.0f}, radius, segments);
  endBody();
}

void MeshBuilder::addPipe(const Vec3& from, const Vec3& to, float radius,
                          PartId part, int segments) {
  beginBody(part);
  addTube(from, to, radius, segments);
  endBody();
}

void MeshBuilder::addFlange(const Vec3& at, const Vec3& axis, float radius,
                            float thickness, PartId part, int segments) {
  const Vec3 n = axis.normalized();
  beginBody(part);
  addTube(at - n * (thickness * 0.5f), at + n * (thickness * 0.5f), radius,
          segments);
  endBody();
}

void MeshBuilder::addCone(const Vec3& base, float radius, float height,
                          PartId part, int segments) {
  beginBody(part);
  const int n = clampSegments(segments);
  if (radius > 0.0f && std::fabs(height) > 1e-6f) {
    const std::uint32_t ring = std::uint32_t(vertices_.size());
    for (int i = 0; i < n; ++i) {
      const float a = 2.0f * kPi * float(i) / float(n);
      vertices_.push_back(
          {base.x + std::cos(a) * radius, base.y, base.z + std::sin(a) * radius});
    }
    const std::uint32_t apex = std::uint32_t(vertices_.size());
    vertices_.push_back({base.x, base.y + height, base.z});
    const std::uint32_t centre = std::uint32_t(vertices_.size());
    vertices_.push_back(base);

    for (int i = 0; i < n; ++i) {
      const std::uint32_t i0 = ring + std::uint32_t(i);
      const std::uint32_t i1 = ring + std::uint32_t((i + 1) % n);
      faces_.push_back({i0, apex, i1, part});
      faces_.push_back({centre, i0, i1, part});  // base disc, outward is -Y
    }
  }
  endBody();
}

void MeshBuilder::addDome(const Vec3& rim, float radius, float height,
                          PartId part, int segments, int rings) {
  beginBody(part);
  const int n = clampSegments(segments);
  const int r = (rings < 1) ? 1 : (rings > 24 ? 24 : rings);
  if (radius > 0.0f && std::fabs(height) > 1e-6f) {
    const std::uint32_t base = std::uint32_t(vertices_.size());
    // Latitude rings from the rim UP TO but not including the tip.  Ring 0 is
    // the rim itself, so the flat cap can be fanned from the same vertices --
    // a second copy of that circle would leave a hairline where the two meet.
    for (int j = 0; j < r; ++j) {
      const float t = float(j) / float(r);           // 0 at the rim, ->1 at the tip
      const float a = t * (kPi * 0.5f);
      const float ringR = std::cos(a) * radius;
      const float y = std::sin(a) * height;
      for (int i = 0; i < n; ++i) {
        const float th = 2.0f * kPi * float(i) / float(n);
        vertices_.push_back({rim.x + std::cos(th) * ringR, rim.y + y,
                             rim.z + std::sin(th) * ringR});
      }
    }
    const std::uint32_t tip = std::uint32_t(vertices_.size());
    vertices_.push_back({rim.x, rim.y + height, rim.z});
    const std::uint32_t centre = std::uint32_t(vertices_.size());
    vertices_.push_back(rim);

    // Winding follows the sign of `height` so a bottom head is not inside-out.
    const bool up = height > 0.0f;
    for (int j = 0; j + 1 < r; ++j) {
      for (int i = 0; i < n; ++i) {
        const std::uint32_t i0 = base + std::uint32_t(j * n + i);
        const std::uint32_t i1 = base + std::uint32_t(j * n + (i + 1) % n);
        const std::uint32_t k0 = i0 + std::uint32_t(n);
        const std::uint32_t k1 = i1 + std::uint32_t(n);
        if (up) {
          faces_.push_back({i0, k0, k1, part});
          faces_.push_back({i0, k1, i1, part});
        } else {
          faces_.push_back({i0, k1, k0, part});
          faces_.push_back({i0, i1, k1, part});
        }
      }
    }
    for (int i = 0; i < n; ++i) {
      const std::uint32_t t0 = base + std::uint32_t((r - 1) * n + i);
      const std::uint32_t t1 = base + std::uint32_t((r - 1) * n + (i + 1) % n);
      const std::uint32_t c0 = base + std::uint32_t(i);
      const std::uint32_t c1 = base + std::uint32_t((i + 1) % n);
      if (up) {
        faces_.push_back({t0, tip, t1, part});
        faces_.push_back({centre, c0, c1, part});  // rim disc faces down
      } else {
        faces_.push_back({t1, tip, t0, part});
        faces_.push_back({centre, c1, c0, part});  // ...and up on a bottom head
      }
    }
  }
  endBody();
}

void MeshBuilder::addSphere(const Vec3& center, float radius, PartId part,
                            int segments, int rings) {
  beginBody(part);
  const int n = clampSegments(segments);
  const int r = (rings < 2) ? 2 : (rings > 32 ? 32 : rings);
  if (radius > 0.0f) {
    const std::uint32_t base = std::uint32_t(vertices_.size());
    // Latitude rings EXCLUDING both poles, which are added separately -- a pole
    // shared by n triangles is one vertex, not n coincident ones.
    for (int j = 1; j < r; ++j) {
      const float phi = kPi * float(j) / float(r);
      const float y = std::cos(phi) * radius;
      const float ring = std::sin(phi) * radius;
      for (int i = 0; i < n; ++i) {
        const float a = 2.0f * kPi * float(i) / float(n);
        vertices_.push_back({center.x + std::cos(a) * ring, center.y + y,
                             center.z + std::sin(a) * ring});
      }
    }
    const std::uint32_t north = std::uint32_t(vertices_.size());
    vertices_.push_back({center.x, center.y + radius, center.z});
    const std::uint32_t south = std::uint32_t(vertices_.size());
    vertices_.push_back({center.x, center.y - radius, center.z});

    for (int j = 0; j + 2 < r; ++j) {
      for (int i = 0; i < n; ++i) {
        const std::uint32_t i0 = base + std::uint32_t(j * n + i);
        const std::uint32_t i1 = base + std::uint32_t(j * n + (i + 1) % n);
        const std::uint32_t j0 = i0 + std::uint32_t(n);
        const std::uint32_t j1 = i1 + std::uint32_t(n);
        faces_.push_back({i0, j1, j0, part});
        faces_.push_back({i0, i1, j1, part});
      }
    }
    for (int i = 0; i < n; ++i) {
      const std::uint32_t i0 = base + std::uint32_t(i);
      const std::uint32_t i1 = base + std::uint32_t((i + 1) % n);
      faces_.push_back({north, i1, i0, part});
      const std::uint32_t k0 = base + std::uint32_t((r - 2) * n + i);
      const std::uint32_t k1 = base + std::uint32_t((r - 2) * n + (i + 1) % n);
      faces_.push_back({south, k0, k1, part});
    }
  }
  endBody();
}

}  // namespace geeyoou
