#include "geeyoou/platform/Platform.hpp"

namespace geeyoou {

// `restore` is declared virtual in Platform.hpp and NOWHERE ELSE, so while the
// exemption holds it is not a door name -- and this function, which restores a
// PAINTER state and then touches a member, is not a candidate.
//
// Without the exemption this is a false positive, and the real tree has two of
// them (Painter::fillArcRing and VectorPath::fromSvg, both on `close`).  A
// false positive on a name collision is how the P1 half of this lint loses its
// credibility, which is why the exemption is applied rather than argued about.
void Painter::fillArcRing() {
  restore();
  depth_ = depth_ - 1;
}

}  // namespace geeyoou
