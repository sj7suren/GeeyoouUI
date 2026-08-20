#pragma once
//
// The 3D viewport, and the only Widget in scene3d/.
//
// -----------------------------------------------------------------------------
// THERE IS NO GPU.  THERE IS NO Z-BUFFER.  THIS IS A PAINTER'S ALGORITHM.
//
// The whole library rasterises through Blend2D, which is a 2D engine
// (docs/architecture.md section 3.5), so a triangle here is a `fillTriangle`
// call and depth is decided by the ORDER those calls are made in.  Two
// consequences, and neither is a compromise once they are read together:
//
//   * every primitive MeshBuilder emits is a CONVEX CLOSED body, and for a
//     convex body with back faces culled the surviving faces form a height
//     field over the view plane -- no face of a body can hide another face of
//     the same body.  Sorting is therefore a question about BODIES, a few dozen
//     comparisons, and within a body the order does not matter at all.
//   * the residue is bodies that INTERPENETRATE, which sort by centroid and can
//     sort wrongly.  That is a modelling error in this library's terms: a pump
//     does not pass through a tank.  Bodies that merely touch are separated by a
//     plane and always come out right.
//
// What that buys is the reason it was chosen: no second pixel path (section 3.5
// keeps BL* names inside two translation units), no depth buffer to allocate per
// frame, antialiasing and clipping for free, and the same
// Canvas -> Painter -> paintTree route means a scene renders into an
// OffscreenImage without a window -- which is how the golden test sees it.
//
// -----------------------------------------------------------------------------
// WHAT IS DELIBERATELY ABSENT: textures, shadows, perspective-correct
// interpolation, speculars, animation, more than one light, section planes.
//
// Not a backlog -- a decision.  What carries meaning on a plant display is
// SHAPE, POSITION and STATUS COLOUR; the operator is answering one question,
// "which pump has gone red".  Texture and shadow spend contrast, and status
// colour is what needs the contrast.  One directional light fixed in CAMERA
// space plus an ambient term, so that orbiting a model does not make its overall
// brightness swim.
//
#include <cstdint>
#include <vector>

#include "geeyoou/core/Signal.hpp"
#include "geeyoou/render/Painter.hpp"
#include "geeyoou/scene3d/Camera.hpp"
#include "geeyoou/scene3d/Mesh.hpp"
#include "geeyoou/scene3d/Scene3D.hpp"
#include "geeyoou/widget/Widget.hpp"

namespace geeyoou {

// What decides a part's colour.
//
// Three, because an operator asks three different questions of the same model
// and no single colouring answers all of them.
enum class ColorMode : std::uint8_t {
  Status,    // the alarm question: which one has gone red
  Material,  // the geometry question: what is this thing, ignore the alarms
  Value,     // the process question: where is it hot -- a ramp over partValue()
};

class View3D : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(View3D, Widget)

  View3D();

  // The view does NOT own the scene and does not extend its lifetime -- the same
  // contract TableView has with TableModel.  Passing nullptr is legal and is how
  // an application takes a scene away before destroying it.
  //
  // THIS IS ALSO WHERE THE FRAME BUFFERS ARE SIZED.  Everything the renderer
  // needs per frame is reserved here, once, from the scene's own counts, so that
  // onPaint never allocates -- see the budget note on Scene3D and the case in
  // tests/widget/test_scene3d.cpp that asserts it.
  void setScene(Scene3D* s);
  Scene3D* scene() const { return scene_; }

  const Camera& camera() const { return camera_; }
  void setCamera(const Camera& c);
  // Put the whole scene on screen.  A no-op with no scene, which is why it is
  // safe to call from a builder before anything has been added.
  void frameAll();
  // frameAll(), plus the default orientation.  What a "reset view" button calls.
  void resetView();

  void setColorMode(ColorMode m);
  ColorMode colorMode() const { return colorMode_; }

  // Callouts: a leader line from a part to a small label.  Off costs nothing --
  // the scene is not asked for them at all.
  void setAnnotationsVisible(bool on);
  bool areAnnotationsVisible() const { return notes_; }

  // A ground reference.  Worth having: without one, an orbit reads as the model
  // wobbling rather than as the camera moving.
  //
  // ⚠️ It does NOT take part in depth sorting -- it is drawn first, so it is
  // always behind the model.  Correct while looking down at a model standing on
  // it, which is what a plant view does; from below, the floor is drawn behind
  // the model rather than in front of it.  Stated rather than hidden.
  void setGridVisible(bool on);
  bool isGridVisible() const { return grid_; }

  // Draws the part under the pointer a shade brighter.  Costs one pick per mouse
  // move; turn it off for a scene that is being driven by something else.
  void setHoverHighlight(bool on);

  void setSelectedPart(PartId id);
  PartId selectedPart() const { return selected_; }
  PartId hoveredPart() const { return hovered_; }

  // What the LAST paint actually submitted, after culling.  Diagnostic, and the
  // number the budget test asserts an upper bound on.
  std::size_t lastDrawnFaces() const { return drawnFaces_; }

  // Which part is under a point in this widget's own coordinates, or kNoPart.
  //
  // It answers from the LAST FRAME's projected geometry, which is what makes it
  // agree with what is on screen by construction: you pick the thing you can
  // see, not the thing a separately-written ray caster thinks is there.  Before
  // the first paint it answers kNoPart rather than rendering on demand.
  PartId partAt(Point local) const;

  Signal<PartId> partClicked;
  Signal<> cameraChanged;

  SizeHint sizeHint() const override;

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;
  void onMouse(const MouseEvent& e) override;
  void onGeometryChanged() override;

 private:
  // A vertex after transform.  Both spaces are kept because both are needed and
  // neither can be recovered from the other: `view` shades the face and sorts
  // it, `screen` draws and picks it.
  struct ProjVert {
    Vec3 view;
    Point screen;
    bool ok = false;  // in front of the eye; a face with any false vertex is dropped
  };

  // One convex body, ready to be ordered.
  struct BodyRef {
    std::uint32_t node = 0;
    std::uint32_t body = 0;
    std::uint32_t vertexBase = 0;  // where this node's vertices start in proj_
    float depth = 0.0f;            // view-space z of the body centre; more negative = further
  };

  // Resolves every part's colour and visibility ONCE per frame into partLook_.
  //
  // Not an optimisation for its own sake: it is what makes "each part is read at
  // most once per frame" true, which Scene3D counts and the budget case asserts.
  // The obvious shape -- looking the part up inside the body loop -- reads it
  // once per BODY, and a vessel is three bodies wearing one part.
  void resolveParts();
  void projectScene(const Rect& viewport);
  // The viewport's own ground, and it deliberately does NOT follow the theme's
  // field colour.  See the definition: a white field cannot show a white model.
  void paintBackdrop(Painter& p, const Rect& viewport) const;
  void paintGrid(Painter& p, const Rect& viewport) const;
  void paintAnnotations(Painter& p, const Rect& viewport) const;
  void paintBodies(Painter& p);
  void paintEmpty(Painter& p) const;
  Color colorFor(const Scene3D::Part& part, bool highlighted) const;
  Color accentFor(const Scene3D::Part& part) const;
  // One statement each, so the emit is the last thing the frame does.
  void emitCameraChanged();
  void emitPartClicked(PartId id);

  Scene3D* scene_ = nullptr;
  Camera camera_;

  // Has anybody moved the camera?  Until they have, a resize re-frames; after
  // they have, a resize leaves their view alone.  Reset hands it back.
  bool cameraTouched_ = false;
  bool grid_ = true;
  bool notes_ = true;
  ColorMode colorMode_ = ColorMode::Status;
  bool hoverHighlight_ = true;
  PartId hovered_ = kNoPart;
  PartId selected_ = kNoPart;

  // --- drag state ---
  bool dragging_ = false;
  bool panning_ = false;
  Point dragLast_;
  Point pressAt_;
  float dragTravel_ = 0.0f;  // how far the pointer has moved since the press

  // --- per-frame scratch, reserved by setScene and never grown in onPaint ---
  struct PartLook {
    Color color;   // what the body is filled with, per the colour mode
    // What a CALLOUT is drawn in, and it is deliberately not `color`.  A label's
    // job is "which part, and how is it" -- so it follows the STATUS even while
    // the model is being shaded by material or by value.  It also has to stay
    // legible on a white label, and a part whose material is white would give a
    // border nobody can see.
    Color accent;
    bool visible = true;
  };
  std::vector<PartLook> partLook_;
  std::vector<ProjVert> proj_;
  std::vector<BodyRef> order_;
  std::vector<std::uint32_t> nodeVertexBase_;
  bool hasFrame_ = false;
  std::size_t drawnFaces_ = 0;
  Mat4 lastProj_;
};

}  // namespace geeyoou
