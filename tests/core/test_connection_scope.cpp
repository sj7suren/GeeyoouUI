//
// ConnectionScope contract tests.
//
// The whole point of the class is that BOTH destruction orders are safe:
//
//   subscriber dies first  -> the slot stops being called (the corpse-repaint
//                             bug: a widget subscribed to the process-lifetime
//                             skin registry)
//   sender dies first      -> the scope's destructor still runs, and must not
//                             touch the freed signal
//
// The second one is why a naive RAII wrapper around a Connection was not
// enough: the old Connection held nothing but an id and needed the caller to
// hand it a signal reference to disconnect through.
//
#include "geeyoou/core/ConnectionScope.hpp"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

#include "framework/Test.hpp"

using geeyoou::Connection;
using geeyoou::ConnectionScope;
using geeyoou::Signal;

GEEYOOU_TEST(scope, destruction_unsubscribes_everything_it_owns) {
  Signal<> sig;
  int a = 0;
  int b = 0;
  int loose = 0;

  sig.connect([&] { ++loose; });  // not owned by the scope
  {
    ConnectionScope scope;
    scope += sig.connect([&] { ++a; });
    scope += sig.connect([&] { ++b; });
    CHECK_EQ(scope.size(), std::size_t(2));

    sig.emit();
    CHECK_EQ(a, 1);
    CHECK_EQ(b, 1);
    CHECK_EQ(loose, 1);
  }

  sig.emit();
  CHECK_EQ(a, 1);      // scope is gone
  CHECK_EQ(b, 1);
  CHECK_EQ(loose, 2);  // everything else keeps working
  CHECK_EQ(sig.size(), std::size_t(1));
}

GEEYOOU_TEST(scope, outliving_the_signal_is_a_no_op) {
  // Exactly the shape of Window vs skins(): a unit test destroys the sender
  // first, production destroys the subscriber first, and the same code has to
  // be correct under both.
  ConnectionScope scope;
  {
    Signal<int> sig;
    scope += sig.connect([](int) {});
    CHECK_EQ(scope.size(), std::size_t(1));
  }
  // The signal is gone; ~ConnectionScope is about to fire at the end of the
  // case.  Doing it explicitly here means a failure shows up as this case
  // rather than as a mystery crash in whatever runs next.
  scope.clear();
  CHECK(scope.empty());
}

GEEYOOU_TEST(scope, invalid_handles_are_dropped_rather_than_stored) {
  ConnectionScope scope;
  scope += Connection();
  CHECK(scope.empty());
  CHECK_EQ(scope.size(), std::size_t(0));
}

GEEYOOU_TEST(scope, clear_is_idempotent_and_reusable) {
  Signal<> sig;
  int hits = 0;
  ConnectionScope scope;

  scope += sig.connect([&] { ++hits; });
  scope.clear();
  scope.clear();
  sig.emit();
  CHECK_EQ(hits, 0);

  // A cleared scope is empty, not poisoned: a widget that re-wires itself on a
  // model swap reuses the same member.
  scope += sig.connect([&] { ++hits; });
  sig.emit();
  CHECK_EQ(hits, 1);
}

GEEYOOU_TEST(scope, unsubscribes_in_reverse_order_of_subscription) {
  Signal<> sig;
  std::string order;
  // Disconnecting is silent, so the order is recorded by the destructor of a
  // guard the slot captures.  Non-copyable and held behind a shared_ptr on
  // purpose: a guard that could be copied would also be moved, and every
  // husk left behind would write a spurious entry.
  struct Marker {
    Marker(std::string* o, char t) : out(o), tag(t) {}
    ~Marker() { *out += tag; }
    Marker(const Marker&) = delete;
    Marker& operator=(const Marker&) = delete;
    std::string* out;
    char tag;
  };

  {
    ConnectionScope scope;
    scope += sig.connect([m = std::make_shared<Marker>(&order, 'a')] {});
    scope += sig.connect([m = std::make_shared<Marker>(&order, 'b')] {});
    CHECK(order.empty());
  }
  CHECK_EQ(order, std::string("ba"));
}

GEEYOOU_TEST(scope, moving_a_scope_moves_the_ownership) {
  Signal<> sig;
  int hits = 0;
  {
    ConnectionScope inner;
    inner += sig.connect([&] { ++hits; });

    ConnectionScope outer = std::move(inner);
    CHECK_EQ(outer.size(), std::size_t(1));
    CHECK(inner.empty());

    sig.emit();
    CHECK_EQ(hits, 1);
  }
  sig.emit();
  CHECK_EQ(hits, 1);  // the moved-to scope released it exactly once
}

GEEYOOU_TEST(scope, move_assignment_releases_what_the_target_held) {
  Signal<> sig;
  int replaced = 0;
  int kept = 0;

  ConnectionScope target;
  target += sig.connect([&] { ++replaced; });

  {
    ConnectionScope source;
    source += sig.connect([&] { ++kept; });
    target = std::move(source);
  }

  sig.emit();
  CHECK_EQ(replaced, 0);  // dropped when the target was overwritten
  CHECK_EQ(kept, 1);
}
