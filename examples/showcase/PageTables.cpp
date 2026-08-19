// 表格 —— 基础样式、行内控件与编辑、分页。
//
// LAID OUT BY THE ENGINE.  There is not one coordinate in this file: every page
// is a column of bands, each band a line of panels.  A table is given stretch so
// that dragging the window taller shows MORE ROWS rather than more empty panel,
// which is the whole reason a size hint on a pull-model view reports a viewport
// (ADR-R2-09, and tests/widget/test_table.cpp pins it).
//
// The advanced four -- frozen panes, merging, trees, 200 000 rows -- are next
// door in PageTablesTree.cpp.
#include <algorithm>
#include <memory>
#include <string>
#include <vector>

#include "Pages.hpp"
#include "TableDemoData.hpp"
#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/BoxLayout.hpp"
#include "geeyoou/widget/GroupBox.hpp"
#include "geeyoou/widget/Label.hpp"
#include "geeyoou/widget/PushButton.hpp"
#include "geeyoou/widget/ToggleSwitch.hpp"

namespace showcase {

using namespace geeyoou;

namespace {
constexpr float kBandGap = 16.0f;
constexpr float kPanelGap = 20.0f;
constexpr float kItemGap = 10.0f;

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

Label* caption(Widget* parent, BoxLayout* into, const char* s) {
  auto* l = parent->add<Label>();
  l->setText(s);
  l->addStyleClass("caption");
  l->setPixelSize(11.0f);
  l->setAlign(HAlign::Left, VAlign::Middle);
  into->addWidget(l);
  return l;
}

// The five columns every "just show me the register" table uses.
std::vector<TableView::Column> registerColumns() {
  std::vector<TableView::Column> cols;
  cols.push_back(kindColumn("#", 48.0f, CellKind::Index));
  cols.push_back(textColumn("位号", 110.0f, /*sortable=*/true));
  cols.push_back(textColumn("名称", 0.0f, /*sortable=*/true));  // takes the slack
  cols.push_back(textColumn("区域", 120.0f, /*sortable=*/true));
  cols.push_back(kindColumn("状态", 90.0f, CellKind::Chip));
  return cols;
}

std::vector<Field> registerFields() {
  return {Field::Index, Field::Tag, Field::Name, Field::Area, Field::Status};
}

// An empty register, so the empty-state panel has something honest to be empty
// ABOUT -- a table with a null model would also draw the empty state, and would
// prove nothing about what a real screen does when a filter matches nothing.
std::vector<Device>& noDevices() {
  static std::vector<Device> none;
  return none;
}

}  // namespace

// ==================================================== 基础表格与状态 =========
Size buildTablesBasicPage(Widget* content) {
  BoxLayout* page = stack(content, kBandGap);

  // ---------------- 普通表格 ----------------
  Widget* upper = band(content, page, /*stretch=*/3);
  BoxLayout* upperRow = line(upper, kPanelGap);

  auto* gMain = upper->add<GroupBox>();
  gMain->setTitle("普通表格 · 斑马纹 / 列排序 / 行选择");
  upperRow->addWidget(gMain, 1);
  BoxLayout* mainStack = stack(gMain, kItemGap);

  auto* mainPanel = gMain->add<TablePanel>(
      std::make_unique<DeviceModel>(&demoDevices(), registerFields()));
  mainPanel->setDesignHeight(300.0f);
  TableView* main = mainPanel->table();
  main->setColumns(registerColumns());
  main->setSelectionMode(TableView::SelectionMode::Multi);
  main->rowsReset();
  mainStack->addWidget(mainPanel, 1);

  auto* mainNote = gMain->add<Label>();
  mainNote->addStyleClass("caption");
  mainNote->setPixelSize(11.0f);
  mainNote->setText("点击表头排序（升 / 降 / 取消三态）· Ctrl 加选 · Shift 连选");
  mainStack->addWidget(mainNote);

  // Sorting is the MODEL's job; the view only moved its indicator.  Doing it
  // here, in the page, is the worked example of that split.
  main->sortChanged.connect([main, mainNote](int col, SortOrder order) {
    std::vector<Device>& rows = demoDevices();
    if (order != SortOrder::None) {
      const bool asc = order == SortOrder::Ascending;
      std::stable_sort(rows.begin(), rows.end(),
                       [col, asc](const Device& a, const Device& b) {
                         const std::string& x =
                             col == 3 ? a.area : (col == 2 ? a.name : a.tag);
                         const std::string& y =
                             col == 3 ? b.area : (col == 2 ? b.name : b.tag);
                         return asc ? x < y : y < x;
                       });
    }
    main->rowsReset();
    mainNote->setText(order == SortOrder::None
                          ? "已取消排序 —— 顺序回到模型给出的原始顺序"
                          : "排序由模型完成，视图只移动了表头的指示符");
  });

  // ---------------- 开关面板 ----------------
  auto* gStyle = upper->add<GroupBox>();
  gStyle->setTitle("样式开关");
  upperRow->addWidget(gStyle, 0);
  BoxLayout* styleStack = stack(gStyle, kItemGap);

  caption(gStyle, styleStack, "改的是表格属性，不是主题");

  auto* swZebra = gStyle->add<ToggleSwitch>();
  swZebra->setText("奇偶行变色");
  swZebra->setChecked(true);
  swZebra->toggled.connect([main](bool on) { main->setAlternatingRows(on); });
  styleStack->addWidget(swZebra);

  auto* swGrid = gStyle->add<ToggleSwitch>();
  swGrid->setText("网格线");
  swGrid->setChecked(true);
  swGrid->toggled.connect([main](bool on) { main->setGridVisible(on, on); });
  styleStack->addWidget(swGrid);

  auto* swHover = gStyle->add<ToggleSwitch>();
  swHover->setText("悬停高亮");
  swHover->setChecked(true);
  swHover->toggled.connect([main](bool on) { main->setHoverHighlight(on); });
  styleStack->addWidget(swHover);

  auto* swHeader = gStyle->add<ToggleSwitch>();
  swHeader->setText("显示表头");
  swHeader->setChecked(true);
  swHeader->toggled.connect([main](bool on) { main->setHeaderVisible(on); });
  styleStack->addWidget(swHeader);

  auto* swDense = gStyle->add<ToggleSwitch>();
  swDense->setText("紧凑行高");
  swDense->toggled.connect(
      [main](bool on) { main->setRowHeight(on ? 26.0f : 34.0f); });
  styleStack->addWidget(swDense);

  // ---------------- 空状态 / 加载中 ----------------
  Widget* lower = band(content, page, /*stretch=*/2);
  BoxLayout* lowerRow = line(lower, kPanelGap);

  auto* gEmpty = lower->add<GroupBox>();
  gEmpty->setTitle("空状态");
  lowerRow->addWidget(gEmpty, 1);
  BoxLayout* emptyStack = stack(gEmpty, kItemGap);

  auto* emptyPanel = gEmpty->add<TablePanel>(
      std::make_unique<DeviceModel>(&noDevices(), registerFields()));
  emptyPanel->setDesignHeight(200.0f);
  TableView* emptyTable = emptyPanel->table();
  emptyTable->setColumns(registerColumns());
  emptyTable->setEmptyText("没有匹配的仪表", "放宽筛选条件，或检查所选区域");
  emptyTable->rowsReset();
  emptyStack->addWidget(emptyPanel, 1);

  caption(gEmpty, emptyStack, "表头仍然在：空的是数据，不是这张表");

  auto* gLoading = lower->add<GroupBox>();
  gLoading->setTitle("加载中");
  lowerRow->addWidget(gLoading, 1);
  BoxLayout* loadStack = stack(gLoading, kItemGap);

  auto* loadPanel = gLoading->add<TablePanel>(
      std::make_unique<DeviceModel>(&demoDevices(), registerFields()));
  loadPanel->setDesignHeight(200.0f);
  TableView* loadTable = loadPanel->table();
  loadTable->setColumns(registerColumns());
  loadTable->rowsReset();
  loadTable->setLoading(true);
  loadStack->addWidget(loadPanel, 1);

  Widget* loadRow = band(gLoading, loadStack);
  BoxLayout* loadRowL = line(loadRow, kItemGap);
  auto* swLoad = loadRow->add<ToggleSwitch>();
  swLoad->setText("忙碌");
  swLoad->setChecked(true);
  swLoad->toggled.connect([loadTable](bool on) { loadTable->setLoading(on); });
  loadRowL->addWidget(swLoad);
  auto* loadNote = loadRow->add<Label>();
  loadNote->addStyleClass("caption");
  loadNote->setPixelSize(11.0f);
  loadNote->setText("行仍在下面：刷新不该让操作员丢失位置");
  loadRowL->addWidget(loadNote, 1);

  return content->sizeHint().preferred;
}

// ================================================ 行内控件与行内编辑 =========
Size buildTablesEditPage(Widget* content) {
  BoxLayout* page = stack(content, kBandGap);

  Widget* top = band(content, page, /*stretch=*/1);
  BoxLayout* topRow = line(top, kPanelGap);

  auto* g = top->add<GroupBox>();
  g->setTitle("行内编辑 · 行内下拉 / 多选 / 数值 / 开关 / 勾选 / 进度条 / 操作");
  topRow->addWidget(g, 1);
  BoxLayout* gStack = stack(g, kItemGap);

  std::vector<Field> fields = {Field::Selector, Field::Index,    Field::Tag,
                               Field::Type,     Field::Tags,     Field::Range,
                               Field::Progress, Field::Running,  Field::Picked,
                               Field::Actions};

  auto* panel = g->add<TablePanel>(
      std::make_unique<DeviceModel>(&demoDevices(), fields));
  panel->setDesignHeight(420.0f);
  TableView* t = panel->table();

  std::vector<TableView::Column> cols;
  cols.push_back(kindColumn("", 42.0f, CellKind::Selector));
  cols.push_back(kindColumn("#", 44.0f, CellKind::Index));

  TableView::Column tag = textColumn("位号", 110.0f);
  tag.editable = true;
  tag.editor = CellEditor::Text;
  cols.push_back(tag);

  TableView::Column type = textColumn("类型", 110.0f);
  type.editable = true;
  type.editor = CellEditor::Select;
  type.options = {SelectItem("温度", "温度"), SelectItem("压力", "压力"),
                  SelectItem("流量", "流量"), SelectItem("液位", "液位"),
                  SelectItem("阀门", "阀门")};
  cols.push_back(type);

  // Flexible, so the SLACK goes to text rather than to the progress bar: a
  // 230-pixel bar is not more informative than a 180-pixel one, and the tag list
  // is the column that actually gets truncated.
  TableView::Column tags = textColumn("标签", 0.0f);
  tags.editable = true;
  tags.editor = CellEditor::MultiSelect;
  tags.options = {SelectItem("关键", "关键"), SelectItem("联锁", "联锁"),
                  SelectItem("常规", "常规"), SelectItem("备用", "备用")};
  cols.push_back(tags);

  TableView::Column range = textColumn("量程", 100.0f);
  range.align = HAlign::Right;
  range.editable = true;
  range.editor = CellEditor::Number;
  range.minValue = 0.0;
  range.maxValue = 1000.0;
  range.step = 10.0;
  range.decimals = 0;
  cols.push_back(range);

  cols.push_back(kindColumn("完成度", 180.0f, CellKind::Progress));
  cols.push_back(kindColumn("投用", 78.0f, CellKind::Switch));
  cols.push_back(kindColumn("复核", 66.0f, CellKind::Check));

  TableView::Column ops = kindColumn("操作", 130.0f, CellKind::Actions);
  ops.actions = {CellAction("edit", "编辑"),
                 CellAction("delete", "删除", CellAction::Tone::Danger)};
  cols.push_back(ops);

  t->setColumns(cols);
  t->setSelectionMode(TableView::SelectionMode::Multi);
  t->rowsReset();
  gStack->addWidget(panel, 1);

  auto* log = g->add<Label>();
  log->addStyleClass("caption");
  log->setPixelSize(11.0f);
  log->setText("单击选中一格，再单击它进入编辑 · 回车提交，Esc 还原本格原值");
  gStack->addWidget(log);

  // THE SIGNALS ARE THE POINT of this page: everything below proves that the
  // painted cells are wired to real events, not decoration.
  t->cellEdited.connect([log](int row, int col, const std::string& v) {
    log->setText("已提交：第 " + std::to_string(row + 1) + " 行，第 " +
                 std::to_string(col + 1) + " 列 → " + v);
  });
  t->cellToggled.connect([log](int row, int col, bool on) {
    log->setText("已切换：第 " + std::to_string(row + 1) + " 行，第 " +
                 std::to_string(col + 1) + " 列 → " + (on ? "开" : "关"));
  });
  t->actionTriggered.connect([log, t](int row, const std::string& id) {
    if (id == "delete") {
      std::vector<Device>& rows = demoDevices();
      if (row >= 0 && row < int(rows.size())) {
        const std::string tagText = rows[std::size_t(row)].tag;
        rows.erase(rows.begin() + row);
        // rowsReset, not rowsChanged: the COUNT moved, and everything the view
        // caches about it -- selection, current cell, an open editor -- has to be
        // rechecked in one place.
        t->rowsReset();
        log->setText("已删除：" + tagText + "（模型删的行，视图只是重新问了一次行数）");
        return;
      }
    }
    log->setText("操作：第 " + std::to_string(row + 1) + " 行 → " + id);
  });

  Widget* bottom = band(content, page);
  BoxLayout* bottomRow = line(bottom, kItemGap);

  auto* btnSelectAll = bottom->add<PushButton>();
  btnSelectAll->setText("全选");
  btnSelectAll->clicked.connect([t] { t->selectAllRows(); });
  bottomRow->addWidget(btnSelectAll);

  auto* btnClear = bottom->add<PushButton>();
  btnClear->setText("清空选择");
  btnClear->clicked.connect([t] { t->clearSelection(); });
  bottomRow->addWidget(btnClear);

  auto* selInfo = bottom->add<Label>();
  selInfo->addStyleClass("caption");
  selInfo->setPixelSize(11.0f);
  selInfo->setText("已选 0 行");
  bottomRow->addWidget(selInfo, 1);
  t->selectionChanged.connect([t, selInfo] {
    selInfo->setText("已选 " + std::to_string(t->selectedRows().size()) + " 行");
  });

  return content->sizeHint().preferred;
}

// ========================================================== 分页表格 =========
Size buildTablesPagedPage(Widget* content) {
  BoxLayout* page = stack(content, kBandGap);

  Widget* top = band(content, page, /*stretch=*/1);
  BoxLayout* topRow = line(top, kPanelGap);

  auto* g = top->add<GroupBox>();
  g->setTitle("分页表格 · 分页移动的是模型的窗口，不是视图的能力");
  topRow->addWidget(g, 1);
  BoxLayout* gStack = stack(g, kItemGap);

  auto* panel = g->add<TablePanel>(
      std::make_unique<DeviceModel>(&demoDevices(), registerFields()),
      /*withPager=*/true);
  panel->setDesignHeight(440.0f);
  TableView* t = panel->table();
  TablePager* pager = panel->pager();
  auto* model = static_cast<DeviceModel*>(panel->model());

  t->setColumns(registerColumns());
  t->setSelectionMode(TableView::SelectionMode::Single);

  const int total = int(demoDevices().size());
  pager->setTotal(total);
  pager->setPageSize(10);
  pager->setPageSizeOptions({10, 20, 50});
  model->setWindow(pager->firstRow(), pager->pageSize());
  t->rowsReset();
  gStack->addWidget(panel, 1);

  // THE WIRING, and it is three lines because the two controls know nothing
  // about each other: the pager announces a page, the application moves the
  // model's window, the view is told the rows changed.
  auto apply = [model, t, pager] {
    model->setWindow(pager->firstRow(), pager->pageSize());
    t->rowsReset();
    t->scrollToTop();
  };
  pager->pageChanged.connect([apply](int) { apply(); });
  pager->pageSizeChanged.connect([apply](int) { apply(); });

  auto* note = g->add<Label>();
  note->addStyleClass("caption");
  note->setPixelSize(11.0f);
  note->setText(
      "分页与虚拟滚动不是二选一：视图永远只画看得见的行，分页只决定它拿到哪一段");
  gStack->addWidget(note);

  return content->sizeHint().preferred;
}

}  // namespace showcase
