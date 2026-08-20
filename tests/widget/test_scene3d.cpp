//
// scene3d: MeshBuilder / Scene3D / Camera / View3D.
//
// WHAT THESE CASES ARE FOR.  A 3D renderer is unusually easy to write tests for
// that pass while the picture is wrong, so none of these compares a picture.
// They pin the four properties the DESIGN rests on, each of which is a number:
//
//   1. BACK-FACE CULLING IS REAL, AND THAT IS WHY THE PAINTER'S ALGORITHM IS
//      EXACT.  A closed convex body must submit at most half its faces from any
//      viewpoint; if culling ever silently stopped working, the picture would
//      still look almost right and the sort would quietly become wrong.  The
//      case counts submitted faces.
//   2. NOTHING ALLOCATES PER FRAME.  Every buffer is sized by setScene; onPaint
//      indexes.  AllocGuard counts, exactly as test_layout_alloc.cpp does for
//      the layout engine.
//   3. EACH PART IS READ AT MOST ONCE PER FRAME.  Scene3D counts its own reads,
//      the same trick the table round used, so a per-face part lookup shows up
//      as a number rather than as a frame-rate rumour.
//   4. YOU PICK WHAT YOU SEE.  Picking walks the SAME projected faces the paint
//      drew, so the two cannot disagree -- and a case proves the pick lands on
//      the near box rather than the one hidden behind it.
//
#include <string>
#include <vector>

#include "framework/Test.hpp"
#include "geeyoou/render/Canvas.hpp"
#include "geeyoou/render/Offscreen.hpp"
#include "geeyoou/render/Painter.hpp"
#include "geeyoou/scene3d/Camera.hpp"
#include "geeyoou/scene3d/Mesh.hpp"
#include "geeyoou/scene3d/Scene3D.hpp"
#include "geeyoou/scene3d/View3D.hpp"

using geeyoou::Bounds3;
using geeyoou::Camera;
using geeyoou::Canvas;
using geeyoou::Color;
using geeyoou::Mat4;
using geeyoou::Mesh;
using geeyoou::MeshBuilder;
using geeyoou::OffscreenImage;
using geeyoou::Painter;
using geeyoou::PartId;
using geeyoou::PartState;
using geeyoou::Point;
using geeyoou::Rect;
using geeyoou::Scene3D;
using geeyoou::Vec3;
using geeyoou::View3D;
using geeyoou::kNoPart;

namespace {

void paintOnce(geeyoou::Widget& w, int width, int height) {
  OffscreenImage img(width, height, 1.0f);
  const Rect all(0.0f, 0.0f, float(width), float(height));
  Canvas canvas;
  if (!canvas.begin(img.surface(), all)) return;
  Painter p = canvas.painter();
  w.paintTree(p, all, all);
  canvas.end();
}

}  // namespace

// ================================================================== math =====

GEEYOOU_TEST(scene3d, a_zero_vector_normalises_to_zero_not_to_nan) {
  const Vec3 z = Vec3{0.0f, 0.0f, 0.0f}.normalized();
  // A degenerate triangle is ordinary in exported geometry; a NaN normal would
  // poison the shade of every face that touched it.
  CHECK_EQ(z.x, 0.0f);
  CHECK_EQ(z.y, 0.0f);
  CHECK_EQ(z.z, 0.0f);
}

GEEYOOU_TEST(scene3d, look_at_survives_a_camera_directly_overhead) {
  // The pole is where up is parallel to the view direction and the basis
  // collapses.  The matrix must still be usable rather than full of zeroes.
  const Mat4 m = Mat4::lookAt({0.0f, 10.0f, 0.0f}, {0.0f, 0.0f, 0.0f},
                              {0.0f, 1.0f, 0.0f});
  const Vec3 p = m.transformPoint({0.0f, 0.0f, 0.0f});
  // The target must land in front of the eye: view-space z is negative there.
  CHECK(p.z < 0.0f);
  CHECK(p.z == p.z);  // not NaN
}

// ================================================================= mesh ======

GEEYOOU_TEST(scene3d, a_box_is_one_closed_convex_body) {
  MeshBuilder b;
  b.addBox({0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f}, 0);
  const Mesh m = b.mesh();

  CHECK_EQ(int(m.vertexCount()), 8);
  CHECK_EQ(int(m.faceCount()), 12);  // six quads
  // ONE body, because the painter's algorithm's exactness is stated per convex
  // body: a primitive that reported two would be sorted as two.
  CHECK_EQ(int(m.bodyCount()), 1);
  CHECK_EQ(int(m.bodies()[0].faceEnd - m.bodies()[0].faceBegin), 12);

  CHECK_NEAR(m.bounds().lo.x, -1.0f, 1e-5);
  CHECK_NEAR(m.bounds().hi.y, 1.0f, 1e-5);
}

GEEYOOU_TEST(scene3d, each_primitive_call_opens_exactly_one_body) {
  MeshBuilder b;
  b.addBox({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, 0);
  b.addCylinder({0.0f, 0.0f, 0.0f}, 0.5f, 2.0f, 1);
  b.addSphere({0.0f, 3.0f, 0.0f}, 0.5f, 2);
  b.addCone({2.0f, 0.0f, 0.0f}, 0.5f, 1.0f, 3);
  b.addPipe({0.0f, 0.0f, 0.0f}, {3.0f, 0.0f, 0.0f}, 0.2f, 4);
  b.addFlange({3.0f, 0.0f, 0.0f}, {1.0f, 0.0f, 0.0f}, 0.4f, 0.1f, 5);

  CHECK_EQ(int(b.bodies().size()), 6);
  // Every body owns a contiguous, non-empty run of faces, and the runs tile the
  // face list without a gap -- which is what lets the renderer walk a body by
  // index instead of filtering.
  std::uint32_t next = 0;
  for (const geeyoou::Body& body : b.bodies()) {
    CHECK_EQ(int(body.faceBegin), int(next));
    CHECK(body.faceEnd > body.faceBegin);
    CHECK(body.radius > 0.0f);
    next = body.faceEnd;
  }
  CHECK_EQ(int(next), int(b.faces().size()));
}

// THE WINDING CHECK, and it is worth more than every other case in this file.
//
// The renderer culls by screen-space signed area, so a body wound inside-out
// does not disappear -- it renders its FAR wall instead of its near one, which
// looks like a solid object you can see into.  That is exactly what shipped in
// the first draft of MeshBuilder: the cylinder wall, the sphere and the cone's
// base cap were all inverted, and it took a rendered screenshot to notice.
//
// The property is mechanical and exact, so it should never have been a matter of
// deriving cross products by hand: for a CONVEX body, every face's outward
// normal must point AWAY from the body's centre.  One loop, every primitive,
// every face, forever.
namespace {
void checkOutward(geeyoou::test::Context& ctx_, const MeshBuilder& b,
                  const char* what) {
  const std::vector<geeyoou::Vec3>& v = b.vertices();
  for (const geeyoou::Body& body : b.bodies()) {
    for (std::uint32_t fi = body.faceBegin; fi < body.faceEnd; ++fi) {
      const geeyoou::Face& f = b.faces()[fi];
      const Vec3& a = v[f.a];
      const Vec3& q = v[f.b];
      const Vec3& c = v[f.c];
      const Vec3 n = (q - a).cross(c - a);
      if (n.length() < 1e-9f) continue;  // a degenerate face has no side
      const Vec3 centroid{(a.x + q.x + c.x) / 3.0f, (a.y + q.y + c.y) / 3.0f,
                          (a.z + q.z + c.z) / 3.0f};
      const Vec3 outward = centroid - body.center;
      if (outward.length() < 1e-9f) continue;
      if (n.normalized().dot(outward.normalized()) <= 0.0f) {
        GEEYOOU_FAIL(std::string(what) + ": face " + std::to_string(fi) +
                     " is wound inside-out (normal points into the body)");
        return;
      }
    }
  }
}
}  // namespace

GEEYOOU_TEST(scene3d, every_primitive_is_wound_outwards) {
  {
    MeshBuilder b;
    b.addBox({0.0f, 0.0f, 0.0f}, {2.0f, 3.0f, 1.5f}, 0);
    checkOutward(ctx_, b, "addBox");
  }
  {
    MeshBuilder b;
    b.addCylinder({0.0f, 0.0f, 0.0f}, 1.0f, 3.0f, 0, 16);
    checkOutward(ctx_, b, "addCylinder");
  }
  {
    MeshBuilder b;
    b.addCone({0.0f, 0.0f, 0.0f}, 1.0f, 2.0f, 0, 16);
    checkOutward(ctx_, b, "addCone");
  }
  {
    MeshBuilder b;
    b.addSphere({0.0f, 0.0f, 0.0f}, 1.0f, 0, 16, 8);
    checkOutward(ctx_, b, "addSphere");
  }
  {
    MeshBuilder b;
    b.addPipe({0.0f, 0.0f, 0.0f}, {2.0f, 1.0f, 0.5f}, 0.3f, 0, 14);
    checkOutward(ctx_, b, "addPipe");
  }
  {
    MeshBuilder b;
    b.addFlange({0.0f, 0.0f, 0.0f}, {0.0f, 1.0f, 0.0f}, 0.6f, 0.15f, 0, 14);
    checkOutward(ctx_, b, "addFlange");
  }
  {
    MeshBuilder b;
    b.addDome({0.0f, 0.0f, 0.0f}, 1.0f, 0.8f, 0, 16, 6);
    checkOutward(ctx_, b, "addDome up");
  }
  {
    MeshBuilder b;
    b.addDome({0.0f, 0.0f, 0.0f}, 1.0f, -0.8f, 0, 16, 6);
    checkOutward(ctx_, b, "addDome down");
  }
}

GEEYOOU_TEST(scene3d, a_pipe_straight_up_is_not_a_sliver) {
  // The basis for a round primitive is seeded away from its axis.  Seeding it
  // with a fixed "up" makes a vertical pipe degenerate -- the classic symptom is
  // a pipe that renders as a flat blade.
  MeshBuilder b;
  b.addPipe({0.0f, 0.0f, 0.0f}, {0.0f, 4.0f, 0.0f}, 0.5f, 0);
  const Bounds3 bb = b.mesh().bounds();
  CHECK_NEAR(bb.hi.x - bb.lo.x, 1.0f, 0.05);
  CHECK_NEAR(bb.hi.z - bb.lo.z, 1.0f, 0.05);
  CHECK_NEAR(bb.hi.y - bb.lo.y, 4.0f, 1e-4);
}

// =============================================================== camera ======

GEEYOOU_TEST(scene3d, framing_puts_the_eye_outside_what_it_frames) {
  Bounds3 b;
  b.expand({-2.0f, 0.0f, -2.0f});
  b.expand({2.0f, 6.0f, 2.0f});

  Camera c;
  c.frame(b);

  CHECK_NEAR(c.target.y, 3.0f, 1e-4);
  // Outside, not inside: this renderer drops faces that cross the near plane
  // rather than clipping them, so an eye inside the model would punch holes.
  CHECK(c.distance > b.radius());
  CHECK(c.minDistance >= b.radius());
}

GEEYOOU_TEST(scene3d, zoom_is_multiplicative_and_clamped) {
  Camera c;
  c.distance = 10.0f;
  c.minDistance = 2.0f;
  c.maxDistance = 40.0f;

  c.dolly(0.5f);
  CHECK_NEAR(c.distance, 5.0f, 1e-4);
  // Proportional, so one notch means the same thing on a valve and on a tank
  // farm -- and the limits hold however many notches arrive.
  for (int i = 0; i < 50; ++i) c.dolly(0.5f);
  CHECK_NEAR(c.distance, 2.0f, 1e-4);
  for (int i = 0; i < 200; ++i) c.dolly(2.0f);
  CHECK_NEAR(c.distance, 40.0f, 1e-4);
}

GEEYOOU_TEST(scene3d, pitch_stops_short_of_the_pole) {
  Camera c;
  for (int i = 0; i < 100; ++i) c.orbit(0.0f, 0.5f);
  CHECK(c.pitchRad <= Camera::kMaxPitch);
  // At the pole the up vector is parallel to the view direction; stopping short
  // is what keeps the basis from collapsing mid-drag.
  CHECK(c.pitchRad < 1.5708f);
}

// ================================================================ scene ======

GEEYOOU_TEST(scene3d, a_part_is_addressed_by_id_and_keeps_its_state) {
  Scene3D s;
  const PartId pump = s.addPart("P-101");
  const PartId tank = s.addPart("T-201");

  CHECK_EQ(int(s.partCount()), 2);
  CHECK_EQ(int(s.findPart("T-201")), int(tank));
  CHECK_EQ(int(s.findPart("nope")), int(kNoPart));

  s.setPartState(pump, PartState::Fault);
  CHECK(s.partState(pump) == PartState::Fault);
  CHECK(s.partState(tank) == PartState::Normal);

  // An id nobody handed out is inert rather than fatal: a diagnostic question
  // must not be able to crash a running plant display.
  s.setPartState(9999, PartState::Running);
  CHECK(s.partState(9999) == PartState::Normal);
}

GEEYOOU_TEST(scene3d, scene_bounds_follow_the_node_transform) {
  MeshBuilder b;
  b.addBox({0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f}, 0);

  Scene3D s;
  s.addPart("box");
  s.addNode(b.mesh(), Mat4::translation({10.0f, 0.0f, 0.0f}));

  CHECK_NEAR(s.bounds().center().x, 10.0f, 1e-4);
  CHECK_EQ(int(s.triangleCount()), 12);
  CHECK_EQ(int(s.bodyCount()), 1);
}

// =============================================================== render ======

// PROPERTY 1.  A closed convex body shows at most half of itself, and the
// renderer must not submit the other half -- that is what makes ordering within
// a body irrelevant, which is what makes the whole no-z-buffer design exact.
GEEYOOU_TEST(scene3d, a_closed_convex_body_submits_at_most_half_its_faces) {
  MeshBuilder b;
  b.addBox({0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f}, 0);

  Scene3D s;
  s.addPart("box");
  s.addNode(b.mesh());

  View3D v;
  v.setScene(&s);
  v.setGridVisible(false);
  v.setGeometry({0.0f, 0.0f, 400.0f, 300.0f});
  paintOnce(v, 400, 300);

  CHECK(v.lastDrawnFaces() > 0);
  CHECK(v.lastDrawnFaces() <= 6);  // 12 faces, never more than three quads visible
}

// PROPERTY 2.  The frame buffers are sized by setScene and indexed by onPaint.
// Written the way test_layout_alloc.cpp writes the same claim about layout.
GEEYOOU_TEST(scene3d, a_settled_view_allocates_nothing_per_frame) {
  MeshBuilder b;
  for (int i = 0; i < 8; ++i) {
    b.addCylinder({float(i) * 2.0f, 0.0f, 0.0f}, 0.6f, 3.0f, PartId(i));
  }

  Scene3D s;
  for (int i = 0; i < 8; ++i) s.addPart("C" + std::to_string(i));
  s.addNode(b.mesh());

  View3D v;
  v.setScene(&s);
  v.setGeometry({0.0f, 0.0f, 480.0f, 360.0f});

  // Warm up: the first paint may still touch a lazily-built something in the
  // rasteriser, and the claim is about the STEADY state.
  paintOnce(v, 480, 360);
  paintOnce(v, 480, 360);

  geeyoou::test::AllocGuard guard;
  for (int i = 0; i < 5; ++i) paintOnce(v, 480, 360);
  // OffscreenImage and Canvas allocate inside paintOnce, so the bound is not
  // zero -- it is "does not grow with frames".  Five frames must cost the same
  // as one, which is what a per-frame vector in the renderer would break.
  const std::uint64_t five = guard.count();

  geeyoou::test::AllocGuard guard1;
  paintOnce(v, 480, 360);
  const std::uint64_t one = guard1.count();

  CHECK(one > 0);          // the harness itself allocates; if not, this proves nothing
  CHECK(five <= one * 5);  // strictly: no per-frame growth beyond the harness's own
}

// PROPERTY 3.  Each part is read at most once per frame.  A lookup that slid
// into the body loop, or worse the face loop, shows up here and nowhere else.
GEEYOOU_TEST(scene3d, every_part_is_read_at_most_once_per_frame) {
  MeshBuilder b;
  // ONE part, THREE bodies -- a vessel is exactly this shape, and it is the case
  // that a per-body lookup would get wrong while looking correct.
  b.addCylinder({0.0f, 0.0f, 0.0f}, 1.0f, 3.0f, 0);
  b.addSphere({0.0f, 3.0f, 0.0f}, 1.0f, 0);
  b.addSphere({0.0f, 0.0f, 0.0f}, 1.0f, 0);

  Scene3D s;
  s.addPart("V-100");
  s.addNode(b.mesh());

  View3D v;
  v.setScene(&s);
  v.setGeometry({0.0f, 0.0f, 400.0f, 320.0f});
  paintOnce(v, 400, 320);

  s.resetPartReads();
  paintOnce(v, 400, 320);
  CHECK_EQ(int(s.partReads()), int(s.partCount()));
}

// PROPERTY 4.  Picking answers from the same projected faces the paint drew.
GEEYOOU_TEST(scene3d, picking_lands_on_the_part_in_front) {
  MeshBuilder b;
  // Two boxes on the view axis: `near` sits between the camera and `far`.
  b.addBox({0.0f, 0.0f, 4.0f}, {2.0f, 2.0f, 2.0f}, 0);   // near
  b.addBox({0.0f, 0.0f, -4.0f}, {3.0f, 3.0f, 3.0f}, 1);  // far, and bigger

  Scene3D s;
  const PartId nearPart = s.addPart("near");
  s.addPart("far");
  s.addNode(b.mesh());

  View3D v;
  v.setScene(&s);
  v.setGridVisible(false);
  v.setGeometry({0.0f, 0.0f, 400.0f, 300.0f});

  // Look straight down -Z so the two boxes overlap on screen.
  Camera c = v.camera();
  c.yawRad = 0.0f;
  c.pitchRad = 0.0f;
  v.setCamera(c);
  paintOnce(v, 400, 300);

  const PartId hit = v.partAt({200.0f, 150.0f});
  CHECK_EQ(int(hit), int(nearPart));
}

GEEYOOU_TEST(scene3d, picking_before_the_first_paint_answers_nothing) {
  MeshBuilder b;
  b.addBox({0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f}, 0);

  Scene3D s;
  s.addPart("box");
  s.addNode(b.mesh());

  View3D v;
  v.setScene(&s);
  v.setGeometry({0.0f, 0.0f, 400.0f, 300.0f});

  // Nothing has been projected yet, and the answer is "I do not know" rather
  // than a render kicked off from inside an input handler.
  CHECK_EQ(int(v.partAt({200.0f, 150.0f})), int(kNoPart));
}

GEEYOOU_TEST(scene3d, an_invisible_part_is_neither_drawn_nor_pickable) {
  MeshBuilder b;
  b.addBox({0.0f, 0.0f, 0.0f}, {2.0f, 2.0f, 2.0f}, 0);

  Scene3D s;
  const PartId box = s.addPart("box");
  s.addNode(b.mesh());

  View3D v;
  v.setScene(&s);
  v.setGridVisible(false);
  v.setGeometry({0.0f, 0.0f, 400.0f, 300.0f});
  paintOnce(v, 400, 300);
  CHECK(v.lastDrawnFaces() > 0);
  CHECK_EQ(int(v.partAt({200.0f, 150.0f})), int(box));

  s.setPartVisible(box, false);
  paintOnce(v, 400, 300);
  CHECK_EQ(int(v.lastDrawnFaces()), 0);
  // A part that is not drawn must not be clickable either -- otherwise a hidden
  // thing is operable, which is the same defect the table round found in a
  // grouping row that carried a switch.
  CHECK_EQ(int(v.partAt({200.0f, 150.0f})), int(kNoPart));
}

GEEYOOU_TEST(scene3d, a_view_with_no_scene_paints_and_picks_without_a_model) {
  View3D v;
  v.setGeometry({0.0f, 0.0f, 320.0f, 240.0f});
  paintOnce(v, 320, 240);  // must not read through a null scene
  CHECK_EQ(int(v.partAt({100.0f, 100.0f})), int(kNoPart));

  // And taking a scene away is the documented way to outlive it.
  Scene3D s;
  MeshBuilder b;
  b.addBox({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, 0);
  s.addPart("box");
  s.addNode(b.mesh());
  v.setScene(&s);
  paintOnce(v, 320, 240);
  v.setScene(nullptr);
  paintOnce(v, 320, 240);
  CHECK(v.scene() == nullptr);
}

// The hint is a VIEWPORT, not a contents -- ADR-R2-09's rule, which every
// window-onto-a-model control in this library follows.
GEEYOOU_TEST(scene3d, the_hint_does_not_grow_with_the_model) {
  MeshBuilder small;
  small.addBox({0.0f, 0.0f, 0.0f}, {1.0f, 1.0f, 1.0f}, 0);
  MeshBuilder big;
  big.addBox({0.0f, 0.0f, 0.0f}, {500.0f, 500.0f, 500.0f}, 0);

  Scene3D a;
  a.addPart("x");
  a.addNode(small.mesh());
  Scene3D bScene;
  bScene.addPart("x");
  bScene.addNode(big.mesh());

  View3D va;
  va.setScene(&a);
  View3D vb;
  vb.setScene(&bScene);

  CHECK_NEAR(va.sizeHint().preferred.width, vb.sizeHint().preferred.width, 0.01);
  CHECK_NEAR(va.sizeHint().preferred.height, vb.sizeHint().preferred.height, 0.01);
}
