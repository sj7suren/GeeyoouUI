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
#include <memory>
#include <vector>

#include "framework/Test.hpp"
#include "geeyoou/widget/Layout.hpp"
#include "geeyoou/widget/Widget.hpp"

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

  void onChildAppended() override { ++appended; }
  void onChildRemoved(std::size_t index) override { removedAt.push_back(index); }

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
  // The engine itself never calls it -- placing children needs only arrange, and
  // a measure pass that nothing consumes is a pass nobody pays for.
  CHECK_EQ(lay->measures, 0);
  const Rect snapshot = a->geometry();
  const SizeHint hint = lay->measure(root);
  CHECK_EQ(lay->measures, 1);
  CHECK_NEAR(hint.preferred.height, 90.0f, kEps);  // 3 rows of 30, no spacing
  CHECK(a->geometry() == snapshot);
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
