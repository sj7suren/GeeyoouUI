#include "geeyoou/scene3d/View3D.hpp"

#include <algorithm>
#include <cmath>

#include "geeyoou/render/Theme.hpp"

namespace geeyoou {
namespace {
// The light, fixed in CAMERA space.  Fixing it to the camera rather than to the
// world is what stops a model's overall brightness from swimming as it is
// orbited -- on a plant display the thing that changes colour must be the thing
// whose STATE changed, and nothing else.
// Well off the view axis on purpose.  A light close to the camera lights every
// face that can be seen at almost the same angle, and a box then comes out with
// three faces the same shade -- which reads as a flat hexagon rather than as a
// box.  Shape is one of the three things this control exists to convey, so the
// key light is thrown up and to the left where it separates the three.
const Vec3 kLightDir = Vec3{-0.42f, 0.72f, 0.55f}.normalized();
// Low enough that the unlit side is clearly darker, high enough that a part in
// shadow still shows its STATUS COLOUR -- which is the one thing that must never
// be lost to lighting.
constexpr float kAmbient = 0.34f;

// Overdraw, and it is a COVERAGE fix rather than a cosmetic one.
//
// Blend2D antialiases each fill independently.  Two triangles sharing an edge
// each cover the pixels along it partially -- say a and 1-a -- and compositing
// one over the other leaves `a(1-a)` of the background showing, up to a quarter
// of it.  That is a real gap, not a rounding artefact, and on a cylinder wall it
// draws the diagonal of every quad as a hairline; in perspective those hairlines
// converge and the shell looks like it is made of wire.
//
// ⚠️ THE FIRST ATTEMPT PUSHED EACH VERTEX AWAY FROM THE FACE CENTROID, AND THAT
// DOES ALMOST NOTHING FOR THE EDGE THAT MATTERS.  On a long thin triangle the
// shared diagonal's endpoints lie nearly along their own radial direction, so
// they slide ALONG the edge instead of across it.  The offset has to be applied
// to the EDGES -- each pushed outward along its own normal, then intersected --
// which is what inflateTriangle below does.
constexpr float kSeamBleed = 0.6f;

constexpr float kMinWidth = 200.0f;
constexpr float kMinHeight = 160.0f;
constexpr float kPreferredWidth = 520.0f;
constexpr float kPreferredHeight = 380.0f;

constexpr float kOrbitPerPixel = 0.010f;
constexpr float kPanPerPixel = 0.0022f;
constexpr float kWheelZoom = 1.12f;
// Farther than this from the press and it was a drag, not a click.  Four pixels
// is about the shake of a hand on a plant HMI trackball.
constexpr float kClickSlop = 4.0f;

// THE VIEWPORT HAS ITS OWN GROUND, AND IT IS NEVER THE THEME'S FIELD COLOUR.
//
// This is the fix for a defect that only shows under a light skin: `field` there
// is #FFFFFF, the models are light grey, and a white model on a white field is
// not a subtle contrast problem -- it is an invisible one.  A 3D viewport is not
// a text box; it is a window onto a world, and every CAD and modelling tool on
// the planet gives it a mid-tone of its own for exactly this reason.
//
// So the backdrop is picked from the theme's BRIGHTNESS rather than from any of
// its colours, and it is clamped to the middle of the range in both directions:
// dark enough to show a white vessel, light enough to show a dark one.
bool themeIsLight(const Theme& t) {
  // Rec. 601 luma, which is close enough for a light/dark decision and needs no
  // colour-space machinery.
  const float y = 0.299f * float(t.background.red()) +
                  0.587f * float(t.background.green()) +
                  0.114f * float(t.background.blue());
  return y > 127.0f;
}

// Lighting DARKENS.  Always, under every skin.
//
// The first version mixed unlit faces towards Theme::background, which is a
// perfectly reasonable-looking line of code and is wrong twice over: under a
// light skin it makes shadows WHITE, so a model loses its shading exactly where
// it needed it most.  Shading is physics; the theme has no vote.
const Color kShadow = Color::rgb(0x07, 0x0A, 0x11);

float signedArea(Point a, Point b, Point c) {
  return (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
}

// Grows a triangle by pushing each of its three edges `d` pixels outward along
// its own normal and re-intersecting them.  Exact for any shape, including the
// long thin ones a cylinder wall is made of.
//
// Degenerate inputs (a sliver, two coincident vertices) leave the vertex where
// it was: the intersection of two near-parallel offset lines runs off to
// infinity, and a triangle drawn a thousand pixels away is a far worse artefact
// than the seam this is fixing.
void inflateTriangle(Point& a, Point& b, Point& c, float d) {
  const Point ctr{(a.x + b.x + c.x) / 3.0f, (a.y + b.y + c.y) / 3.0f};

  struct Line {
    Point p;  // a point on the offset line
    Point v;  // its direction
    bool ok = false;
  };

  auto offset = [&](Point p, Point q) {
    Line l;
    const float dx = q.x - p.x;
    const float dy = q.y - p.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len < 1e-4f) return l;
    // Either normal will do; take the one that points away from the centroid.
    float nx = -dy / len;
    float ny = dx / len;
    if ((p.x - ctr.x) * nx + (p.y - ctr.y) * ny < 0.0f) {
      nx = -nx;
      ny = -ny;
    }
    l.p = Point{p.x + nx * d, p.y + ny * d};
    l.v = Point{dx / len, dy / len};
    l.ok = true;
    return l;
  };

  // The displacement is CAPPED, and that cap is the whole difference between a
  // clean surface and a screenful of spikes.  A sliver triangle -- and a
  // tessellated cylinder is full of them near its silhouette -- has two edges
  // that are almost parallel, so their offset lines meet a very long way away.
  // Rejecting only the exactly-parallel case let those through, and the first
  // build after this function landed drew several hundred-pixel needles across
  // the model.  Anything that wants to move further than a few times `d` is a
  // vertex the offset cannot help; it stays where it is, and the worst that
  // costs is the hairline this was fixing, on one edge.
  const float maxMove = d * 6.0f;
  auto meet = [maxMove](const Line& l1, const Line& l2, Point fallback) {
    if (!l1.ok || !l2.ok) return fallback;
    const float det = l1.v.x * (-l2.v.y) - l1.v.y * (-l2.v.x);
    if (std::fabs(det) < 1e-4f) return fallback;
    const float rx = l2.p.x - l1.p.x;
    const float ry = l2.p.y - l1.p.y;
    const float t = (rx * (-l2.v.y) - ry * (-l2.v.x)) / det;
    const Point hit{l1.p.x + l1.v.x * t, l1.p.y + l1.v.y * t};
    const float mx = hit.x - fallback.x;
    const float my = hit.y - fallback.y;
    if (mx * mx + my * my > maxMove * maxMove) return fallback;
    return hit;
  };

  const Line ab = offset(a, b);
  const Line bc = offset(b, c);
  const Line ca = offset(c, a);

  const Point na = meet(ca, ab, a);
  const Point nb = meet(ab, bc, b);
  const Point nc = meet(bc, ca, c);
  a = na;
  b = nb;
  c = nc;
}

bool pointInTriangle(Point p, Point a, Point b, Point c) {
  const float d1 = signedArea(a, b, p);
  const float d2 = signedArea(b, c, p);
  const float d3 = signedArea(c, a, p);
  const bool neg = (d1 < 0.0f) || (d2 < 0.0f) || (d3 < 0.0f);
  const bool pos = (d1 > 0.0f) || (d2 > 0.0f) || (d3 > 0.0f);
  return !(neg && pos);
}
}  // namespace

View3D::View3D() { setFocusPolicy(FocusPolicy::Tab); }

// EVERY per-frame buffer is sized here and never grown again.  onPaint indexes
// into these; it does not push_back into them.  That is the whole of the
// zero-allocation claim, and tests/widget/test_scene3d.cpp holds it to it.
void View3D::setScene(Scene3D* s) {
  scene_ = s;
  hasFrame_ = false;
  cameraTouched_ = false;
  hovered_ = kNoPart;
  drawnFaces_ = 0;

  proj_.clear();
  order_.clear();
  nodeVertexBase_.clear();
  if (!scene_) {
    update();
    return;
  }

  proj_.resize(scene_->vertexCount());
  partLook_.resize(scene_->partCount());
  order_.reserve(scene_->bodyCount());
  nodeVertexBase_.reserve(scene_->nodeCount());

  frameAll();
  update();
}

void View3D::setCamera(const Camera& c) {
  camera_ = c;
  cameraTouched_ = true;
  update();
}

void View3D::frameAll() {
  if (!scene_) return;
  // The viewport's aspect if there is one yet.  A view that is framed before it
  // has been given a geometry -- which is the normal order when a page builds
  // its widgets -- frames against a square and is re-framed by the first resize.
  const Rect r = localRect();
  const float aspect =
      r.height() > 1.0f ? r.width() / r.height() : 1.0f;
  camera_.frame(scene_->bounds(), aspect);
  update();
}

void View3D::resetView() {
  camera_.yawRad = 0.7f;
  camera_.pitchRad = 0.45f;
  cameraTouched_ = false;
  frameAll();
}

void View3D::setGridVisible(bool on) {
  grid_ = on;
  update();
}

void View3D::setColorMode(ColorMode m) {
  colorMode_ = m;
  update();
}

void View3D::setAnnotationsVisible(bool on) {
  notes_ = on;
  update();
}

void View3D::setHoverHighlight(bool on) {
  hoverHighlight_ = on;
  hovered_ = kNoPart;
  update();
}

void View3D::setSelectedPart(PartId id) {
  selected_ = id;
  update();
}

SizeHint View3D::sizeHint() const {
  // A VIEWPORT, not a contents -- the same rule ADR-R2-09 states for every
  // window-onto-a-model control in this library.  A hint derived from the
  // scene's extent would make a bigger plant demand a bigger panel, which is
  // exactly backwards: a bigger plant is why you zoom out.
  SizeHint h;
  h.preferred = Size{kPreferredWidth, kPreferredHeight};
  h.min = Size{kMinWidth, kMinHeight};
  return h;
}

void View3D::onGeometryChanged() {
  hasFrame_ = false;
  // Re-frame ONLY while the operator has not touched the camera.  Re-framing
  // after every resize would undo a zoom the moment somebody dragged the window
  // edge; never re-framing would leave the first framing -- computed against a
  // square, before any geometry existed -- in place forever.
  if (!cameraTouched_ && scene_) frameAll();
}

// ============================================================== projection ===
//
// One pass over every vertex of every visible node, into `proj_`.  The camera
// matrices are built once for the frame rather than per node, and the model
// matrix is folded in per node -- so a vertex costs two matrix-vector products
// and a divide, and nothing here allocates.
void View3D::projectScene(const Rect& viewport) {
  order_.clear();
  nodeVertexBase_.clear();
  if (!scene_ || viewport.isEmpty()) return;

  const float aspect = viewport.width() / std::max(1.0f, viewport.height());
  const Mat4 view = camera_.view();
  const Mat4 proj = camera_.projection(aspect);
  lastProj_ = proj;

  const float halfW = viewport.width() * 0.5f;
  const float halfH = viewport.height() * 0.5f;

  std::uint32_t base = 0;
  for (std::size_t ni = 0; ni < scene_->nodeCount(); ++ni) {
    const Scene3D::Node& n = scene_->node(ni);
    nodeVertexBase_.push_back(base);
    const std::size_t vcount = n.mesh.vertexCount();
    if (!n.visible || n.mesh.empty()) {
      base += std::uint32_t(vcount);
      continue;
    }

    const Mat4 modelView = view * n.transform;
    const Vec3* verts = n.mesh.vertices();
    for (std::size_t i = 0; i < vcount; ++i) {
      ProjVert& out = proj_[base + i];
      out.view = modelView.transformPoint(verts[i]);
      float w = 1.0f;
      const Vec3 clip = proj.transformPoint(out.view, &w);
      // w <= 0 means at or behind the eye.  This renderer does NOT clip against
      // the near plane -- it drops such faces -- which is why Camera::frame sets
      // a minimum distance that keeps the eye outside the model.  A dropped face
      // is a hole; a clipped one would be a second rasteriser.
      out.ok = w > 1e-4f;
      if (!out.ok) continue;
      const float inv = 1.0f / w;
      out.screen = Point{viewport.x() + halfW + clip.x * inv * halfW,
                         viewport.y() + halfH - clip.y * inv * halfH};
    }

    const Body* bodies = n.mesh.bodies();
    for (std::size_t bi = 0; bi < n.mesh.bodyCount(); ++bi) {
      BodyRef r;
      r.node = std::uint32_t(ni);
      r.body = std::uint32_t(bi);
      r.vertexBase = base;
      r.depth = modelView.transformPoint(bodies[bi].center).z;
      order_.push_back(r);
    }
    base += std::uint32_t(vcount);
  }

  // Farthest first.  View-space z is NEGATIVE in front of the eye, so "more
  // negative" is "further away" and ascending order paints back to front.
  std::sort(order_.begin(), order_.end(),
            [](const BodyRef& a, const BodyRef& b) { return a.depth < b.depth; });
}

// ================================================================ painting ===
//
// ONE pass over the parts, before any geometry is touched.  See the declaration
// for why this exists rather than a lookup inside the body loop.
void View3D::resolveParts() {
  if (!scene_) return;
  const std::size_t n = scene_->partCount();
  for (std::size_t i = 0; i < n; ++i) {
    const Scene3D::Part& p = scene_->part(PartId(i));
    const bool hot = (PartId(i) == selected_) ||
                     (hoverHighlight_ && PartId(i) == hovered_);
    partLook_[i].color = colorFor(p, hot);
    partLook_[i].accent = accentFor(p);
    partLook_[i].visible = p.visible;
  }
}

// The heat ramp: cold -> nominal -> warm -> over.
//
// Built from the theme's SEMANTIC colours rather than from hard-coded hues, so a
// plant that recolours "warning" gets a ramp that still matches its own alarm
// pages -- which is the whole reason those tokens are named by meaning.
Color rampColor(const Theme& t, float v) {
  v = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
  if (v < 0.34f) return t.accent.lerp(t.success, v / 0.34f);
  if (v < 0.67f) return t.success.lerp(t.warn, (v - 0.34f) / 0.33f);
  return t.warn.lerp(t.danger, (v - 0.67f) / 0.33f);
}

// The callout colour: the part's CONDITION, or the theme's accent when there is
// nothing to report.  Never the material -- see PartLook::accent.
Color View3D::accentFor(const Scene3D::Part& p) const {
  const Theme& t = Theme::current();
  switch (p.state) {
    case PartState::Running: return t.success;
    case PartState::Stopped: return t.textDim;
    case PartState::Fault: return t.danger;
    case PartState::Maintenance: return t.warn;
    case PartState::Disabled: return t.textDisabled;
    case PartState::Normal:
    default: return t.accent;
  }
}

Color View3D::colorFor(const Scene3D::Part& p, bool highlighted) const {
  const Theme& t = Theme::current();
  Color base = t.panelBorder;

  if (colorMode_ == ColorMode::Material) {
    base = p.material;
  } else if (colorMode_ == ColorMode::Value) {
    base = rampColor(t, p.value);
  } else {
    switch (p.state) {
      // Resolved HERE, against the theme that is live now.  Never stored.
      case PartState::Running:     base = t.success; break;
      case PartState::Stopped:     base = t.textDim; break;
      case PartState::Fault:       base = t.danger; break;
      case PartState::Maintenance: base = t.warn; break;
      case PartState::Disabled:    base = t.textDisabled; break;
      case PartState::Normal:
      default:
        // No status to report, so the part is drawn in its MATERIAL -- a stored,
        // absolute colour, because steel is grey under every skin.  That split
        // is what lets both live here without the captured-theme bug returning.
        base = p.material;
        break;
    }
  }
  if (highlighted) base = base.lerp(Color::rgb(0xFF, 0xFF, 0xFF), 0.28f);
  return base;
}

void View3D::paintBodies(Painter& p) {
  drawnFaces_ = 0;

  for (const BodyRef& ref : order_) {
    const Scene3D::Node& n = scene_->node(ref.node);
    const Body& body = n.mesh.bodies()[ref.body];

    // A TABLE LOOKUP, not a scene query.  resolveParts() already answered this
    // once for the whole frame; asking again here is what would turn "once per
    // part" into "once per body".
    if (body.part >= partLook_.size()) continue;
    const PartLook& look = partLook_[body.part];
    if (!look.visible) continue;
    const Color base = look.color;

    const Face* faces = n.mesh.faces();
    for (std::uint32_t fi = body.faceBegin; fi < body.faceEnd; ++fi) {
      const Face& f = faces[fi];
      const ProjVert& a = proj_[ref.vertexBase + f.a];
      const ProjVert& b = proj_[ref.vertexBase + f.b];
      const ProjVert& c = proj_[ref.vertexBase + f.c];
      if (!a.ok || !b.ok || !c.ok) continue;

      // BACK-FACE CULL IN SCREEN SPACE.  Doing it after projection rather than
      // with a view-space dot product gets the perspective for free and is one
      // cross product; it is also the same number the picker uses, so what is
      // drawn and what is pickable cannot disagree.
      const float area = signedArea(a.screen, b.screen, c.screen);
      if (area <= 0.0f) continue;

      const Vec3 nrm = (b.view - a.view).cross(c.view - a.view).normalized();
      const float diffuse = std::max(0.0f, nrm.dot(kLightDir));
      const float shade = kAmbient + (1.0f - kAmbient) * diffuse;
      // Towards the shadow tone, never towards the page.  Capped at 0.72 so a
      // face in full shade still shows WHICH COLOUR it is -- losing the status
      // colour to the lighting would defeat the point of the control.
      const Color col = base.lerp(kShadow, (1.0f - shade) * 0.72f);

      Point sa = a.screen;
      Point sb = b.screen;
      Point sc = c.screen;
      inflateTriangle(sa, sb, sc, kSeamBleed);
      p.fillTriangle(sa, sb, sc, col);
      ++drawnFaces_;
    }
  }
}

// A vertical gradient, in bands, because Painter has no gradient and does not
// need one for this: sixteen strips across a viewport are invisible as bands and
// cost sixteen rectangles.  Lighter at the top, the way a sky is, which also
// gives the eye a horizon to judge the orbit against.
void View3D::paintBackdrop(Painter& p, const Rect& viewport) const {
  const Theme& t = Theme::current();
  const bool light = themeIsLight(t);
  // Mid-tones, both of them, and neither taken from the theme's own surfaces.
  const Color top = light ? Color::rgb(0xC2, 0xC9, 0xD6) : Color::rgb(0x26, 0x2E, 0x3C);
  const Color bottom = light ? Color::rgb(0x94, 0x9E, 0xB0) : Color::rgb(0x11, 0x15, 0x1D);

  const int bands = 16;
  const float h = viewport.height() / float(bands);
  for (int i = 0; i < bands; ++i) {
    const float f = (float(i) + 0.5f) / float(bands);
    p.fillRect({viewport.x(), viewport.y() + h * float(i), viewport.width(), h + 1.0f},
               top.lerp(bottom, f));
  }
}

void View3D::paintGrid(Painter& p, const Rect& viewport) const {
  if (!scene_ || scene_->bounds().empty()) return;

  const Theme& t = Theme::current();
  const Bounds3& b = scene_->bounds();
  const float y = b.lo.y;
  const float half = std::max(b.radius(), 1e-3f) * 1.6f;
  const int lines = 10;
  const float step = half * 2.0f / float(lines);

  const float aspect = viewport.width() / std::max(1.0f, viewport.height());
  const Mat4 vp = camera_.projection(aspect) * camera_.view();
  const float halfW = viewport.width() * 0.5f;
  const float halfH = viewport.height() * 0.5f;
  const Vec3 c = b.center();

  auto project = [&](const Vec3& world, Point& out) {
    float w = 1.0f;
    const Vec3 clip = vp.transformPoint(world, &w);
    if (w <= 1e-4f) return false;
    const float inv = 1.0f / w;
    out = Point{viewport.x() + halfW + clip.x * inv * halfW,
                viewport.y() + halfH - clip.y * inv * halfH};
    return true;
  };

  // Off the BACKDROP, not off the theme: t.grid is a near-white under a light
  // skin and would vanish into a mid-grey viewport.
  const bool light = themeIsLight(t);
  const Color line = light ? Color::rgb(0x7C, 0x86, 0x99).withAlpha(190)
                           : Color::rgb(0x3A, 0x44, 0x58).withAlpha(190);
  for (int i = 0; i <= lines; ++i) {
    const float o = -half + step * float(i);
    Point p0, p1;
    if (project({c.x + o, y, c.z - half}, p0) && project({c.x + o, y, c.z + half}, p1)) {
      p.strokeLine(p0, p1, line, 1.0f);
    }
    if (project({c.x - half, y, c.z + o}, p0) && project({c.x + half, y, c.z + o}, p1)) {
      p.strokeLine(p0, p1, line, 1.0f);
    }
  }
}

// Callouts, drawn LAST so nothing in the model covers them.
//
// The anchor is the part's centre pushed by the annotation's offset; the label
// sits a fixed number of PIXELS from it, so a label stays readable at any zoom
// rather than growing with the model.  A label whose anchor is behind the eye is
// dropped -- there is no sensible place on screen for "behind you".
void View3D::paintAnnotations(Painter& p, const Rect& viewport) const {
  if (!scene_ || scene_->annotationCount() == 0) return;

  const Theme& t = Theme::current();
  const bool light = themeIsLight(t);
  const Color panel = light ? Color::rgb(0xFF, 0xFF, 0xFF).withAlpha(235)
                            : Color::rgb(0x14, 0x19, 0x23).withAlpha(235);
  const Color titleCol = light ? Color::rgb(0x17, 0x1F, 0x2E) : Color::rgb(0xE6, 0xEB, 0xF4);
  const Color valueCol = light ? Color::rgb(0x5A, 0x68, 0x80) : Color::rgb(0x86, 0x94, 0xAD);

  const float aspect = viewport.width() / std::max(1.0f, viewport.height());
  const Mat4 vp = camera_.projection(aspect) * camera_.view();
  const float halfW = viewport.width() * 0.5f;
  const float halfH = viewport.height() * 0.5f;

  for (std::size_t i = 0; i < scene_->annotationCount(); ++i) {
    const Scene3D::Annotation& a = scene_->annotation(AnnotationId(i));
    if (!a.visible) continue;
    if (a.part < partLook_.size() && !partLook_[a.part].visible) continue;

    const Vec3 world = scene_->partCenter(a.part) + a.offset;
    float w = 1.0f;
    const Vec3 clip = vp.transformPoint(world, &w);
    if (w <= 1e-4f) continue;
    const float inv = 1.0f / w;
    const Point anchor{viewport.x() + halfW + clip.x * inv * halfW,
                       viewport.y() + halfH - clip.y * inv * halfH};
    // A MARGIN, not the viewport itself.  A callout is offset ABOVE the part it
    // names, and the camera frames the model's bounds -- which do not include
    // the offset -- so the anchor of a label on the tallest part is routinely a
    // few dozen pixels off the top edge.  Dropping those made the labels that
    // mattered most the ones that vanished.  Anchors BEHIND the eye are still
    // dropped, above: there is no sensible place on screen for "behind you".
    if (!viewport.deflated(-64.0f).contains(anchor)) continue;

    const Color accent =
        (a.part < partLook_.size()) ? partLook_[a.part].accent : t.accent;

    const float tw = measureText(a.title, t.fontSmall).width;
    const float vw = a.value.empty() ? 0.0f : measureText(a.value, t.fontSmall).width;
    const float boxW = std::max(tw, vw) + 18.0f;
    const float boxH = a.value.empty() ? 22.0f : 34.0f;

    // Up and to the right, then pulled back inside the viewport if that would
    // take it off the edge.  A callout that leaves the widget is a callout that
    // gets clipped in half.
    float bx = anchor.x + 26.0f;
    float by = anchor.y - 30.0f - boxH * 0.5f;
    if (bx + boxW > viewport.right() - 4.0f) bx = anchor.x - 26.0f - boxW;
    if (by < viewport.y() + 4.0f) by = anchor.y + 26.0f;

    const Rect box(bx, by, boxW, boxH);
    p.strokeLine(anchor, {box.x() < anchor.x ? box.right() : box.x(),
                          box.center().y}, accent.withAlpha(200), 1.0f);
    p.fillCircle(anchor, 3.0f, accent);

    p.fillRoundRect(box, 5.0f, panel);
    p.strokeRoundRect(box.deflated(0.5f), 5.0f, accent.withAlpha(210), 1.0f);
    // The status colour repeated as a bar on the leading edge: at a glance the
    // label says WHICH part and HOW it is, without reading either line.
    p.fillRoundRect({box.x() + 2.0f, box.y() + 4.0f, 3.0f, box.height() - 8.0f}, 1.5f,
                    accent);

    if (a.value.empty()) {
      p.drawText({box.x() + 12.0f, box.center().y}, a.title, t.fontSmall, titleCol,
                 HAlign::Left, VAlign::Middle);
    } else {
      p.drawText({box.x() + 12.0f, box.y() + 11.0f}, a.title, t.fontSmall, titleCol,
                 HAlign::Left, VAlign::Middle);
      p.drawText({box.x() + 12.0f, box.y() + 24.0f}, a.value, t.fontSmall, valueCol,
                 HAlign::Left, VAlign::Middle);
    }
  }
}

void View3D::paintEmpty(Painter& p) const {
  const Theme& t = Theme::current();
  const Rect r = localRect();
  // Against the BACKDROP, which is a mid-tone under either skin, so the message
  // needs a colour of its own rather than the theme's dim text.
  const Color fg = themeIsLight(t) ? Color::rgb(0x4A, 0x53, 0x63)
                                   : Color::rgb(0x8A, 0x95, 0xA8);
  p.drawText(r.center(), "未加载三维模型", t.fontBody, fg, HAlign::Center,
             VAlign::Middle);
}

void View3D::onPaint(Painter& p, const Rect&) {
  const Theme& t = Theme::current();
  const Rect r = localRect();

  const Rect viewport = r.deflated(1.0f);

  p.save();
  p.clip(r);
  if (!viewport.isEmpty()) paintBackdrop(p, viewport);
  p.restore();
  p.strokeRoundRect(r.deflated(0.5f), t.radius,
                    hasFocus() ? t.focusRing : t.panelBorder, 1.0f);

  if (!scene_ || scene_->nodeCount() == 0 || viewport.isEmpty()) {
    paintEmpty(p);
    return;
  }

  p.save();
  p.clip(viewport);
  resolveParts();
  projectScene(viewport);
  if (grid_) paintGrid(p, viewport);
  paintBodies(p);
  if (notes_) paintAnnotations(p, viewport);
  p.restore();

  hasFrame_ = true;
}

// ================================================================= picking ===
//
// FRONT TO BACK over the same face list the frame drew, so the answer is the
// thing on top.  `order_` is farthest-first for painting, so this walks it in
// reverse -- and within a convex body at most one front face can cover a point,
// which is why no further ordering is needed once a body is entered.
PartId View3D::partAt(Point local) const {
  if (!hasFrame_ || !scene_) return kNoPart;

  for (auto it = order_.rbegin(); it != order_.rend(); ++it) {
    const BodyRef& ref = *it;
    const Scene3D::Node& n = scene_->node(ref.node);
    const Body& body = n.mesh.bodies()[ref.body];
    if (body.part >= partLook_.size()) continue;
    if (!partLook_[body.part].visible) continue;

    const Face* faces = n.mesh.faces();
    for (std::uint32_t fi = body.faceBegin; fi < body.faceEnd; ++fi) {
      const Face& f = faces[fi];
      const ProjVert& a = proj_[ref.vertexBase + f.a];
      const ProjVert& b = proj_[ref.vertexBase + f.b];
      const ProjVert& c = proj_[ref.vertexBase + f.c];
      if (!a.ok || !b.ok || !c.ok) continue;
      if (signedArea(a.screen, b.screen, c.screen) <= 0.0f) continue;
      if (pointInTriangle(local, a.screen, b.screen, c.screen)) return body.part;
    }
  }
  return kNoPart;
}

// =================================================================== input ===
void View3D::emitCameraChanged() { cameraChanged.emit(); }

void View3D::emitPartClicked(PartId id) { partClicked.emit(id); }

void View3D::onMouse(const MouseEvent& e) {
  if (!isEffectivelyEnabled()) return;

  // Decided in the switch, announced after it.  A signal emitted from inside a
  // `case` has a `break` after it, and a break is code after a door -- the same
  // shape TablePager::onMouse was rewritten for.
  enum class Fire { None, Camera, Click };
  Fire fire = Fire::None;
  PartId clicked = kNoPart;

  switch (e.action) {
    case MouseAction::Leave:
      if (hovered_ != kNoPart) {
        hovered_ = kNoPart;
        update();
      }
      dragging_ = false;
      e.accept();
      break;

    case MouseAction::Press:
      if (e.button == MouseButton::Left) {
        dragging_ = true;
        // Shift is the pan modifier: this library's MouseEvent has no middle
        // button convention and no double click, so the two drags are told apart
        // by a key rather than by a button that may not exist on a plant panel.
        panning_ = e.shift;
        dragLast_ = e.pos;
        pressAt_ = e.pos;
        dragTravel_ = 0.0f;
        setFocus();
      }
      e.accept();
      break;

    case MouseAction::Move: {
      if (dragging_) {
        const float dx = e.pos.x - dragLast_.x;
        const float dy = e.pos.y - dragLast_.y;
        dragLast_ = e.pos;
        dragTravel_ += std::fabs(dx) + std::fabs(dy);
        cameraTouched_ = true;
        if (panning_) {
          camera_.pan(-dx * kPanPerPixel, dy * kPanPerPixel);
        } else {
          // Dragging right turns the model right, which means moving the CAMERA
          // left -- the sign here is the difference between a control that feels
          // like a turntable and one that feels broken.
          camera_.orbit(-dx * kOrbitPerPixel, dy * kOrbitPerPixel);
        }
        update();
        fire = Fire::Camera;
      } else if (hoverHighlight_) {
        const PartId h = partAt(e.pos);
        if (h != hovered_) {
          hovered_ = h;
          update();
        }
      }
      e.accept();
      break;
    }

    case MouseAction::Release:
      if (e.button == MouseButton::Left && dragging_) {
        dragging_ = false;
        // A press and a release in the same place is a click; anything further
        // was an orbit that happened to end over a part.
        if (dragTravel_ <= kClickSlop) {
          clicked = partAt(e.pos);
          selected_ = clicked;
          update();
          fire = Fire::Click;
        }
      }
      e.accept();
      break;

    case MouseAction::Wheel:
      cameraTouched_ = true;
      camera_.dolly(e.wheelDelta > 0.0f ? 1.0f / kWheelZoom : kWheelZoom);
      update();
      fire = Fire::Camera;
      e.accept();
      break;

    default:
      break;
  }

  if (fire == Fire::None) return;
  if (fire == Fire::Click) {
    emitPartClicked(clicked);
    return;
  }
  emitCameraChanged();
}

}  // namespace geeyoou
