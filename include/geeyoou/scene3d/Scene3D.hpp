#pragma once
//
// What is in the world, and what state it is in.
//
// NOT A WIDGET, and it never goes in the widget tree -- docs/architecture.md
// section 3.3.  The application owns a Scene3D and hands a raw pointer to a
// View3D, the same contract TableView has with TableModel, with the same
// obligation: the scene outlives every view that draws it, or the view is told
// setScene(nullptr) first.
//
// -----------------------------------------------------------------------------
// PART STATE IS PUSHED, NOT PULLED -- AND THAT IS THE OPPOSITE OF TableView.
//
// The inconsistency is deliberate, so here is the reason before anybody
// "fixes" it.  A table's row count is unbounded (two hundred thousand rows) and
// only a screenful is ever drawn, so pulling per visible cell is what makes it
// free.  A scene's PART COUNT is bounded and small -- a skid has dozens, not
// thousands -- and its states change on EVENTS, not per frame.  Pulling would
// therefore buy nothing and cost a virtual call per part per frame, every frame,
// forever.  Section 4's rule decides it: do not pay for what you do not use.
//
// A consequence worth stating: because state is pushed, nothing in this class
// is virtual and nothing in scene3d/ adds a name to the door lint's P1 set.
//
// -----------------------------------------------------------------------------
// PARTS ARE ADDRESSED BY ID, NEVER BY STRING, AT RUN TIME.
//
// addPart("P-101") returns a stable PartId and the caller keeps it.  Looking a
// part up by name every time a tag updates would put a string compare on the
// path a plant walks a hundred times a second, and would make a typo a silent
// no-op instead of a compile-time-ish error at build.
//
#include <cstdint>
#include <string>
#include <vector>

#include "geeyoou/core/TagId.hpp"
#include "geeyoou/core/Types.hpp"
#include "geeyoou/scene3d/Mesh.hpp"
#include "geeyoou/scene3d/Vec3.hpp"

namespace geeyoou {

// What a part is DOING.  Semantics only -- there is not a Color in this enum,
// and that is on purpose: a colour stored here would be the skin that happened
// to be active when the screen was built, which is a defect this repository has
// already paid for once.  View3D resolves these against the live theme, per
// paint.
enum class PartState : std::uint8_t {
  Normal,       // nothing to report; drawn in the part's own material colour
  Running,
  Stopped,
  Fault,
  Maintenance,
  Disabled,     // not commissioned / not in service
};

// A callout: a line from a point on the model to a small label.
//
// It is SCENE data rather than view data, for the same reason the parts are:
// what a piece of equipment is called does not change when you look at it from
// a second viewport.  The anchor follows a PART, not a fixed coordinate, so a
// node that is moved takes its labels with it.
using AnnotationId = std::uint16_t;
inline constexpr AnnotationId kNoAnnotation = 0xFFFF;

class Scene3D {
 public:
  // The most triangles this class will accept.  Asserted in addNode rather than
  // checked per frame: a budget that is enforced during painting is a budget
  // that costs something every frame in order to tell you about a mistake you
  // made once, at build time.
  static constexpr std::size_t kMaxTriangles = 20000;

  struct Part {
    std::string name;
    PartState state = PartState::Normal;
    // The part's MATERIAL, not its theme colour.  A tank is grey because it is
    // made of steel, and that is true under every skin -- so this one is a
    // stored, absolute colour and the state colours are not.  The distinction is
    // the whole reason both can coexist without the skin bug coming back.
    Color material = Color::rgb(0x9A, 0xA6, 0xB8);
    float value = 0.0f;  // 0..1, for ColorMode::Value
    bool visible = true;
    TagId tag = TagId::Invalid;  // metadata only: what to open when this part is clicked
  };

  struct Annotation {
    PartId part = kNoPart;
    Vec3 offset;           // from the part's centre, in world units
    std::string title;
    std::string value;     // the live half; empty draws the title alone
    bool visible = true;
  };

  struct Node {
    Mesh mesh;                        // a window; the application owns the arrays
    Mat4 transform = Mat4::identity();
    bool visible = true;
  };

  // --- parts ----------------------------------------------------------------
  PartId addPart(std::string name, Color material = Color::rgb(0x9A, 0xA6, 0xB8));
  std::size_t partCount() const { return parts_.size(); }
  const Part& part(PartId id) const;
  bool validPart(PartId id) const { return id < parts_.size(); }
  // -1 when no part carries that name.  Intended for BUILD time and for tests;
  // see the header note on why run-time code keeps the id instead.
  PartId findPart(const std::string& name) const;

  void setPartState(PartId id, PartState s);
  // A scalar in 0..1 for the heat-map colour mode.  Kept next to the state
  // rather than instead of it: "running" and "at 82% of its limit" are two
  // different questions, and an operator switches between them.
  void setPartValue(PartId id, float v);
  float partValue(PartId id) const;
  PartState partState(PartId id) const;
  void setPartVisible(PartId id, bool on);
  void setPartMaterial(PartId id, Color c);
  void bindPart(PartId id, TagId tag);

  // --- geometry -------------------------------------------------------------
  //
  // The mesh is NOT copied.  Whatever the arrays belong to -- a MeshBuilder, a
  // loader the application wrote, a static table -- must outlive this scene.
  std::size_t addNode(const Mesh& mesh, const Mat4& transform = Mat4::identity());
  std::size_t nodeCount() const { return nodes_.size(); }
  const Node& node(std::size_t i) const { return nodes_[i]; }
  void setNodeTransform(std::size_t i, const Mat4& m);
  void setNodeVisible(std::size_t i, bool on);
  void clear();

  // Where a part IS, in world space: the centre of the bodies that carry it.
  //
  // Cached when geometry changes rather than computed per frame -- a label asks
  // for it every frame and the answer cannot have moved since the last node
  // edit.  Parts with no geometry answer with the scene centre, which puts a
  // stray label somewhere visible instead of at the origin of the universe.
  Vec3 partCenter(PartId id) const;

  // --- annotations ----------------------------------------------------------
  AnnotationId addAnnotation(PartId part, std::string title, Vec3 offset = {});
  std::size_t annotationCount() const { return notes_.size(); }
  const Annotation& annotation(AnnotationId id) const;
  bool validAnnotation(AnnotationId id) const { return id < notes_.size(); }
  void setAnnotationText(AnnotationId id, std::string title, std::string value);
  void setAnnotationValue(AnnotationId id, std::string value);
  void setAnnotationVisible(AnnotationId id, bool on);

  // Union of every node's mesh bounds, in WORLD space.  What View3D::frameAll
  // asks.  Recomputed only when the node list changes, not per frame.
  const Bounds3& bounds() const { return bounds_; }

  std::size_t triangleCount() const { return triangles_; }
  // Total bodies across every node -- what the view's sort buffer is sized from.
  std::size_t bodyCount() const { return bodies_; }
  std::size_t vertexCount() const { return vertices_; }

  // How many times a part's record was read since the last reset.
  //
  // DIAGNOSTIC, and it exists because a budget nobody measures is a wish.  The
  // renderer must touch each part at most ONCE per frame -- if it ever grows a
  // per-face lookup, this counter is what says so, and tests/widget/test_scene3d
  // asserts the bound.  Nothing in the library branches on it.
  std::uint64_t partReads() const { return partReads_; }
  void resetPartReads() const { partReads_ = 0; }

 private:
  void recomputeBounds();
  void recomputePartCenters();

  std::vector<Part> parts_;
  std::vector<Node> nodes_;
  std::vector<Annotation> notes_;
  std::vector<Vec3> partCenters_;
  Bounds3 bounds_;
  std::size_t triangles_ = 0;
  std::size_t bodies_ = 0;
  std::size_t vertices_ = 0;
  mutable std::uint64_t partReads_ = 0;
};

}  // namespace geeyoou
