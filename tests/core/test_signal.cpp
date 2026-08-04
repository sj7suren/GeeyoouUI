//
// Signal<> contract tests.
//
// The interesting half of a signal implementation is what happens when a slot
// touches the signal it is running inside.  These cases pin down contract D7:
//
//   * a slot MAY connect;    the new slot does not run in this emission
//   * a slot MAY disconnect; the removed slot does not run, IMMEDIATELY, even
//     when it was still ahead of us and even when it removes itself
//   * a slot MAY destroy other objects
//   * a slot MAY NOT destroy the object that owns the signal it is running in
//
// Two of the cases below used to assert the OPPOSITE of the middle rule.  That
// was not an oversight: emit() copied the slot list before dispatch, so a
// disconnect was one emission late, and the tests recorded that as the shipped
// behaviour rather than glossing over it.  D7 changed it deliberately -- see
// docs/iterations/01-lifecycle-and-tests.md -- and the cases were rewritten
// with it.  They are marked below so nobody reads them as a regression.
//
#include "geeyoou/core/Signal.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

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

// D7 CONTRACT CHANGE (was: disconnect_inside_slot_takes_effect_next_emission).
// The old contract let `b` run once more after being disconnected, because the
// dispatch snapshot predated the removal.  That one-emission window is the
// dangling path this iteration set out to close: the usual reason a slot
// disconnects another one is that the other one's receiver is about to die.
GEEYOOU_TEST(signal, disconnect_inside_slot_takes_effect_immediately) {
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
  CHECK_EQ(b, 0);  // removed while still ahead of us, and skipped

  sig.emit();
  CHECK_EQ(a, 2);
  CHECK_EQ(b, 0);
}

GEEYOOU_TEST(signal, a_slot_may_disconnect_itself_while_running) {
  // The one-shot idiom, and the case that makes the callable outlive its own
  // entry: the slot is erased from the list halfway through its own call.
  Signal<int> sig;
  int runs = 0;
  int lastValue = 0;
  Connection self;

  self = sig.connect([&](int v) {
    ++runs;
    lastValue = v;
    sig.disconnect(self);
  });
  sig.connect([&](int) { ++runs; });

  sig.emit(7);
  CHECK_EQ(runs, 2);
  CHECK_EQ(lastValue, 7);

  sig.emit(9);
  CHECK_EQ(runs, 3);      // only the survivor
  CHECK_EQ(lastValue, 7);  // the one-shot never saw the second emission
}

GEEYOOU_TEST(signal, disconnect_all_inside_a_slot_stops_the_emission) {
  Signal<> sig;
  int first = 0;
  int second = 0;

  sig.connect([&] {
    ++first;
    sig.disconnectAll();
  });
  sig.connect([&] { ++second; });

  sig.emit();
  CHECK_EQ(first, 1);
  CHECK_EQ(second, 0);
  CHECK(sig.empty());
}

namespace {

// A receiver that unsubscribes in its destructor -- the pattern every
// widget/model pairing uses.  It keeps no reference to the sender: the handle
// knows which signal it came from, which is what lets the sender die first.
// The counter lives behind a shared_ptr captured BY VALUE, so a call arriving
// after the object died would be observable rather than a crash.
class Receiver {
 public:
  Receiver(Signal<>& sig, std::shared_ptr<int> hits) {
    conn_ = sig.connect([hits] { ++*hits; });
  }
  ~Receiver() { conn_.disconnect(); }

  Receiver(const Receiver&) = delete;
  Receiver& operator=(const Receiver&) = delete;

 private:
  Connection conn_;
};

}  // namespace

// D7 CONTRACT CHANGE (assertion was: *hits == 1).
// Under the old snapshot contract the destroyed receiver's slot still ran for
// the in-flight emission, and only survived because this test's closure happens
// to capture shared state by value.  A closure capturing `this` -- which is
// what every widget writes -- was a use-after-free.  That is the defect; the
// assertion below is the fix.
GEEYOOU_TEST(signal, destroying_a_receiver_inside_a_slot_unsubscribes_it_now) {
  Signal<> sig;
  auto hits = std::make_shared<int>(0);
  std::unique_ptr<Receiver> victim;
  int killerRuns = 0;

  // Killer first, victim second: the ordering where the victim's entry is still
  // AHEAD of us in the dispatch list when it gets destroyed.
  sig.connect([&] {
    ++killerRuns;
    victim.reset();
  });
  victim = std::make_unique<Receiver>(sig, hits);

  sig.emit();
  CHECK_EQ(killerRuns, 1);
  CHECK(victim == nullptr);
  CHECK_EQ(*hits, 0);  // the dead receiver is not called

  sig.emit();
  CHECK_EQ(killerRuns, 2);
  CHECK_EQ(*hits, 0);
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

// ----------------------------------------------------------------- ownership ---
GEEYOOU_TEST(signal, a_connection_outliving_its_signal_disconnects_harmlessly) {
  // The order this library actually hits: a widget subscribes to the
  // process-lifetime skin registry... and then the registry is what goes first
  // in a unit test, or the widget is what goes first in production.  Both have
  // to be survivable, and the second one is what the handle makes safe.
  Connection c;
  {
    Signal<> sig;
    c = sig.connect([] {});
    CHECK(c.valid());
    CHECK(c.senderAlive());
  }
  CHECK(!c.senderAlive());
  c.disconnect();  // must not touch the freed signal
  CHECK(!c.valid());
}

GEEYOOU_TEST(signal, disconnect_through_the_handle_is_idempotent) {
  Signal<> sig;
  int hits = 0;
  Connection c = sig.connect([&] { ++hits; });

  sig.emit();
  CHECK_EQ(hits, 1);

  c.disconnect();
  CHECK(sig.empty());
  c.disconnect();  // second call names nothing
  sig.emit();
  CHECK_EQ(hits, 1);
}

// ------------------------------------------------------------ move semantics ---
//
// The trap the control block introduces: the block holds a back-pointer to its
// signal, so a defaulted move would leave every outstanding handle aimed at the
// husk.  The next disconnect would then edit an object nobody owns.
GEEYOOU_TEST(signal, moving_a_signal_carries_its_subscriptions_and_handles) {
  Signal<int> src;
  int seen = 0;
  Connection c = src.connect([&](int v) { seen += v; });

  Signal<int> dst = std::move(src);
  CHECK(src.empty());
  CHECK(!dst.empty());

  dst.emit(3);
  CHECK_EQ(seen, 3);
  src.emit(100);  // the husk owns nothing
  CHECK_EQ(seen, 3);

  // The handle now has to name a slot inside dst -- not an address inside src.
  c.disconnect();
  CHECK(dst.empty());
  dst.emit(5);
  CHECK_EQ(seen, 3);
}

GEEYOOU_TEST(signal, move_assignment_drops_the_targets_own_subscribers) {
  Signal<> victim;
  Signal<> incoming;
  int victimHits = 0;
  int incomingHits = 0;

  Connection victimConn = victim.connect([&] { ++victimHits; });
  Connection incomingConn = incoming.connect([&] { ++incomingHits; });

  victim = std::move(incoming);

  victim.emit();
  CHECK_EQ(victimHits, 0);   // its own slot went with the overwritten list
  CHECK_EQ(incomingHits, 1);

  // The displaced handle must go dead rather than start naming whichever
  // incoming slot happens to share its id.
  CHECK(!victimConn.senderAlive());
  victimConn.disconnect();
  victim.emit();
  CHECK_EQ(incomingHits, 2);

  incomingConn.disconnect();
  CHECK(victim.empty());
}

GEEYOOU_TEST(signal, a_moved_from_signal_is_reusable) {
  Signal<> src;
  int oldHits = 0;
  src.connect([&] { ++oldHits; });
  Signal<> dst = std::move(src);

  int newHits = 0;
  const Connection c = src.connect([&] { ++newHits; });
  CHECK(c.valid());
  src.emit();
  CHECK_EQ(newHits, 1);
  CHECK_EQ(oldHits, 0);
}

// ------------------------------------------------------- allocation profile ---
//
// docs/architecture.md section 1, rule 2: an upper computer runs for months, so
// "how many times did that allocate" is a first-class question rather than an
// optimisation.  emit() therefore captures a list of slot IDS on the stack
// instead of copying the slot list.
GEEYOOU_TEST(signal, emit_allocates_nothing_within_the_inline_capacity) {
  Signal<int> sig;
  {
    // An empty signal takes the early return.  This is what makes it safe to
    // declare a signal on a widget that nobody wired.
    const AllocGuard g;
    sig.emit(1);
    CHECK_EQ(g.count(), std::uint64_t(0));
  }

  // Exactly the inline capacity: the boundary, not a comfortable margin.
  int seen = 0;
  for (int i = 0; i < 8; ++i) sig.connect([&](int v) { seen += v; });
  CHECK_EQ(sig.size(), std::size_t(8));

  {
    const AllocGuard g;
    for (int i = 0; i < 1000; ++i) sig.emit(1);
    CHECK_EQ(g.count(), std::uint64_t(0));
  }
  CHECK_EQ(seen, 8000);
}

GEEYOOU_TEST(signal, emit_spills_to_the_heap_exactly_once_past_the_capacity) {
  // One subscriber past the inline buffer.  Asserted as an exact count so that
  // "zero allocations" above is known to be the inline path doing its job
  // rather than an accident of how this particular closure is stored.
  Signal<int> sig;
  int seen = 0;
  for (int i = 0; i < 9; ++i) sig.connect([&](int v) { seen += v; });
  CHECK_EQ(sig.size(), std::size_t(9));

  const AllocGuard g;
  for (int i = 0; i < 1000; ++i) sig.emit(1);
  CHECK_EQ(g.count(), std::uint64_t(1000));
  CHECK_EQ(seen, 9000);
}

GEEYOOU_TEST(signal, a_large_emission_still_visits_every_slot_in_order) {
  // The spill path is the one nothing in the library exercises, so it gets its
  // own correctness case rather than only an allocation-count one.
  Signal<> sig;
  std::string order;
  for (int i = 0; i < 20; ++i) {
    sig.connect([&order, i] { order += char('a' + i); });
  }
  sig.emit();
  CHECK_EQ(order, std::string("abcdefghijklmnopqrst"));
}
