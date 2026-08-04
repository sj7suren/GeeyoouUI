//
// Signal<> contract tests.
//
// The interesting half of a signal implementation is what happens when a slot
// touches the signal it is running inside.  Signal::emit answers that by
// copying the slot list before dispatch, so these cases pin down what that copy
// actually buys -- and what it does NOT buy, which is the part a caller has to
// know about.
//
#include "geeyoou/core/Signal.hpp"

#include <memory>
#include <string>

#include "framework/Test.hpp"

using geeyoou::Connection;
using geeyoou::Signal;
using geeyoou::test::AllocGuard;

// ------------------------------------------------------------------ basics ---
GEEYOOU_TEST(signal, connect_emit_disconnect) {
  Signal<int> sig;
  CHECK(sig.empty());

  int seen = 0;
  const Connection c = sig.connect([&](int v) { seen += v; });
  CHECK(c.valid());
  CHECK(!sig.empty());

  sig.emit(3);
  CHECK_EQ(seen, 3);
  sig.emit(4);
  CHECK_EQ(seen, 7);

  sig.disconnect(c);
  CHECK(sig.empty());
  sig.emit(100);
  CHECK_EQ(seen, 7);  // nothing arrives after disconnect
}

GEEYOOU_TEST(signal, default_connection_disconnects_nothing) {
  Signal<> sig;
  int hits = 0;
  sig.connect([&] { ++hits; });

  const Connection none;
  CHECK(!none.valid());
  CHECK_EQ(none.id(), std::uint64_t(0));
  sig.disconnect(none);  // must not eat an unrelated slot
  sig.emit();
  CHECK_EQ(hits, 1);

  // Ids are never reused, so disconnecting twice is a harmless no-op rather
  // than a way to remove whoever inherited the id.
  const Connection c = sig.connect([&] { ++hits; });
  sig.disconnect(c);
  sig.disconnect(c);
  sig.emit();
  CHECK_EQ(hits, 2);
}

GEEYOOU_TEST(signal, slots_run_in_connection_order) {
  Signal<> sig;
  std::string order;
  sig.connect([&] { order += "a"; });
  const Connection cb = sig.connect([&] { order += "b"; });
  sig.connect([&] { order += "c"; });

  sig.emit();
  CHECK_EQ(order, std::string("abc"));

  // Removing from the middle must not disturb the order of the survivors.
  order.clear();
  sig.disconnect(cb);
  sig.emit();
  CHECK_EQ(order, std::string("ac"));
}

GEEYOOU_TEST(signal, disconnect_all_and_multiple_arguments) {
  Signal<int, const std::string&> sig;
  int lastValue = 0;
  std::string lastName;
  sig.connect([&](int v, const std::string& n) { lastValue = v; lastName = n; });

  sig.emit(42, "pump");
  CHECK_EQ(lastValue, 42);
  CHECK_EQ(lastName, std::string("pump"));

  sig.disconnectAll();
  CHECK(sig.empty());
  sig.emit(7, "valve");
  CHECK_EQ(lastValue, 42);
}

// ------------------------------------------------------------- reentrancy ---
GEEYOOU_TEST(signal, connect_inside_slot_defers_to_the_next_emission) {
  Signal<> sig;
  int outer = 0;
  int inner = 0;
  bool added = false;

  sig.connect([&] {
    ++outer;
    if (!added) {
      added = true;
      sig.connect([&] { ++inner; });
    }
  });

  sig.emit();
  CHECK_EQ(outer, 1);
  // The dispatch snapshot predates the new slot, which is what stops a slot
  // that subscribes on first use from recursing into itself.
  CHECK_EQ(inner, 0);

  sig.emit();
  CHECK_EQ(outer, 2);
  CHECK_EQ(inner, 1);
}

GEEYOOU_TEST(signal, disconnect_inside_slot_takes_effect_next_emission) {
  Signal<> sig;
  int a = 0;
  int b = 0;
  Connection cb;

  sig.connect([&] {
    ++a;
    sig.disconnect(cb);
  });
  cb = sig.connect([&] { ++b; });

  sig.emit();
  CHECK_EQ(a, 1);
  // CURRENT CONTRACT: the snapshot was taken before the disconnect, so `b` still
  // runs for THIS emission.  That is not Qt's behaviour, where a disconnect is
  // honoured immediately.  Asserted rather than glossed over, because anything
  // that changes it changes the meaning of every disconnect-in-a-slot call site.
  CHECK_EQ(b, 1);

  sig.emit();
  CHECK_EQ(a, 2);
  CHECK_EQ(b, 1);  // gone from here on
}

namespace {

// A receiver that unsubscribes in its destructor -- the pattern every
// widget/model pairing uses.  Its counter lives behind a shared_ptr, captured
// BY VALUE, so this test can observe a call that arrives after the object died
// without reading freed memory.
class Receiver {
 public:
  Receiver(Signal<>& sig, std::shared_ptr<int> hits) : sig_(sig) {
    conn_ = sig_.connect([hits] { ++*hits; });
  }
  ~Receiver() { sig_.disconnect(conn_); }

  Receiver(const Receiver&) = delete;
  Receiver& operator=(const Receiver&) = delete;

 private:
  Signal<>& sig_;
  Connection conn_;
};

}  // namespace

GEEYOOU_TEST(signal, destroying_a_receiver_inside_a_slot_is_survivable) {
  Signal<> sig;
  auto hits = std::make_shared<int>(0);
  std::unique_ptr<Receiver> victim;
  int killerRuns = 0;

  // Killer first, victim second: the ordering where the victim's entry is still
  // AHEAD of us in the snapshot when it gets destroyed.
  sig.connect([&] {
    ++killerRuns;
    victim.reset();
  });
  victim = std::make_unique<Receiver>(sig, hits);

  sig.emit();
  CHECK_EQ(killerRuns, 1);
  CHECK(victim == nullptr);
  // CURRENT CONTRACT, and the sharp edge of the snapshot: the dead receiver's
  // slot still fires for the in-flight emission.  It survives here only because
  // the closure captured shared state by value.  A slot capturing `this` would
  // be a use-after-free -- see the report accompanying this change.
  CHECK_EQ(*hits, 1);

  sig.emit();
  CHECK_EQ(killerRuns, 2);
  CHECK_EQ(*hits, 1);
}

GEEYOOU_TEST(signal, emitting_the_same_signal_from_a_slot_terminates) {
  Signal<int> sig;
  int depth = 0;
  int maxDepth = 0;

  sig.connect([&](int v) {
    ++depth;
    if (depth > maxDepth) maxDepth = depth;
    if (v > 0) sig.emit(v - 1);  // recursion is the caller's problem, not a hang
    --depth;
  });

  sig.emit(3);
  CHECK_EQ(maxDepth, 4);
  CHECK_EQ(depth, 0);
}

// ------------------------------------------------------- allocation profile ---
GEEYOOU_TEST(signal, emit_allocation_profile) {
  Signal<int> sig;
  {
    // An empty signal takes the early return: no snapshot, no allocation.  This
    // is what makes it safe to declare a signal on a widget that nobody wired.
    const AllocGuard g;
    sig.emit(1);
    CHECK_EQ(g.count(), std::uint64_t(0));
  }

  int seen = 0;
  sig.connect([&](int v) { seen += v; });
  {
    // A non-empty emission copies the slot list -- the cost Signal.hpp documents
    // and the reason docs/architecture.md forbids signals in a per-frame hot
    // path.  Asserted as "at least one" rather than an exact count: how many of
    // the std::function copies the small-buffer absorbs is a library detail.
    const AllocGuard g;
    sig.emit(1);
    CHECK_GE(g.count(), std::uint64_t(1));
  }
  CHECK_EQ(seen, 1);
}
