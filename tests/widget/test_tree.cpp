//
// Widget tree tests: ownership, coordinate mapping, enabled cascade, focus order.
//
// All of these run on a DETACHED subtree -- no Window is created.  That is legal
// by construction: Widget::update() looks for a Window, finds none, and returns,
// so a tree with no window is a fully functional tree minus repainting.  It is
// also the reason this file needs neither a message loop nor a display.
//
#include "geeyoou/widget/Widget.hpp"

#include <string>
#include <vector>

#include "framework/Test.hpp"

using geeyoou::FocusPolicy;
using geeyoou::Point;
using geeyoou::Rect;
using geeyoou::Widget;
using geeyoou::Window;

namespace {
constexpr float kEps = 0.0005f;
}  // namespace

// ---------------------------------------------------------------- ownership ---
GEEYOOU_TEST(tree, add_builds_parent_and_child_links) {
  Widget root;
  Widget* a = root.add<Widget>();
  Widget* b = root.add<Widget>();
  Widget* a1 = a->add<Widget>();

  REQUIRE(a != nullptr);
  REQUIRE(b != nullptr);
  REQUIRE(a1 != nullptr);

  CHECK_EQ(root.children().size(), std::size_t(2));
  CHECK_EQ(root.children()[0].get(), a);
  CHECK_EQ(root.children()[1].get(), b);  // append order, not reordered
  CHECK_EQ(a->children().size(), std::size_t(1));

  CHECK_EQ(a->parent(), &root);
  CHECK_EQ(b->parent(), &root);
  CHECK_EQ(a1->parent(), a);
  CHECK_EQ(root.parent(), static_cast<Widget*>(nullptr));

  // A detached tree has no window; update() during add<>() must have been a
  // no-op rather than a null dereference.
  CHECK_EQ(root.window(), static_cast<Window*>(nullptr));
  CHECK_EQ(a1->window(), static_cast<Window*>(nullptr));
}

// --------------------------------------------------------------- coordinates ---
GEEYOOU_TEST(tree, map_to_window_accumulates_every_ancestor_origin) {
  Widget root;
  root.setGeometry({0.0f, 0.0f, 400.0f, 300.0f});
  Widget* panel = root.add<Widget>();
  panel->setGeometry({30.0f, 20.0f, 200.0f, 150.0f});
  Widget* leaf = panel->add<Widget>();
  leaf->setGeometry({5.0f, 7.0f, 50.0f, 20.0f});

  const Point origin = leaf->mapToWindow({0.0f, 0.0f});
  CHECK_NEAR(origin.x, 35.0f, kEps);
  CHECK_NEAR(origin.y, 27.0f, kEps);

  // An interior point maps by the same offset -- mapToWindow is a translation,
  // never a scale (DPI enters exactly twice, and neither time is here).
  const Point inner = leaf->mapToWindow({10.0f, 3.0f});
  CHECK_NEAR(inner.x, 45.0f, kEps);
  CHECK_NEAR(inner.y, 30.0f, kEps);

  const Rect wr = leaf->windowRect();
  CHECK_NEAR(wr.x(), 35.0f, kEps);
  CHECK_NEAR(wr.y(), 27.0f, kEps);
  CHECK_NEAR(wr.width(), 50.0f, kEps);
  CHECK_NEAR(wr.height(), 20.0f, kEps);

  // localRect is geometry moved to the origin: what onPaint sees.
  CHECK_NEAR(leaf->localRect().x(), 0.0f, kEps);
  CHECK_NEAR(leaf->localRect().width(), 50.0f, kEps);
}

GEEYOOU_TEST(tree, content_offset_moves_children_not_the_scroller) {
  Widget root;
  root.setGeometry({0.0f, 0.0f, 400.0f, 300.0f});
  Widget* panel = root.add<Widget>();
  panel->setGeometry({30.0f, 20.0f, 200.0f, 150.0f});
  Widget* leaf = panel->add<Widget>();
  leaf->setGeometry({5.0f, 7.0f, 50.0f, 20.0f});

  panel->setContentOffset({0.0f, 40.0f});
  CHECK_NEAR(panel->contentOffset().y, 40.0f, kEps);

  // The scroller itself stays put; only what it contains moves.
  CHECK_NEAR(panel->windowRect().y(), 20.0f, kEps);
  CHECK_NEAR(leaf->windowRect().y(), -13.0f, kEps);  // 20 + 7 - 40
  CHECK_NEAR(leaf->windowRect().x(), 35.0f, kEps);   // x untouched

  // Offsets compose down the chain, which is what makes a list inside a scroll
  // area inside another scroll area work without either knowing about the other.
  root.setContentOffset({10.0f, 0.0f});
  CHECK_NEAR(panel->windowRect().x(), 20.0f, kEps);
  CHECK_NEAR(leaf->windowRect().x(), 25.0f, kEps);
  CHECK_NEAR(leaf->windowRect().y(), -13.0f, kEps);

  // Setting the same offset again is a no-op, not a redundant invalidation.
  panel->setContentOffset({0.0f, 40.0f});
  CHECK_NEAR(leaf->windowRect().y(), -13.0f, kEps);
}

// ------------------------------------------------------------------ enabled ---
GEEYOOU_TEST(tree, disabling_a_container_cascades_to_the_whole_subtree) {
  Widget root;
  Widget* group = root.add<Widget>();
  Widget* field = group->add<Widget>();
  Widget* deep = field->add<Widget>();

  CHECK(root.isEffectivelyEnabled());
  CHECK(deep->isEffectivelyEnabled());

  group->setEnabled(false);
  CHECK(!group->isEnabled());
  CHECK(!group->isEffectivelyEnabled());
  CHECK(!field->isEffectivelyEnabled());
  CHECK(!deep->isEffectivelyEnabled());
  // The descendants' OWN flag is untouched, so re-enabling the container
  // restores exactly what the operator had before.
  CHECK(field->isEnabled());
  CHECK(deep->isEnabled());
  CHECK(root.isEffectivelyEnabled());

  // An individually disabled leaf stays disabled when the container comes back.
  deep->setEnabled(false);
  group->setEnabled(true);
  CHECK(field->isEffectivelyEnabled());
  CHECK(!deep->isEffectivelyEnabled());
}

GEEYOOU_TEST(tree, focusability_needs_policy_visibility_and_enabled) {
  Widget root;
  Widget* w = root.add<Widget>();

  CHECK(!w->isFocusable());  // FocusPolicy::None by default
  w->setFocusPolicy(FocusPolicy::Tab);
  CHECK(w->isFocusable());

  w->setVisible(false);
  CHECK(!w->isFocusable());
  CHECK(!w->isVisible());
  w->setVisible(true);

  w->setEnabled(false);
  CHECK(!w->isFocusable());
  w->setEnabled(true);

  root.setEnabled(false);
  CHECK(!w->isFocusable());  // through isEffectivelyEnabled
  root.setEnabled(true);

  // A hidden ANCESTOR is deliberately not part of isFocusable(): the predicate
  // is per-widget, and it is collectFocusable that prunes hidden subtrees.
  root.setVisible(false);
  CHECK(w->isFocusable());
  std::vector<Widget*> order;
  root.collectFocusable(order);
  CHECK(order.empty());

  // Click policy is focusable but not tab-reachable.
  root.setVisible(true);
  w->setFocusPolicy(FocusPolicy::Click);
  CHECK(w->isFocusable());
  order.clear();
  root.collectFocusable(order);
  CHECK(order.empty());
}

GEEYOOU_TEST(tree, collect_focusable_is_preorder_and_prunes_subtrees) {
  Widget root;
  Widget* a = root.add<Widget>();
  Widget* a1 = a->add<Widget>();
  Widget* b = root.add<Widget>();       // Click: skipped by Tab
  Widget* b1 = b->add<Widget>();        // ...but its children are still visited
  Widget* hidden = root.add<Widget>();
  Widget* hidden1 = hidden->add<Widget>();
  Widget* off = root.add<Widget>();
  Widget* off1 = off->add<Widget>();
  Widget* e = root.add<Widget>();

  for (Widget* w : {a, a1, b1, hidden, hidden1, off, off1, e}) {
    w->setFocusPolicy(FocusPolicy::Tab);
  }
  b->setFocusPolicy(FocusPolicy::Click);
  hidden->setVisible(false);
  off->setEnabled(false);

  std::vector<Widget*> order;
  root.collectFocusable(order);
  REQUIRE(order.size() == std::size_t(4));
  CHECK_EQ(order[0], a);
  CHECK_EQ(order[1], a1);
  CHECK_EQ(order[2], b1);  // parent skipped, child still reached
  CHECK_EQ(order[3], e);

  // The root joins the front of its own traversal when it is itself focusable.
  root.setFocusPolicy(FocusPolicy::Tab);
  order.clear();
  root.collectFocusable(order);
  REQUIRE(order.size() == std::size_t(5));
  CHECK_EQ(order[0], &root);
  CHECK_EQ(order[1], a);
}

// ------------------------------------------------------------ style identity ---
GEEYOOU_TEST(tree, style_classes_split_on_whitespace) {
  Widget w;
  w.setStyleClasses("  danger   big \t wide \n");
  REQUIRE(w.styleClasses().size() == std::size_t(3));
  CHECK_EQ(w.styleClasses()[0], std::string("danger"));
  CHECK_EQ(w.styleClasses()[1], std::string("big"));
  CHECK_EQ(w.styleClasses()[2], std::string("wide"));
  CHECK(w.hasStyleClass("big"));
  CHECK(!w.hasStyleClass("bi"));  // whole token, not a prefix

  w.addStyleClass("danger");  // already present
  CHECK_EQ(w.styleClasses().size(), std::size_t(3));
  w.addStyleClass("");  // rejected, not stored as an empty class
  CHECK_EQ(w.styleClasses().size(), std::size_t(3));
  w.addStyleClass("tall");
  CHECK_EQ(w.styleClasses().size(), std::size_t(4));

  w.removeStyleClass("big");
  CHECK(!w.hasStyleClass("big"));
  CHECK_EQ(w.styleClasses().size(), std::size_t(3));
  w.removeStyleClass("nosuch");
  CHECK_EQ(w.styleClasses().size(), std::size_t(3));

  // setStyleClasses REPLACES rather than appends.
  w.setStyleClasses("only");
  CHECK_EQ(w.styleClasses().size(), std::size_t(1));
  w.setStyleClasses("");
  CHECK(w.styleClasses().empty());

  CHECK_EQ(w.objectName(), std::string(""));
  w.setObjectName("startPump");
  CHECK_EQ(w.objectName(), std::string("startPump"));
  CHECK(w.styleMatchesType("Widget"));
  CHECK(!w.styleMatchesType("PushButton"));
}
