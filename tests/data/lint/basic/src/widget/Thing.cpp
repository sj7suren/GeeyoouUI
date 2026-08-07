// Fixture translation unit for tools/test-lint-door-coverage.ps1.
//
// Every function here is a case.  The names say what each one is FOR, because a
// fixture whose purpose has to be reconstructed from its behaviour is a fixture
// that gets deleted the first time it is inconvenient.
#include "geeyoou/widget/Thing.hpp"

namespace geeyoou {

// CANDIDATE.  A door (P1 onDecorated) with a member read after it and no
// cursor.  This is the shape the whole lint exists for.
void Thing::unguardedDoorThenMemberRead() {
  onDecorated();
  count_ = count_ + 1;
}

// NOT A CANDIDATE.  Same door, but the frame holds a cursor.
void Thing::guardedDoorThenMemberRead() {
  const detail::DeathWatch self(this);
  onDecorated();
  if (!self.alive()) return;
  count_ = count_ + 1;
}

// NOT A CANDIDATE.  The door is the last statement, so there is no "after" for
// anything to be dangerous in.  Section 11.4 rows #5, #6 and #10.
void Thing::doorIsTheLastStatement() {
  count_ = count_ + 1;
  onDecorated();
}

// NOT A CANDIDATE.  A QUALIFIED call is statically bound, so it is not a P1
// door -- section 11.4 says so in as many words.
void Thing::qualifiedCallIsNotADoor() {
  Thing::onDecorated();
  count_ = count_ + 1;
}

// NOT A CANDIDATE.  The door name appears only in a comment, and this file is
// full of them: onDecorated(); sizeHint(); layoutRect(); emit(x).  The comments
// in the real tree are longer than the code and they quote the door names in
// order to explain the doors, so a scanner that reads raw text reports a door
// inside the sentence that documents it.
void Thing::doorNameOnlyInAComment() {
  count_ = count_ + 1;
  count_ = count_ + 2;
}

// NOT A CANDIDATE.  The door name appears only inside a string literal.
void Thing::doorNameOnlyInAString() {
  const char* msg = "call onDecorated() then read count_";
  count_ = count_ + 1;
  (void)msg;
}

// CANDIDATE.  Proves the override-with-no-`virtual` declaration shape reaches
// the call site: layoutRect() is declared with `override` alone.
void Thing::overrideOnlyVirtualIsStillADoor() {
  layoutRect();
  count_ = count_ + 1;
}

// NOT A CANDIDATE.  `Thing` is a DESTRUCTOR name, not a door name.  If the
// declaration scan mistakes `~Thing() override` for a virtual member called
// `Thing`, this constructor call turns into a door and this function turns into
// a false positive.
void Thing::destructorNameIsNotADoorName() {
  Thing* t = new Thing();
  count_ = count_ + 1;
  delete t;
}

}  // namespace geeyoou
