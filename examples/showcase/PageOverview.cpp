// 概览 —— landing page: what this library is, what is in it, what is not.
#include <cstdio>
#include <string>

#include "Pages.hpp"
#include "geeyoou/hmi/StatusLed.hpp"
#include "geeyoou/render/Icon.hpp"
#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/GroupBox.hpp"
#include "geeyoou/widget/Label.hpp"
#include "geeyoou/widget/ProgressBar.hpp"
#include "i18n/I18n.hpp"

namespace showcase {

using namespace geeyoou;

namespace {

// A headline number with a caption and an icon -- the standard admin-console
// stat tile.  Written as a widget rather than three loose Labels so the whole
// tile repaints as one unit when its value changes.
class StatTile : public Widget {
 public:
  void set(Icon icon, std::string caption, std::string value, Color accent) {
    icon_ = icon;
    caption_ = std::move(caption);
    value_ = std::move(value);
    accent_ = accent;
    update();
  }
  void setValue(std::string v) {
    if (value_ == v) return;
    value_ = std::move(v);
    update();
  }

 protected:
  void onPaint(Painter& p, const Rect&) override {
    const Theme& t = Theme::current();
    const Rect r = localRect();
    p.fillRoundRect(r, t.radius, t.panel);
    p.strokeRoundRect(r.deflated(0.5f), t.radius, t.panelBorder, 1.0f);
    p.fillRoundRect({0.0f, 8.0f, 3.0f, r.height() - 16.0f}, 1.5f, accent_);

    p.fillRoundRect({r.right() - 54.0f, 16.0f, 38.0f, 38.0f}, 8.0f,
                    accent_.withAlpha(38));
    drawIcon(p, icon_, {r.right() - 54.0f, 16.0f, 38.0f, 38.0f}, accent_);

    p.drawText({18.0f, 20.0f}, caption_, t.fontSmall, t.textDim, HAlign::Left,
               VAlign::Top);
    p.drawText({18.0f, 38.0f}, value_, 22.0f, t.text, HAlign::Left, VAlign::Top);
  }

 private:
  Icon icon_ = Icon::None;
  std::string caption_;
  std::string value_;
  Color accent_;
};

Label* para(Widget* parent, float x, float y, float w, float h, std::string s,
            float size = 12.0f) {
  auto* l = parent->add<Label>();
  l->setGeometry({x, y, w, h});
  l->setText(std::move(s));
  l->addStyleClass("caption");
  l->setPixelSize(size);
  l->setAlign(HAlign::Left, VAlign::Top);
  l->setWordWrap(true);  // the text below has hard newlines AND long lines
  return l;
}

}  // namespace

Size buildOverviewPage(Widget* content, AppState& app) {
  const Theme& th = Theme::current();

  // ---------------- stat tiles ----------------
  struct TileSpec { Icon icon; std::string caption; const char* value; Color accent; };
  const TileSpec kTiles[] = {
      {Icon::Check, tr("控件总数"), "32", th.accent},
      {Icon::Settings, tr("代码行数"), "13.6k", th.ok},
      {Icon::Warning, tr("上行依赖"), "0", th.warn},
      {Icon::Refresh, tr("后端"), "Win32", th.primary},
  };
  StatTile* tiles[4];
  for (int i = 0; i < 4; ++i) {
    tiles[i] = content->add<StatTile>();
    tiles[i]->setGeometry({float(i) * 236.0f, 0, 220, 72});
    tiles[i]->set(kTiles[i].icon, kTiles[i].caption, kTiles[i].value, kTiles[i].accent);
  }

  // The first tile becomes a live readout, so the landing page proves the
  // acquisition thread is running before you visit any other page.
  auto* ticker = content->add<Ticker>();
  ticker->setGeometry({0, 0, 0, 0});
  ticker->divisor = 6;  // ~5 Hz
  StatTile* liveTile = tiles[0];
  ticker->onTick = [&app, liveTile] {
    char buf[48];
    std::snprintf(buf, sizeof(buf), "%.1f °C", app.hub.lastValue(app.chTemp));
    liveTile->setValue(buf);
  };
  tiles[0]->set(Icon::Warning, tr("釜内温度（实时）"), "--", th.accent);

  // ---------------- what this is ----------------
  auto* gAbout = content->add<GroupBox>();
  gAbout->setGeometry({0, 92, 604, 262});
  gAbout->setTitle(tr("GeeyoouUI 是什么"));

  para(gAbout, 14, 44, 576, 200,
       tr("面向工控 HMI / 上位机的跨平台 C++20 自绘控件库。\n\n"
       "· 自绘渲染（Blend2D），跨平台外观完全一致\n"
       "· 无 moc —— 信号槽用模板 + std::function，没有代码生成步骤\n"
       "· 脏矩形增量重绘 —— HMI 画面 90% 的像素是静止的\n"
       "· 热路径零分配 —— 实时数据走固定容量环形缓冲\n"
       "· 平台层是纯虚接口 —— 新后端只需给出一块像素缓冲\n\n"
       "左侧导航逐页浏览各控件族。所有页面共用同一个采集线程与 DataHub：\n"
       "「HMI 监控」和「运维控制台」看到的是同一份实时数据。"));

  // ---------------- layer map ----------------
  auto* gLayers = content->add<GroupBox>();
  gLayers->setGeometry({620, 92, 424, 262});
  gLayers->setTitle(tr("分层与规模"));

  struct LayerSpec { std::string name; int lines; Color color; };
  const LayerSpec kLayers[] = {
      {tr("widget  控件树 + 窗口层"), 8489, th.accent},
      {tr("render  绘制 / 主题 / 样式 / 图标"), 2681, th.warn},
      {tr("hmi     领域控件"), 980, th.ok},
      {tr("platform 移植边界"), 819, th.textDim},
      {tr("core    无依赖基础"), 660, th.primary},
  };
  for (int i = 0; i < 5; ++i) {
    auto* l = gLayers->add<Label>();
    l->setGeometry({14, 46.0f + float(i) * 40.0f, 230, 20});
    l->setText(kLayers[i].name);
    l->setPixelSize(12.0f);

    auto* bar = gLayers->add<ProgressBar>();
    bar->setGeometry({14, 66.0f + float(i) * 40.0f, 396, 12});
    bar->setRange(0, 9000);
    bar->setValue(kLayers[i].lines);
    bar->setBarColor(kLayers[i].color);
    bar->setTextVisible(false);

    auto* n = gLayers->add<Label>();
    n->setGeometry({250, 46.0f + float(i) * 40.0f, 160, 20});
    n->setText(std::to_string(kLayers[i].lines) + tr(" 行"));
    n->addStyleClass("caption");
    n->setPixelSize(11.0f);
    n->setAlign(HAlign::Right, VAlign::Middle);
  }

  // ---------------- not implemented ----------------
  auto* gTodo = content->add<GroupBox>();
  gTodo->setGeometry({0, 370, 1044, 180});
  gTodo->setTitle(tr("明确未实现（有意推迟，理由见 docs/architecture.md §4）"));

  struct TodoSpec { std::string text; StatusLed::State state; };
  const TodoSpec kTodo[] = {
      {tr("布局引擎 —— v1 用绝对坐标，符合固定分辨率组态画面习惯"), StatusLed::State::Off},
      {tr("IME 内联预编辑 —— 中文输入可用，但组合串由系统 IME 窗口绘制"), StatusLed::State::Warn},
      {tr("撤销/重做、双击选词 —— 文本控件无 undo 栈"), StatusLed::State::Off},
      {tr("无障碍 UIA —— 领域专用库，优先级低"), StatusLed::State::Off},
      {tr("X11 / Cocoa 后端 —— 接口已预留，未实现"), StatusLed::State::Warn},
  };
  for (int i = 0; i < 5; ++i) {
    auto* led = gTodo->add<StatusLed>();
    led->setGeometry({14.0f + float(i % 2) * 512.0f,
                      44.0f + float(i / 2) * 32.0f, 500, 26});
    led->setCaption(kTodo[i].text);
    led->setState(kTodo[i].state);
  }

  return {1052.0f, 566.0f};
}

}  // namespace showcase
