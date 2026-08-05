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

GEEYOOU_TEST(signal, disconnect_removes_exactly_one_slot) {
  // eraseById() returns the moment it erases.  Dropping that `return` leaves
  // the loop stepping an iterator the erase invalidated, which is a crash --
  // eventually, somewhere else, and only if the allocator cooperates.  This
  // case makes the same defect a plain arithmetic failure instead: the size
  // must fall by exactly one per disconnect, and only the named slot may stop
  // arriving.
  Signal<> sig;
  constexpr int kSlots = 6;
  int hits[kSlots] = {};
  Connection conns[kSlots];
  for (int i = 0; i < kSlots; ++i) {
    conns[i] = sig.connect([&hits, i] { ++hits[i]; });
  }
  CHECK_EQ(sig.size(), std::size_t(kSlots));

  // From the MIDDLE: erasing the last entry is the one case where running off
  // the end of the loop happens to be harmless.
  sig.disconnect(conns[2]);
  CHECK_EQ(sig.size(), std::size_t(kSlots - 1));
  sig.disconnect(conns[3]);
  CHECK_EQ(sig.size(), std::size_t(kSlots - 2));
  // Already gone: a second disconnect must remove nothing at all.
  sig.disconnect(conns[2]);
  CHECK_EQ(sig.size(), std::size_t(kSlots - 2));

  sig.emit();
  CHECK_EQ(hits[0], 1);
  CHECK_EQ(hits[1], 1);
  CHECK_EQ(hits[2], 0);
  CHECK_EQ(hits[3], 0);
  CHECK_EQ(hits[4], 1);
  CHECK_EQ(hits[5], 1);
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

// D7 VIOLATION, on purpose -- the only case in this file that breaks the
// contract instead of exercising it.
//
// Release is the build that runs unattended in a plant, and there the assert in
// ~Signal does not exist.  "The contract is documented" therefore used to mean
// that a violation ran on into a slot list ~Signal had already destroyed: a
// use-after-free, whose symptom surfaced somewhere else entirely, hours later.
// It no longer does.  ~Signal clears the control block's `alive` flag in EVERY
// build and the dispatch loop re-reads it once per slot, which downgrades the
// violation to something bounded and reportable: the emission stops.
//
// This is not a licence to violate D7 -- the slots behind the offender silently
// not running is still a defect -- it is the difference between a defect a
// service engineer can describe over the phone and one that needs a memory
// dump.
//
// Debug cannot host this case: the assert fires first and takes the process
// with it, which is the stronger behaviour and the reason it stays.  That half
// is covered out-of-process by tests/core/test_d7_assert.cpp.
//
// READ THIS BEFORE TRUSTING IT AS A REGRESSION GUARD.  Measured, not assumed:
// with the `alive` check deleted from emit(), this case still reports PASS in a
// plain Release build.  It has to.  Past the violation the program is in
// undefined behaviour, and what the freed vector header happens to contain
// decided the outcome -- here it read back as "no such slot", which is
// indistinguishable from having stopped.  Only ASan turns the difference into a
// verdict, and there it is unambiguous:
//
//   heap-use-after-free ... in std::vector<Signal<>::Entry>::size
//     #1 Signal<>::findSlot   include/geeyoou/core/Signal.hpp
//     #2 Signal<>::emit       include/geeyoou/core/Signal.hpp
//     freed by ... the slot two frames up
//
// So this case earns its keep under the sanitiser, not in the nightly gate.
// The DETERMINISTIC guard on the same mechanism -- the one that fails on an
// ordinary build the moment somebody deletes the check -- is the case below it,
// which reaches the same code path without ever entering UB.
GEEYOOU_TEST(signal, destroying_the_owner_from_a_slot_stops_the_emission) {
#ifdef NDEBUG
  struct Owner {
    Signal<> sig;
  };

  Owner* owner = new Owner();
  int killerRuns = 0;
  int behind = 0;
  // Killer first, and TWO slots behind it: with one, "stopped" and "ran on"
  // differ only in a counter; with two, a partial stop would show up as well.
  owner->sig.connect([&] {
    ++killerRuns;
    delete owner;
  });
  owner->sig.connect([&] { ++behind; });
  owner->sig.connect([&] { ++behind; });

  // Under ASan this line is the whole point of the case: it used to read the
  // freed Entry vector, and now it must return having touched nothing but the
  // pinned control block.
  owner->sig.emit();

  CHECK_EQ(killerRuns, 1);
  CHECK_EQ(behind, 0);
#else
  geeyoou::test::note(
      "[skip] signal.destroying_the_owner_from_a_slot_stops_the_emission："
      "Debug 下 ~Signal 的 assert 先于降级保护触发并终止进程，"
      "该分支由 d7.* 的子进程用例覆盖");
#endif
}

// The deterministic half of the case above, and the one a build without a
// sanitiser can actually fail on.
//
// It exercises the same mechanism -- detach() clearing the control block's
// `alive` flag, emit() noticing -- through the OTHER caller of detach():
// move-assignment, which likewise replaces the slot list an emission is walking
// but, unlike a D7 violation, is entirely defined behaviour.  Nothing is freed
// while anybody still names it: the Signal object survives, the callable that
// did the deed is pinned by emit(), and the block outlives both.
//
// The ids are what make the assertion sharp.  Both signals number their slots
// from 1, so the id list emit() captured (1, 2, 3) also names three live slots
// in the INCOMING list.  Without the `alive` check, dispatch would resolve those
// ids against the new list and call the wrong signal's subscribers -- which is
// the same defect a D7 violation produces, minus the undefined behaviour that
// makes it unobservable.  With the check, the emission stops, and `fromIncoming`
// stays 0 on every allocator, every optimiser and every build type.
GEEYOOU_TEST(signal, an_emission_stops_when_its_slot_list_is_replaced_under_it) {
  Signal<> sig;
  Signal<> incoming;

  int fromIncoming = 0;
  for (int i = 0; i < 3; ++i) incoming.connect([&] { ++fromIncoming; });

  int mover = 0;
  int behind = 0;
  sig.connect([&] {
    ++mover;
    sig = std::move(incoming);
  });
  sig.connect([&] { ++behind; });
  sig.connect([&] { ++behind; });

  sig.emit();
  CHECK_EQ(mover, 1);
  CHECK_EQ(behind, 0);        // our own remaining slots are gone, not called
  CHECK_EQ(fromIncoming, 0);  // and the new ones do not inherit this emission

  // Stopping the in-flight emission must not leave the signal broken: the very
  // next one dispatches the list it now owns, in full.
  CHECK_EQ(sig.size(), std::size_t(3));
  sig.emit();
  CHECK_EQ(fromIncoming, 3);
  CHECK_EQ(behind, 0);
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
namespace {

// MSVC's debugging iterators allocate a proxy object per CONTAINER, including
// the spill vector emit() leaves empty on the inline path.  Under
// _ITERATOR_DEBUG_LEVEL != 0 the counts below would therefore be measuring the
// STL's debug bookkeeping, not emit()'s allocation profile -- one allocation
// per emission that does not exist in anything anyone ships.
//
// So the COUNTS are asserted in Release only, loudly (a note in the summary)
// rather than silently, while the functional half of both cases still runs in
// Debug.  Weakening the Release gate to accommodate a Debug-only artefact would
// be the wrong trade: Release is the build that runs in the plant.
#if defined(_ITERATOR_DEBUG_LEVEL) && _ITERATOR_DEBUG_LEVEL != 0
constexpr bool kStlAllocatesPerContainer = true;
#else
constexpr bool kStlAllocatesPerContainer = false;
#endif

void noteAllocCountsSkipped(const char* which) {
  geeyoou::test::note(std::string("[skip] ") + which +
                      "：分配计数断言仅在 Release 生效"
                      "（_ITERATOR_DEBUG_LEVEL != 0 时 STL 每个容器自带一次代理分配）");
}

}  // namespace

GEEYOOU_TEST(signal, emit_allocates_nothing_within_the_inline_capacity) {
  Signal<int> sig;
  {
    // An empty signal takes the early return.  This is what makes it safe to
    // declare a signal on a widget that nobody wired.  Free of the proxy caveat
    // below: the early return constructs no container at all.
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
    if constexpr (kStlAllocatesPerContainer) {
      noteAllocCountsSkipped("signal.emit_allocates_nothing_within_the_inline_capacity");
    } else {
      CHECK_EQ(g.count(), std::uint64_t(0));
    }
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
  if constexpr (kStlAllocatesPerContainer) {
    noteAllocCountsSkipped("signal.emit_spills_to_the_heap_exactly_once_past_the_capacity");
  } else {
    CHECK_EQ(g.count(), std::uint64_t(1000));
  }
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
