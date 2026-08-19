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

  // Shape 4: protected virtual -- the access specifier is not part of the test,
  // which is the whole point of scanning declarations rather than call sites.
 protected:
  virtual void onDecorated();
};

}  // namespace geeyoou
