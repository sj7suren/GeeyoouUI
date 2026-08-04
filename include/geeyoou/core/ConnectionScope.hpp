#pragma once
//
// ConnectionScope: subscriptions that end when the subscriber does.
//
// The bug this exists to make unwritable:
//
//     skins().changed.connect([this] { update(); });   // never disconnected
//
// The skin registry is a process-lifetime singleton, so a slot capturing a
// widget's `this` outlives that widget by however long the program keeps
// running -- and repaints a corpse on the next skin change.  Every such site
// used to hold a Connection by hand and remember to disconnect it in its
// destructor.  "Remember to" is not a lifetime policy.
//
//     class Panel : public Widget {
//       ...
//       conns_ += skins().changed.connect([this] { update(); });
//      private:
//       ConnectionScope conns_;   // declared LAST: destroyed FIRST
//     };
//
// Declaring the scope last matters.  Members are destroyed in reverse order, so
// a scope declared after everything it captures unsubscribes before any of that
// state is gone.
//
// Deliberately in core/ and deliberately ignorant of Widget: the pairing of a
// subscription with an owner's lifetime is not a widget problem, and a data
// channel or a protocol decoder needs exactly the same thing.
//
#include <cstddef>
#include <utility>
#include <vector>

#include "geeyoou/core/Signal.hpp"

namespace geeyoou {

class ConnectionScope {
 public:
  ConnectionScope() = default;
  ~ConnectionScope() { clear(); }

  // Non-copyable for the same reason a Signal is: two owners of one
  // subscription means the second release is either a double disconnect or a
  // dangling one.
  ConnectionScope(const ConnectionScope&) = delete;
  ConnectionScope& operator=(const ConnectionScope&) = delete;

  ConnectionScope(ConnectionScope&&) noexcept = default;
  ConnectionScope& operator=(ConnectionScope&& other) noexcept {
    if (this != &other) {
      clear();
      items_ = std::move(other.items_);
    }
    return *this;
  }

  // Takes ownership of whatever connect() returned.  Invalid handles are
  // dropped rather than stored, so a scope's size is the number of live
  // subscriptions and not the number of times somebody called it.
  ConnectionScope& operator+=(Connection c) {
    if (c.valid()) items_.push_back(std::move(c));
    return *this;
  }

  void add(Connection c) { *this += std::move(c); }

  // Unsubscribes everything, in reverse order of subscription.  Reverse because
  // that is the order the rest of C++ releases things in, and because a slot
  // added later is the more likely one to depend on an earlier one still being
  // wired.
  //
  // Safe against senders that died first: Connection::disconnect() routes
  // through a weak reference to the signal and does nothing when it has gone.
  void clear() {
    for (std::size_t i = items_.size(); i > 0; --i) items_[i - 1].disconnect();
    items_.clear();
  }

  bool empty() const { return items_.empty(); }
  std::size_t size() const { return items_.size(); }

 private:
  std::vector<Connection> items_;
};

}  // namespace geeyoou
