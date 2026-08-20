// 下拉选择 —— the whole dropdown family.
//
// The controls sit inside GroupBoxes AND inside the page's ScrollArea, which is
// the case that proves the popup escapes both: it is parented to the Window, so
// neither the group's bounds nor the scroll viewport's clip apply to it.
#include <cstdio>
#include <string>
#include <vector>

#include "Pages.hpp"
#include "geeyoou/core/Date.hpp"
#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/Cascader.hpp"
#include "geeyoou/widget/ComboBox.hpp"
#include "geeyoou/widget/DatePicker.hpp"
#include "geeyoou/widget/GroupBox.hpp"
#include "geeyoou/widget/Label.hpp"
#include "geeyoou/widget/MenuButton.hpp"
#include "geeyoou/widget/MultiSelect.hpp"
#include "geeyoou/widget/SearchableSelect.hpp"
#include "geeyoou/widget/TreeSelect.hpp"
#include "i18n/I18n.hpp"

namespace showcase {

using namespace geeyoou;

namespace {

Label* caption(Widget* parent, float x, float y, float w, std::string s) {
  auto* l = parent->add<Label>();
  l->setGeometry({x, y, w, 20});
  l->setText(s);
  l->addStyleClass("caption");
  l->setPixelSize(11.0f);
  l->setAlign(HAlign::Left, VAlign::Middle);
  return l;
}

struct Tag {
  const char* name;
  std::string desc;  // translated -- see tags() below
  const char* addr;
  const char* unit;
};

// Built on demand and rebuilt when the language changes.
//
// This used to be a namespace-scope `const Tag kTags[]`, which is exactly
// the shape that cannot survive a language switch: it is initialised once,
// in whatever language happened to be current, and no amount of rebuilding
// the PAGE re-runs it.  Returned by reference so the callers still do not
// copy it.
const std::vector<Tag>& tags() {
  static std::vector<Tag> v;
  static int builtFor = -1;
  if (builtFor == lang()) return v;
  builtFor = lang();
  v = {
      {"TI-101", tr("反应釜进料温度"), "40001", "°C"},
      {"TI-102", tr("反应釜夹套温度"), "40002", "°C"},
      {"PI-201", tr("系统压力"), "40010", "MPa"},
      {"PI-202", tr("泵出口压力"), "40011", "MPa"},
      {"FI-301", tr("进料流量"), "40020", "m³/h"},
      {"FI-302", tr("回流流量"), "40021", "m³/h"},
      {"LI-401", tr("反应釜液位"), "40030", "%"},
      {"AI-501", tr("pH 值"), "40040", "pH"},
      {"AI-502", tr("溶氧"), "40041", "mg/L"},
      {"SI-601", tr("搅拌转速"), "40050", "rpm"},
      {"VI-701", tr("振动烈度"), "40060", "mm/s"},
      {"EI-801", tr("电机电流"), "40070", "A"},
  };
  return v;
}

}  // namespace

Size buildSelectsPage(Widget* content) {

  auto* status = content->add<Label>();
  status->setGeometry({0, 634, 952, 26});
  status->addStyleClass("caption");
  status->setPixelSize(12.0f);
  status->setText(tr("状态：就绪 · Enter/↓ 展开，↑↓ 移动，Alt+1..9 快捷选中，Esc 关闭"));
  auto say = [status](const std::string& s) { status->setText(tr("状态：") + s); };

  // ---------------- 基础单选 / 分组 ----------------
  auto* gBasic = content->add<GroupBox>();
  gBasic->setGeometry({0, 0, 460, 190});
  gBasic->setTitle(tr("基础单选 / 分组"));

  caption(gBasic, 14, 44, 200, tr("扁平列表"));
  auto* cbMode = gBasic->add<ComboBox>();
  cbMode->setGeometry({14, 66, 200, 34});
  cbMode->setPlaceholder(tr("请选择运行模式"));
  cbMode->setItems({SelectItem(tr("停机"), "stop"), SelectItem(tr("手动"), "manual"),
                    SelectItem(tr("半自动"), "semi"), SelectItem(tr("全自动"), "auto")});
  cbMode->setCurrentValue("manual");

  caption(gBasic, 232, 44, 220, tr("带分组标题 + 禁用项"));
  auto* cbUnit = gBasic->add<ComboBox>();
  cbUnit->setGeometry({232, 66, 214, 34});
  cbUnit->setPlaceholder(tr("请选择设备"));
  {
    std::vector<SelectItem> items;
    items.push_back(SelectItem::group(tr("反应单元")));
    items.push_back(SelectItem(tr("R-101 主反应釜"), tr("运行"), "R101"));
    items.push_back(SelectItem(tr("R-102 备用反应釜"), tr("停机"), "R102"));
    items.push_back(SelectItem::group(tr("分离单元")));
    items.push_back(SelectItem(tr("T-201 精馏塔"), tr("运行"), "T201"));
    SelectItem broken(tr("T-202 精馏塔"), tr("检修中"), "T202");
    broken.enabled = false;
    items.push_back(std::move(broken));
    items.push_back(SelectItem(tr("C-301 冷凝器"), tr("运行"), "C301"));
    cbUnit->setItems(std::move(items));
  }

  caption(gBasic, 14, 112, 430, tr("长列表（120 项，只渲染可见行）"));
  auto* cbLong = gBasic->add<ComboBox>();
  cbLong->setGeometry({14, 134, 432, 34});
  cbLong->setPlaceholder(tr("请选择配方"));
  {
    std::vector<SelectItem> items;
    for (int i = 1; i <= 120; ++i) {
      items.push_back(SelectItem(tr("配方 R-") + std::to_string(1000 + i),
                                 tr("批次 ") + std::to_string(i),
                                 "R" + std::to_string(1000 + i)));
    }
    cbLong->setItems(std::move(items));
  }

  // ---------------- 搜索匹配 ----------------
  auto* gSearch = content->add<GroupBox>();
  gSearch->setGeometry({480, 0, 460, 190});
  gSearch->setTitle(tr("搜索匹配下拉（多字段）"));

  caption(gSearch, 14, 44, 430, tr("可搜位号 / 中文描述 / Modbus 地址 / 单位"));
  auto* selTag = gSearch->add<SearchableSelect>();
  selTag->setGeometry({14, 68, 432, 34});
  selTag->setPlaceholder(tr("输入以搜索位号…"));
  selTag->setMaxVisibleRows(8);
  selTag->setPopupWidth(432);
  {
    std::vector<SelectItem> items;
    for (const Tag& t : tags()) {
      SelectItem it(std::string(t.name) + "  " + t.desc, t.unit, t.name);
      it.extraFields = {t.addr, t.desc, t.unit};  // searched, never drawn
      items.push_back(std::move(it));
    }
    selTag->setItems(std::move(items));
  }

  auto* hint = gSearch->add<Label>();
  hint->setGeometry({14, 110, 432, 60});
  hint->addStyleClass("caption");
  hint->setPixelSize(11.0f);
  hint->setAlign(HAlign::Left, VAlign::Top);
  hint->setText(tr("试试输入 “温度” / “400” / “MPa” —— 分别命中描述、地址、单位"));

  // ---------------- 多选 ----------------
  auto* gMulti = content->add<GroupBox>();
  gMulti->setGeometry({0, 206, 460, 170});
  gMulti->setTitle(tr("多选下拉"));

  caption(gMulti, 14, 44, 200, tr("超过 2 项折叠为摘要"));
  auto* msAlarm = gMulti->add<MultiSelect>();
  msAlarm->setGeometry({14, 66, 200, 34});
  msAlarm->setPlaceholder(tr("请选择报警级别"));
  msAlarm->setItems({SelectItem(tr("紧急"), "1", "L1"), SelectItem(tr("高"), "2", "L2"),
                     SelectItem(tr("中"), "3", "L3"), SelectItem(tr("低"), "4", "L4"),
                     SelectItem(tr("提示"), "5", "L5")});
  msAlarm->setChecked(0, true);
  msAlarm->setChecked(1, true);

  caption(gMulti, 232, 44, 220, tr("带全选 / 清空"));
  auto* msChan = gMulti->add<MultiSelect>();
  msChan->setGeometry({232, 66, 214, 34});
  msChan->setPlaceholder(tr("请选择趋势通道"));
  {
    std::vector<SelectItem> items;
    for (const Tag& t : tags()) items.push_back(SelectItem(t.name, t.unit, t.name));
    msChan->setItems(std::move(items));
  }

  auto* msStatus = gMulti->add<Label>();
  msStatus->setGeometry({14, 112, 432, 44});
  msStatus->addStyleClass("caption");
  msStatus->setPixelSize(11.0f);
  msStatus->setAlign(HAlign::Left, VAlign::Top);
  msStatus->setText(tr("已选通道：（无）"));

  // ---------------- 树形 ----------------
  auto* gTree = content->add<GroupBox>();
  gTree->setGeometry({480, 206, 460, 170});
  gTree->setTitle(tr("树形下拉"));

  caption(gTree, 14, 44, 300, tr("仅叶子可选，显示全路径"));
  auto* tsTag = gTree->add<TreeSelect>();
  tsTag->setGeometry({14, 66, 432, 34});
  tsTag->setPlaceholder(tr("请选择测点"));
  tsTag->setMaxVisibleRows(9);
  tsTag->setPopupWidth(432);
  {
    TreeItem react(tr("反应单元"));
    { TreeItem r101(tr("R-101 主反应釜"));
      r101.children.push_back(TreeItem(tr("温度 TI-101"), "TI-101"));
      r101.children.push_back(TreeItem(tr("压力 PI-201"), "PI-201"));
      r101.children.push_back(TreeItem(tr("液位 LI-401"), "LI-401"));
      TreeItem r102(tr("R-102 备用反应釜"));
      r102.children.push_back(TreeItem(tr("温度 TI-102"), "TI-102"));
      react.children.push_back(std::move(r101));
      react.children.push_back(std::move(r102)); }
    TreeItem sep(tr("分离单元"));
    { TreeItem t201(tr("T-201 精馏塔"));
      t201.children.push_back(TreeItem(tr("塔顶温度 TI-211"), "TI-211"));
      t201.children.push_back(TreeItem(tr("塔釜温度 TI-212"), "TI-212"));
      sep.children.push_back(std::move(t201));
      sep.children.push_back(TreeItem(tr("C-301 冷凝器出口 TI-311"), "TI-311")); }
    TreeItem util(tr("公用工程"));
    util.children.push_back(TreeItem(tr("循环水温度 TI-901"), "TI-901"));
    util.children.push_back(TreeItem(tr("蒸汽压力 PI-902"), "PI-902"));
    react.expanded = true;
    tsTag->setRoots({std::move(react), std::move(sep), std::move(util)});
  }

  auto* treeStatus = gTree->add<Label>();
  treeStatus->setGeometry({14, 112, 432, 44});
  treeStatus->addStyleClass("caption");
  treeStatus->setPixelSize(11.0f);
  treeStatus->setAlign(HAlign::Left, VAlign::Top);
  treeStatus->setText(tr("已选测点：（无）"));

  // ---------------- 菜单 / 级联 / 日期 ----------------
  auto* gMore = content->add<GroupBox>();
  gMore->setGeometry({0, 392, 940, 224});
  gMore->setTitle(tr("动作菜单 / 拆分按钮 / 级联 / 日期"));

  caption(gMore, 14, 44, 200, tr("动作菜单"));
  auto* menuBtn = gMore->add<MenuButton>();
  menuBtn->setGeometry({14, 66, 150, 36});
  menuBtn->setText(tr("批次操作"));
  menuBtn->setIcon(Icon::Menu);
  menuBtn->setItems({
      MenuItem(tr("开始批次"), "start", Icon::Play),
      MenuItem(tr("暂停批次"), "pause", Icon::Pause),
      MenuItem(tr("终止批次"), "stop", Icon::Stop),
      MenuItem::sep(),
      MenuItem(tr("导出报表"), "export", Icon::Download),
      MenuItem(tr("打印"), "print", Icon::Save),
      MenuItem::sep(),
      [] { MenuItem m(tr("删除记录"), "delete", Icon::Trash); m.enabled = false; return m; }(),
  });

  caption(gMore, 180, 44, 200, tr("拆分按钮"));
  auto* splitBtn = gMore->add<SplitButton>();
  splitBtn->setGeometry({180, 66, 168, 36});
  splitBtn->setText(tr("下发配方"));
  splitBtn->setIcon(Icon::Upload);
  splitBtn->setVariant(ButtonVariant::Primary);
  splitBtn->setItems({MenuItem(tr("下发并启动"), "send_start"),
                      MenuItem(tr("仅下发"), "send_only"),
                      MenuItem(tr("下发到备用釜"), "send_backup")});

  caption(gMore, 364, 44, 220, tr("级联选择"));
  auto* casc = gMore->add<Cascader>();
  casc->setGeometry({364, 66, 220, 36});
  casc->setPlaceholder(tr("选择设备位置"));
  casc->setColumnWidth(150.0f);
  {
    TreeItem w1(tr("一号车间"));
    { TreeItem l1(tr("A 产线"));
      l1.children.push_back(TreeItem(tr("R-101 反应釜"), "W1-A-R101"));
      l1.children.push_back(TreeItem(tr("T-201 精馏塔"), "W1-A-T201"));
      TreeItem l2(tr("B 产线"));
      l2.children.push_back(TreeItem(tr("R-102 反应釜"), "W1-B-R102"));
      w1.children.push_back(std::move(l1));
      w1.children.push_back(std::move(l2)); }
    TreeItem w2(tr("二号车间"));
    { TreeItem l3(tr("C 产线"));
      l3.children.push_back(TreeItem(tr("C-301 冷凝器"), "W2-C-C301"));
      l3.children.push_back(TreeItem(tr("P-401 输送泵"), "W2-C-P401"));
      w2.children.push_back(std::move(l3)); }
    casc->setRoots({std::move(w1), std::move(w2)});
  }

  caption(gMore, 600, 44, 200, tr("日期选择"));
  auto* datePick = gMore->add<DatePicker>();
  datePick->setGeometry({600, 66, 146, 36});
  datePick->setPlaceholder(tr("选择日期"));
  {
    const Date today = fromUnixMillis(nowMs());
    datePick->setToday(today);
    datePick->setDate(today);
    datePick->setRange(addDays(today, -365), addDays(today, 30));
  }

  caption(gMore, 762, 44, 170, tr("禁用 / 必填未选"));
  auto* cbDisabled = gMore->add<ComboBox>();
  cbDisabled->setGeometry({762, 66, 164, 36});
  cbDisabled->setPlaceholder(tr("已禁用"));
  cbDisabled->setEnabled(false);

  auto* cbInvalid = gMore->add<ComboBox>();
  cbInvalid->setGeometry({762, 112, 164, 36});
  cbInvalid->setPlaceholder(tr("必填项未选"));
  cbInvalid->setInvalid(true);

  auto* cbFlip = gMore->add<ComboBox>();
  cbFlip->setGeometry({14, 150, 240, 36});
  cbFlip->setPlaceholder(tr("靠近底部——应向上弹"));
  cbFlip->setItems({SelectItem(tr("选项一")), SelectItem(tr("选项二")), SelectItem(tr("选项三")),
                    SelectItem(tr("选项四")), SelectItem(tr("选项五")), SelectItem(tr("选项六"))});

  // ---------------- signals ----------------
  cbMode->currentValueChanged.connect([say](const std::string& v) { say(tr("运行模式 = ") + v); });
  cbUnit->currentIndexChanged.connect([say, cbUnit](int) { say(tr("设备 = ") + cbUnit->currentValue()); });
  cbLong->currentValueChanged.connect([say](const std::string& v) { say(tr("配方 = ") + v); });
  selTag->currentValueChanged.connect([say](const std::string& v) { say(tr("位号 = ") + v); });
  selTag->noMatch.connect([say](const std::string& q) { say(tr("无匹配：\"") + q + "\""); });
  msAlarm->selectionChanged.connect([say, msAlarm] {
    char buf[64];
    std::snprintf(buf, sizeof(buf), tr("报警级别已选 %d 项").c_str(),
                  msAlarm->checkedCount());
    say(buf);
  });
  msChan->selectionChanged.connect([msStatus, msChan] {
    const auto vals = msChan->checkedValues();
    std::string s = tr("已选通道：");
    if (vals.empty()) s += tr("（无）");
    for (std::size_t i = 0; i < vals.size(); ++i) { if (i) s += tr("、"); s += vals[i]; }
    msStatus->setText(s);
  });
  tsTag->selectionChanged.connect([treeStatus, say](const std::string& v) {
    treeStatus->setText(tr("已选测点：") + v);
    say(tr("测点 = ") + v);
  });
  menuBtn->triggered.connect([say](const std::string& id) { say(tr("菜单 → ") + id); });
  splitBtn->clicked.connect([say] { say(tr("拆分按钮主操作：下发配方")); });
  splitBtn->triggered.connect([say](const std::string& id) { say(tr("拆分按钮菜单 → ") + id); });
  casc->selectionChanged.connect([say](const std::string& v) { say(tr("级联 → ") + v); });
  datePick->dateChanged.connect([say](Date d) { say(tr("日期 → ") + toIsoString(d)); });

  return {950.0f, 676.0f};
}

}  // namespace showcase
