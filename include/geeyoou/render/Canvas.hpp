#pragma once
//
// Binds a Surface to the rendering backend and hands out a Painter.
//
// This is the piece that used to live inside the Win32 backend.  Moving it here
// means a new platform backend supplies pixels and nothing else -- it never
// learns that Blend2D exists.  See docs/architecture.md section 3.5.
//
#include <memory>

#include "geeyoou/core/Surface.hpp"
#include "geeyoou/core/Types.hpp"
#include "geeyoou/render/Painter.hpp"

namespace geeyoou {

class Canvas {
 public:
  Canvas();
  ~Canvas();
  Canvas(const Canvas&) = delete;
  Canvas& operator=(const Canvas&) = delete;

  // Attaches to `surface` and opens a drawing pass restricted to
  // `dirtyPhysical` (physical pixels).  The returned Painter is already scaled
  // by the surface's dpr, so everything drawn through it is in LOGICAL pixels.
  //
  // Re-attaching to the same pixels/size is cheap; the backing image object is
  // only rebuilt when the surface actually changes.
  bool begin(const Surface& surface, const Rect& dirtyPhysical);
  Painter painter();
  void end();

 private:
  struct Impl;
  std::unique_ptr<Impl> d_;
};

}  // namespace geeyoou
