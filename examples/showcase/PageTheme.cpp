// 主题与皮肤 —— the style layer, driven live.
//
// Three knobs, in increasing order of power:
//   1. 皮肤    -- swap the whole registered Theme + style sheet
//   2. 主题色  -- recolour the active theme around one brand colour
//   3. 样式表  -- QSS-like selectors, edited and applied without a rebuild
//
// The point of putting all three on one page is that they compose: pick a skin,
// tint it, then override the two controls the customer complained about.
#include <string>
#include <vector>

#include "Pages.hpp"
#include "ShowcaseWindow.hpp"
#include "geeyoou/render/Skin.hpp"
#include "geeyoou/render/StyleSheet.hpp"
#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/CheckBox.hpp"
#include "geeyoou/widget/ComboBox.hpp"
#include "geeyoou/widget/GroupBox.hpp"
#include "geeyoou/widget/Label.hpp"
#include "geeyoou/widget/LineEdit.hpp"
#include "geeyoou/widget/ProgressBar.hpp"
#include "geeyoou/widget/PushButton.hpp"
#include "geeyoou/widget/Slider.hpp"
#include "geeyoou/widget/TextArea.hpp"
#include "geeyoou/widget/ToggleSwitch.hpp"

namespace showcase {

using namespace geeyoou;

namespace {

Label* caption(Widget* parent, float x, float y, float w, const char* s) {
  auto* l = parent->add<Label>();
  l->setGeometry({x, y, w, 20});
  l->setText(s);
  // No setColor(): the label follows the sheet's `.caption` rule instead, so it
  // re-colours itself when the skin changes.  A hard-coded colour here is
  // exactly the bug that makes a light skin unreadable.
  l->addStyleClass("caption");
  l->setPixelSize(11.0f);
  l->setAlign(HAlign::Left, VAlign::Middle);
  return l;
}

struct NamedColor {
  const char* name;
  Color color;
};

const char* kSampleQss =
    "/* 选择器示例：改完点「应用样式表」 */\n"
    "\n"
    "/* 按 objectName 选中单个控件 */\n"
    "#emergencyStop {\n"
    "  accent: #FF3B30;\n"
    "  border-radius: 2;\n"
    "  border-width: 2;\n"
    "}\n"
    "\n"
    "/* 按 style class 选中一组控件 */\n"
    ".pill { border-radius: 16; }\n"
    "\n"
    "/* 按类型 + 状态；@token 引用当前主题 */\n"
    "PushButton:hover { border-color: @accent; }\n"
    "\n"
    "/* 后代选择器：只影响这个分组里的进度条 */\n"
    "#styleDemo ProgressBar { accent: @warn; }\n";

}  // namespace

Size buildThemePage(Widget* content, ShowcaseWindow& win) {
  auto* status = content->add<Label>();
  status->setGeometry({0, 628, 960, 24});
  status->addStyleClass("caption");
  status->setPixelSize(12.0f);
  status->setText("当前皮肤：" + skins().currentName());
  auto say = [status](const std::string& s) { status->setText(s); };

  // ------------------------------------------------------------------ 皮肤 ---
  auto* gSkin = content->add<GroupBox>();
  gSkin->setGeometry({0, 0, 300, 250});
  gSkin->setTitle("皮肤（注册表）");

  caption(gSkin, 14, 42, 272, "已注册皮肤");
  auto* cSkin = gSkin->add<ComboBox>();
  cSkin->setGeometry({14, 64, 272, 32});
  {
    std::vector<SelectItem> items;
    for (const Skin* s : skins().all()) items.push_back(SelectItem(s->title, s->name));
    cSkin->setItems(std::move(items));
  }
  cSkin->setCurrentValue(skins().currentName());
  cSkin->currentValueChanged.connect([say](const std::string& name) {
    skins().apply(name);
    say("已切换皮肤：" + name + "（整棵控件树立即重绘）");
  });

  auto* about = gSkin->add<Label>();
  about->setGeometry({14, 106, 272, 130});
  about->addStyleClass("caption");
  about->setPixelSize(12.0f);
  about->setWordWrap(true);
  // Top-anchored: a wrapped label defaults to VAlign::Middle, which centres the
  // whole block and lets it spill out of both ends of its box.
  about->setAlign(HAlign::Left, VAlign::Top);
  about->setText(
      "一个皮肤 = Theme（token 结构体）+ 一段样式表。\n\n"
      "库里所有控件每次绘制都从 Theme::current() 取色，"
      "所以换皮肤不需要逐控件通知，整窗重绘一次即可。");

  // ---------------------------------------------------------------- 主题色 ---
  auto* gAccent = content->add<GroupBox>();
  gAccent->setGeometry({320, 0, 300, 250});
  gAccent->setTitle("主题色");

  static const std::vector<NamedColor> kAccents = {
      {"仪表蓝", Color::rgb(0x2F, 0xA8, 0xFF)},
      {"品牌青", Color::rgb(0x12, 0xC2, 0xC2)},
      {"安全绿", Color::rgb(0x3E, 0xD1, 0x7A)},
      {"工程橙", Color::rgb(0xFF, 0x8A, 0x1E)},
      {"警示红", Color::rgb(0xFF, 0x4D, 0x5E)},
      {"品牌紫", Color::rgb(0x8B, 0x5C, 0xF6)},
  };
  caption(gAccent, 14, 42, 272, "一个颜色带动整套配色");
  auto* cAccent = gAccent->add<ComboBox>();
  cAccent->setGeometry({14, 64, 272, 32});
  {
    std::vector<SelectItem> items;
    for (const NamedColor& c : kAccents) items.push_back(SelectItem(c.name));
    cAccent->setItems(std::move(items));
  }
  cAccent->setCurrentIndex(0);
  cAccent->currentIndexChanged.connect([say](int i) {
    if (i < 0 || i >= int(kAccents.size())) return;
    skins().setAccent(kAccents[std::size_t(i)].color);
    say(std::string("主题色 = ") + kAccents[std::size_t(i)].name +
        "（accent / primary / focusRing / 选区 一起走）");
  });

  auto* accentNote = gAccent->add<Label>();
  accentNote->setGeometry({14, 106, 272, 130});
  accentNote->addStyleClass("caption");
  accentNote->setPixelSize(12.0f);
  accentNote->setWordWrap(true);
  accentNote->setAlign(HAlign::Left, VAlign::Top);
  accentNote->setText(
      "只有派生自品牌色的 token 会动：accent / primary / "
      "focusRing / 选区底色，以及填充按钮上的字色（按亮度自动选黑或白）。\n\n"
      "ok / warn / alarm 不动 —— 报警必须永远是报警色。");

  // ---------------------------------------------------------------- 样式表 ---
  auto* gQss = content->add<GroupBox>();
  gQss->setGeometry({640, 0, 320, 250});
  gQss->setTitle("样式表（类 QSS）");

  auto* editor = gQss->add<TextArea>();
  editor->setGeometry({14, 42, 292, 158});
  editor->setText(kSampleQss);

  auto* apply = gQss->add<PushButton>();
  apply->setGeometry({14, 208, 140, 30});
  apply->setText("应用样式表");
  apply->setVariant(ButtonVariant::Primary);

  auto* reset = gQss->add<PushButton>();
  reset->setGeometry({166, 208, 140, 30});
  reset->setText("清空");

  apply->clicked.connect([editor, say, &win] {
    // Goes through the window so the app's base rules stay underneath it.
    win.setUserStyleSheet(editor->text());
    const auto& errs = activeStyleSheet().errors();
    if (errs.empty()) {
      say("样式表已应用：" + std::to_string(activeStyleSheet().ruleCount()) +
          " 条规则生效");
    } else {
      // Parse errors never throw and never blank the sheet: whatever parsed
      // cleanly is already live, which matters on a running plant display.
      say("样式表有 " + std::to_string(errs.size()) + " 处问题：" + errs.front());
    }
  });
  reset->clicked.connect([editor, say, &win] {
    editor->setText("");
    win.setUserStyleSheet("");
    say("样式表已清空，只剩皮肤自带规则与 Theme token");
  });

  // ------------------------------------------------------------ 受控的样本 ---
  auto* gDemo = content->add<GroupBox>();
  gDemo->setGeometry({0, 270, 620, 320});
  gDemo->setTitle("样本控件（上面的规则作用在这里）");
  // Gives the sheet an ancestor to hang a descendant selector off.
  gDemo->setObjectName("styleDemo");

  caption(gDemo, 14, 42, 156, "#emergencyStop");
  auto* stop = gDemo->add<PushButton>();
  stop->setGeometry({14, 64, 150, 38});
  stop->setText("紧急停车");
  stop->setVariant(ButtonVariant::Danger);
  stop->setObjectName("emergencyStop");

  caption(gDemo, 180, 42, 356, ".pill —— 按 style class 命中");
  auto* pill1 = gDemo->add<PushButton>();
  pill1->setGeometry({180, 64, 110, 38});
  pill1->setText("启动");
  pill1->setVariant(ButtonVariant::Success);
  pill1->addStyleClass("pill");

  auto* pill2 = gDemo->add<PushButton>();
  pill2->setGeometry({300, 64, 110, 38});
  pill2->setText("停止");
  pill2->addStyleClass("pill");

  auto* plain = gDemo->add<PushButton>();
  plain->setGeometry({426, 64, 110, 38});
  plain->setText("未加类");

  caption(gDemo, 14, 116, 280, "#styleDemo ProgressBar —— 后代选择器");
  auto* bar = gDemo->add<ProgressBar>();
  bar->setGeometry({14, 138, 400, 18});
  bar->setValue(64);

  caption(gDemo, 14, 168, 280, "跟随 accent 的控件");
  auto* sw = gDemo->add<ToggleSwitch>();
  sw->setGeometry({14, 192, 120, 30});
  sw->setText("进料泵");
  sw->setChecked(true);

  auto* cb = gDemo->add<CheckBox>();
  cb->setGeometry({150, 192, 130, 30});
  cb->setText("安全联锁");
  cb->setChecked(true);

  auto* sl = gDemo->add<Slider>();
  sl->setGeometry({296, 192, 240, 30});
  sl->setRange(0, 100);
  sl->setValue(42);

  auto* field = gDemo->add<LineEdit>();
  field->setGeometry({14, 232, 260, 32});
  field->setPlaceholder("LineEdit —— background / border-radius");

  auto* ro = gDemo->add<LineEdit>();
  ro->setGeometry({286, 232, 250, 32});
  ro->setText("只读字段 :read-only");
  ro->setReadOnly(true);

  // ------------------------------------------------------------------ 速查 ---
  auto* gRef = content->add<GroupBox>();
  gRef->setGeometry({640, 270, 320, 344});
  gRef->setTitle("语法速查");

  auto* ref = gRef->add<Label>();
  ref->setGeometry({14, 40, 292, 292});
  ref->addStyleClass("caption");
  ref->setPixelSize(11.0f);
  ref->setAlign(HAlign::Left, VAlign::Top);
  ref->setText(
      "选择器\n"
      "  *   PushButton   .danger   #pump1\n"
      "  GroupBox Label（后代）  A, B（分组）\n"
      "状态\n"
      "  :hover :pressed :checked :focus\n"
      "  :disabled :read-only :invalid :open\n"
      "属性\n"
      "  color  background  border-color\n"
      "  border-width  border-radius\n"
      "  font-size  accent  icon-color  padding\n"
      "颜色\n"
      "  #RGB  #RRGGBB  #AARRGGBB\n"
      "  rgb() rgba() transparent\n"
      "  @accent @panel @text …（跟随当前主题）");

  // Header controls belong to the window, not to this page -- but they are the
  // most visible proof that a skin change reaches the chrome too.
  win.headerAction.connect([say](const std::string& s) { say(s); });

  return {960, 662};
}

}  // namespace showcase
