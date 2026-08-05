#include "geeyoou/core/TagRegistry.hpp"

#include <cassert>

namespace geeyoou {

TagId TagRegistry::intern(std::string_view name) {
  if (name.empty()) return TagId::Invalid;

  // Heterogeneous lookup: no std::string is built for a name that is already
  // known, which is the overwhelmingly common case once configuration is done.
  const auto it = ids_.find(name);
  if (it != ids_.end()) return it->second;

  // The ids are dense and start at 1, so this cannot collide with Invalid, and
  // it cannot reach FirstExternal until two billion tags have been interned --
  // at which point the assert says so rather than the id quietly becoming an
  // external one.  Release builds get Invalid, which fails loudly at the point
  // of use instead of naming the wrong point.
  const std::uint32_t next = std::uint32_t(byId_.size()) + 1u;
  assert(next < tagValue(TagId::FirstExternal) &&
         "TagRegistry exhausted its half of the id space");
  if (next >= tagValue(TagId::FirstExternal)) return TagId::Invalid;

  const auto inserted = ids_.emplace(std::string(name), tagFromValue(next));
  // The key of the node just inserted.  Node-based container: this address
  // outlives every rehash, which is the whole reason byId_ can hold pointers.
  byId_.push_back(&inserted.first->first);
  return inserted.first->second;
}

std::string_view TagRegistry::name(TagId id) const {
  // External ids are not ours to name, and neither is Invalid.  Both come back
  // empty rather than asserting: name() is what a display calls, and a screen
  // that shows a blank tag is a screen that can still be read.
  if (id == TagId::Invalid || isExternal(id)) return {};
  const std::uint32_t index = tagValue(id) - 1u;
  if (index >= byId_.size()) return {};
  return *byId_[index];
}

bool TagRegistry::contains(std::string_view name) const {
  return !name.empty() && ids_.find(name) != ids_.end();
}

TagId TagRegistry::find(std::string_view name) const {
  if (name.empty()) return TagId::Invalid;
  const auto it = ids_.find(name);
  return it == ids_.end() ? TagId::Invalid : it->second;
}

TagRegistry& defaultTagRegistry() {
  static TagRegistry instance;
  return instance;
}

}  // namespace geeyoou
