#pragma once
//
// PRIVATE to the library.  Not installed, never reachable from include/.
//
// VectorPath hides its BLPath behind a pimpl so consumers do not need Blend2D
// on their include path.  Two translation units inside the library still need
// to see it -- VectorPath.cpp to build the path and Painter.cpp to draw it --
// and defining the same struct in both would be an ODR violation, benign or
// not.  One private header is the honest fix.
//
#include <blend2d/blend2d.h>

#include "geeyoou/render/VectorPath.hpp"

namespace geeyoou {

struct VectorPath::Impl {
  BLPath path;
};

}  // namespace geeyoou
