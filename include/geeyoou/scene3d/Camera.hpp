#pragma once
//
// An orbit camera, as a VALUE.
//
// No state machine, no callbacks, no pointer back to a view: four numbers and
// the two matrices they imply.  That is what makes it testable without a window
// and copyable into a saved workspace -- and it is why the drag handling lives
// in View3D rather than here.  A camera that knew about the mouse would be a
// camera that could not be unit tested.
//
// ORBIT, NOT FREE-FLY, and that is the right choice for the job: an operator
// inspecting a skid wants to walk around it, not to get lost inside it.  The
// target is the thing being looked at, and it stays the thing being looked at.
//
#include <algorithm>
#include <cmath>

#include "geeyoou/scene3d/Vec3.hpp"

namespace geeyoou {

struct Camera {
  Vec3 target;              // what the orbit goes around
  float distance = 10.0f;   // eye-to-target
  float yawRad = 0.7f;      // around +Y
  float pitchRad = 0.45f;   // above the horizon
  float fovYRad = 0.75f;    // full vertical field of view
  float zNear = 0.05f;
  float zFar = 5000.0f;

  // Pitch stops just short of the poles.  AT the pole the up vector is parallel
  // to the view direction and the basis collapses; a fifth of a degree of slack
  // is invisible and removes the whole class of problem.
  static constexpr float kMaxPitch = 1.5672f;  // ~89.8 degrees

  // A minimum that keeps the eye OUTSIDE anything it is framing.  Not vanity:
  // this renderer does not clip against the near plane -- it drops faces that
  // cross it (see View3D) -- so an eye that walks into a tank would make the
  // tank vanish rather than open up.  Framing sets this from the model's size.
  float minDistance = 0.2f;
  float maxDistance = 2000.0f;

  Vec3 eye() const {
    const float cp = std::cos(pitchRad);
    return target + Vec3{distance * cp * std::sin(yawRad), distance * std::sin(pitchRad),
                         distance * cp * std::cos(yawRad)};
  }

  Mat4 view() const { return Mat4::lookAt(eye(), target, {0.0f, 1.0f, 0.0f}); }

  Mat4 projection(float aspect) const {
    return Mat4::perspective(fovYRad, aspect, zNear, zFar);
  }

  void orbit(float dYaw, float dPitch) {
    yawRad += dYaw;
    pitchRad = std::clamp(pitchRad + dPitch, -kMaxPitch, kMaxPitch);
  }

  // MULTIPLICATIVE, not additive: one wheel notch should mean the same
  // proportional change whether the model is a valve or a tank farm.  An
  // additive zoom is the classic reason a camera crawls at one scale and jumps
  // at another.
  void dolly(float factor) {
    distance = std::clamp(distance * factor, minDistance, maxDistance);
  }

  // Pans in the camera's own plane, scaled by distance so a drag moves the model
  // by the same number of PIXELS regardless of how far away it is.
  void pan(float dRight, float dUp) {
    const Vec3 f = (target - eye()).normalized();
    Vec3 right = f.cross(Vec3{0.0f, 1.0f, 0.0f}).normalized();
    if (right.length() < 1e-6f) right = Vec3{1.0f, 0.0f, 0.0f};
    const Vec3 up = right.cross(f);
    target += right * (dRight * distance) + up * (dUp * distance);
  }

  // Put the whole box on screen, with a little air around it.  Also sets the
  // distance limits, because "how close may I get" is a question about THIS
  // model and there is no good universal answer.
  //
  // `aspect` MATTERS, and leaving it out is what made the first version frame a
  // plant skid from three times too far away.  Fitting the bounding SPHERE to
  // the vertical field is the easy formula and it is badly wrong for the shape
  // this library actually draws: a skid is wide, shallow and short, so its
  // bounding sphere is far bigger than anything you can see, and the camera
  // dutifully backs off until the sphere fits.  What has to fit is the BOX --
  // its height against the vertical field, its footprint against the horizontal
  // one -- and the horizontal field is only known once the viewport is.
  void frame(const Bounds3& b, float aspect = 1.0f) {
    if (b.empty()) return;
    target = b.center();

    const Vec3 e = b.extent();
    const float halfUp = std::max(e.y * 0.5f, 1e-3f);
    // Worst case over an orbit: x and z trade places, so the footprint that has
    // to fit sideways is the diagonal of the base rather than either edge.
    const float halfWide =
        std::max(0.5f * std::sqrt(e.x * e.x + e.z * e.z), 1e-3f);

    const float tanV = std::tan(std::max(fovYRad * 0.5f, 1e-3f));
    const float tanH = tanV * std::max(aspect, 1e-3f);
    // Plus halfWide: the near side of the model is closer than its centre, and
    // the camera has to clear that too.
    distance = std::max(halfUp / tanV, halfWide / tanH) * 1.15f + halfWide;

    const float radius = std::max(b.radius(), 1e-3f);
    minDistance = radius * 1.05f;
    maxDistance = radius * 40.0f;
    zNear = std::max(radius * 0.01f, 1e-3f);
    zFar = radius * 200.0f;
    distance = std::clamp(distance, minDistance, maxDistance);
  }
};

}  // namespace geeyoou
