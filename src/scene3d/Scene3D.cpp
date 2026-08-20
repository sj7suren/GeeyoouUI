#include "geeyoou/scene3d/Scene3D.hpp"

#include <cassert>

namespace geeyoou {
namespace {
// Returned by part() for an id nobody ever handed out.  A reference has to name
// something, and the alternatives are worse: an assert would turn a diagnostic
// question into a crash, and a null would move the problem one line down the
// caller.  A part that is Normal, invisible and unnamed is inert wherever it
// lands.
const Scene3D::Part& missingPart() {
  static const Scene3D::Part p = [] {
    Scene3D::Part x;
    x.visible = false;
    return x;
  }();
  return p;
}
}  // namespace

PartId Scene3D::addPart(std::string name, Color material) {
  // kNoPart is the sentinel a Face carries when it belongs to nothing, so the
  // ids handed out must never reach it.  A scene with 65 535 parts is not a
  // scene, it is a mistake, and it is better to say so here.
  assert(parts_.size() < kNoPart && "too many parts for a PartId");
  Part p;
  p.name = std::move(name);
  p.material = material;
  parts_.push_back(std::move(p));
  return PartId(parts_.size() - 1);
}

const Scene3D::Part& Scene3D::part(PartId id) const {
  if (!validPart(id)) return missingPart();
  ++partReads_;
  return parts_[id];
}

PartId Scene3D::findPart(const std::string& name) const {
  for (std::size_t i = 0; i < parts_.size(); ++i) {
    if (parts_[i].name == name) return PartId(i);
  }
  return kNoPart;
}

void Scene3D::setPartState(PartId id, PartState s) {
  if (!validPart(id)) return;
  parts_[id].state = s;
}

void Scene3D::setPartValue(PartId id, float v) {
  if (!validPart(id)) return;
  parts_[id].value = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

float Scene3D::partValue(PartId id) const {
  return validPart(id) ? parts_[id].value : 0.0f;
}

PartState Scene3D::partState(PartId id) const {
  return validPart(id) ? parts_[id].state : PartState::Normal;
}

void Scene3D::setPartVisible(PartId id, bool on) {
  if (!validPart(id)) return;
  parts_[id].visible = on;
}

void Scene3D::setPartMaterial(PartId id, Color c) {
  if (!validPart(id)) return;
  parts_[id].material = c;
}

void Scene3D::bindPart(PartId id, TagId tag) {
  if (!validPart(id)) return;
  parts_[id].tag = tag;
}

// The budget is checked HERE, once, when geometry arrives -- not in the
// renderer.  A view that re-counted triangles every frame would be paying for
// the check sixty times a second in order to report a mistake that was made once
// at build time.  Over budget, the node is refused rather than truncated: half a
// pump drawn silently is worse than a pump that is visibly absent.
std::size_t Scene3D::addNode(const Mesh& mesh, const Mat4& transform) {
  assert(triangles_ + mesh.faceCount() <= kMaxTriangles &&
         "scene over the triangle budget -- see Scene3D::kMaxTriangles");
  if (triangles_ + mesh.faceCount() > kMaxTriangles) return nodeCount();

  Node n;
  n.mesh = mesh;
  n.transform = transform;
  nodes_.push_back(n);
  triangles_ += mesh.faceCount();
  bodies_ += mesh.bodyCount();
  vertices_ += mesh.vertexCount();
  recomputeBounds();
  recomputePartCenters();
  return nodes_.size() - 1;
}

void Scene3D::setNodeTransform(std::size_t i, const Mat4& m) {
  if (i >= nodes_.size()) return;
  nodes_[i].transform = m;
  recomputeBounds();
  recomputePartCenters();
}

void Scene3D::setNodeVisible(std::size_t i, bool on) {
  if (i >= nodes_.size()) return;
  nodes_[i].visible = on;
}

Vec3 Scene3D::partCenter(PartId id) const {
  if (id < partCenters_.size()) return partCenters_[id];
  return bounds_.empty() ? Vec3{} : bounds_.center();
}

AnnotationId Scene3D::addAnnotation(PartId part, std::string title, Vec3 offset) {
  Annotation a;
  a.part = part;
  a.title = std::move(title);
  a.offset = offset;
  notes_.push_back(std::move(a));
  return AnnotationId(notes_.size() - 1);
}

const Scene3D::Annotation& Scene3D::annotation(AnnotationId id) const {
  static const Annotation none = [] {
    Annotation a;
    a.visible = false;
    return a;
  }();
  return validAnnotation(id) ? notes_[id] : none;
}

void Scene3D::setAnnotationText(AnnotationId id, std::string title,
                                std::string value) {
  if (!validAnnotation(id)) return;
  notes_[id].title = std::move(title);
  notes_[id].value = std::move(value);
}

void Scene3D::setAnnotationValue(AnnotationId id, std::string value) {
  if (!validAnnotation(id)) return;
  notes_[id].value = std::move(value);
}

void Scene3D::setAnnotationVisible(AnnotationId id, bool on) {
  if (!validAnnotation(id)) return;
  notes_[id].visible = on;
}

void Scene3D::clear() {
  nodes_.clear();
  parts_.clear();
  notes_.clear();
  partCenters_.clear();
  bounds_ = Bounds3{};
  triangles_ = 0;
  bodies_ = 0;
  vertices_ = 0;
}

// The average of a part's body centres, in world space.  Body centres rather
// than vertices: a body already carries the centre its sorting sphere uses, so
// this is O(bodies) instead of O(vertices) and needs no second definition of
// "where is this thing".
void Scene3D::recomputePartCenters() {
  partCenters_.assign(parts_.size(), Vec3{});
  std::vector<int> counts(parts_.size(), 0);
  for (const Node& n : nodes_) {
    const Body* bodies = n.mesh.bodies();
    for (std::size_t bi = 0; bi < n.mesh.bodyCount(); ++bi) {
      const PartId p = bodies[bi].part;
      if (p >= parts_.size()) continue;
      partCenters_[p] += n.transform.transformPoint(bodies[bi].center);
      ++counts[p];
    }
  }
  for (std::size_t i = 0; i < partCenters_.size(); ++i) {
    if (counts[i] > 0) partCenters_[i] = partCenters_[i] * (1.0f / float(counts[i]));
    else partCenters_[i] = bounds_.empty() ? Vec3{} : bounds_.center();
  }
}

// The eight corners of each mesh's local box, pushed through that node's
// transform.  Transforming the box rather than the vertices is what keeps this
// O(nodes) instead of O(vertices) -- and a rotated box's corners bound the
// rotated model, which is all a camera needs to frame it.
void Scene3D::recomputeBounds() {
  bounds_ = Bounds3{};
  for (const Node& n : nodes_) {
    const Bounds3& b = n.mesh.bounds();
    if (b.empty()) continue;
    for (int i = 0; i < 8; ++i) {
      const Vec3 corner{(i & 1) ? b.hi.x : b.lo.x, (i & 2) ? b.hi.y : b.lo.y,
                        (i & 4) ? b.hi.z : b.lo.z};
      bounds_.expand(n.transform.transformPoint(corner));
    }
  }
}

}  // namespace geeyoou
