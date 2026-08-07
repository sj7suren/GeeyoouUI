// THE SECOND HOLE, as a fixture.
//
// Section 11.4 records two DIFFERENT reasons the enumeration missed a door, and
// says in as many words that closing one does not close the other:
//
//   * layoutRect() was missed because A VIRTUAL CALL HAS NO NAME TO GREP.  The
//     fix is declaration-side generation.
//   * Layout::onInvalidated / onChildAppended / onChildRemoved were missed
//     because THE SUBJECT WAS WRITTEN TOO NARROWLY -- "any virtual of a widget"
//     -- and Layout is not a widget.  Declaration-side generation rooted at
//     Widget.hpp misses all three of them just as thoroughly.
//
// So this fixture declares its virtual in a header that is NOT the widget base,
// and the source below calls it.  A scan root of `Widget.hpp` passes this case
// while being broken; only a root of the whole include tree catches it.
#pragma once

namespace geeyoou {

class Layouty {
 protected:
  virtual void onInvalidated();
};

}  // namespace geeyoou
