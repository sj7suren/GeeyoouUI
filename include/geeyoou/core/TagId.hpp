#pragma once
//
// TagId: the identity of a process variable.
//
// An upper computer's data is addressed by TAG -- "P-101.流量", "TIC-205.PV" --
// and today this library passes those around as std::string (DataHub::channel,
// AlarmRecord::tag).  A string identity costs a comparison per character on
// every alarm filter, an allocation per copy, and it makes "is this the same
// point?" a question about text rather than about the plant.  TagId is the
// handle those strings resolve to once, at configuration time.
//
// THE LAYOUT OF THE VALUE SPACE, and why it is this and not something else:
//
//   0                     Invalid.  Zero is what a default-constructed or
//                         memset field holds, and it must never name a real
//                         point -- exactly the reason Icon::None is 0.
//   1 .. 0x7FFF'FFFF      Interned by a TagRegistry, in the order it first saw
//                         each name.  Dense, so a registry can index a vector
//                         with them.
//   0x8000'0000 and up    EXTERNAL: a point whose identity is owned by another
//                         system (an OPC UA server, a PLC's symbol table, a
//                         historian).  The high bit is set.
//
// The reserved range is the TOP BIT rather than a block at the bottom (which is
// how Icon::FirstCustom does it) because the two questions are not the same
// one.  Icon has a built-in set and asks "is this one of mine?", which is a
// range comparison against a list that grows every release.  TagId has no
// built-in set at all; what anybody ever asks is "did this come from outside?",
// and that should be one bit test that no future release can invalidate.
//
// A TagId is only meaningful together with the registry that issued it.  Two
// registries hand out the same small integers, so passing an id from one to the
// other is the same class of mistake as passing an index into the wrong vector.
// Almost every application has exactly one (defaultTagRegistry); the ones that
// do not have it because they must, and they are the reason the class is not a
// singleton.  See core/TagRegistry.hpp.
//
#include <cstdint>

namespace geeyoou {

enum class TagId : std::uint32_t {
  Invalid = 0,
  // The first id that belongs to an external system.  Not "the number of
  // built-in tags": there are none.
  FirstExternal = 0x8000'0000u,
};

// One bit test.  A tag from an external system is not less real than an
// interned one -- this says where its NAME is authoritative, which is what
// decides whether this process may rename it.
constexpr bool isExternal(TagId t) {
  return (static_cast<std::uint32_t>(t) &
          static_cast<std::uint32_t>(TagId::FirstExternal)) != 0u;
}

constexpr bool isValid(TagId t) { return t != TagId::Invalid; }

// The underlying integer.  Provided because a tag id has to survive a trip
// through a protocol frame, a log line or a configuration file, and a
// static_cast at every one of those sites is a static_cast that will eventually
// be written on the wrong type.
constexpr std::uint32_t tagValue(TagId t) { return static_cast<std::uint32_t>(t); }

// The inverse.  DELIBERATELY NOT CHECKED against any registry: this is the
// deserialisation edge, and the only honest thing to do with an unknown number
// is to hand it back as a TagId whose name() lookup then comes back empty.
constexpr TagId tagFromValue(std::uint32_t v) { return static_cast<TagId>(v); }

}  // namespace geeyoou
