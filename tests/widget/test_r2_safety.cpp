//
// What a layout pass has to survive when application code runs in the middle of
// it, driven by the REAL BoxLayout and GridLayout.
//
// test_layout_engine.cpp already covers the same protocol with toy layouts, and
// every one of the cases below passed there: SuicidalLayout returns on the line
// after it kills its host, so nothing it owns is ever read again.  A real
// arrange does not have that shape -- it comes back from setGeometry into
// `i < items_.size()`, into scratch_[i * kStride], into host.children() -- and
// that is the difference between a toy that proves the guard exists and a case
// that proves it is in the right place.
//
// These were reported by the appsec review of R2 as three HIGH memory-safety
// defects, all three of them GREEN under the whole suite and visible only under
// /fsanitize=address.  Each case here is the reduced form of one of those
// probes; before the fixes they produce, in order, a heap-use-after-free at
// Widget.cpp:321, a heap-buffer-overflow at BoxLayout.cpp:296 and a
// heap-use-after-free at BoxLayout.cpp:294.
//
#include <cstddef>
#include <functional>

#include "framework/Test.hpp"
#include "geeyoou/widget/BoxLayout.hpp"
#include "geeyoou/widget/GridLayout.hpp"
#include "geeyoou/widget/Layout.hpp"
#include "geeyoou/widget/ScrollArea.hpp"
#include "geeyoou/widget/Widget.hpp"

using geeyoou::BoxLayout;
using geeyoou::GridLayout;
using geeyoou::Layout;
using geeyoou::Point;
using geeyoou::Rect;
using geeyoou::ScrollArea;
using geeyoou::Size;
using geeyoou::SizeHint;
using geeyoou::Widget;

namespace {
constexpr float kEps = 0.0005f;

// A widget with an opinion, so the arithmetic below is predictable.
class Sized : public Widget {
 public:
  Sized(float w, float h) : w_(w), h_(h) {}

  SizeHint sizeHint() const override {
    return SizeHint{Size{w_, h_}, Size{w_, h_},
                    Size{geeyoou::kUnbounded, geeyoou::kUnbounded}};
  }

 private:
  float w_;
  float h_;
};

// Runs one callback from inside its own onGeometryChanged, which is the door
// the library itself already opens: AppWindow::relayout emits contentResized
// from exactly there.  Fires ONCE -- a hook that re-armed itself would be
// testing the convergence limiter rather than the memory safety.
class Hook : public Sized {
 public:
  Hook() : Sized(30.0f, 20.0f) {}

  std::function<void()> once;

 protected:
  void onGeometryChanged() override {
    if (!once) return;
    std::function<void()> f;
    f.swap(once);
    f();
  }
};

// How far an area will actually scroll, measured through the public API only.
// A non-zero range IS a visible scrollbar: both come from the same
// viewportSize()/contentSize() pair.
float scrollRange(ScrollArea& sa, bool horizontal) {
  const Point before = sa.scrollOffset();
  sa.scrollTo(horizontal ? Point{1.0e6f, before.y} : Point{before.x, 1.0e6f});
  const Point at = sa.scrollOffset();
  sa.scrollTo(before);
  return horizontal ? at.x : at.y;
}

}  // namespace

// ============================================== S-1: the host dies mid-pass ===
GEEYOOU_TEST(r2_safety, a_real_boxlayout_survives_its_host_dying_inside_arrange) {
  const std::size_t hostsBefore = geeyoou::detail::g_layoutHosts;

  Widget root;
  root.setGeometry({0.0f, 0.0f, 400.0f, 400.0f});
  Widget* host = root.add<Widget>();
  BoxLayout* box = host->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);

  // First in the column, so three more items are still to be placed when it
  // takes the whole subtree away.  That is the part SuicidalLayout cannot show.
  Hook* trigger = host->add<Hook>();
  box->addWidget(trigger);
  for (int i = 0; i < 3; ++i) box->addWidget(host->add<Sized>(30.0f, 20.0f));

  trigger->once = [&root, host] { root.removeChild(host); };

  host->setGeometry({0.0f, 0.0f, 200.0f, 300.0f});

  CHECK(root.children().empty());
  // The layout was parked, not leaked: the host counter is what every zero-cost
  // gate in Widget is keyed on, so a leaked increment would quietly turn the
  // engine on for the whole process.
  CHECK_EQ(geeyoou::detail::g_layoutHosts, hostsBefore);

  // ...and the tree is still usable, which is the other half of "survives".
  Widget* replacement = root.add<Widget>();
  replacement->setGeometry({0.0f, 0.0f, 10.0f, 10.0f});
  CHECK_EQ(root.children().size(), std::size_t(1));
}

GEEYOOU_TEST(r2_safety, a_real_gridlayout_survives_its_host_dying_inside_arrange) {
  const std::size_t hostsBefore = geeyoou::detail::g_layoutHosts;

  Widget root;
  root.setGeometry({0.0f, 0.0f, 400.0f, 400.0f});
  Widget* host = root.add<Widget>();
  GridLayout* grid = host->setLayout<GridLayout>();

  Hook* trigger = host->add<Hook>();
  grid->addWidget(trigger, 0, 0);
  for (int r = 0; r < 3; ++r) {
    for (int c = 0; c < 3; ++c) {
      if (r == 0 && c == 0) continue;
      grid->addWidget(host->add<Sized>(30.0f, 20.0f), r, c);
    }
  }
  // Without stretch, track 0 stays at its content's minimum whatever the host
  // is given, the trigger's rectangle never changes, and M3's idempotence check
  // means onGeometryChanged is never called at all -- a case that quietly tests
  // nothing.
  grid->setColumnStretch(0, 1);
  grid->setRowStretch(0, 1);

  trigger->once = [&root, host] { root.removeChild(host); };

  host->setGeometry({0.0f, 0.0f, 200.0f, 300.0f});

  CHECK(root.children().empty());
  CHECK_EQ(geeyoou::detail::g_layoutHosts, hostsBefore);
}

// A widget destroyed from inside its OWN onGeometryChanged, with the parent's
// arrange still to finish.  The frame that used to walk into freed memory is
// setGeometry's own, one level below the layout pass.
GEEYOOU_TEST(r2_safety, a_child_may_destroy_itself_from_its_own_geometry_change) {
  Widget root;
  root.setGeometry({0.0f, 0.0f, 400.0f, 400.0f});
  BoxLayout* box = root.setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);

  Hook* doomed = root.add<Hook>();
  box->addWidget(doomed);
  Sized* survivor = root.add<Sized>(30.0f, 20.0f);
  box->addWidget(survivor);

  doomed->once = [&root, doomed] { root.removeChild(doomed); };

  root.setGeometry({0.0f, 0.0f, 200.0f, 300.0f});

  CHECK_EQ(root.children().size(), std::size_t(1));
  CHECK_EQ(root.children()[0].get(), static_cast<Widget*>(survivor));
  // The re-run the removal scheduled moved the survivor up into the free slot.
  CHECK_NEAR(survivor->geometry().y(), 0.0f, kEps);
}

// =========================== S-2: the item list grows while it is being read ===
GEEYOOU_TEST(r2_safety, a_boxlayout_item_list_may_grow_from_inside_its_own_arrange) {
  Widget root;
  root.setGeometry({0.0f, 0.0f, 400.0f, 400.0f});
  Widget* host = root.add<Widget>();
  BoxLayout* box = host->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);

  Hook* trigger = host->add<Hook>();
  box->addWidget(trigger);

  // add<T>() + addWidget() is ordinary public API, and it makes items_ longer
  // than the scratch buffer this pass allocated -- which holds exactly the item
  // count it started with and not one float more.
  trigger->once = [host, box] {
    for (int i = 0; i < 8; ++i) box->addWidget(host->add<Sized>(30.0f, 20.0f));
  };

  host->setGeometry({0.0f, 0.0f, 200.0f, 300.0f});

  CHECK_EQ(box->itemCount(), std::size_t(9));
  // Not placed by guesswork off a half-filled buffer, and not left at the
  // origin either: the re-run their own addWidget scheduled placed all nine.
  // Nine 20px rows with the default 6px spacing.
  for (std::size_t i = 0; i < host->children().size(); ++i) {
    CHECK_NEAR(host->children()[i]->geometry().y(), float(i) * 26.0f, kEps);
    CHECK_NEAR(host->children()[i]->geometry().height(), 20.0f, kEps);
  }
}

GEEYOOU_TEST(r2_safety, a_gridlayout_cell_list_may_grow_from_inside_its_own_arrange) {
  Widget root;
  root.setGeometry({0.0f, 0.0f, 400.0f, 400.0f});
  Widget* host = root.add<Widget>();
  GridLayout* grid = host->setLayout<GridLayout>();

  Hook* trigger = host->add<Hook>();
  grid->addWidget(trigger, 0, 0);
  grid->setColumnStretch(0, 1);  // so the trigger's rectangle actually moves
  grid->setRowStretch(0, 1);

  trigger->once = [host, grid] {
    for (int i = 1; i < 6; ++i) {
      grid->addWidget(host->add<Sized>(30.0f, 20.0f), i, i);
    }
  };

  host->setGeometry({0.0f, 0.0f, 200.0f, 300.0f});

  CHECK_EQ(grid->rowCount(), std::size_t(6));
  CHECK_EQ(grid->columnCount(), std::size_t(6));
  // Every cell ended up somewhere, and on its own diagonal square rather than
  // stacked on the first one.
  for (std::size_t i = 1; i < host->children().size(); ++i) {
    CHECK(host->children()[i]->geometry().x() > 0.0f);
    CHECK(host->children()[i]->geometry().y() > 0.0f);
  }
}

// ===================== S-1b: the layout itself is replaced while it is running ===
GEEYOOU_TEST(r2_safety, replacing_a_layout_from_inside_its_own_arrange_is_survivable) {
  const std::size_t hostsBefore = geeyoou::detail::g_layoutHosts;

  Widget root;
  root.setGeometry({0.0f, 0.0f, 400.0f, 400.0f});
  Widget* host = root.add<Widget>();
  BoxLayout* column = host->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
  const Layout* wasColumn = column;

  Hook* trigger = host->add<Hook>();
  Sized* b = host->add<Sized>(30.0f, 20.0f);
  Sized* c = host->add<Sized>(30.0f, 20.0f);
  column->addWidget(trigger);
  column->addWidget(b);
  column->addWidget(c);

  // Switching a panel from a row to a column when the window gets narrow is an
  // ordinary thing for an HMI page to do, and onGeometryChanged is where it
  // would be done.  It also frees the object whose items_ the arrange three
  // frames up is still walking.
  trigger->once = [host, trigger, b, c] {
    BoxLayout* row = host->setLayout<BoxLayout>(BoxLayout::Orientation::Horizontal);
    row->addWidget(trigger);
    row->addWidget(b);
    row->addWidget(c);
  };

  host->setGeometry({0.0f, 0.0f, 300.0f, 200.0f});

  // Pointer VALUES only -- `wasColumn` names a freed object by now, which is
  // the whole point: what the application asked for is the layout that wins.
  CHECK_NE(static_cast<const Layout*>(host->layout()), wasColumn);
  CHECK_EQ(geeyoou::detail::g_layoutHosts, hostsBefore + 1);

  // Side by side, in order: the row placed them in the pass's second round.
  CHECK_NEAR(b->geometry().y(), c->geometry().y(), kEps);
  CHECK(b->geometry().x() < c->geometry().x());
}

// ================== S-2b: measure() and arrange() share the scratch buffers ===
//
// Asking a container how big it wants to be, from inside a geometry change, is
// what sizeHint() is FOR -- ScrollArea::relayout does it on every resize.  It
// used to re-enter the layout that was arranging and reset its working buffers
// to the minimums, which for a grid meant every cell but the first landing on
// the same point.  Nothing is marked dirty by that, so the wrong geometry is
// the final picture, not a frame of flicker.
namespace {

// Asks `target` for its size hint, once, from inside its own geometry change.
class Asker : public Sized {
 public:
  Asker() : Sized(40.0f, 30.0f) {}

  Widget* target = nullptr;

 protected:
  void onGeometryChanged() override {
    if (!target) return;
    Widget* t = target;
    target = nullptr;
    const volatile float w = t->sizeHint().preferred.width;
    (void)w;
  }
};

template <class Build>
void checkReentrantMeasureChangesNothing(geeyoou::test::Context& ctx_,
                                         Build build) {
  Widget reference;
  Asker* quiet = build(reference);
  quiet->target = nullptr;
  reference.setGeometry({0.0f, 0.0f, 300.0f, 200.0f});
  reference.setGeometry({0.0f, 0.0f, 500.0f, 260.0f});

  Widget subject;
  Asker* noisy = build(subject);
  subject.setGeometry({0.0f, 0.0f, 300.0f, 200.0f});
  noisy->target = &subject;  // armed only for the SECOND pass
  subject.setGeometry({0.0f, 0.0f, 500.0f, 260.0f});

  REQUIRE(reference.children().size() == subject.children().size());
  for (std::size_t i = 0; i < subject.children().size(); ++i) {
    CHECK(subject.children()[i]->geometry() ==
          reference.children()[i]->geometry());
  }
}

}  // namespace

GEEYOOU_TEST(r2_safety, a_reentrant_measure_does_not_move_a_box_that_is_arranging) {
  checkReentrantMeasureChangesNothing(ctx_, [](Widget& host) {
    BoxLayout* box = host.setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
    Asker* first = host.add<Asker>();
    box->addWidget(first, 1);
    for (int i = 0; i < 3; ++i) box->addWidget(host.add<Sized>(40.0f, 30.0f), 1);
    return first;
  });
}

GEEYOOU_TEST(r2_safety, a_reentrant_measure_does_not_move_a_grid_that_is_arranging) {
  checkReentrantMeasureChangesNothing(ctx_, [](Widget& host) {
    GridLayout* grid = host.setLayout<GridLayout>();
    Asker* first = host.add<Asker>();
    grid->addWidget(first, 0, 0);
    for (int r = 0; r < 3; ++r) {
      for (int c = 0; c < 3; ++c) {
        if (r == 0 && c == 0) continue;
        grid->addWidget(host.add<Sized>(60.0f, 20.0f), r, c);
      }
    }
    grid->setColumnStretch(0, 1);
    grid->setRowStretch(0, 1);
    return first;
  });
}

// ============================= S-3: what a natural size is allowed to come from ===
GEEYOOU_TEST(r2_safety, a_layouts_own_output_is_never_latched_as_a_natural_size) {
  Widget root;
  BoxLayout* box = root.setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
  Widget* panel = root.add<Widget>();  // no sizeHint of its own
  box->addWidget(panel, 1);

  root.setGeometry({0.0f, 0.0f, 500.0f, 300.0f});
  CHECK_NEAR(panel->geometry().width(), 500.0f, kEps);
  // ADR-R2-09 pointing the other way.  500 is what the LAYOUT decided, not what
  // anybody asked for; latching it would make this panel claim 500 for ever and
  // never fit into a narrower window again.
  CHECK_NEAR(panel->sizeHint().preferred.width, 0.0f, kEps);

  root.setGeometry({0.0f, 0.0f, 120.0f, 300.0f});
  CHECK_NEAR(panel->geometry().width(), 120.0f, kEps);  // ...and it comes back

  // A size a HUMAN gave it still latches, which is the half of ADR-R2-09 that
  // was right all along.
  Widget standalone;
  standalone.setGeometry({0.0f, 0.0f, 42.0f, 17.0f});
  CHECK_NEAR(standalone.sizeHint().preferred.width, 42.0f, kEps);
  standalone.setGeometry({0.0f, 0.0f, 8.0f, 4.0f});
  CHECK_NEAR(standalone.sizeHint().preferred.width, 42.0f, kEps);
}

// ================================ S-5: the scrollbar that was always there ===
GEEYOOU_TEST(r2_safety, a_scrollarea_does_not_invent_a_horizontal_scrollbar) {
  // Short, narrow content in a large area.  Nothing here justifies scrolling in
  // either direction, and the answer used to be 10px sideways -- exactly the
  // width of the bar that needHBar() subtracted unconditionally.
  ScrollArea fits;
  BoxLayout* a = fits.content()->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
  for (int i = 0; i < 3; ++i) a->addWidget(fits.content()->add<Sized>(80.0f, 30.0f));
  fits.setGeometry({0.0f, 0.0f, 400.0f, 300.0f});
  CHECK_NEAR(scrollRange(fits, true), 0.0f, kEps);
  CHECK_NEAR(scrollRange(fits, false), 0.0f, kEps);

  // Tall content: a vertical bar is legitimate, a horizontal one still is not.
  ScrollArea tall;
  BoxLayout* b = tall.content()->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
  for (int i = 0; i < 20; ++i) b->addWidget(tall.content()->add<Sized>(80.0f, 30.0f));
  tall.setGeometry({0.0f, 0.0f, 400.0f, 300.0f});
  CHECK_NEAR(scrollRange(tall, true), 0.0f, kEps);
  CHECK(scrollRange(tall, false) > 0.0f);

  // ...and content that really is wider than the area still scrolls, by exactly
  // the difference.  A fix that just switched the bar off would pass the two
  // cases above and lose the feature.
  ScrollArea wide;
  BoxLayout* c = wide.content()->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
  c->addWidget(wide.content()->add<Sized>(600.0f, 30.0f));
  wide.setGeometry({0.0f, 0.0f, 400.0f, 300.0f});
  CHECK_NEAR(scrollRange(wide, true), 200.0f, kEps);
}

GEEYOOU_TEST(r2_safety, a_scrollarea_narrowed_after_being_wide_scrolls_back_to_nothing) {
  // The two defects compounded: the page latched the width it was first
  // arranged at, and the phantom bar made the leftover scrollable.  Opened at
  // 1000 and dragged to 300, the area stayed 710px scrollable for ever.
  ScrollArea sa;
  BoxLayout* box = sa.content()->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
  box->addWidget(sa.content()->add<Widget>(), 1);  // a plain panel, no hint

  sa.setGeometry({0.0f, 0.0f, 1000.0f, 400.0f});
  CHECK_NEAR(scrollRange(sa, true), 0.0f, kEps);

  sa.setGeometry({0.0f, 0.0f, 300.0f, 400.0f});
  CHECK_NEAR(sa.contentSize().width, 300.0f, kEps);
  CHECK_NEAR(scrollRange(sa, true), 0.0f, kEps);
}

// ================================================ grid track index clamping ===
GEEYOOU_TEST(r2_safety, a_grid_index_past_what_a_form_can_be_is_clamped_and_counted) {
  geeyoou::detail::resetLayoutDiagnostics();

  Widget root;
  root.setGeometry({0.0f, 0.0f, 200.0f, 200.0f});
  GridLayout* grid = root.setLayout<GridLayout>();
  grid->addWidget(root.add<Sized>(20.0f, 10.0f), 100000, 0);

  // Clamped to GridLayout::kMaxTrack (4096) rather than to 0xFFFE, which would
  // have made every pass resize six vectors to 131068 entries -- and counted,
  // rather than asserted, so the same thing happens in both configurations.
  CHECK_EQ(int(geeyoou::detail::layoutDiagnostics().indexClamped), 1);
  CHECK_EQ(grid->rowCount(), std::size_t(4097));

  geeyoou::detail::resetLayoutDiagnostics();
}
