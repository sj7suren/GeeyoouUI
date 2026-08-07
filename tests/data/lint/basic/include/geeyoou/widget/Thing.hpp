// Fixture header for tools/test-lint-door-coverage.ps1.
//
// The P1 half of the candidate set is generated FROM THIS FILE, not from a list
// of names at the call sites -- that is section 11.9 property 4, and it is the
// property that layoutRect() slipped through for two rounds.  So this header
// carries one of every declaration shape the generator has to get right.
#pragma once

namespace geeyoou {

class Thing {
 public:
  // Shape 1: the plain `virtual`.
  virtual int sizeHint();

  // Shape 2: an override with NO `virtual` on the line.  Half of the real tree
  // writes its overrides this way and a `virtual`-only scan misses every one.
  int layoutRect() override;

  // Shape 3: a destructor written as `~Thing() override`, i.e. WITHOUT the word
  // `virtual`.  A `virtual ~` filter lets this through and then `Thing` becomes
  // a door name, which matches every `new Thing(` in the library.  Measured on
  // the real tree: it produced five phantom door names.
  ~Thing() override;

  // Shape 5: NOT virtual at all, and named after a P2 primitive.  The P2 half
  // of the predicate does not come from this header -- it comes from the P2
  // clause of the document's section 11.4 -- so this declaration is here to
  // keep the fixture readable, not to feed the scan.  What it must not do is
  // become a P1 name: no `virtual`, no `override`, therefore no P1.
  void setGeometry();

  // Shape 6: a PLAIN INLINE DEFINITION IN A HEADER, with a door in it.
  //
  // This header is not just declarations to be harvested for P1 names -- it is
  // CODE, and the candidate side has to scan it.  It did not, for three rounds:
  // property 4's "the scan root is the whole of include\geeyoou" was applied to
  // the declaration side only, while the candidate side stayed rooted at src\.
  //
  // This case falls to the SCAN ROOT alone.  Keep it separate from shape 7
  // below, which falls to a second and independent thing, so that removing
  // either fix reddens exactly one case.
  void reseat() {
    setGeometry();
    count_ = count_ + 1;
  }

  // Shape 7: a TEMPLATE inline definition in a header, with a door in it.
  //
  // Widening the scan root buys NOTHING without this one.  `template` is in the
  // splitter's not-a-function-head list, so a template body read as "not a
  // function": the splitter descended into it looking for functions inside,
  // found none, and the body -- doors and all -- was never scanned.  Measured
  // on the real tree: AppWindow.hpp split into header/content/isBorderVisible
  // and NOT setContent, whose body is `add<T>()` followed by two writes.
  template <class T>
  T* adopt(T* child) {
    add<T>(child);
    count_ = count_ + 1;
    return child;
  }

  // Shape 4: protected virtual -- the access specifier is not part of the test,
  // which is the whole point of scanning declarations rather than call sites.
 protected:
  virtual void onDecorated();
};

}  // namespace geeyoou
