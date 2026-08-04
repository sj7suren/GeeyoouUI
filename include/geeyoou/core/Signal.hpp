#pragma once
//
// Signals and slots without a meta-object compiler.
//
// Qt needed moc because 1990s C++ had no lambdas, no std::function and no
// variadic templates.  With C++20 a signal is just a vector of callables, so
// GeeyoouUI has no code-generation step at all.
//
// What we give up is runtime reflection (looking a property up by name), which
// QML-style dynamic binding needs.  An HMI library does not.
// See docs/architecture.md section 3.2.
//
#include <cstdint>
#include <functional>
#include <utility>
#include <vector>

namespace geeyoou {

template <class... Args>
class Signal;

// Handle returned by connect(); disconnecting through it is optional.
class Connection {
 public:
  Connection() = default;

  bool valid() const { return id_ != 0; }
  std::uint64_t id() const { return id_; }

 private:
  template <class...>
  friend class Signal;

  explicit Connection(std::uint64_t id) : id_(id) {}
  std::uint64_t id_ = 0;
};

template <class... Args>
class Signal {
 public:
  using Slot = std::function<void(Args...)>;

  Signal() = default;

  // Non-copyable: a signal belongs to the object that declares it.  Copying one
  // would silently duplicate subscriptions.
  Signal(const Signal&) = delete;
  Signal& operator=(const Signal&) = delete;
  Signal(Signal&&) = default;
  Signal& operator=(Signal&&) = default;

  Connection connect(Slot slot) {
    const std::uint64_t id = ++lastId_;
    slots_.push_back(Entry{id, std::move(slot)});
    return Connection(id);
  }

  void disconnect(const Connection& c) {
    for (auto it = slots_.begin(); it != slots_.end(); ++it) {
      if (it->id == c.id()) {
        slots_.erase(it);
        return;
      }
    }
  }

  void disconnectAll() { slots_.clear(); }

  bool empty() const { return slots_.empty(); }

  // Named emit() rather than operator() so call sites read as `sig.emit(v)`.
  //
  // The slot list is copied before dispatch so a slot may legally connect,
  // disconnect, or destroy things mid-emission without invalidating our
  // iterator.  This costs an allocation per emission, which is why signals must
  // not be fired from a per-frame hot path -- HMI value updates are event-rate
  // (Hz to kHz), not pixel-rate.
  void emit(Args... args) const {
    if (slots_.empty()) return;
    const std::vector<Entry> snapshot = slots_;
    for (const Entry& e : snapshot) {
      if (e.fn) e.fn(args...);
    }
  }

 private:
  struct Entry {
    std::uint64_t id;
    Slot fn;
  };

  std::vector<Entry> slots_;
  std::uint64_t lastId_ = 0;
};

}  // namespace geeyoou
