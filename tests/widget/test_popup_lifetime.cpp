//
// What a select-style control may legally have done to it from inside the very
// notification it is emitting.
//
// Every one of these controls parks its popup on the WINDOW and subscribes to
// it, so the callback runs with a stack that looks like
//
//     Window::handleMouse -> PopupList::onMouse -> rowActivated.emit()
//                         -> our lambda -> setCurrentIndex() -> app slot
//
// and the application slot at the bottom is entitled (contract D7) to destroy
// anything except the object that owns the signal being emitted -- which is the
// PopupList, not the control.  So "the operator picked a value" is a perfectly
// legal moment to tear down the screen the control lives on, and any statement
// the control runs AFTER that dispatch is a use-after-free.
//
// The three controls therefore CLOSE FIRST and dispatch second, which these
// cases pin down by observing the order the application sees.  The other half
// of the same rule -- do not hold a reference across an emit -- is the
// MenuButton case at the top.
//
#include <string>
#include <vector>

#include "framework/Test.hpp"
#include "geeyoou/core/ConnectionScope.hpp"
#include "geeyoou/core/Date.hpp"
#include "geeyoou/core/Event.hpp"
#include "geeyoou/widget/ComboBox.hpp"
#include "geeyoou/widget/DatePicker.hpp"
#include "geeyoou/widget/MenuButton.hpp"
#include "geeyoou/widget/SelectModel.hpp"
#include "geeyoou/widget/Window.hpp"

using geeyoou::ComboBox;
using geeyoou::ConnectionScope;
using geeyoou::Date;
using geeyoou::Key;
using geeyoou::KeyEvent;
using geeyoou::MenuButton;
using geeyoou::MenuItem;
using geeyoou::MouseAction;
using geeyoou::MouseButton;
using geeyoou::MouseEvent;
using geeyoou::Rect;
using geeyoou::SelectItem;
using geeyoou::Widget;

namespace {

class TestWindow : public geeyoou::Window {
 public:
  TestWindow() : Window("geeyoou popup lifetime test", 480, 360) {}

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

// ---------------------------------------------------------------- MenuButton ---
GEEYOOU_TEST(popup_lifetime, menu_trigger_survives_set_items_from_its_own_slot) {
  // trigger() used to hold `const MenuItem& m` across triggeredIndex.emit() and
  // only then read m.id for the second emission.  Relabelling a menu from that
  // slot -- a language switch, a permission refresh -- calls setItems(), the
  // vector reallocates, and the reference names freed memory.
  TestWindow win;
  MenuButton* mb = win.add<MenuButton>();
  mb->setGeometry({0.0f, 0.0f, 140.0f, 32.0f});
  mb->setItems({MenuItem("导出", "export"), MenuItem("打印", "print")});

  std::string gotId;
  int gotIndex = -1;
  int relabels = 0;
  ConnectionScope conns;
  conns += mb->triggeredIndex.connect([&](int i) {
    gotIndex = i;
    ++relabels;
    // Grows the vector well past its capacity, so the storage really moves.
    std::vector<MenuItem> fresh;
    for (int n = 0; n < 16; ++n) {
      fresh.push_back(MenuItem("Item " + std::to_string(n), "id" + std::to_string(n)));
    }
    mb->setItems(std::move(fresh));
  });
  conns += mb->triggered.connect([&](const std::string& id) { gotId = id; });

  win.setFocusWidget(mb);
  win.handleKey(keyPress(Key::Down));  // Down opens the menu
  REQUIRE(mb->isMenuOpen());
  win.handleKey(keyPress(Key::Enter));  // ...and Enter fires the highlighted row

  CHECK_EQ(relabels, 1);
  CHECK_EQ(gotIndex, 0);
  // The value, not merely "something arrived": a dangling reference here used to
  // hand the application whatever the reallocated buffer happened to hold.
  CHECK_EQ(gotId, std::string("export"));
  CHECK_EQ(mb->items().size(), std::size_t(16));
  CHECK(!mb->isMenuOpen());
}

GEEYOOU_TEST(popup_lifetime, menu_closes_before_it_dispatches) {
  TestWindow win;
  MenuButton* mb = win.add<MenuButton>();
  mb->setGeometry({0.0f, 0.0f, 140.0f, 32.0f});
  mb->setItems({MenuItem("导出", "export")});

  std::string order;
  ConnectionScope conns;
  conns += win.popupClosed.connect([&] { order += "closed;"; });
  conns += mb->triggered.connect([&](const std::string&) { order += "triggered;"; });

  win.setFocusWidget(mb);
  win.handleKey(keyPress(Key::Down));
  REQUIRE(mb->isMenuOpen());
  win.handleKey(keyPress(Key::Enter));

  // Deliberately this way round: the dispatch has to be the LAST thing the
  // callback does, because it is the one that may destroy the button.  The
  // application therefore learns the menu closed before it learns what was
  // picked -- see the comment on MenuButton::ensureMenu().
  CHECK_EQ(order, std::string("closed;triggered;"));
}

// ------------------------------------------------------------------ ComboBox ---
GEEYOOU_TEST(popup_lifetime, select_closes_before_it_dispatches_on_click) {
  // The mouse path, which is the one that runs the rowActivated lambda.
  TestWindow win;
  ComboBox* cb = win.add<ComboBox>();
  cb->setGeometry({20.0f, 20.0f, 160.0f, 32.0f});
  cb->setItems({SelectItem("一号泵", "p1"), SelectItem("二号泵", "p2")});

  std::string order;
  int index = -99;
  ConnectionScope conns;
  conns += win.popupClosed.connect([&] { order += "closed;"; });
  conns += cb->currentIndexChanged.connect([&](int i) {
    order += "changed;";
    index = i;
  });

  cb->open();
  Widget* list = win.popup();
  REQUIRE(list != nullptr);

  // Just inside the list's first row: 6px past its top edge clears the 5px pad
  // whatever the theme's row height turns out to be.
  const Rect r = list->windowRect();
  win.handleMouse(mouseAt(MouseAction::Press, r.center().x, r.y() + 6.0f));

  CHECK_EQ(order, std::string("closed;changed;"));
  CHECK_EQ(index, 0);
  CHECK_EQ(cb->currentIndex(), 0);
  CHECK(!cb->isOpen());
}

GEEYOOU_TEST(popup_lifetime, select_closes_before_it_dispatches_on_enter) {
  // The keyboard path, which activates through SelectBase::onKey instead.
  TestWindow win;
  ComboBox* cb = win.add<ComboBox>();
  cb->setGeometry({20.0f, 20.0f, 160.0f, 32.0f});
  cb->setItems({SelectItem("一号泵", "p1"), SelectItem("二号泵", "p2")});

  std::string order;
  ConnectionScope conns;
  conns += win.popupClosed.connect([&] { order += "closed;"; });
  conns += cb->currentValueChanged.connect(
      [&](const std::string& v) { order += "changed:" + v + ";"; });

  win.setFocusWidget(cb);
  win.handleKey(keyPress(Key::Down));  // opens onto the current row
  REQUIRE(cb->isOpen());
  win.handleKey(keyPress(Key::Down));  // moves the highlight to row 1
  win.handleKey(keyPress(Key::Enter));

  CHECK_EQ(order, std::string("closed;changed:p2;"));
  CHECK(!cb->isOpen());
}

// ---------------------------------------------------------------- DatePicker ---
GEEYOOU_TEST(popup_lifetime, date_picker_closes_before_it_dispatches) {
  TestWindow win;
  geeyoou::DatePicker* dp = win.add<geeyoou::DatePicker>();
  dp->setGeometry({20.0f, 20.0f, 180.0f, 32.0f});
  dp->setDate(Date{2026, 8, 15});

  std::string order;
  Date chosen;
  ConnectionScope conns;
  conns += win.popupClosed.connect([&] { order += "closed;"; });
  conns += dp->dateChanged.connect([&](Date d) {
    order += "changed;";
    chosen = d;
  });

  dp->open();
  Widget* cal = win.popup();
  REQUIRE(cal != nullptr);

  // First cell of the month grid.  Its geometry is CalendarView's business; all
  // this case needs is a click that lands on some in-range day, so it aims at
  // the middle of the top-left cell using the layout constants that widget
  // publishes through preferredSize().
  const Rect r = cal->windowRect();
  const float gridTop = 8.0f + 34.0f + 22.0f;  // pad + header + weekday strip
  const float cellW = (r.width() - 16.0f) / 7.0f;
  const float cellH = (r.height() - gridTop - 8.0f) / 6.0f;
  win.handleMouse(mouseAt(MouseAction::Press, r.x() + 8.0f + cellW * 0.5f,
                          r.y() + gridTop + cellH * 0.5f));

  CHECK_EQ(order, std::string("closed;changed;"));
  CHECK(chosen.valid());
  CHECK(dp->date() == chosen);
  CHECK(!dp->isOpen());
}
