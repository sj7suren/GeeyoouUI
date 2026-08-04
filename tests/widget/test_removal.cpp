//
// Widget removal tests: takeChild / removeChild / clearChildren, and the two
// lifetime holes that adding them opens.
//
// Until this API existed, nothing in the library could destroy a widget short
// of destroying the whole Window, and the REQ-6 audit
// (docs/iterations/01-lifecycle-and-tests.md) recorded that the D7 contract was
// held up by that absence rather than by any design.  The interesting cases
// below are therefore not "does erase() work" but:
//
//   * the Window observes widgets with four raw pointers (focus, hover, mouse
//     grab, popup) and they routinely name a GRANDCHILD of whatever is being
//     removed, so the detach announcement has to walk the whole subtree;
//   * an event bubble is a loop that dereferences a widget AND THEN reads its
//     parent, so a handler that removes widgets used to leave it walking freed
//     memory.
//
// The Window cases build a real platform window.  It is never shown and no
// message loop runs; what they exercise is Window's own bookkeeping, and a fake
// window would only prove that the fake works.
//
#include "geeyoou/widget/Widget.hpp"

#include <memory>
#include <vector>

#include "framework/Test.hpp"
#include "geeyoou/core/ConnectionScope.hpp"
#include "geeyoou/core/Event.hpp"
#include "geeyoou/core/Signal.hpp"
#include "geeyoou/render/Canvas.hpp"
#include "geeyoou/render/Offscreen.hpp"
#include "geeyoou/render/Painter.hpp"
#include "geeyoou/widget/Window.hpp"

using geeyoou::Canvas;
using geeyoou::ConnectionScope;
using geeyoou::FocusPolicy;
using geeyoou::Key;
using geeyoou::KeyEvent;
using geeyoou::MouseAction;
using geeyoou::MouseButton;
using geeyoou::MouseEvent;
using geeyoou::OffscreenImage;
using geeyoou::Painter;
using geeyoou::Rect;
using geeyoou::Signal;
using geeyoou::Widget;

namespace {

// Counts its own destruction through a pointer the CALLER owns -- the widget is
// gone by the time the assertion runs, so the counter cannot live in it.
class Tracer : public Widget {
 public:
  explicit Tracer(int* deaths) : deaths_(deaths) {}
  ~Tracer() override {
    if (deaths_) ++*deaths_;
  }

 private:
  int* deaths_ = nullptr;
};

// Same trick for paint and for input, so a subtree that is supposed to be gone
// can be shown to be gone rather than merely unreferenced.
class Probe : public Widget {
 public:
  explicit Probe(int* paints) : paints_(paints) {}

 protected:
  void onPaint(Painter&, const Rect&) override {
    if (paints_) ++*paints_;
  }

 private:
  int* paints_ = nullptr;
};

// Counts the events that reach it.  Used as an ANCESTOR of the widget whose
// handler removes things, so the test can say where the bubble stopped.
class Counter : public Widget {
 public:
  int mouse = 0;
  int key = 0;

 protected:
  void onMouse(const MouseEvent&) override { ++mouse; }
  void onKey(const KeyEvent&) override { ++key; }
};

// Stands in for everything that emits from inside an event handler and OUTLIVES
// the widgets its slots destroy: a navigation bar, Window::popupClosed, a
// DataHub.  Keeping the signal here rather than on the emitting widget is what
// makes the test respect contract D7 -- the object that owns the signal is not
// one of the objects the slot destroys.
struct Navigator {
  Signal<> go;
};

// A control that reports one mouse action (and any key) to the Navigator and
// does NOT accept the event, so the bubble carries on afterwards.  That
// continuation is the thing under test.
class Emitter : public Widget {
 public:
  explicit Emitter(Navigator* nav, MouseAction fireOn = MouseAction::Press)
      : nav_(nav), fireOn_(fireOn) {}

 protected:
  void onMouse(const MouseEvent& e) override {
    if (e.action == fireOn_ && nav_) nav_->go.emit();
  }
  void onKey(const KeyEvent& e) override {
    if (e.pressed && nav_) nav_->go.emit();
  }

 private:
  Navigator* nav_ = nullptr;
  MouseAction fireOn_ = MouseAction::Press;
};

// Emits from the focus-OUT notification, which is where LineEdit emits
// editingFinished -- the one place a press runs application code before the
// press itself has been delivered to anybody.
class FocusEmitter : public Widget {
 public:
  explicit FocusEmitter(Navigator* nav) : nav_(nav) {
    setFocusPolicy(FocusPolicy::Tab);
  }

 protected:
  void onFocusChanged(bool focused) override {
    if (!focused && nav_) nav_->go.emit();
  }

 private:
  Navigator* nav_ = nullptr;
};

// Feeding synthetic input through Window::handleMouse is the only way to reach
// the hover and mouse-grab bookkeeping; both are set by dispatch and have no
// setter, by design.
class TestWindow : public geeyoou::Window {
 public:
  TestWindow() : Window("geeyoou removal test", 400, 300) {}

  using Window::handleKey;
  using Window::handleMouse;
};

MouseEvent mouseAt(MouseAction action, float x, float y) {
  MouseEvent e;
  e.action = action;
  e.button = (action == MouseAction::Press || action == MouseAction::Release)
                 ? MouseButton::Left
                 : MouseButton::None;
  e.windowPos = {x, y};
  return e;
}

KeyEvent keyPress(Key k) {
  KeyEvent e;
  e.key = k;
  e.pressed = true;
  return e;
}

}  // namespace

// ---------------------------------------------------------------- ownership ---
GEEYOOU_TEST(removal, take_child_hands_the_whole_subtree_to_the_caller) {
  Widget root;
  Widget* a = root.add<Widget>();
  Widget* b = root.add<Widget>();
  Widget* a1 = a->add<Widget>();

  std::unique_ptr<Widget> owned = root.takeChild(a);
  REQUIRE(owned != nullptr);
  CHECK_EQ(owned.get(), a);

  // The parent has let go...
  CHECK_EQ(root.children().size(), std::size_t(1));
  CHECK_EQ(root.children()[0].get(), b);
  CHECK_EQ(a->parent(), static_cast<Widget*>(nullptr));
  // ...but the subtree UNDER the taken widget travelled with it intact.
  CHECK_EQ(a->children().size(), std::size_t(1));
  CHECK_EQ(a1->parent(), a);

  // Taking the same widget twice, taking a grandchild from the wrong parent,
  // and taking nothing at all are all answered with nullptr rather than with
  // an erased-something-else.
  CHECK(root.takeChild(a) == nullptr);
  CHECK(root.takeChild(a1) == nullptr);
  CHECK(root.takeChild(nullptr) == nullptr);
  CHECK_EQ(root.children().size(), std::size_t(1));

  // The caller now owns it: re-parenting is just an add of what we hold.
  CHECK_EQ(a->window(), static_cast<geeyoou::Window*>(nullptr));
}

GEEYOOU_TEST(removal, remove_child_destroys_in_place_including_descendants) {
  int deaths = 0;
  Widget root;
  Tracer* t = root.add<Tracer>(&deaths);
  t->add<Tracer>(&deaths);
  t->add<Tracer>(&deaths)->add<Tracer>(&deaths);
  Widget* survivor = root.add<Widget>();

  root.removeChild(t);
  CHECK_EQ(deaths, 4);  // the node and all three descendants
  CHECK_EQ(root.children().size(), std::size_t(1));
  CHECK_EQ(root.children()[0].get(), survivor);

  // Removing something that is not ours must not touch its real parent.
  Widget other;
  Widget* o = other.add<Widget>();
  root.removeChild(o);
  CHECK_EQ(other.children().size(), std::size_t(1));
  root.removeChild(nullptr);
  CHECK_EQ(root.children().size(), std::size_t(1));
}

GEEYOOU_TEST(removal, clear_children_empties_the_list_and_is_idempotent) {
  int deaths = 0;
  Widget root;
  for (int i = 0; i < 4; ++i) {
    Tracer* row = root.add<Tracer>(&deaths);
    row->add<Tracer>(&deaths);
  }
  CHECK_EQ(root.children().size(), std::size_t(4));

  root.clearChildren();
  CHECK_EQ(deaths, 8);
  CHECK(root.children().empty());

  root.clearChildren();  // nothing left to do, and no way to say so wrongly
  CHECK(root.children().empty());
}

// -------------------------------------------------- window observer pointers ---
GEEYOOU_TEST(removal, detach_reaches_every_node_of_the_removed_subtree) {
  // The one that is easiest to get wrong: all three pointers name the
  // GRANDCHILD, and it is the child that gets removed.  A notification that
  // only announced the removal's root would leave every one of them dangling.
  TestWindow win;
  Widget* page = win.add<Widget>();
  page->setGeometry({0.0f, 0.0f, 400.0f, 300.0f});
  Widget* row = page->add<Widget>();
  row->setGeometry({0.0f, 0.0f, 200.0f, 50.0f});
  Widget* cell = row->add<Widget>();
  cell->setGeometry({0.0f, 0.0f, 100.0f, 20.0f});
  cell->setFocusPolicy(FocusPolicy::Tab);

  win.setFocusWidget(cell);
  win.handleMouse(mouseAt(MouseAction::Move, 10.0f, 10.0f));
  win.handleMouse(mouseAt(MouseAction::Press, 10.0f, 10.0f));

  REQUIRE(win.focusWidget() == cell);
  REQUIRE(win.hoveredWidget() == cell);
  REQUIRE(win.mouseGrabWidget() == cell);

  page->removeChild(row);

  CHECK_EQ(win.focusWidget(), static_cast<Widget*>(nullptr));
  CHECK_EQ(win.hoveredWidget(), static_cast<Widget*>(nullptr));
  CHECK_EQ(win.mouseGrabWidget(), static_cast<Widget*>(nullptr));
  CHECK(page->children().empty());

  // And the window is still usable afterwards -- a Move that finds nothing must
  // not try to send a Leave to what used to be hovered.
  win.handleMouse(mouseAt(MouseAction::Move, 380.0f, 280.0f));
  CHECK_EQ(win.hoveredWidget(), page);
}

GEEYOOU_TEST(removal, removing_the_focused_widget_clears_the_focus) {
  TestWindow win;
  Widget* a = win.add<Widget>();
  a->setGeometry({0.0f, 0.0f, 100.0f, 40.0f});
  a->setFocusPolicy(FocusPolicy::Tab);
  Widget* b = win.add<Widget>();
  b->setGeometry({0.0f, 60.0f, 100.0f, 40.0f});
  b->setFocusPolicy(FocusPolicy::Tab);

  win.setFocusWidget(a);
  REQUIRE(win.focusWidget() == a);

  win.removeChild(a);
  CHECK_EQ(win.focusWidget(), static_cast<Widget*>(nullptr));

  // Focus is CLEARED, not moved: Tab from nowhere still finds the survivor, so
  // the operator is never stranded without a keyboard target.
  CHECK(win.focusNext(false));
  CHECK_EQ(win.focusWidget(), b);
}

GEEYOOU_TEST(removal, removing_the_open_popup_closes_it_properly) {
  TestWindow win;
  int closes = 0;
  ConnectionScope conns;
  conns += win.popupClosed.connect([&closes] { ++closes; });

  // Case 1: the popup itself is removed.
  Widget* pop = win.add<Widget>();
  pop->setGeometry({0.0f, 0.0f, 120.0f, 80.0f});
  win.openPopup(pop, {0.0f, 0.0f, 60.0f, 24.0f});
  REQUIRE(win.popup() == pop);

  win.removeChild(pop);
  CHECK_EQ(win.popup(), static_cast<Widget*>(nullptr));
  // popupClosed, not just a nulled pointer: the control that opened the popup
  // clears its own "I am open" state from this signal and nothing else.
  CHECK_EQ(closes, 1);

  // Case 2: the popup is a DESCENDANT of what gets removed, which is the shape
  // a Cascader's column stack has.
  Widget* holder = win.add<Widget>();
  Widget* nested = holder->add<Widget>();
  nested->setGeometry({0.0f, 0.0f, 120.0f, 80.0f});
  win.openPopup(nested, {0.0f, 0.0f, 60.0f, 24.0f});
  REQUIRE(win.popup() == nested);

  win.removeChild(holder);
  CHECK_EQ(win.popup(), static_cast<Widget*>(nullptr));
  CHECK_EQ(closes, 2);
}

// ------------------------------------------------------- removal from a slot ---
GEEYOOU_TEST(removal, a_slot_may_remove_the_widget_the_mouse_bubble_stands_on) {
  // The crash this exists to prevent: dispatchMouse called onMouse and THEN
  // read w->parent_.  Here the handler's slot removes `row`, which destroys
  // both `row` and the `btn` whose handler is running -- so both the current
  // node and every ancestor up to `page` are freed before that read.
  TestWindow win;
  Navigator nav;
  int deaths = 0;

  Counter* page = win.add<Counter>();
  page->setGeometry({0.0f, 0.0f, 400.0f, 300.0f});
  Counter* row = page->add<Counter>();
  row->setGeometry({0.0f, 0.0f, 200.0f, 50.0f});
  Emitter* btn = row->add<Emitter>(&nav);
  btn->setGeometry({0.0f, 0.0f, 100.0f, 20.0f});
  btn->add<Tracer>(&deaths);

  ConnectionScope conns;
  conns += nav.go.connect([page, row] { page->removeChild(row); });

  // Hover first, so the press does not also carry an Enter -- that one bubbles
  // legitimately and would be indistinguishable from the bubble under test.
  win.handleMouse(mouseAt(MouseAction::Move, 10.0f, 10.0f));
  REQUIRE(win.hoveredWidget() == btn);
  page->mouse = 0;
  row->mouse = 0;

  win.handleMouse(mouseAt(MouseAction::Press, 10.0f, 10.0f));

  CHECK_EQ(deaths, 1);  // the removal really happened
  CHECK(page->children().empty());
  // The bubble ENDS at the widget that left the tree.  Continuing would mean
  // reading a freed parent pointer, and there is no ancestor left to resume
  // from: what is removed is always a whole subtree, so if any ancestor of the
  // current node is going, the current node is going with it.
  CHECK_EQ(page->mouse, 0);
  // The window's own pointers were dropped on the way through.
  CHECK_EQ(win.hoveredWidget(), static_cast<Widget*>(nullptr));
  CHECK_EQ(win.mouseGrabWidget(), static_cast<Widget*>(nullptr));

  // Still alive and still dispatching.
  Counter* fresh = win.add<Counter>();
  fresh->setGeometry({0.0f, 0.0f, 400.0f, 300.0f});
  win.handleMouse(mouseAt(MouseAction::Move, 20.0f, 20.0f));
  CHECK_GE(fresh->mouse, 1);
}

GEEYOOU_TEST(removal, a_slot_may_remove_the_widget_the_key_bubble_stands_on) {
  // Same hole in dispatchKey.  Enter on a focused control is exactly where an
  // application swaps the screen out from under itself.
  TestWindow win;
  Navigator nav;
  int deaths = 0;

  Counter* page = win.add<Counter>();
  page->setGeometry({0.0f, 0.0f, 400.0f, 300.0f});
  Counter* row = page->add<Counter>();
  row->setGeometry({0.0f, 0.0f, 200.0f, 50.0f});
  Emitter* btn = row->add<Emitter>(&nav);
  btn->setGeometry({0.0f, 0.0f, 100.0f, 20.0f});
  btn->setFocusPolicy(FocusPolicy::Tab);
  btn->add<Tracer>(&deaths);

  ConnectionScope conns;
  conns += nav.go.connect([page, row] { page->removeChild(row); });

  win.setFocusWidget(btn);
  REQUIRE(win.focusWidget() == btn);

  win.handleKey(keyPress(Key::Enter));

  CHECK_EQ(deaths, 1);
  CHECK_EQ(page->key, 0);
  CHECK_EQ(win.focusWidget(), static_cast<Widget*>(nullptr));

  // A key with nothing focused now falls to the window instead of into a hole.
  win.handleKey(keyPress(Key::Escape));
}

GEEYOOU_TEST(removal, a_slot_may_destroy_the_bubble_subtree_without_the_removal_api) {
  // The removal API is not the only door out of the tree, and the bubble guard
  // used to be armed on that door alone: cancellation happened inside
  // announceDetached, which only runs for takeChild/removeChild/clearChildren.
  //
  // Here the slot destroys the subtree by DROPPING THE OWNING unique_ptr --
  // exactly what an application does with the result of takeChild(), and the
  // same code path a Window declared on the stack takes when it goes out of
  // scope.  Nothing announces that, so ~Widget has to cancel the bubble itself.
  Navigator nav;
  int deaths = 0;

  std::unique_ptr<Counter> page = std::make_unique<Counter>();
  page->setGeometry({0.0f, 0.0f, 400.0f, 300.0f});
  Counter* row = page->add<Counter>();
  row->setGeometry({0.0f, 0.0f, 200.0f, 50.0f});
  Emitter* btn = row->add<Emitter>(&nav);
  btn->setGeometry({0.0f, 0.0f, 100.0f, 20.0f});
  btn->add<Tracer>(&deaths);

  ConnectionScope conns;
  conns += nav.go.connect([&page] { page.reset(); });

  // Straight into the bubble, no Window involved: what is under test is the
  // walk, and every widget on it -- including the one it is standing on -- is
  // about to be freed by the handler.
  btn->dispatchMouse(mouseAt(MouseAction::Press, 10.0f, 10.0f));

  CHECK_EQ(deaths, 1);
  CHECK(page == nullptr);  // the whole tree went, and the walk did not follow it
}

GEEYOOU_TEST(removal, detach_still_reaches_a_sibling_that_a_slot_shifted_down) {
  // The announcement walks the child list while application code is allowed to
  // edit it.  Re-reading size() each step stops the overrun but NOT the shift:
  // a slot that removes an EARLIER sibling moves every later one down by one,
  // and an index walk then steps straight over a whole subtree.  Nothing in
  // that subtree gets announced, so the window keeps its focus/hover/grab
  // pointer aimed into memory that is freed moments later.
  TestWindow win;
  int deaths = 0;

  Widget* holder = win.add<Widget>();
  holder->setGeometry({0.0f, 0.0f, 400.0f, 300.0f});
  Widget* first = holder->add<Widget>();      // what the slot removes
  Widget* pop = holder->add<Widget>();        // announcing THIS is what emits
  pop->setGeometry({0.0f, 0.0f, 120.0f, 80.0f});
  Widget* later = holder->add<Widget>();      // the one that used to be skipped
  Widget* deep = later->add<Tracer>(&deaths); // ...and the pointer aimed into it
  deep->setGeometry({0.0f, 0.0f, 60.0f, 20.0f});
  deep->setFocusPolicy(FocusPolicy::Tab);

  win.openPopup(pop, {0.0f, 0.0f, 60.0f, 24.0f});
  win.setFocusWidget(deep);
  REQUIRE(win.popup() == pop);
  REQUIRE(win.focusWidget() == deep);

  bool once = false;
  ConnectionScope conns;
  conns += win.popupClosed.connect([&] {
    if (once) return;
    once = true;
    holder->removeChild(first);  // shifts `pop` and `later` down one slot
  });

  win.removeChild(holder);

  CHECK(once);                 // the slot really ran, mid-announcement
  CHECK_EQ(deaths, 1);
  // The point of the case: `deep` sits under the sibling the shift displaced,
  // and it was still announced.
  CHECK_EQ(win.focusWidget(), static_cast<Widget*>(nullptr));
  CHECK(win.children().empty());
}

GEEYOOU_TEST(removal, detach_survives_a_slot_that_removes_the_node_being_announced) {
  // The other half of the same hazard: widgetDetached() emits popupClosed, and
  // a slot on it may remove the very node whose announcement is in flight (D7
  // allows destroying objects other than the signal's owner).  The walk then
  // has to stop rather than read that node's child list.
  TestWindow win;
  int deaths = 0;

  Widget* holder = win.add<Widget>();
  holder->setGeometry({0.0f, 0.0f, 400.0f, 300.0f});
  Tracer* pop = holder->add<Tracer>(&deaths);
  pop->setGeometry({0.0f, 0.0f, 120.0f, 80.0f});
  pop->add<Tracer>(&deaths);

  win.openPopup(pop, {0.0f, 0.0f, 60.0f, 24.0f});
  REQUIRE(win.popup() == pop);

  bool once = false;
  ConnectionScope conns;
  conns += win.popupClosed.connect([&] {
    if (once) return;
    once = true;
    holder->removeChild(pop);  // the node that is being announced right now
  });

  win.removeChild(holder);

  CHECK(once);
  CHECK_EQ(deaths, 2);  // the popup and its child, destroyed exactly once each
  CHECK_EQ(win.popup(), static_cast<Widget*>(nullptr));
  CHECK(win.children().empty());

  // Still usable: a stale observer pointer would surface on the next event.
  Counter* fresh = win.add<Counter>();
  fresh->setGeometry({0.0f, 0.0f, 400.0f, 300.0f});
  win.handleMouse(mouseAt(MouseAction::Move, 20.0f, 20.0f));
  CHECK_GE(fresh->mouse, 1);
}

GEEYOOU_TEST(removal, an_enter_handler_may_remove_what_the_pointer_is_over) {
  // Window::handleMouse picks a `target`, then sends synthetic Enter/Leave, and
  // only afterwards delivers the real event to that same target.  A handler
  // that removed the target used to leave the rest of the function holding a
  // freed pointer -- the bubble guard does not help here, because the stale
  // pointer is Window's local, not the walk's.
  TestWindow win;
  Navigator nav;
  int deaths = 0;

  Counter* page = win.add<Counter>();
  page->setGeometry({0.0f, 0.0f, 400.0f, 300.0f});
  Widget* row = page->add<Widget>();
  row->setGeometry({0.0f, 0.0f, 200.0f, 50.0f});
  Emitter* cell = row->add<Emitter>(&nav, MouseAction::Enter);
  cell->setGeometry({0.0f, 0.0f, 100.0f, 20.0f});
  cell->add<Tracer>(&deaths);

  ConnectionScope conns;
  conns += nav.go.connect([page, row] { page->removeChild(row); });

  // The press carries the Enter, since nothing was hovered before it.
  win.handleMouse(mouseAt(MouseAction::Press, 10.0f, 10.0f));

  CHECK_EQ(deaths, 1);
  CHECK_EQ(win.hoveredWidget(), static_cast<Widget*>(nullptr));
  CHECK_EQ(win.mouseGrabWidget(), static_cast<Widget*>(nullptr));
  CHECK(page->children().empty());
}

GEEYOOU_TEST(removal, a_focus_out_handler_may_remove_what_was_just_clicked) {
  // Clicking moves the focus BEFORE the press is delivered, and the focus-out
  // notification is application code.  Here it removes the subtree containing
  // the widget that was clicked.
  TestWindow win;
  Navigator nav;
  int deaths = 0;

  Counter* page = win.add<Counter>();
  page->setGeometry({0.0f, 0.0f, 400.0f, 300.0f});
  FocusEmitter* field = page->add<FocusEmitter>(&nav);
  field->setGeometry({0.0f, 0.0f, 100.0f, 20.0f});
  Widget* row = page->add<Widget>();
  row->setGeometry({0.0f, 40.0f, 200.0f, 50.0f});
  Widget* button = row->add<Widget>();
  button->setGeometry({0.0f, 0.0f, 100.0f, 20.0f});
  button->setFocusPolicy(FocusPolicy::Tab);
  button->add<Tracer>(&deaths);

  ConnectionScope conns;
  conns += nav.go.connect([page, row] { page->removeChild(row); });

  win.setFocusWidget(field);
  REQUIRE(win.focusWidget() == field);

  win.handleMouse(mouseAt(MouseAction::Press, 10.0f, 45.0f));

  CHECK_EQ(deaths, 1);
  // The focus landed on `button` and was taken away again by the removal, so it
  // must read as "nothing focused" rather than as the widget that just died.
  CHECK_EQ(win.focusWidget(), static_cast<Widget*>(nullptr));
  CHECK_EQ(win.mouseGrabWidget(), static_cast<Widget*>(nullptr));
  CHECK_EQ(win.hoveredWidget(), static_cast<Widget*>(nullptr));
  CHECK_EQ(page->children().size(), std::size_t(1));
}

// ------------------------------------------------------ traversal after removal ---
GEEYOOU_TEST(removal, the_removed_subtree_leaves_focus_order_paint_and_hit_test) {
  int keptPaints = 0;
  int gonePaints = 0;

  Widget root;
  root.setGeometry({0.0f, 0.0f, 200.0f, 200.0f});
  Probe* keep = root.add<Probe>(&keptPaints);
  keep->setGeometry({0.0f, 0.0f, 100.0f, 100.0f});
  keep->setFocusPolicy(FocusPolicy::Tab);
  Probe* gone = root.add<Probe>(&gonePaints);
  gone->setGeometry({100.0f, 0.0f, 100.0f, 100.0f});
  gone->setFocusPolicy(FocusPolicy::Tab);
  Probe* goneChild = gone->add<Probe>(&gonePaints);
  goneChild->setGeometry({0.0f, 0.0f, 50.0f, 50.0f});
  goneChild->setFocusPolicy(FocusPolicy::Tab);

  OffscreenImage img(200, 200);
  REQUIRE(img.valid());
  const Rect whole(0.0f, 0.0f, 200.0f, 200.0f);

  auto paintOnce = [&img, &whole](Widget& w) {
    Canvas canvas;
    if (!canvas.begin(img.surface(), whole)) return false;
    Painter p = canvas.painter();
    w.paintTree(p, whole, whole);
    canvas.end();
    return true;
  };

  std::vector<Widget*> order;
  root.collectFocusable(order);
  REQUIRE(order.size() == std::size_t(3));
  CHECK_EQ(order[1], gone);
  CHECK_EQ(root.hitTest({110.0f, 10.0f}), goneChild);
  CHECK_EQ(root.hitTest({160.0f, 60.0f}), gone);
  REQUIRE(paintOnce(root));
  CHECK_EQ(keptPaints, 1);
  CHECK_EQ(gonePaints, 2);

  root.removeChild(gone);

  order.clear();
  root.collectFocusable(order);
  REQUIRE(order.size() == std::size_t(1));
  CHECK_EQ(order[0], keep);

  CHECK_EQ(root.hitTest({110.0f, 10.0f}), &root);
  CHECK_EQ(root.hitTest({160.0f, 60.0f}), &root);
  CHECK_EQ(root.hitTest({10.0f, 10.0f}), keep);

  keptPaints = 0;
  gonePaints = 0;
  REQUIRE(paintOnce(root));
  CHECK_EQ(keptPaints, 1);
  CHECK_EQ(gonePaints, 0);  // nothing left to draw, and nothing tried
}
