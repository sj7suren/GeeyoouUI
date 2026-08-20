// 输入与按钮 —— text-entry family plus the button variants.
// Also the manual IME test bed: switch to a Chinese input method and type into
// any field; the candidate window should follow the caret.
//
// LAID OUT BY THE ENGINE (R2/T-11): no coordinates, and no design size typed in
// at the bottom.  The button strip in particular used to be
// `14 + i * 102` -- six buttons on a grid that only fits captions of the length
// they happen to have today.  It is now a row of six items that ask for the
// width of their own labels.
#include <string>

#include "Pages.hpp"
#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/BoxLayout.hpp"
#include "geeyoou/widget/GridLayout.hpp"
#include "geeyoou/widget/GroupBox.hpp"
#include "geeyoou/widget/IconButton.hpp"
#include "geeyoou/widget/Label.hpp"
#include "geeyoou/widget/LineEdit.hpp"
#include "geeyoou/widget/PasswordEdit.hpp"
#include "geeyoou/widget/PushButton.hpp"
#include "geeyoou/widget/SearchBox.hpp"
#include "geeyoou/widget/Separator.hpp"
#include "geeyoou/widget/TextArea.hpp"
#include "i18n/I18n.hpp"

namespace showcase {

using namespace geeyoou;

namespace {
constexpr float kBandGap = 16.0f;
constexpr float kPanelGap = 20.0f;
constexpr float kItemGap = 10.0f;
// A caption sits closer to the field it names than that pair sits to the next
// one: proximity is what groups them, so the two gaps must differ.
constexpr float kCaptionGap = 4.0f;

BoxLayout* stack(Widget* host, float spacing) {
  auto* b = host->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
  b->setSpacing(spacing);
  return b;
}

BoxLayout* line(Widget* host, float spacing) {
  auto* b = host->setLayout<BoxLayout>(BoxLayout::Orientation::Horizontal);
  b->setSpacing(spacing);
  return b;
}

Widget* band(Widget* parent, BoxLayout* into, std::uint16_t stretch = 0) {
  Widget* w = parent->add<Widget>();
  into->addWidget(w, stretch);
  return w;
}

// A caption above the control it names.  The spacing between the two is set on
// the box by the caller (kCaptionGap), so this only makes the label.
Label* caption(Widget* parent, BoxLayout* into, std::string s) {
  auto* l = parent->add<Label>();
  l->setText(s);
  l->addStyleClass("caption");
  l->setPixelSize(11.0f);
  l->setAlign(HAlign::Left, VAlign::Middle);
  into->addWidget(l);
  return l;
}
}  // namespace

Size buildInputsPage(Widget* content) {
  BoxLayout* page = stack(content, kBandGap);

  Widget* upper = band(content, page);
  BoxLayout* upperRow = line(upper, kPanelGap);
  Widget* lower = band(content, page);
  BoxLayout* lowerRow = line(lower, kPanelGap);

  auto* status = content->add<Label>();
  status->addStyleClass("caption");
  status->setPixelSize(12.0f);
  status->setText(tr("状态：就绪 · Ctrl+A/C/V 可用，可直接输入中文"));
  auto say = [status](const std::string& s) { status->setText(tr("状态：") + s); };

  // ---------------- 文本输入 ----------------
  auto* gText = upper->add<GroupBox>();
  gText->setTitle(tr("文本输入"));
  upperRow->addWidget(gText, 1);
  BoxLayout* text = stack(gText, kCaptionGap);

  caption(gText, text, tr("普通输入框"));
  auto* edName = gText->add<LineEdit>();
  edName->setPlaceholder(tr("请输入配方名称"));
  text->addWidget(edName);
  text->addSpacing(kItemGap);

  caption(gText, text, tr("带清除按钮 + 长度上限 12 字"));
  auto* edLimited = gText->add<LineEdit>();
  edLimited->setPlaceholder(tr("最多 12 个字符（中文也按字算）"));
  edLimited->setClearButtonEnabled(true);
  edLimited->setMaxLength(12);
  text->addWidget(edLimited);
  text->addSpacing(kItemGap);

  caption(gText, text, tr("搜索框"));
  auto* edSearch = gText->add<SearchBox>();
  edSearch->setPlaceholder(tr("搜索位号 / 报警内容"));
  text->addWidget(edSearch);
  text->addSpacing(kItemGap);

  // Two labelled fields side by side: each column is a caption stacked over its
  // field, so a longer caption grows its own column instead of colliding with
  // the neighbour's.
  Widget* passRow = band(gText, text);
  BoxLayout* passRowL = line(passRow, kItemGap);

  Widget* passCol1 = band(passRow, passRowL, 1);
  BoxLayout* passCol1L = stack(passCol1, kCaptionGap);
  caption(passCol1, passCol1L, tr("密码框（无眼睛）"));
  auto* edPass1 = passCol1->add<PasswordEdit>();
  edPass1->setPlaceholder(tr("操作员密码"));
  edPass1->setText("secret123");
  passCol1L->addWidget(edPass1);

  Widget* passCol2 = band(passRow, passRowL, 1);
  BoxLayout* passCol2L = stack(passCol2, kCaptionGap);
  caption(passCol2, passCol2L, tr("密码框（可切换可见）"));
  auto* edPass2 = passCol2->add<PasswordEdit>();
  edPass2->setPlaceholder(tr("工程师密码"));
  edPass2->setRevealEnabled(true);
  edPass2->setText("engineer");
  passCol2L->addWidget(edPass2);
  text->addSpacing(kItemGap);

  caption(gText, text, tr("校验失败态 / 只读态"));
  Widget* stateRow = band(gText, text);
  BoxLayout* stateRowL = line(stateRow, kItemGap);
  auto* edInvalid = stateRow->add<LineEdit>();
  edInvalid->setText("192.168.1.999");
  edInvalid->setInvalid(true);
  stateRowL->addWidget(edInvalid, 1);

  auto* edReadOnly = stateRow->add<LineEdit>();
  edReadOnly->setText(tr("PLC-01 (只读)"));
  edReadOnly->setReadOnly(true);
  stateRowL->addWidget(edReadOnly, 1);
  text->addStretch();

  // ---------------- 多行文本 ----------------
  auto* gArea = upper->add<GroupBox>();
  gArea->setTitle(tr("多行文本框"));
  upperRow->addWidget(gArea, 1);
  BoxLayout* area = stack(gArea, kCaptionGap);

  caption(gArea, area, tr("软换行 · 上下键跨行 · 滚轮滚动 · Ctrl+A 全选"));
  auto* edArea = gArea->add<TextArea>();
  edArea->setText(
      tr("交接班记录：\n"
      "1. 08:20 进料泵 P-101 启动，流量稳定在 52 m³/h。\n"
      "2. 09:05 釜内温度到达设定值 165 °C，转入保温阶段。\n"
      "3. 10:30 泄压阀 V-303 手动排空一次，压力由 8.2 降至 5.1 MPa。\n"
      "4. 11:00 巡检未发现异常，交接完毕。"));
  // The one item on this page that should absorb spare height: a shift note is
  // as long as the shift was.
  area->addWidget(edArea, 1);
  area->addSpacing(kItemGap);

  caption(gArea, area, tr("占位符 / 只读"));
  Widget* areaRow = band(gArea, area);
  BoxLayout* areaRowL = line(areaRow, kItemGap);
  auto* areaEmpty = areaRow->add<TextArea>();
  areaEmpty->setPlaceholder(tr("在此填写备注…"));
  areaRowL->addWidget(areaEmpty, 1);

  auto* areaRO = areaRow->add<TextArea>();
  areaRO->setText(tr("此栏由系统写入，不可编辑。"));
  areaRO->setReadOnly(true);
  areaRowL->addWidget(areaRO, 1);

  // ---------------- 按钮变体 ----------------
  auto* gBtn = lower->add<GroupBox>();
  gBtn->setTitle(tr("按钮变体"));
  lowerRow->addWidget(gBtn, 2);
  BoxLayout* btn = stack(gBtn, kItemGap);

  struct VariantSpec { std::string label; ButtonVariant v; };
  const VariantSpec kVariants[] = {
      {"Default", ButtonVariant::Default}, {"Primary", ButtonVariant::Primary},
      {"Success", ButtonVariant::Success}, {"Warning", ButtonVariant::Warning},
      {"Danger",  ButtonVariant::Danger},  {"Ghost",   ButtonVariant::Ghost},
  };
  Widget* variantRow = band(gBtn, btn);
  BoxLayout* variantRowL = line(variantRow, kItemGap);
  for (const VariantSpec& spec : kVariants) {
    auto* b = variantRow->add<PushButton>();
    b->setText(spec.label);
    b->setVariant(spec.v);
    variantRowL->addWidget(b, 1);
  }

  caption(gBtn, btn, tr("带图标 · 禁用 · loading"));
  Widget* actionRow = band(gBtn, btn);
  BoxLayout* actionRowL = line(actionRow, kItemGap);

  auto* bSave = actionRow->add<PushButton>();
  bSave->setText(tr("保存"));
  bSave->setIcon(Icon::Save);
  bSave->setVariant(ButtonVariant::Primary);
  actionRowL->addWidget(bSave);

  auto* bDelete = actionRow->add<PushButton>();
  bDelete->setText(tr("删除"));
  bDelete->setIcon(Icon::Trash);
  bDelete->setVariant(ButtonVariant::Danger);
  actionRowL->addWidget(bDelete);

  auto* bDisabled = actionRow->add<PushButton>();
  bDisabled->setText(tr("已禁用"));
  bDisabled->setIcon(Icon::Lock);
  bDisabled->setVariant(ButtonVariant::Success);
  bDisabled->setEnabled(false);
  actionRowL->addWidget(bDisabled);

  auto* bLoading = actionRow->add<PushButton>();
  bLoading->setText(tr("下发参数"));
  bLoading->setIcon(Icon::Upload);
  bLoading->setVariant(ButtonVariant::Primary);
  bLoading->setLoadingText(tr("下发中…"));
  actionRowL->addWidget(bLoading);

  auto* bLatch = actionRow->add<PushButton>();
  bLatch->setText(tr("锁定"));
  bLatch->setVariant(ButtonVariant::Warning);
  bLatch->setCheckable(true);
  actionRowL->addWidget(bLatch);
  actionRowL->addStretch();

  auto* sep = gBtn->add<Separator>();
  btn->addWidget(sep);
  btn->addStretch();

  // ---------------- 图标按钮 ----------------
  auto* gIcon = lower->add<GroupBox>();
  gIcon->setTitle(tr("图标按钮"));
  lowerRow->addWidget(gIcon, 1);
  BoxLayout* icons = stack(gIcon, kItemGap);

  const Icon kIcons[] = {Icon::Play,     Icon::Pause,  Icon::Stop,  Icon::Refresh,
                         Icon::Settings, Icon::Filter, Icon::Copy,  Icon::Edit,
                         Icon::Download, Icon::Menu,   Icon::Check, Icon::Warning};
  // Two rows of six, expressed as two rows of six -- not as `i % 6` and
  // `i / 6` against a 48px pitch.
  auto* iconGrid = gIcon->add<Widget>();
  icons->addWidget(iconGrid);
  auto* iconGridL = iconGrid->setLayout<GridLayout>();
  iconGridL->setSpacing(6.0f);
  for (int i = 0; i < 12; ++i) {
    auto* b = iconGrid->add<IconButton>();
    b->setIcon(kIcons[i]);
    iconGridL->addWidget(b, i / 6, i % 6);
  }

  Widget* circleRow = band(gIcon, icons);
  BoxLayout* circleRowL = line(circleRow, 6.0f);

  auto* bCircle = circleRow->add<IconButton>();
  bCircle->setIcon(Icon::Plus);
  bCircle->setVariant(ButtonVariant::Primary);
  bCircle->setCircular(true);
  circleRowL->addWidget(bCircle);

  auto* bCircleDanger = circleRow->add<IconButton>();
  bCircleDanger->setIcon(Icon::Close);
  bCircleDanger->setVariant(ButtonVariant::Danger);
  bCircleDanger->setCircular(true);
  circleRowL->addWidget(bCircleDanger);

  auto* bSpin = circleRow->add<IconButton>();
  bSpin->setIcon(Icon::Refresh);
  bSpin->setVariant(ButtonVariant::Success);
  bSpin->setCircular(true);
  bSpin->setLoading(true);  // permanently spinning, to show the animation
  circleRowL->addWidget(bSpin);
  circleRowL->addStretch();
  icons->addStretch();

  // ---------------- 状态行 ----------------
  page->addWidget(status);

  // ---------------- signals ----------------
  edName->textChanged.connect([say](const std::string& s) {
    say(tr("配方名称 = \"") + s + "\"");
  });
  edLimited->textChanged.connect([say](const std::string& s) {
    say(tr("受限字段 = \"") + s + "\"");
  });
  edSearch->searchRequested.connect([say](const std::string& s) {
    say(tr("执行搜索：\"") + s + "\"");
  });
  edArea->textChanged.connect([say, edArea](const std::string&) {
    say(tr("交接班记录已修改，共 ") + std::to_string(edArea->lineCount()) + tr(" 显示行"));
  });
  bSave->clicked.connect([say] { say(tr("已保存")); });
  bDelete->clicked.connect([say] { say(tr("已删除")); });
  bLatch->toggled.connect([say](bool on) { say(on ? tr("画面已锁定") : tr("画面已解锁")); });
  bLoading->clicked.connect([bLoading, say] {
    bLoading->setLoading(true);
    say(tr("参数下发中…（点击“锁定”可解除）"));
  });
  bLatch->clicked.connect([bLoading] { bLoading->setLoading(false); });

  return content->sizeHint().preferred;
}

}  // namespace showcase
