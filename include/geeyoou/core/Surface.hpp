#pragma once
//
// The pixel buffer a platform backend hands upwards.
//
// This lives in core/ rather than in platform/ or render/ on purpose: it is the
// contract BETWEEN those two layers, and putting it in either one would make
// that layer depend on the other.  In core/ both already depend on it and no
// new edge appears.  See docs/architecture.md section 2.
//
#include <cstddef>
#include <cstdint>

namespace geeyoou {

// A writable, top-down, 32-bit premultiplied BGRA image.
//
// The format is not negotiable: it is byte-identical to a Win32 32bpp BI_RGB
// DIB section AND to Blend2D's BL_FORMAT_PRGB32, which is what lets the whole
// pipeline run with zero copies.  An X11 or Cocoa backend must present the same
// layout (both can).
struct Surface {
  void* pixels = nullptr;
  int width = 0;   // PHYSICAL pixels
  int height = 0;  // PHYSICAL pixels
  std::intptr_t stride = 0;
  float dpr = 1.0f;  // device pixel ratio, for the render layer's transform

  bool valid() const { return pixels && width > 0 && height > 0 && stride != 0; }
};

}  // namespace geeyoou
