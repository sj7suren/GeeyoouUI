//
// TagId and TagRegistry (T-12).
//
// Nothing in the library uses these yet, on purpose: DataHub::channel and
// AlarmRecord::tag keep their std::string fields for now, because doing a
// layout migration and an identity migration in the same round means two blast
// radii overlapping.  What this file pins down is the CONTRACT those fields
// will be migrated onto, while it is still cheap to change.
//
#include <cstdint>
#include <string>
#include <vector>

#include "framework/Test.hpp"
#include "geeyoou/core/TagId.hpp"
#include "geeyoou/core/TagRegistry.hpp"

using geeyoou::isExternal;
using geeyoou::isValid;
using geeyoou::TagId;
using geeyoou::tagFromValue;
using geeyoou::TagRegistry;
using geeyoou::tagValue;
using geeyoou::test::AllocGuard;

GEEYOOU_TEST(tag, zero_is_never_a_tag_and_the_top_bit_is_never_ours) {
  CHECK_EQ(tagValue(TagId::Invalid), std::uint32_t(0));
  CHECK(!isValid(TagId::Invalid));
  CHECK(!isExternal(TagId::Invalid));

  // The reserved range is one bit, so "did this come from outside" is one test
  // rather than a comparison against a list that grows every release.
  CHECK_EQ(tagValue(TagId::FirstExternal), std::uint32_t(0x80000000u));
  CHECK(isExternal(TagId::FirstExternal));
  CHECK(isExternal(tagFromValue(0xFFFFFFFFu)));
  CHECK(isExternal(tagFromValue(0x80000001u)));
  CHECK(!isExternal(tagFromValue(0x7FFFFFFFu)));

  // constexpr, so a switch over well-known tags stays a jump table.
  static_assert(isExternal(TagId::FirstExternal), "");
  static_assert(!isExternal(TagId::Invalid), "");
}

GEEYOOU_TEST(tag, the_same_name_is_always_the_same_id) {
  TagRegistry reg;
  CHECK_EQ(reg.size(), std::size_t(0));

  const TagId flow = reg.intern("P-101.流量");
  const TagId temp = reg.intern("TIC-205.PV");
  CHECK(isValid(flow));
  CHECK(isValid(temp));
  CHECK_NE(flow, temp);
  CHECK_EQ(reg.size(), std::size_t(2));

  // The point of the whole class.
  CHECK_EQ(reg.intern("P-101.流量"), flow);
  CHECK_EQ(reg.intern(std::string("P-101.流量")), flow);
  CHECK_EQ(reg.size(), std::size_t(2));

  // Interned ids are ours: dense, from 1, and never in the external half.
  CHECK_EQ(tagValue(flow), std::uint32_t(1));
  CHECK_EQ(tagValue(temp), std::uint32_t(2));
  CHECK(!isExternal(flow));

  CHECK_EQ(reg.name(flow), std::string_view("P-101.流量"));
  CHECK_EQ(reg.name(temp), std::string_view("TIC-205.PV"));
}

GEEYOOU_TEST(tag, an_unknown_id_answers_empty_instead_of_guessing) {
  TagRegistry reg;
  const TagId known = reg.intern("PT-303");

  CHECK(reg.name(TagId::Invalid).empty());
  // Never issued by this registry.
  CHECK(reg.name(tagFromValue(tagValue(known) + 7u)).empty());
  // An external id: real, but its name lives in the system that owns it.
  CHECK(reg.name(TagId::FirstExternal).empty());
  CHECK(reg.name(tagFromValue(0x8000002Au)).empty());

  // An empty name is "no tag configured", not a point called "".
  CHECK_EQ(reg.intern(""), TagId::Invalid);
  CHECK_EQ(reg.size(), std::size_t(1));
  CHECK(!reg.contains(""));
}

GEEYOOU_TEST(tag, asking_whether_a_name_is_known_does_not_make_it_known) {
  // A query with a side effect on the thing queried is how a typo in a
  // configuration file becomes a permanent registry entry.
  TagRegistry reg;
  reg.intern("FV-12");

  CHECK(reg.contains("FV-12"));
  CHECK(!reg.contains("FV-13"));
  CHECK_EQ(reg.find("FV-13"), TagId::Invalid);
  CHECK_EQ(reg.size(), std::size_t(1));
  CHECK_EQ(reg.find("FV-12"), reg.intern("FV-12"));
}

GEEYOOU_TEST(tag, two_registries_do_not_see_each_other) {
  // The reason this is a class and not a singleton: four PLCs from three
  // vendors, and two of them call the first analogue input "AI0".
  TagRegistry plcA;
  TagRegistry plcB;

  const TagId a = plcA.intern("AI0");
  const TagId b = plcB.intern("AI0");
  CHECK_EQ(a, b);  // same small integer...
  CHECK(!plcB.contains("PUMP1"));
  plcA.intern("PUMP1");
  CHECK(plcA.contains("PUMP1"));
  CHECK(!plcB.contains("PUMP1"));  // ...and completely separate namespaces

  // The process-wide instance is one of these, not a different kind of thing.
  geeyoou::defaultTagRegistry().intern("SYS.HEARTBEAT");
  CHECK(geeyoou::defaultTagRegistry().contains("SYS.HEARTBEAT"));
  CHECK(!plcA.contains("SYS.HEARTBEAT"));
}

GEEYOOU_TEST(tag, names_survive_the_map_rehashing_under_them) {
  // byId_ holds pointers into the map's keys.  That is only sound because an
  // unordered_map is node-based; if this ever silently became a flat map, the
  // short names -- the ones that live inside the string object itself -- would
  // be the first to come back as garbage.  Enough entries to force several
  // rehashes, and every one of them re-read afterwards.
  TagRegistry reg;
  std::vector<TagId> ids;
  ids.reserve(512);
  for (int i = 0; i < 512; ++i) {
    ids.push_back(reg.intern("T" + std::to_string(i)));  // short: SSO territory
  }
  CHECK_EQ(reg.size(), std::size_t(512));

  for (int i = 0; i < 512; ++i) {
    const std::string expected = "T" + std::to_string(i);
    CHECK_EQ(reg.name(ids[std::size_t(i)]), std::string_view(expected));
    CHECK_EQ(tagValue(ids[std::size_t(i)]), std::uint32_t(i + 1));
  }
}

GEEYOOU_TEST(tag, resolving_a_known_name_allocates_nothing) {
  // A driver resolves the tag of every frame it decodes.  Building a
  // std::string per lookup of a name that has been known since startup is the
  // waste that is invisible until the frame rate is -- hence the transparent
  // hash on the map.
  TagRegistry reg;
  const TagId id = reg.intern("TIC-205.PV");
  const std::string_view name("TIC-205.PV");

  AllocGuard guard;
  guard.reset();
  for (int i = 0; i < 1000; ++i) {
    CHECK_EQ(reg.find(name), id);
    CHECK(reg.contains(name));
    CHECK_EQ(reg.intern(name), id);
  }
  CHECK_EQ(guard.count(), std::uint64_t(0));
  CHECK_EQ(guard.frees(), std::uint64_t(0));
}
