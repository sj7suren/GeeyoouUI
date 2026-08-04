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
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <utility>
#include <vector>

namespace geeyoou {

template <class... Args>
class Signal;

namespace detail {

// The one thing a Connection and the Signal that produced it share.
//
// A Connection cannot simply hold a Signal*: the SENDER is routinely destroyed
// first (a Window dies while the skin registry it subscribed to lives on for
// the rest of the process), and a raw back-pointer would turn every late
// disconnect into a use-after-free.  So the back-pointer lives here instead, in
// a block the signal owns and every connection only observes through a
// weak_ptr -- when the signal dies the block dies with it, lock() fails, and
// the disconnect is silently dropped.
//
// Type-erased through a plain function pointer rather than a std::function:
// the block is allocated once per signal and must stay small.
struct SignalBlock {
  void* signal = nullptr;
  void (*disconnectFn)(void* signal, std::uint64_t id) = nullptr;

  // Nesting depth of emit() on the owning signal.  Kept here rather than in the
  // Signal so it costs nothing in the (common) unsubscribed case, and so a
  // Debug build and a Release build agree on sizeof(Signal).
  int emitDepth = 0;

  void disconnect(std::uint64_t id) {
    if (signal && disconnectFn) disconnectFn(signal, id);
  }
};

}  // namespace detail

// Handle returned by connect().
//
// Copyable and cheap.  Holding one does NOT keep the signal alive, and both
// destruction orders are safe: disconnecting after the sender died is a no-op,
// and dropping the handle without disconnecting simply leaves the slot
// subscribed.
class Connection {
 public:
  Connection() = default;

  bool valid() const { return id_ != 0; }
  std::uint64_t id() const { return id_; }

  // Whether the signal that produced this handle is still alive.  Says nothing
  // about whether the slot is still subscribed -- that would cost a lookup, and
  // nothing has needed it.
  bool senderAlive() const { return !block_.expired(); }

  // Unsubscribes through the originating signal, whichever one that was.  This
  // is what makes ConnectionScope -- or any other RAII owner -- possible: the
  // holder no longer needs a reference to the sender just to be able to let go
  // of it.  Idempotent, and a no-op once the sender is gone.
  void disconnect() {
    if (const std::shared_ptr<detail::SignalBlock> b = block_.lock()) {
      b->disconnect(id_);
    }
    block_.reset();
    id_ = 0;
  }

 private:
  template <class...>
  friend class Signal;

  Connection(std::weak_ptr<detail::SignalBlock> block, std::uint64_t id)
      : block_(std::move(block)), id_(id) {}

  std::weak_ptr<detail::SignalBlock> block_;
  std::uint64_t id_ = 0;
};

template <class... Args>
class Signal {
 public:
  using Slot = std::function<void(Args...)>;

  Signal() = default;

  ~Signal() {
    // Contract D7: a slot may destroy anything EXCEPT the object that owns the
    // signal it is running inside.  Doing so unwinds into a list emit() is
    // still walking, and the resulting crash lands somewhere unrelated -- so it
    // is caught at the scene instead.
    assert(!isEmitting() && "Signal destroyed from inside its own emit()");
    detach();
  }

  // Non-copyable: a signal belongs to the object that declares it.  Copying one
  // would silently duplicate subscriptions.
  Signal(const Signal&) = delete;
  Signal& operator=(const Signal&) = delete;

  // Movable, but NOT with the compiler's default: the control block holds a
  // back-pointer to the signal, and a defaulted move would leave it aimed at
  // the husk we moved out of, so every later disconnect would edit the wrong
  // object.  The subscriptions travel with the slots, which is the only
  // reading of "move" that does not silently drop them.
  Signal(Signal&& other) noexcept
      : slots_(std::move(other.slots_)),
        block_(std::move(other.block_)),
        lastId_(other.lastId_) {
    if (block_) block_->signal = this;
    other.slots_.clear();
    other.lastId_ = 0;
  }

  Signal& operator=(Signal&& other) noexcept {
    if (this == &other) return *this;
    // Our own subscribers are about to lose their slots, so their handles have
    // to go dead rather than start pointing at the incoming ones.
    detach();
    slots_ = std::move(other.slots_);
    block_ = std::move(other.block_);
    lastId_ = other.lastId_;
    if (block_) block_->signal = this;
    other.slots_.clear();
    other.lastId_ = 0;
    return *this;
  }

  Connection connect(Slot slot) {
    ensureBlock();
    const std::uint64_t id = ++lastId_;
    // The callable is held behind a shared_ptr so that emit() can pin it for
    // the duration of one call.  That is what lets a slot unsubscribe ITSELF --
    // the entry leaves the vector immediately, but the callable it named stays
    // alive until it returns.  One allocation, paid once at subscribe time,
    // which is never a hot path.
    slots_.push_back(Entry{id, std::make_shared<Slot>(std::move(slot))});
    return Connection(block_, id);
  }

  void disconnect(const Connection& c) { eraseById(c.id()); }

  void disconnectAll() { slots_.clear(); }

  bool empty() const { return slots_.empty(); }
  std::size_t size() const { return slots_.size(); }

  // Whether an emission is in progress on this signal, directly or through a
  // slot that emitted it again.  Public so an owner can assert the D7 contract
  // in a Release build too.
  bool isEmitting() const { return block_ && block_->emitDepth > 0; }

  // Named emit() rather than operator() so call sites read as `sig.emit(v)`.
  //
  // Allocation-free for up to kInlineSlots subscribers, which covers every
  // signal in the library: what gets captured is a list of slot IDS on the
  // stack, not a copy of the slot list.  The old copy-the-vector approach cost
  // one heap allocation per emission plus one per std::function that did not
  // fit its small-buffer -- unacceptable for a process that runs for months
  // (docs/architecture.md section 1, rule 2).
  //
  // Contract D7, which the ID list is what implements:
  //   * a slot MAY connect;    the new slot does not run in this emission
  //   * a slot MAY disconnect; the removed slot does not run, even if it was
  //     still ahead of us -- including when it disconnects itself
  //   * a slot MAY destroy other objects
  //   * a slot MAY NOT destroy the object that owns this signal
  void emit(Args... args) const {
    if (slots_.empty()) return;
    // Invariant: a non-empty slot list means connect() ran, which allocated the
    // block.  Nothing clears one without the other.
    assert(block_ && "signal has slots but no control block");

    const std::size_t n = slots_.size();
    std::uint64_t inlineIds[kInlineSlots];
    std::vector<std::uint64_t> spilledIds;
    std::uint64_t* ids = inlineIds;
    if (n > kInlineSlots) {
      spilledIds.resize(n);
      ids = spilledIds.data();
    }
    for (std::size_t i = 0; i < n; ++i) ids[i] = slots_[i].id;

    const DepthGuard guard(block_);
    for (std::size_t i = 0; i < n; ++i) {
      // Resolved afresh every iteration: the slot we just called is allowed to
      // have added or removed entries, either of which moves them.  Nothing may
      // be held across the call except the shared_ptr below.
      const SlotPtr fn = findSlot(ids[i]);
      if (fn && *fn) (*fn)(args...);
    }
  }

 private:
  using SlotPtr = std::shared_ptr<Slot>;

  struct Entry {
    std::uint64_t id;
    SlotPtr fn;
  };

  // Eight covers every signal the library declares with room to spare; the
  // spill path exists so that a customer wiring thirty widgets to one data
  // channel degrades to the old cost instead of overflowing a buffer.
  static constexpr std::size_t kInlineSlots = 8;

  // Holds the block by SHARED pointer, not by reference.  The signal's own
  // strong reference is the only one there is, and a slot that violates D7 --
  // destroying the object that owns the signal it is running inside -- runs
  // ~Signal, which drops it and frees the block.  A referencing guard would
  // then decrement `emitDepth` through a dangling pointer on the way out: a
  // silent four-byte write into freed memory, and in a Release build (where the
  // D7 assert is compiled out) that stray write IS the first thing that
  // happens.  Pinning the block turns it back into a plain leak-free no-op, so
  // the assert stays the first symptom.
  //
  // Cost is one atomic increment/decrement pair per emission and no allocation.
  struct DepthGuard {
    explicit DepthGuard(std::shared_ptr<detail::SignalBlock> b)
        : block(std::move(b)) {
      ++block->emitDepth;
    }
    ~DepthGuard() { --block->emitDepth; }
    DepthGuard(const DepthGuard&) = delete;
    DepthGuard& operator=(const DepthGuard&) = delete;
    std::shared_ptr<detail::SignalBlock> block;
  };

  // slots_ is kept sorted by id -- ids only ever increase and erase preserves
  // order -- so this is a binary search rather than the scan the ID-list
  // approach would otherwise imply.  Hand-rolled to keep <algorithm> out of a
  // header that almost every other header includes.
  SlotPtr findSlot(std::uint64_t id) const {
    std::size_t lo = 0;
    std::size_t hi = slots_.size();
    while (lo < hi) {
      const std::size_t mid = lo + (hi - lo) / 2;
      if (slots_[mid].id < id) {
        lo = mid + 1;
      } else {
        hi = mid;
      }
    }
    if (lo < slots_.size() && slots_[lo].id == id) return slots_[lo].fn;
    return nullptr;
  }

  void eraseById(std::uint64_t id) {
    if (id == 0) return;  // a default-constructed Connection removes nothing
    for (auto it = slots_.begin(); it != slots_.end(); ++it) {
      if (it->id == id) {
        slots_.erase(it);
        return;
      }
    }
  }

  // Lazy on purpose: a signal nobody subscribed to -- and widgets declare
  // plenty of those -- costs a vector header and nothing else.
  void ensureBlock() {
    if (block_) return;
    block_ = std::make_shared<detail::SignalBlock>();
    block_->signal = this;
    block_->disconnectFn = &disconnectThunk;
  }

  static void disconnectThunk(void* signal, std::uint64_t id) {
    static_cast<Signal*>(signal)->eraseById(id);
  }

  // Cuts every outstanding Connection loose.  Releasing our shared_ptr is what
  // actually expires their weak_ptrs; clearing the back-pointer first keeps the
  // block harmless in case anything else ever holds a strong reference.
  void detach() {
    if (!block_) return;
    block_->signal = nullptr;
    block_->disconnectFn = nullptr;
    block_.reset();
  }

  std::vector<Entry> slots_;
  std::shared_ptr<detail::SignalBlock> block_;
  std::uint64_t lastId_ = 0;
};

}  // namespace geeyoou
