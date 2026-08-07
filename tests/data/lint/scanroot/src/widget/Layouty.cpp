#include "geeyoou/widget/Layouty.hpp"

namespace geeyoou {

// N1's shape, reduced: a hook declared in a header that is not the widget base,
// and a member read on the line after it.
void Layouty::invalidate() {
  onInvalidated();
  if (host_) host_->performLayout();
}

}  // namespace geeyoou
