#pragma once
//
// TagRegistry: names in, TagIds out, and never the reverse.
//
// THREE DECISIONS, all of them load-bearing:
//
// 1. IT IS A CLASS, NOT A SINGLETON.  A singleton would make it impossible to
//    talk to two devices whose tag namespaces collide -- which is the normal
//    case for an upper computer talking to four PLCs from three vendors -- and
//    it would make every test that interns a name depend on every test that ran
//    before it.  This library already pays for that mistake once: Theme is a
//    global, and so every golden case has to open with resetStyling().  There
//    is a process-wide instance, defaultTagRegistry(), for the applications
//    that want one; it is a function-local static, so it costs nothing until
//    somebody asks for it and it cannot be destroyed before its last user.
//
// 2. IT ONLY EVER GROWS.  There is no remove(), no clear(), no reuse of ids.
//    A control room screen runs for months, and the moment an id could be
//    recycled, every TagId already sitting in an alarm record, a trend buffer
//    or a queued protocol frame becomes a value that silently means something
//    else.  The memory an unused name holds is a few dozen bytes; the bug is a
//    wrong number on a screen, which is the one failure mode this library
//    exists to avoid.  Registries are built from configuration at startup and
//    then read from, so "grows without bound" is bounded by the config file.
//
// 3. NAMES ARE STORED ONCE, AND THEIR ADDRESSES ARE STABLE.  The map owns the
//    text; the id->name vector holds pointers INTO it.  That works because an
//    unordered_map is node-based: rehashing moves nodes, not the strings inside
//    them.  The same trick with a vector<string> would break the day the vector
//    reallocated, because a short name lives inside the string object itself
//    (SSO) and moves with it -- which is the sort of bug that survives every
//    test on the developer's machine and shows up as a corrupted tag name in
//    the plant.
//
// Threading: UI/configuration thread only, like everything else in this
// library.  Interning from an acquisition thread while a page reads names is
// undefined; resolve tags at configuration time, which is when their names are
// known anyway.
//
#include <cstddef>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "geeyoou/core/TagId.hpp"

namespace geeyoou {

class TagRegistry {
 public:
  TagRegistry() = default;

  TagRegistry(const TagRegistry&) = delete;
  TagRegistry& operator=(const TagRegistry&) = delete;

  // The name's id, assigning one if this is the first time it has been seen.
  // The same name always comes back as the same id, for the lifetime of this
  // registry.
  //
  // An EMPTY name is not a tag and gets TagId::Invalid rather than an id of its
  // own: an empty string is what an unset configuration field holds, and giving
  // it a real identity would make "no tag configured" indistinguishable from a
  // point that happens to be called "".
  TagId intern(std::string_view name);

  // The name behind an id, or an empty view when this registry did not issue
  // it -- which includes every external id, whose name lives in the system that
  // owns it.  The view is valid for as long as the registry is.
  std::string_view name(TagId id) const;

  // Whether the name has an id already.  Distinct from intern() != Invalid,
  // which would CREATE one: a query that has a side effect on the thing being
  // queried is how a typo in a configuration file ends up as a permanent
  // registry entry.
  bool contains(std::string_view name) const;

  // Its id, or Invalid.  intern()'s const half.
  TagId find(std::string_view name) const;

  // How many names have been interned.  Only grows.
  std::size_t size() const { return byId_.size(); }
  bool empty() const { return byId_.empty(); }

 private:
  // Transparent hashing, so a lookup with a string_view does not have to build
  // a std::string first.  Resolving a tag name from a protocol frame is a hot
  // path in a driver, and an allocation per lookup of an ALREADY KNOWN name is
  // the kind of waste that is invisible until the frame rate is.
  struct Hash {
    using is_transparent = void;
    std::size_t operator()(std::string_view s) const noexcept {
      return std::hash<std::string_view>{}(s);
    }
  };

  std::unordered_map<std::string, TagId, Hash, std::equal_to<>> ids_;
  // Index = tagValue(id) - 1.  Points into the keys of ids_, which are stable.
  std::vector<const std::string*> byId_;
};

// The process-wide registry, for applications that have one namespace.  A
// function-local static: constructed on first use, destroyed after everything
// that ran before it, and no static initialisation order to reason about.
TagRegistry& defaultTagRegistry();

}  // namespace geeyoou
