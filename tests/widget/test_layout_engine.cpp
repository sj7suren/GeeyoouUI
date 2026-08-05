//
// The layout engine skeleton: idempotent geometry, the Layout base class, and
// the four mechanisms that stop a layout pass from re-entering itself.
//
// There is no BoxLayout or GridLayout yet -- these cases drive a deliberately
// hostile toy Layout instead, because what is being pinned down here is the
// PROTOCOL (when does a pass run, what may it touch, what happens when the
// thing it is arranging is destroyed underneath it), not any particular
// arithmetic.  See docs/iterations/02-layout-engine.md.
//
#include <algorithm>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#include "framework/Test.hpp"
#include "geeyoou/widget/Layout.hpp"
#include "geeyoou/widget/Widget.hpp"

#if defined(_MSC_VER)
#include <crtdbg.h>
#endif

using geeyoou::Layout;
using geeyoou::LayoutOverflow;
using geeyoou::Margins;
using geeyoou::Rect;
using geeyoou::Size;
using geeyoou::SizeHint;
using geeyoou::Widget;

namespace {
constexpr float kEps = 0.0005f;

// Counts what the base class promises to call, so the ordering rules can be
// asserted rather than described.
class ProbeWidget : public Widget {
 public:
  int geometryChanges = 0;

 protected:
  void onGeometryChanged() override { ++geometryChanges; }
};

// Stacks children top to bottom at a fixed row height, reporting whatever did
// not fit.  Items are addressed BY INDEX -- the structural rule from
// Layout.hpp -- and the row height is a property with its own invalidation.
class StackLayout : public Layout {
 public:
  int arranges = 0;
  mutable int measures = 0;  // measure() is const, and must stay that way
  int appended = 0;
  std::vector<std::size_t> removedAt;

  void setRowHeight(float px) {
    rowHeight_ = px;
    invalidate();
  }

  void onChildAppended() override { ++appended; }
  void onChildRemoved(std::size_t index) override { removedAt.push_back(index); }

 protected:
  // Protected, matching the base declarations: the two of them share this
  // object's working state, and the latch that keeps them out of each other's
  // way is in Layout::measureFor / Layout::arrangeFor.  A toy layout that
  // published them again would be modelling the hole rather than the protocol.
  SizeHint measure(const Widget& host) const override {
    ++measures;
    float h = 0.0f;
    float w = 0.0f;
    for (const std::unique_ptr<Widget>& c : host.children()) {
      if (!c->isVisible()) continue;
      if (h > 0.0f) h += spacing();
      h += rowHeight_;
      w = (std::max)(w, c->sizeHint().preferred.width);
    }
    return SizeHint{Size{w, h}, Size{w, h}, Size{geeyoou::kUnbounded, geeyoou::kUnbounded}};
  }

  LayoutOverflow arrange(Widget& host, const Rect& content) override {
    ++arranges;
    LayoutOverflow of;
    float y = content.y();
    // By index, and re-reading children() every step: a child's
    // onGeometryChanged runs application code, and application code is entitled
    // to remove widgets.
    for (std::size_t i = 0; i < host.children().size(); ++i) {
      Widget* c = host.children()[i].get();
      if (!c->isVisible()) continue;
      if (y + rowHeight_ > content.bottom()) {
        ++of.clippedCount;
        of.heightShort = (std::max)(of.heightShort, y + rowHeight_ - content.bottom());
      }
      c->setGeometry({content.x(), y, content.width(), rowHeight_});
      y += rowHeight_ + spacing();
    }
    return of;
  }

 private:
  float rowHeight_ = 20.0f;
};

}  // namespace

// ================================================================== T-02 ===
GEEYOOU_TEST(layout_engine, rect_equality_is_exact) {
  const Rect a(1.0f, 2.0f, 3.0f, 4.0f);
  CHECK(a == Rect(1.0f, 2.0f, 3.0f, 4.0f));
  CHECK(!(a != Rect(1.0f, 2.0f, 3.0f, 4.0f)));
  // One field at a time, so a comparison that forgot an edge cannot pass.
  CHECK(a != Rect(1.5f, 2.0f, 3.0f, 4.0f));
  CHECK(a != Rect(1.0f, 2.5f, 3.0f, 4.0f));
  CHECK(a != Rect(1.0f, 2.0f, 3.5f, 4.0f));
  CHECK(a != Rect(1.0f, 2.0f, 3.0f, 4.5f));
  CHECK(Rect() == Rect());
  // Not epsilon-based: half a pixel is a difference.
  CHECK(Rect(0.0f, 0.0f, 100.0f, 100.0f) != Rect(0.0f, 0.0f, 100.0f, 100.5f));
}

GEEYOOU_TEST(layout_engine, setting_the_same_geometry_does_no_work) {
  Widget root;
  ProbeWidget* w = root.add<ProbeWidget>();

  w->setGeometry({10.0f, 20.0f, 100.0f, 40.0f});
  CHECK_EQ(w->geometryChanges, 1);

  // The whole point of M3: the second call is free, and the widget is never
  // told anything happened.
  w->setGeometry({10.0f, 20.0f, 100.0f, 40.0f});
  CHECK_EQ(w->geometryChanges, 1);
  w->setGeometry({10.0f, 20.0f, 100.0f, 40.0f});
  CHECK_EQ(w->geometryChanges, 1);

  // Any real change still gets through, including one that only moves an edge.
  w->setGeometry({10.0f, 20.0f, 100.0f, 40.5f});
  CHECK_EQ(w->geometryChanges, 2);
  w->setGeometry({10.5f, 20.0f, 100.0f, 40.5f});
  CHECK_EQ(w->geometryChanges, 3);
  CHECK_NEAR(w->geometry().x(), 10.5f, kEps);

  // An empty rectangle is a value like any other -- the first set of a default
  // widget is a no-op because its geometry ALREADY is {0,0,0,0}.
  ProbeWidget* fresh = root.add<ProbeWidget>();
  fresh->setGeometry({});
  CHECK_EQ(fresh->geometryChanges, 0);
}

// ================================================================== T-03 ===
GEEYOOU_TEST(layout_engine, natural_size_is_latched_once_and_never_rewritten) {
  Widget root;
  Widget* w = root.add<Widget>();

  // Nothing yet: a widget that has never had a size does not claim one.
  CHECK_NEAR(w->sizeHint().preferred.width, 0.0f, kEps);
  CHECK_NEAR(w->sizeHint().preferred.height, 0.0f, kEps);
  CHECK_NEAR(w->sizeHint().min.width, 0.0f, kEps);
  CHECK_NEAR(w->sizeHint().max.width, geeyoou::kUnbounded, kEps);

  // A zero-height rectangle is not a size worth remembering.
  w->setGeometry({0.0f, 0.0f, 200.0f, 0.0f});
  CHECK_NEAR(w->sizeHint().preferred.width, 0.0f, kEps);

  w->setGeometry({0.0f, 0.0f, 200.0f, 80.0f});
  CHECK_NEAR(w->sizeHint().preferred.width, 200.0f, kEps);
  CHECK_NEAR(w->sizeHint().preferred.height, 80.0f, kEps);

  // ADR-R2-09, and the reason the hint is not just geometry(): squeezing a
  // widget must not change what it says it wants, or a window dragged smaller
  // would ratchet its contents down and never let them back out.
  w->setGeometry({0.0f, 0.0f, 40.0f, 10.0f});
  CHECK_NEAR(w->sizeHint().preferred.width, 200.0f, kEps);
  CHECK_NEAR(w->sizeHint().preferred.height, 80.0f, kEps);
  w->setGeometry({0.0f, 0.0f, 4.0f, 1.0f});
  CHECK_NEAR(w->sizeHint().preferred.width, 200.0f, kEps);
  // ...and growing again does not move it either.
  w->setGeometry({0.0f, 0.0f, 900.0f, 900.0f});
  CHECK_NEAR(w->sizeHint().preferred.width, 200.0f, kEps);
  CHECK_NEAR(w->sizeHint().preferred.height, 80.0f, kEps);
}

GEEYOOU_TEST(layout_engine, adopting_a_layout_latches_the_children_that_are_already_there) {
  Widget root;
  root.setGeometry({0.0f, 0.0f, 300.0f, 300.0f});
  Widget* a = root.add<Widget>();
  a->setGeometry({0.0f, 0.0f, 120.0f, 24.0f});
  Widget* b = root.add<Widget>();
  b->setGeometry({0.0f, 0.0f, 90.0f, 36.0f});

  // Hand-positioned until this line; that hand-written size is the best
  // statement of what these widgets want that anybody has made.
  root.setLayout<StackLayout>();
  CHECK_NEAR(a->sizeHint().preferred.width, 120.0f, kEps);
  CHECK_NEAR(a->sizeHint().preferred.height, 24.0f, kEps);
  CHECK_NEAR(b->sizeHint().preferred.height, 36.0f, kEps);

  // The arrange that adoption triggered has already overwritten their geometry;
  // the hint is unmoved.
  CHECK_NEAR(a->geometry().height(), 20.0f, kEps);
  CHECK_NEAR(a->sizeHint().preferred.height, 24.0f, kEps);
}

GEEYOOU_TEST(layout_engine, depth_counts_from_the_root_through_constructor_built_subtrees) {
  Widget root;
  CHECK_EQ(int(root.depth()), 0);

  Widget* a = root.add<Widget>();
  Widget* b = a->add<Widget>();
  Widget* c = b->add<Widget>();
  CHECK_EQ(int(a->depth()), 1);
  CHECK_EQ(int(b->depth()), 2);
  CHECK_EQ(int(c->depth()), 3);

  // The case add<T> alone gets wrong: a container that builds its own children
  // in its CONSTRUCTOR numbered them from zero, before it had a parent.
  struct TwoDeep : Widget {
    TwoDeep() {
      inner = add<Widget>();
      leaf = inner->add<Widget>();
    }
    Widget* inner = nullptr;
    Widget* leaf = nullptr;
  };

  TwoDeep standalone;
  CHECK_EQ(int(standalone.depth()), 0);
  CHECK_EQ(int(standalone.inner->depth()), 1);

  TwoDeep* nested = c->add<TwoDeep>();
  CHECK_EQ(int(nested->depth()), 4);
  CHECK_EQ(int(nested->inner->depth()), 5);
  CHECK_EQ(int(nested->leaf->depth()), 6);
}

// ================================================================== T-05 ===
GEEYOOU_TEST(layout_engine, layout_is_bound_once_and_arranges_the_content_rect) {
  Widget root;
  StackLayout* lay = root.setLayout<StackLayout>();
  REQUIRE(lay != nullptr);
  CHECK_EQ(root.layout(), static_cast<Layout*>(lay));
  CHECK_EQ(lay->host(), &root);
  CHECK_NEAR(lay->spacing(), 6.0f, kEps);  // the documented default

  Widget* a = root.add<Widget>();
  Widget* b = root.add<Widget>();
  Widget* c = root.add<Widget>();
  CHECK_EQ(lay->appended, 3);

  root.setGeometry({0.0f, 0.0f, 200.0f, 300.0f});
  // rowHeight 20, spacing 6, no margins.
  CHECK_NEAR(a->geometry().y(), 0.0f, kEps);
  CHECK_NEAR(b->geometry().y(), 26.0f, kEps);
  CHECK_NEAR(c->geometry().y(), 52.0f, kEps);
  CHECK_NEAR(a->geometry().width(), 200.0f, kEps);

  // Margins come out of the rectangle the layout is handed, and changing them
  // re-runs it -- that is what Layout::invalidate() is for.
  const int before = lay->arranges;
  lay->setMargins({8.0f, 4.0f, 12.0f, 2.0f});
  CHECK_EQ(lay->arranges, before + 1);
  CHECK_NEAR(a->geometry().x(), 8.0f, kEps);
  CHECK_NEAR(a->geometry().y(), 4.0f, kEps);
  CHECK_NEAR(a->geometry().width(), 180.0f, kEps);  // 200 - 8 - 12

  lay->setSpacing(0.0f);
  CHECK_NEAR(b->geometry().y(), 24.0f, kEps);
  lay->setRowHeight(30.0f);
  CHECK_NEAR(b->geometry().y(), 34.0f, kEps);
  CHECK_NEAR(a->geometry().height(), 30.0f, kEps);

  // measure() is a pure query: it can be called at any time and moves nothing.
  //
  // There is no measure PASS: arranging a host reads its children's hints and
  // places them, and a separate top-down measure sweep that nothing consumed
  // would be a pass nobody pays for.  So a host at the TOP of a layout tree --
  // `root` here, which nothing else measures -- has its own measure() called
  // exactly as often as the application calls it, which is zero times.
  //
  // (T-11 note: this assertion used to be described as "the engine never calls
  // measure()".  That is no longer true, and the case below shows why: since
  // Widget::sizeHint() forwards to the layout, a host that is itself an ITEM in
  // another layout is measured by that layout.  Nesting does not work any other
  // way -- the enclosing box has no other means of finding out how big the
  // panel wants to be.)
  CHECK_EQ(lay->measures, 0);
  const Rect snapshot = a->geometry();
  // Through the host: Widget::sizeHint() IS the public way to measure a
  // container, and Layout::measure is protected precisely so that it is the
  // only one.
  const SizeHint hint = root.sizeHint();
  CHECK_EQ(lay->measures, 1);
  CHECK_NEAR(hint.preferred.height, 90.0f, kEps);  // 3 rows of 30, no spacing
  CHECK(a->geometry() == snapshot);
}

GEEYOOU_TEST(layout_engine, a_nested_host_reports_what_its_own_layout_needs) {
  // The change T-11 needed: a container's sizeHint() IS its layout's measure().
  // Without it every nested panel answers with the size it happened to be given
  // by hand once, and a form inside a GroupBox inside a page cannot be sized at
  // all.
  Widget root;
  StackLayout* outer = root.setLayout<StackLayout>();
  outer->setSpacing(0.0f);

  Widget* panel = root.add<Widget>();
  StackLayout* inner = panel->setLayout<StackLayout>();
  inner->setSpacing(0.0f);
  inner->setRowHeight(15.0f);
  for (int i = 0; i < 4; ++i) panel->add<Widget>();

  // 4 rows of 15 with no spacing and no margins.
  const SizeHint h = panel->sizeHint();
  CHECK_NEAR(h.preferred.height, 60.0f, kEps);
  CHECK(inner->measures > 0);

  // ...and it TRACKS the layout: the forwarding is a live query, not a value
  // captured when the layout was adopted.  (What a measure() folds into its
  // answer is that layout's business -- BoxLayout and GridLayout add their
  // margins, this deliberately crude toy does not.)
  inner->setRowHeight(25.0f);
  CHECK_NEAR(panel->sizeHint().preferred.height, 100.0f, kEps);
  panel->add<Widget>();
  CHECK_NEAR(panel->sizeHint().preferred.height, 125.0f, kEps);

  // A widget with NO layout still answers with its natural size: the base
  // behaviour is unchanged for the 32 controls that do not use the engine.
  Widget plain;
  plain.setGeometry({0.0f, 0.0f, 40.0f, 25.0f});
  CHECK_NEAR(plain.sizeHint().preferred.width, 40.0f, kEps);
}

GEEYOOU_TEST(layout_engine, overflow_is_reported_and_never_signalled) {
  Widget root;
  StackLayout* lay = root.setLayout<StackLayout>();
  for (int i = 0; i < 4; ++i) root.add<Widget>();

  // Four 20px rows with 6px gaps need 98px.
  root.setGeometry({0.0f, 0.0f, 100.0f, 98.0f});
  CHECK(!root.lastLayoutOverflow().any());
  CHECK_EQ(root.lastLayoutOverflow().clippedCount, 0);

  root.setGeometry({0.0f, 0.0f, 100.0f, 50.0f});
  CHECK(root.lastLayoutOverflow().any());
  CHECK_EQ(root.lastLayoutOverflow().clippedCount, 2);  // rows 3 and 4
  CHECK_NEAR(root.lastLayoutOverflow().heightShort, 48.0f, kEps);  // 78 + 20 - 50
  // Stored on the layout, not copied onto the widget: 12 bytes the other few
  // thousand widgets in a tree do not carry.
  CHECK_EQ(&root.lastLayoutOverflow(), &lay->lastOverflow());

  // A widget with no layout has nothing to report, and asking is safe.
  Widget bare;
  CHECK(!bare.lastLayoutOverflow().any());
}

GEEYOOU_TEST(layout_engine, child_hooks_carry_the_index_that_moved) {
  Widget root;
  StackLayout* lay = root.setLayout<StackLayout>();
  root.setGeometry({0.0f, 0.0f, 100.0f, 300.0f});

  Widget* a = root.add<Widget>();
  Widget* b = root.add<Widget>();
  Widget* c = root.add<Widget>();
  CHECK_EQ(lay->appended, 3);
  CHECK_NEAR(c->geometry().y(), 52.0f, kEps);

  // Removing the middle one closes the gap: the third child moves up into the
  // second slot, which is exactly what an index-addressed layout must handle.
  root.removeChild(b);
  REQUIRE(lay->removedAt.size() == std::size_t(1));
  CHECK_EQ(lay->removedAt[0], std::size_t(1));
  CHECK_NEAR(a->geometry().y(), 0.0f, kEps);
  CHECK_NEAR(c->geometry().y(), 26.0f, kEps);

  // Hiding is not removing, but it does re-run the pass and give the space back.
  Widget* d = root.add<Widget>();
  CHECK_NEAR(d->geometry().y(), 52.0f, kEps);
  a->setVisible(false);
  CHECK_NEAR(c->geometry().y(), 0.0f, kEps);
  CHECK_NEAR(d->geometry().y(), 26.0f, kEps);
  a->setVisible(true);
  CHECK_NEAR(d->geometry().y(), 52.0f, kEps);
}

GEEYOOU_TEST(layout_engine, invalidate_size_hint_reruns_the_nearest_host_above) {
  Widget root;
  StackLayout* lay = root.setLayout<StackLayout>();
  root.setGeometry({0.0f, 0.0f, 100.0f, 300.0f});
  Widget* mid = root.add<Widget>();
  Widget* leaf = mid->add<Widget>();

  const int before = lay->arranges;
  leaf->invalidateSizeHint();
  // The dirt travels UP: nothing below root owns a layout, so root's is what
  // runs -- once, not once per level.
  CHECK_EQ(lay->arranges, before + 1);

  // performLayout() on a widget with no layout is a no-op rather than an error.
  const int after = lay->arranges;
  leaf->performLayout();
  CHECK_EQ(lay->arranges, after);
  root.performLayout();
  CHECK_EQ(lay->arranges, after + 1);
}

// ================================================================== T-04 ===
namespace {

// Asks its own host to re-run from inside its own arrange, `demands` times.
// One demand is the honest case (the layout learned something while placing);
// an unlimited demand is the pathological one.
class ReentrantLayout : public Layout {
 public:
  int arranges = 0;
  int demands = 0;
  bool sawNestedRun = false;

  SizeHint measure(const Widget&) const override { return SizeHint{}; }

  LayoutOverflow arrange(Widget& host, const Rect& content) override {
    ++arranges;
    for (std::size_t i = 0; i < host.children().size(); ++i) {
      host.children()[i]->setGeometry(
          {content.x(), content.y() + float(i) * 10.0f, content.width(), 10.0f});
    }
    if (demands > 0) {
      --demands;
      const int seen = arranges;
      host.performLayout();  // must NOT recurse
      if (arranges != seen) sawNestedRun = true;
    }
    return LayoutOverflow{};
  }
};

// Removes the host from its parent halfway through arranging it -- which
// destroys the host, and this layout with it.  The geometry path already runs
// application code (AppWindow emits contentResized from inside one), so this is
// a shape the engine has to survive, not a hypothetical.
//
// Nothing below the removal touches a member, which is the same discipline
// every emit site in this library follows: contract D7.
class SuicidalLayout : public Layout {
 public:
  bool armed = false;

  SizeHint measure(const Widget&) const override { return SizeHint{}; }

  LayoutOverflow arrange(Widget& host, const Rect&) override {
    if (!armed) return LayoutOverflow{};
    armed = false;
    Widget* parent = host.parent();
    if (parent) parent->removeChild(&host);  // `this` is gone after this line
    return LayoutOverflow{};
  }
};

}  // namespace

GEEYOOU_TEST(layout_engine, m1_a_nested_request_defers_instead_of_recursing) {
  Widget root;
  ReentrantLayout* lay = root.setLayout<ReentrantLayout>();
  root.add<Widget>();
  root.add<Widget>();

  lay->arranges = 0;
  lay->demands = 1;
  root.setGeometry({0.0f, 0.0f, 100.0f, 100.0f});

  // Exactly two: the original pass, and ONE deferred re-run picked up by the
  // do/while.  Never a nested call -- that is the stack overflow this prevents.
  CHECK_EQ(lay->arranges, 2);
  CHECK(!lay->sawNestedRun);
  CHECK_EQ(lay->demands, 0);
}

GEEYOOU_TEST(layout_engine, m1_a_layout_that_never_settles_is_recorded_not_fatal) {
  geeyoou::detail::resetLayoutDiagnostics();
  CHECK_EQ(int(geeyoou::detail::layoutDiagnostics().notConverged), 0);

  Widget root;
  ReentrantLayout* lay = root.setLayout<ReentrantLayout>();
  root.add<Widget>();

  lay->arranges = 0;
  lay->demands = 1000;  // always dirty again
  root.setGeometry({0.0f, 0.0f, 100.0f, 100.0f});

  // The loop runs the pass twice and then gives up: a layout that has not
  // settled after seeing its own output once will not settle on the third try,
  // and burning the frame budget proving that is worse than being slightly
  // wrong for one frame.
  CHECK_EQ(lay->arranges, 2);
  CHECK_EQ(int(geeyoou::detail::layoutDiagnostics().notConverged), 1);
  CHECK_EQ(geeyoou::detail::layoutDiagnostics().lastNotConvergedHost,
           static_cast<const Widget*>(&root));

  // ...and the widget is still usable afterwards; nothing was aborted.
  lay->demands = 0;
  lay->arranges = 0;
  root.setGeometry({0.0f, 0.0f, 120.0f, 100.0f});
  CHECK_EQ(lay->arranges, 1);

  geeyoou::detail::resetLayoutDiagnostics();
  CHECK_EQ(int(geeyoou::detail::layoutDiagnostics().notConverged), 0);
}

GEEYOOU_TEST(layout_engine, the_guard_survives_a_host_destroyed_inside_its_own_arrange) {
  Widget root;
  Widget* host = root.add<Widget>();
  SuicidalLayout* lay = host->setLayout<SuicidalLayout>();
  host->add<Widget>();
  lay->armed = true;

  // The arrange removes `host` from `root`, which destroys it -- layout and all
  // -- while runLayoutIfAny is standing on it.  Without the cursor this is a
  // write to freed memory on the very next line of that function.
  host->setGeometry({0.0f, 0.0f, 100.0f, 100.0f});

  CHECK(root.children().empty());
  // Still alive, still usable: the pass unwound instead of leaving a dangling
  // entry on the cursor list.
  Widget* replacement = root.add<Widget>();
  replacement->setGeometry({0.0f, 0.0f, 10.0f, 10.0f});
  CHECK_EQ(root.children().size(), std::size_t(1));
}

// ------------------------------------------------------------- zero cost ---
GEEYOOU_TEST(layout_engine, the_host_counter_returns_to_zero) {
  // Everything the engine costs a widget that does not use it is gated on this
  // counter, so a leaked increment would quietly turn the gate off for the
  // whole process.  Balanced across both doors: replacing a layout, and
  // destroying the widget that owns one.
  const std::size_t start = geeyoou::detail::g_layoutHosts;
  {
    Widget root;
    root.setLayout<StackLayout>();
    CHECK_EQ(geeyoou::detail::g_layoutHosts, start + 1);
    root.setLayout<StackLayout>();  // replaces; still one host
    CHECK_EQ(geeyoou::detail::g_layoutHosts, start + 1);

    Widget* child = root.add<Widget>();
    child->setLayout<StackLayout>();
    CHECK_EQ(geeyoou::detail::g_layoutHosts, start + 2);
    root.removeChild(child);
    CHECK_EQ(geeyoou::detail::g_layoutHosts, start + 1);
  }
  CHECK_EQ(geeyoou::detail::g_layoutHosts, start);
  CHECK(!geeyoou::detail::layoutPassActive());
  CHECK_EQ(geeyoou::detail::currentLayoutHost(),
           static_cast<const Widget*>(nullptr));
}

// ============================================================= M2 / M4 ===
//
// Coverage gap the R2 handoff reported open (docs/iterations/02-layout-engine.md
// section 10.4, items 1 and 2): the downward one-way assert and the pass-depth
// ceiling had counters and recording paths, but no case had ever made either of
// them fire.
namespace {

// --- M2: an arrange() that reaches past its own host DIRECT children -------
//
// Same subprocess shape as d7.destroying_the_signal_owner_is_caught_in_debug_
// contained_in_release (test_d7_assert.cpp): the assert lives in
// Widget::setGeometry under #ifndef NDEBUG, so hitting it aborts the process --
// which is only observable from a PARENT that re-runs this executable as a
// child and reads the child exit code and stderr back.
constexpr const char* kM2Env = "GEEYOOU_M2_VIOLATION";

bool envFlagOnM2(const char* name) {
#ifdef _MSC_VER
  std::size_t len = 0;
  char buf[16] = {};
  if (getenv_s(&len, buf, sizeof(buf), name) != 0) return false;
  return len > 1 && buf[0] != '0';
#else
  const char* v = std::getenv(name);
  return v && v[0] != '\0' && v[0] != '0';
#endif
}

std::string selfPathM2() {
#ifdef _MSC_VER
  char* p = nullptr;
  if (_get_pgmptr(&p) != 0 || !p) return {};
  return p;
#else
  return {};
#endif
}

// Violates ADR-R2-08 / the Layout.hpp structural rule 2 on purpose: instead of
// placing children that belong directly to the host, it reaches one level
// further down and setGeometry()s a GRANDCHILD.  That is exactly the mistake
// M2 exists to catch -- a Layout that thinks it owns more of the tree than
// the direct children of its host.
class GrandchildReachingLayout : public Layout {
 public:
  SizeHint measure(const Widget&) const override { return SizeHint{}; }

  LayoutOverflow arrange(Widget& host, const Rect& content) override {
    // setLayout<L>() runs arrange() once immediately, with zero children --
    // guard against that first, harmless call so only the REAL, populated
    // pass triggers the violation below.
    if (host.children().empty()) return LayoutOverflow{};
    Widget& child = *host.children()[0];
    if (child.children().empty()) return LayoutOverflow{};
    Widget& grandchild = *child.children()[0];
    grandchild.setGeometry(content);  // <-- the violation
    return LayoutOverflow{};
  }
};

int violateM2() {
  Widget root;
  GrandchildReachingLayout* lay = root.setLayout<GrandchildReachingLayout>();
  (void)lay;
  Widget* child = root.add<Widget>();
  child->add<Widget>();
  root.setGeometry({0.0f, 0.0f, 100.0f, 100.0f});  // arrange() runs; should abort in Debug
  return 55;  // reached only where the assert was compiled out
}

}  // namespace

GEEYOOU_TEST(layout_engine,
             m2_reaching_past_direct_children_is_caught_in_debug) {
  if (envFlagOnM2(kM2Env)) {
    // --- child ---
#if defined(_MSC_VER)
    _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE);
    _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
    _set_error_mode(_OUT_TO_STDERR);
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
    std::fflush(stdout);
    const int outcome = violateM2();
    std::fflush(nullptr);
    std::exit(outcome);
  }

  // --- parent ---
  const std::string exe = selfPathM2();
  REQUIRE(!exe.empty());

  const std::string log = exe + ".m2.log";
  const std::string cmd = "\"\"" + exe + "\" > \"" + log + "\" 2>&1\"";

#ifdef _MSC_VER
  REQUIRE(_putenv_s(kM2Env, "1") == 0);
#endif
  const int rc = std::system(cmd.c_str());
#ifdef _MSC_VER
  _putenv_s(kM2Env, "");
#endif

  std::string text;
  std::FILE* f = nullptr;
#ifdef _MSC_VER
  if (fopen_s(&f, log.c_str(), "rb") != 0) f = nullptr;
#else
  f = std::fopen(log.c_str(), "rb");
#endif
  if (f) {
    char buf[4096];
    std::size_t n = 0;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) != 0) text.append(buf, n);
    std::fclose(f);
  }
  std::remove(log.c_str());

#ifdef NDEBUG
  CHECK_EQ(rc, 55);
  geeyoou::test::note(
      "[note] layout_engine.m2: Release build compiles the M2 assert out -- "
      "this guard is Debug-only by design, so the child only has to survive.");
#else
  CHECK_NE(rc, 0);
  CHECK_NE(rc, 55);
  const bool named =
      text.find("Layout::arrange may only setGeometry on its host's direct "
                "children") != std::string::npos;
  if (!named) {
    GEEYOOU_FAIL("child did not stop at the M2 assert, tail of its output: " +
                 text.substr(text.size() > 400 ? text.size() - 400 : 0));
  }
#endif
}

// --- M4: the pass-depth ceiling ----------------------------------------------
//
// Needs a widget tree 64+ levels deep with a Layout at every level along the
// path, so that Widget::runLayoutIfAny's synchronous recursion (arrange ->
// setGeometry -> child runLayoutIfAny -> arrange -> ...) pushes g_layoutDepth
// past kMaxTreeDepth.  Debug's add<T> asserts depth_+1 < kMaxTreeDepth, which
// forbids building a tree that deep in the first place -- so this case is
// Release-only, exactly as docs/iterations/02-layout-engine.md section 10.4
// item 2 says it has to be.  Building the same chain in Debug would abort the
// WHOLE test process on an unrelated widget-depth assert before a single
// arrange() ran, which is worse than not covering M4 at all.
namespace {

class SingleChildLayout : public Layout {
 public:
  int arranges = 0;

  SizeHint measure(const Widget&) const override { return SizeHint{}; }

  LayoutOverflow arrange(Widget& host, const Rect& content) override {
    ++arranges;
    if (!host.children().empty()) host.children()[0]->setGeometry(content);
    return LayoutOverflow{};
  }
};

#if defined(NDEBUG)
constexpr bool kCanBuildDeepTree = true;
#else
constexpr bool kCanBuildDeepTree = false;
#endif

// The case body, in its own function so the Debug configuration can DISCARD it
// rather than skip past it.
//
// The early `return` this replaces left the whole rest of the case unreachable
// whenever kCanBuildDeepTree was false, which /W4 reported as seventeen C4702s
// on the Debug side of a two-configuration gate -- and a gate that is noisy on
// one side is a gate nobody reads on either.  A discarded if-constexpr branch
// is never code-generated, so there is nothing left to call unreachable.
//
// The parameter is named ctx_ because that is the name the CHECK macros pick
// up; see framework/Test.hpp.
void checkM4Ceiling([[maybe_unused]] geeyoou::test::Context& ctx_) {
  constexpr int kChainLen = 80;  // > kMaxTreeDepth (64)
  Widget root;
  std::vector<Widget*> chain;
  std::vector<SingleChildLayout*> layouts;
  chain.push_back(&root);

  Widget* cur = &root;
  for (int i = 0; i < kChainLen; ++i) {
    layouts.push_back(cur->setLayout<SingleChildLayout>());
    Widget* next = cur->add<Widget>();
    chain.push_back(next);
    cur = next;
  }

  geeyoou::detail::resetLayoutDiagnostics();
  CHECK_EQ(int(geeyoou::detail::layoutDiagnostics().depthExceeded), 0);

  std::vector<int> before(layouts.size());
  for (std::size_t i = 0; i < layouts.size(); ++i) before[i] = layouts[i]->arranges;

  root.setGeometry({0.0f, 0.0f, 555.0f, 555.0f});

  CHECK_EQ(int(geeyoou::detail::layoutDiagnostics().depthExceeded), 1);
  CHECK_EQ(geeyoou::detail::layoutDiagnostics().lastDepthExceededHost,
           static_cast<const Widget*>(chain[64]));

  for (int i = 0; i < 64; ++i) {
    CHECK_EQ(layouts[std::size_t(i)]->arranges, before[std::size_t(i)] + 1);
  }
  for (std::size_t i = 64; i < layouts.size(); ++i) {
    CHECK_EQ(layouts[i]->arranges, before[i]);
  }

  CHECK(chain[64]->geometry() == Rect(0.0f, 0.0f, 555.0f, 555.0f));
  CHECK(!(chain[65]->geometry() == Rect(0.0f, 0.0f, 555.0f, 555.0f)));

  geeyoou::detail::resetLayoutDiagnostics();
}

}  // namespace

GEEYOOU_TEST(layout_engine, m4_a_pass_that_nests_past_the_ceiling_is_recorded_not_fatal) {
  if constexpr (kCanBuildDeepTree) {
    checkM4Ceiling(ctx_);
  } else {
    geeyoou::test::note(
        "[skip] layout_engine.m4_a_pass_that_nests_past_the_ceiling_is_recorded_not_fatal: "
        "M4 needs 64+ nested Layout hosts; Debug add<T> kMaxTreeDepth assert would abort "
        "tree construction itself first (docs/iterations/02-layout-engine.md section 10.4.2). "
        "This case only runs in the Release-configured process.");
  }
}
