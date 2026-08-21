// 对话框与新控件 —— the app-shell additions, shown together.
//
// This page demonstrates the widgets that make a real application rather than a
// display board: modal dialogs, a tabbed container, an on-screen numeric keypad
// for touchscreens, a right-click menu, and the level bargraph.  Every result
// lands in the status line at the bottom, so the whole page is operable with
// nothing wired to a real process.
#include <cmath>
#include <string>

#include "AppState.hpp"  // showcase::Ticker
#include "Pages.hpp"
#include "ShowcaseWindow.hpp"
#include "geeyoou/hmi/Bargraph.hpp"
#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/ContextMenu.hpp"
#include "geeyoou/widget/Dialog.hpp"
#include "geeyoou/widget/GroupBox.hpp"
#include "geeyoou/widget/Label.hpp"
#include "geeyoou/widget/NumericKeypad.hpp"
#include "geeyoou/widget/PushButton.hpp"
#include "geeyoou/widget/TabView.hpp"
#include "i18n/I18n.hpp"

namespace showcase {

using namespace geeyoou;

namespace {

// A panel that opens a context menu on right-click, so the demo has somewhere
// to right-click.  Owns its ContextMenu, so the popup subscription dies with
// the panel (see ContextMenu's lifetime note).
class RightClickArea : public Widget {
 public:
  std::function<void(const std::string&)> onPick;

  RightClickArea() {
    menu_.setItems({
        MenuItem(tr("确认报警"), "ack", Icon::Check),
        MenuItem(tr("屏蔽此点"), "shelve", Icon::EyeOff),
        MenuItem::sep(),
        MenuItem(tr("查看历史"), "history", Icon::Copy),
        MenuItem(tr("删除记录"), "delete", Icon::Trash),
    });
    menu_.triggered.connect(
        [this](const std::string& id) { if (onPick) onPick(id); });
  }

 protected:
  void onPaint(Painter& p, const Rect&) override {
    const Theme& t = Theme::current();
    const Rect r = localRect();
    p.fillRoundRect(r, t.radius, t.field);
    p.strokeRoundRect(r.deflated(0.5f), t.radius, t.panelBorder, 1.0f);
    p.drawText({r.center().x, r.center().y}, tr("在这块区域点右键"), t.fontBody,
               t.textDim, HAlign::Center, VAlign::Middle);
  }

  void onMouse(const MouseEvent& e) override {
    if (e.action == MouseAction::Press && e.button == MouseButton::Right) {
      menu_.popupAt(window(), e.windowPos);
      e.accept();
    }
  }

 private:
  ContextMenu menu_;
};

}  // namespace

Size buildDialogsPage(Widget* content, ShowcaseWindow& win) {
  // Status line -- every action reports here.
  auto* status = content->add<Label>();
  status->setGeometry({0, 604, 1040, 24});
  status->addStyleClass("caption");
  status->setPixelSize(12.0f);
  status->setText(tr("状态：就绪 · 试试下面的对话框、键盘、右键菜单"));
  auto say = [status](const std::string& s) { status->setText(s); };

  // ---------------------------------------- 标签页（内含棒图）---
  auto* gTabs = content->add<GroupBox>();
  gTabs->setGeometry({0, 0, 520, 320});
  gTabs->setTitle(tr("标签页 · 每页一组棒图"));

  auto* tabs = gTabs->add<TabView>();
  tabs->setGeometry({14, 40, 492, 264});

  struct BarSpec { const char* title; const char* unit; double lo, hi, warn, alarm; };
  const BarSpec kBars[] = {
      {"进料流量", "m³/h", 0, 100, 75, 90},
      {"釜内温度", "°C", 0, 200, 150, 180},
      {"系统压力", "MPa", 0, 10, 7, 8.5},
  };
  const char* kTabNames[] = {"1# 反应釜", "2# 反应釜", "3# 罐区"};
  Bargraph* bars[3][3] = {};
  for (int page = 0; page < 3; ++page) {
    Widget* pg = tabs->addTab(tr(kTabNames[page]));
    for (int i = 0; i < 3; ++i) {
      auto* b = pg->add<Bargraph>();
      b->setGeometry({16.0f + float(i) * 156.0f, 56.0f, 146.0f, 150.0f});
      b->setRange(kBars[i].lo, kBars[i].hi);
      b->setBands(kBars[i].warn, kBars[i].alarm);
      b->setTitle(tr(kBars[i].title));
      b->setUnit(kBars[i].unit);
      bars[page][i] = b;
    }
  }

  // A ticker feeds the bargraphs a sine wave, so the colours move through their
  // bands on their own -- the same animation clock every live page uses.
  auto* ticker = content->add<Ticker>();
  ticker->setGeometry({0, 0, 0, 0});
  ticker->divisor = 3;
  auto phase = std::make_shared<double>(0.0);
  ticker->onTick = [bars, phase] {
    *phase += 0.05;
    for (int page = 0; page < 3; ++page) {
      const double d = double(page) * 0.7;
      bars[page][0]->setValue(50.0 + 30.0 * std::sin(*phase * 0.7 + d));
      bars[page][1]->setValue(120.0 + 55.0 * std::sin(*phase * 0.23 + 1.1 + d));
      bars[page][2]->setValue(5.0 + 3.4 * std::sin(*phase * 0.41 + 2.3 + d));
    }
  };

  // ------------------------------------------------- 对话框 ---
  auto* gDlg = content->add<GroupBox>();
  gDlg->setGeometry({536, 0, 504, 320});
  gDlg->setTitle(tr("对话框 · 模态遮罩"));

  auto* bMsg = gDlg->add<PushButton>();
  bMsg->setGeometry({16, 52, 220, 40});
  bMsg->setText(tr("消息框"));
  bMsg->clicked.connect([&win, say] {
    messageBox(&win, tr("提示"), tr("配方已下发到 2# 反应釜。"),
               {tr("知道了")},
               [say](int) { say(tr("消息框：已确认")); });
  });

  auto* bConfirm = gDlg->add<PushButton>();
  bConfirm->setGeometry({252, 52, 220, 40});
  bConfirm->setText(tr("确认框（危险操作）"));
  bConfirm->setVariant(ButtonVariant::Danger);
  bConfirm->clicked.connect([&win, say] {
    confirmBox(&win, tr("确认操作"),
               tr("确定要停止进料泵 P-101 吗？此操作会中断当前批次。"),
               [say] { say(tr("确认框：操作员确认了停泵")); },
               tr("停止"), tr("取消"), /*danger=*/true);
  });

  auto* bKeypad = gDlg->add<PushButton>();
  bKeypad->setGeometry({16, 104, 456, 40});
  bKeypad->setText(tr("数字键盘设定目标温度（触摸屏）"));
  bKeypad->setVariant(ButtonVariant::Primary);
  bKeypad->clicked.connect([&win, say] {
    numericInput(&win, tr("目标温度"), 165.0,
                 [say](double v) {
                   char buf[64];
                   std::snprintf(buf, sizeof(buf),
                                 tr("已设定目标温度 = %.1f °C").c_str(), v);
                   say(buf);
                 },
                 "°C");
  });

  auto* hint = gDlg->add<Label>();
  hint->setGeometry({16, 160, 456, 130});
  hint->addStyleClass("caption");
  hint->setPixelSize(12.0f);
  hint->setWordWrap(true);
  hint->setText(tr(
      "对话框是全窗口遮罩 + 居中面板：它靠几何实现模态，背后点不到。\n\n"
      "触摸屏没有物理键盘，改设定值必须弹屏上数字键盘——工控现场这是刚需。"));

  // ------------------------------------------------- 右键菜单 ---
  auto* gMenu = content->add<GroupBox>();
  gMenu->setGeometry({0, 336, 1040, 250});
  gMenu->setTitle(tr("右键上下文菜单"));

  auto* area = gMenu->add<RightClickArea>();
  area->setGeometry({16, 52, 1008, 180});
  area->onPick = [say](const std::string& id) {
    say(tr("右键菜单 → ") + id);
  };

  return {1040.0f, 628.0f};
}

}  // namespace showcase
