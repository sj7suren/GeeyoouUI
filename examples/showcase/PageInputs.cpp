// 输入与按钮 —— text-entry family plus the button variants.
// Also the manual IME test bed: switch to a Chinese input method and type into
// any field; the candidate window should follow the caret.
#include <string>

#include "Pages.hpp"
#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/GroupBox.hpp"
#include "geeyoou/widget/IconButton.hpp"
#include "geeyoou/widget/Label.hpp"
#include "geeyoou/widget/LineEdit.hpp"
#include "geeyoou/widget/PasswordEdit.hpp"
#include "geeyoou/widget/PushButton.hpp"
#include "geeyoou/widget/SearchBox.hpp"
#include "geeyoou/widget/Separator.hpp"
#include "geeyoou/widget/TextArea.hpp"

namespace showcase {

using namespace geeyoou;

namespace {
Label* caption(Widget* parent, float x, float y, float w, const char* s) {
  auto* l = parent->add<Label>();
  l->setGeometry({x, y, w, 20});
  l->setText(s);
  l->addStyleClass("caption");
  l->setPixelSize(11.0f);
  l->setAlign(HAlign::Left, VAlign::Middle);
  return l;
}
}  // namespace

Size buildInputsPage(Widget* content) {

  auto* status = content->add<Label>();
  status->setGeometry({0, 668, 952, 26});
  status->addStyleClass("caption");
  status->setPixelSize(12.0f);
  status->setText("状态：就绪 · Ctrl+A/C/V 可用，可直接输入中文");
  auto say = [status](const std::string& s) { status->setText("状态：" + s); };

  // ---------------- 文本输入 ----------------
  auto* gText = content->add<GroupBox>();
  gText->setGeometry({0, 0, 470, 380});
  gText->setTitle("文本输入");

  caption(gText, 14, 44, 200, "普通输入框");
  auto* edName = gText->add<LineEdit>();
  edName->setGeometry({14, 66, 442, 36});
  edName->setPlaceholder("请输入配方名称");

  caption(gText, 14, 112, 260, "带清除按钮 + 长度上限 12 字");
  auto* edLimited = gText->add<LineEdit>();
  edLimited->setGeometry({14, 134, 442, 36});
  edLimited->setPlaceholder("最多 12 个字符（中文也按字算）");
  edLimited->setClearButtonEnabled(true);
  edLimited->setMaxLength(12);

  caption(gText, 14, 180, 200, "搜索框");
  auto* edSearch = gText->add<SearchBox>();
  edSearch->setGeometry({14, 202, 442, 36});
  edSearch->setPlaceholder("搜索位号 / 报警内容");

  caption(gText, 14, 248, 200, "密码框（无眼睛）");
  auto* edPass1 = gText->add<PasswordEdit>();
  edPass1->setGeometry({14, 270, 214, 36});
  edPass1->setPlaceholder("操作员密码");
  edPass1->setText("secret123");

  caption(gText, 242, 248, 220, "密码框（可切换可见）");
  auto* edPass2 = gText->add<PasswordEdit>();
  edPass2->setGeometry({242, 270, 214, 36});
  edPass2->setPlaceholder("工程师密码");
  edPass2->setRevealEnabled(true);
  edPass2->setText("engineer");

  caption(gText, 14, 316, 260, "校验失败态 / 只读态");
  auto* edInvalid = gText->add<LineEdit>();
  edInvalid->setGeometry({14, 338, 214, 34});
  edInvalid->setText("192.168.1.999");
  edInvalid->setInvalid(true);

  auto* edReadOnly = gText->add<LineEdit>();
  edReadOnly->setGeometry({242, 338, 214, 34});
  edReadOnly->setText("PLC-01 (只读)");
  edReadOnly->setReadOnly(true);

  // ---------------- 多行文本 ----------------
  auto* gArea = content->add<GroupBox>();
  gArea->setGeometry({490, 0, 470, 380});
  gArea->setTitle("多行文本框");

  caption(gArea, 14, 44, 380, "软换行 · 上下键跨行 · 滚轮滚动 · Ctrl+A 全选");
  auto* area = gArea->add<TextArea>();
  area->setGeometry({14, 66, 442, 200});
  area->setText(
      "交接班记录：\n"
      "1. 08:20 进料泵 P-101 启动，流量稳定在 52 m³/h。\n"
      "2. 09:05 釜内温度到达设定值 165 °C，转入保温阶段。\n"
      "3. 10:30 泄压阀 V-303 手动排空一次，压力由 8.2 降至 5.1 MPa。\n"
      "4. 11:00 巡检未发现异常，交接完毕。");

  caption(gArea, 14, 276, 300, "占位符 / 只读");
  auto* areaEmpty = gArea->add<TextArea>();
  areaEmpty->setGeometry({14, 298, 216, 74});
  areaEmpty->setPlaceholder("在此填写备注…");

  auto* areaRO = gArea->add<TextArea>();
  areaRO->setGeometry({240, 298, 216, 74});
  areaRO->setText("此栏由系统写入，不可编辑。");
  areaRO->setReadOnly(true);

  // ---------------- 按钮变体 ----------------
  auto* gBtn = content->add<GroupBox>();
  gBtn->setGeometry({0, 396, 630, 210});
  gBtn->setTitle("按钮变体");

  struct VariantSpec { const char* label; ButtonVariant v; };
  const VariantSpec kVariants[] = {
      {"Default", ButtonVariant::Default}, {"Primary", ButtonVariant::Primary},
      {"Success", ButtonVariant::Success}, {"Warning", ButtonVariant::Warning},
      {"Danger",  ButtonVariant::Danger},  {"Ghost",   ButtonVariant::Ghost},
  };
  for (int i = 0; i < 6; ++i) {
    auto* b = gBtn->add<PushButton>();
    b->setGeometry({14.0f + float(i) * 102.0f, 46, 94, 36});
    b->setText(kVariants[i].label);
    b->setVariant(kVariants[i].v);
  }

  caption(gBtn, 14, 92, 300, "带图标 · 禁用 · loading");
  auto* bSave = gBtn->add<PushButton>();
  bSave->setGeometry({14, 114, 118, 38});
  bSave->setText("保存");
  bSave->setIcon(Icon::Save);
  bSave->setVariant(ButtonVariant::Primary);

  auto* bDelete = gBtn->add<PushButton>();
  bDelete->setGeometry({144, 114, 118, 38});
  bDelete->setText("删除");
  bDelete->setIcon(Icon::Trash);
  bDelete->setVariant(ButtonVariant::Danger);

  auto* bDisabled = gBtn->add<PushButton>();
  bDisabled->setGeometry({274, 114, 118, 38});
  bDisabled->setText("已禁用");
  bDisabled->setIcon(Icon::Lock);
  bDisabled->setVariant(ButtonVariant::Success);
  bDisabled->setEnabled(false);

  auto* bLoading = gBtn->add<PushButton>();
  bLoading->setGeometry({404, 114, 138, 38});
  bLoading->setText("下发参数");
  bLoading->setIcon(Icon::Upload);
  bLoading->setVariant(ButtonVariant::Primary);
  bLoading->setLoadingText("下发中…");

  auto* bLatch = gBtn->add<PushButton>();
  bLatch->setGeometry({554, 114, 62, 38});
  bLatch->setText("锁定");
  bLatch->setVariant(ButtonVariant::Warning);
  bLatch->setCheckable(true);

  auto* sep = gBtn->add<Separator>();
  sep->setGeometry({14, 164, 602, 1});

  // ---------------- 图标按钮 ----------------
  auto* gIcon = content->add<GroupBox>();
  gIcon->setGeometry({650, 396, 310, 210});
  gIcon->setTitle("图标按钮");

  const Icon kIcons[] = {Icon::Play,     Icon::Pause,  Icon::Stop,  Icon::Refresh,
                         Icon::Settings, Icon::Filter, Icon::Copy,  Icon::Edit,
                         Icon::Download, Icon::Menu,   Icon::Check, Icon::Warning};
  for (int i = 0; i < 12; ++i) {
    auto* b = gIcon->add<IconButton>();
    b->setGeometry({14.0f + float(i % 6) * 48.0f, 46.0f + float(i / 6) * 48.0f, 42, 42});
    b->setIcon(kIcons[i]);
  }

  auto* bCircle = gIcon->add<IconButton>();
  bCircle->setGeometry({14, 146, 44, 44});
  bCircle->setIcon(Icon::Plus);
  bCircle->setVariant(ButtonVariant::Primary);
  bCircle->setCircular(true);

  auto* bCircleDanger = gIcon->add<IconButton>();
  bCircleDanger->setGeometry({68, 146, 44, 44});
  bCircleDanger->setIcon(Icon::Close);
  bCircleDanger->setVariant(ButtonVariant::Danger);
  bCircleDanger->setCircular(true);

  auto* bSpin = gIcon->add<IconButton>();
  bSpin->setGeometry({122, 146, 44, 44});
  bSpin->setIcon(Icon::Refresh);
  bSpin->setVariant(ButtonVariant::Success);
  bSpin->setCircular(true);
  bSpin->setLoading(true);  // permanently spinning, to show the animation

  // ---------------- signals ----------------
  edName->textChanged.connect([say](const std::string& s) {
    say("配方名称 = \"" + s + "\"");
  });
  edLimited->textChanged.connect([say](const std::string& s) {
    say("受限字段 = \"" + s + "\"");
  });
  edSearch->searchRequested.connect([say](const std::string& s) {
    say("执行搜索：\"" + s + "\"");
  });
  area->textChanged.connect([say, area](const std::string&) {
    say("交接班记录已修改，共 " + std::to_string(area->lineCount()) + " 显示行");
  });
  bSave->clicked.connect([say] { say("已保存"); });
  bDelete->clicked.connect([say] { say("已删除"); });
  bLatch->toggled.connect([say](bool on) { say(on ? "画面已锁定" : "画面已解锁"); });
  bLoading->clicked.connect([bLoading, say] {
    bLoading->setLoading(true);
    say("参数下发中…（点击“锁定”可解除）");
  });
  bLatch->clicked.connect([bLoading] { bLoading->setLoading(false); });

  return {970.0f, 710.0f};
}

}  // namespace showcase
