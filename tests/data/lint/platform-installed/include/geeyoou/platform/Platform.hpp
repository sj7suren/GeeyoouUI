// Section 11.4's exemption for Platform.hpp, as a fixture.
//
// The 21 non-destructor virtuals of the real Platform.hpp are exempt, and the
// reason is STRUCTURAL rather than a usage observation: `Platform& platform()`
// is the only way to obtain an implementation and there is no setter, no
// registration point and no factory, so an application HAS NO INTERFACE to
// override them through.  Section 11.4 marks the difference explicitly --
// "this is not 'nobody does that today', it is 'there is no install point'" --
// and records that "nobody does that today" has lost six times running.
//
// Two things are being tested here, and they are opposite:
//
//   1. While there is no install point, these names are OUT of the P1 set.  Not
//      as a nicety: PlatformWindow declares restore(), show(), close() and
//      invalidate(), and leaving them in makes every painter.restore() and
//      every Layout::invalidate() look like a door.  That is a lint nobody
//      believes, and a lint nobody believes is off.
//   2. The moment an install point appears, the exemption has LAPSED and the
//      gate must go red -- see the -with-install-point variant of this file.
#pragma once

namespace geeyoou {

class PlatformWindow {
 public:
  virtual void restore();
  virtual void show();
  virtual void invalidate();
};

class Platform {
 public:
  virtual PlatformWindow* createWindow();
};

Platform& platform();

}  // namespace geeyoou

// The install point section 11.4 said would end the exemption.
namespace geeyoou { void setPlatform(Platform* p); }
