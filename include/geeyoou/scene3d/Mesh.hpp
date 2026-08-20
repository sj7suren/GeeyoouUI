#pragma once
//
// Triangles, and who owns them.
//
// -----------------------------------------------------------------------------
// `Mesh` IS A WINDOW.  IT OWNS NOTHING.
//
// This is the same decision ListView and TableView already made one layer up
// (docs/architecture.md section 1, rule 2), applied to geometry: a Mesh is three
// pointers and three counts aimed at arrays the APPLICATION owns.  Two things
// follow, and the second is the interesting one:
//
//   1. Handing the same geometry to two views, or drawing it at forty places in
//      a plant layout, copies nothing.
//   2. AN EXTERNAL LOADER NEEDS NO SUPPORT FROM THIS LIBRARY.  A caller who
//      wants OBJ, STL or a vendor format parses it into their own vectors and
//      builds a Mesh over them; nothing here has to know the format existed.
//      That is deliberately how "load a 3D model" is answered in this round --
//      by the ownership direction rather than by a parser -- because a parser
//      inside the library would bring three costs at once: an untrusted input
//      surface, a triangle count nobody can bound (see the budget on Scene3D),
//      and the loss of rule 2, since a parser must own what it produces.
//
// -----------------------------------------------------------------------------
// EVERY PRIMITIVE IS A CONVEX CLOSED BODY, AND THAT IS LOAD-BEARING.
//
// The renderer sorts by depth and paints back to front; it has no z-buffer (see
// View3D.hpp for why).  A painter's algorithm is EXACT for a convex closed body
// once back faces are culled -- the surviving front faces form a height field
// over the view plane, so no face of a body can occlude another face of the same
// body, and their order among themselves does not matter at all.
//
// So MeshBuilder only ever emits convex closed bodies, and it records where each
// one starts and ends.  Ordering is then a question about BODIES, which is a few
// dozen comparisons rather than a few thousand.
//
// What is left over is stated plainly rather than hidden: two bodies that
// INTERPENETRATE can still sort wrongly.  That is a modelling error in this
// library's terms -- a pump does not pass through a tank -- and it is the one
// artefact the design accepts.  Bodies that merely touch (a dome cap sitting on
// a cylinder) are separated by a plane and always sort correctly.
//
#include <cstddef>
#include <cstdint>
#include <vector>

#include "geeyoou/scene3d/Vec3.hpp"

namespace geeyoou {

// Which part a triangle belongs to.  A part is the thing an operator points at
// and the thing a status is set on -- see Scene3D.
using PartId = std::uint16_t;
inline constexpr PartId kNoPart = 0xFFFF;

struct Face {
  std::uint32_t a = 0;
  std::uint32_t b = 0;
  std::uint32_t c = 0;
  PartId part = kNoPart;
};

// One convex closed body: a run of faces, plus the sphere used to sort it.
struct Body {
  std::uint32_t faceBegin = 0;
  std::uint32_t faceEnd = 0;  // exclusive
  Vec3 center;
  float radius = 0.0f;
  PartId part = kNoPart;
};

class Mesh {
 public:
  Mesh() = default;

  // Pointers must outlive the Mesh, and everything they point at must outlive
  // every View3D that draws it.  No reference counting, no copies: the same
  // contract TableView has with TableModel, for the same reason.
  Mesh(const Vec3* vertices, std::size_t vertexCount, const Face* faces,
       std::size_t faceCount, const Body* bodies, std::size_t bodyCount);

  bool empty() const { return faceCount_ == 0 || vertexCount_ == 0; }

  const Vec3* vertices() const { return vertices_; }
  std::size_t vertexCount() const { return vertexCount_; }
  const Face* faces() const { return faces_; }
  std::size_t faceCount() const { return faceCount_; }
  const Body* bodies() const { return bodies_; }
  std::size_t bodyCount() const { return bodyCount_; }

  // Computed once, in the constructor, by one pass over the vertices.  A camera
  // has to be able to frame a model it has never seen, and asking the mesh is
  // the only way that does not involve the application repeating what it just
  // built.
  const Bounds3& bounds() const { return bounds_; }

 private:
  const Vec3* vertices_ = nullptr;
  std::size_t vertexCount_ = 0;
  const Face* faces_ = nullptr;
  std::size_t faceCount_ = 0;
  const Body* bodies_ = nullptr;
  std::size_t bodyCount_ = 0;
  Bounds3 bounds_;
};

// Builds the arrays a Mesh points at.  THE BUILDER OWNS THEM; the application
// keeps the builder alive for as long as it keeps the mesh.
//
// Six primitives, and they are the six an industrial schematic is actually made
// of.  Anything more elaborate is a COMPOSITION of them -- a pressure vessel is
// a cylinder with two dome caps, three bodies that touch and never overlap --
// which is also why there is no primitive for it here.
class MeshBuilder {
 public:
  // `segments` is the number of facets around a round primitive.  16 reads as
  // round at HMI sizes and costs 32 triangles for a cylinder wall; 8 reads as
  // faceted on purpose, which is sometimes what a schematic wants.
  static constexpr int kDefaultSegments = 16;

  void clear();
  void reserve(std::size_t vertices, std::size_t faces);

  // Axis-aligned box centred at `center`.
  void addBox(const Vec3& center, const Vec3& size, PartId part);

  // Cylinder along +Y, `base` is the CENTRE OF THE BOTTOM CAP.  Closed at both
  // ends: an open tube is not a closed body, and the exactness argument above
  // would stop holding for it.
  void addCylinder(const Vec3& base, float radius, float height, PartId part,
                   int segments = kDefaultSegments);

  // Cone along +Y, apex at base + (0, height, 0).
  void addCone(const Vec3& base, float radius, float height, PartId part,
               int segments = kDefaultSegments);

  // UV sphere.  `rings` is latitude, `segments` longitude.
  void addSphere(const Vec3& center, float radius, PartId part,
                 int segments = kDefaultSegments, int rings = 8);

  // A vessel head: half an ellipsoid, closed with a flat disc at its rim.
  //
  // THE REASON THIS EXISTS RATHER THAN "use addSphere at the end of a cylinder".
  // A whole sphere placed on a cylinder's end has half of itself INSIDE the
  // cylinder, and two bodies that interpenetrate are the one arrangement this
  // renderer sorts wrongly -- the vessel comes out looking soft and hollow.  A
  // dome shares exactly one circle with the cylinder it caps and nothing else,
  // which is the "touching, never overlapping" the whole design rests on.
  //
  // `rim` is the centre of the flat end; the tip is at rim + (0, height, 0), so
  // a negative height gives a bottom head.
  void addDome(const Vec3& rim, float radius, float height, PartId part,
               int segments = kDefaultSegments, int rings = 6);

  // A run of pipe between two points, drawn SOLID -- a schematic pipe is a
  // cylinder, and a hollow tube would not be convex.
  void addPipe(const Vec3& from, const Vec3& to, float radius, PartId part,
               int segments = kDefaultSegments);

  // The disc where two pipes bolt together: a short, wide cylinder centred on
  // `at` and facing along `axis`.
  void addFlange(const Vec3& at, const Vec3& axis, float radius, float thickness,
                 PartId part, int segments = kDefaultSegments);

  const std::vector<Vec3>& vertices() const { return vertices_; }
  const std::vector<Face>& faces() const { return faces_; }
  const std::vector<Body>& bodies() const { return bodies_; }

  // A window onto what has been built so far.  Invalidated by any further
  // add*() call, exactly like an iterator into a vector -- and for the same
  // reason, since that is what it is.
  Mesh mesh() const;

 private:
  // Opens a body, and closes the previous one by computing its sphere.  Every
  // add*() begins with this, which is what makes "one call, one convex body" a
  // property of the type rather than of the caller's discipline.
  void beginBody(PartId part);
  void endBody();
  // A cylinder-like side wall between two rings of `segments` vertices, plus the
  // two caps.  addCylinder, addPipe and addFlange are all this function with
  // different arguments; writing it three times is how the three drift.
  void addTube(const Vec3& from, const Vec3& to, float radius, int segments);
  // An orthonormal basis whose Z is `axis`.  Used by everything round.
  static void basisFor(const Vec3& axis, Vec3& outU, Vec3& outV);

  std::vector<Vec3> vertices_;
  std::vector<Face> faces_;
  std::vector<Body> bodies_;
  PartId currentPart_ = kNoPart;
  std::uint32_t bodyFaceBegin_ = 0;
  std::uint32_t bodyVertexBegin_ = 0;
};

}  // namespace geeyoou
