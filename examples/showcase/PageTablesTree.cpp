// 表格 —— 固定列与合并单元格、树形、异步树形、大数据量。
//
// The four pages here are the ones that exist to prove a PROPERTY rather than to
// show a control:
//
//   * frozen panes and merged cells are geometry that has to survive scrolling;
//   * a tree is a row count that changes under the view;
//   * an asynchronous tree is a row count that changes LATER, from a timer, and
//     the whole point of the signal-shaped API is that the answer arrives on the
//     UI thread (docs/architecture.md section 3.11);
//   * 200 000 rows is the claim the pull model is FOR, and this page holds no
//     rows at all -- the model computes each cell from its index, so if the view
//     ever walked the whole model this page would simply stop responding.
//
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "Pages.hpp"
#include "TableDemoData.hpp"
#include "geeyoou/platform/Platform.hpp"
#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/BoxLayout.hpp"
#include "geeyoou/widget/GroupBox.hpp"
#include "geeyoou/widget/Label.hpp"
#include "geeyoou/widget/PushButton.hpp"
#include "geeyoou/widget/TreeTableModel.hpp"

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

// ---------------------------------------------------------------------------
// A model with NO ROWS IN IT.
//
// Every cell is computed from the row index, so this object is about forty bytes
// no matter what tableRowCount() answers.  That is not a trick for the demo: it
// is the pull model's contract taken to its limit, and it is the reason this
// page can offer 200 000 rows and still scroll.
// ---------------------------------------------------------------------------
class SyntheticModel : public TableModel {
 public:
  explicit SyntheticModel(int rows) : rows_(rows) {}
  void setRows(int n) { rows_ = n; }

  int tableRowCount() const override { return rows_; }

  std::string tableText(int row, int col) const override {
    char buf[64];
    switch (col) {
      case 1:
        std::snprintf(buf, sizeof(buf), "TAG-%06d", row + 1);
        return buf;
      case 2:
        std::snprintf(buf, sizeof(buf), "采集点 %d 号", row + 1);
        return buf;
      case 3: {
        static const char* areas[] = {"一号反应区", "二号反应区", "罐区",
                                      "公用工程", "码头"};
        return areas[row % 5];
      }
      case 4: {
        std::snprintf(buf, sizeof(buf), "%.2f", double((row * 37) % 10000) / 100.0);
        return buf;
      }
      case 5:
        return (row % 17 == 0) ? "故障" : ((row % 5 == 0) ? "维护" : "运行");
      default:
        return {};
    }
  }

  double tableNumber(int row, int col) const override {
    return col == 6 ? double((row * 13) % 101) / 100.0 : 0.0;
  }

  bool tableFlag(int row, int col) const override {
    return col == 7 && (row % 17 != 0);
  }

  Color tableAccent(int row, int col) const override {
    if (col == 5) return statusColor(tableText(row, 5));
    if (col < 0 && row % 17 == 0) return Theme::current().danger;
    return Color::rgba(0, 0, 0, 0);
  }

 private:
  int rows_ = 0;
};

// Same base-list trick as TablePanel, and for the same reason: the tree model is
// read through a raw pointer by a child widget, so it has to outlive that child,
// and bases are destroyed after members.  See the note on TableModelHolder in
// TableDemoData.hpp for the defect this shape exists to prevent -- and for why a
// destructor body cannot do the job.
class TreeModelHolder {
 protected:
  // Column 2 is the status word; see StatusTreeModel for why the colour is
  // derived rather than stored.
  StatusTreeModel tree_{2};
};

// ---------------------------------------------------------------------------
// The asynchronous tree, and the two lifetime rules it exists to demonstrate.
//
// The model NEVER fetches.  It emits childrenRequested; this panel remembers the
// id, starts a timer, and answers on the UI thread when the timer fires.  A
// worker thread would push into a queue instead and the drain would look exactly
// like this -- which is the point of the shape (architecture 3.11).
//
// ⚠️ THE TIMER MUST NOT OUTLIVE THIS PANEL.  platform().startTimer takes a
// callback that captures `this`; a panel destroyed while a request is in flight
// would leave the event loop calling into freed memory on the next tick.  The
// destructor stops it, and stopping it is the ONLY thing that destructor does --
// see the comment there for why it may not also touch the tree.
// ---------------------------------------------------------------------------
class AsyncTreePanel : public TreeModelHolder, public Widget {
 public:
  AsyncTreePanel() {
    table_ = add<TableView>();
    table_->setModel(&tree_);

    std::vector<TableView::Column> cols;
    TableView::Column name;
    name.title = "设备 / 位号";
    name.width = 300.0f;
    name.kind = CellKind::Tree;
    cols.push_back(name);
    cols.push_back(textColumn("类型", 110.0f));
    cols.push_back(kindColumn("状态", 90.0f, CellKind::Chip));
    cols.push_back(kindColumn("投用", 78.0f, CellKind::Switch));
    table_->setColumns(cols);
    table_->setSelectionMode(TableView::SelectionMode::Single);

    // Four stations, none of them loaded.  A lazy branch draws a normal
    // collapsed expander -- the operator cannot tell, and should not have to.
    const char* stations[] = {"1# 反应釜", "2# 反应釜", "3# 罐区", "4# 公用工程"};
    for (const char* s : stations) {
      const auto id = tree_.addNode(TreeTableModel::kRootNode, {s, "站点", "运行", ""});
      tree_.setLazy(id, true);
    }
    tree_.structureChanged.connect([this] { table_->rowsReset(); });
    tree_.childrenRequested.connect(
        [this](TreeTableModel::NodeId id) { request(id); });
    table_->rowsReset();
  }

  // The ONLY thing this destructor may do, and the one thing it must.
  //
  // Stopping the timer is not a tree operation: it unhooks a callback that
  // captures `this` from the event loop, which is the difference between a
  // panel that is destroyed and a panel that the next tick calls into.  The
  // table and the model need nothing here -- the base list already orders them.
  ~AsyncTreePanel() override {
    platform().stopTimer(timer_);
    timer_ = 0;
  }

  TableView* table() const { return table_; }
  Signal<std::string> statusChanged;

 protected:
  void onGeometryChanged() override {
    if (table_) table_->setGeometry(localRect());
  }

  SizeHint sizeHint() const override {
    SizeHint h;
    h.preferred = Size{560.0f, 400.0f};
    h.min = Size{320.0f, 180.0f};
    return h;
  }

 private:
  void request(TreeTableModel::NodeId id) {
    pending_.push_back(id);
    statusChanged.emit("已请求子节点，等待返回…（模拟 700ms 网络往返）");
    if (timer_ != 0) return;
    // A repeating timer drained one request per tick, rather than one timer per
    // request: ids are not reused and a timer per request would have to be
    // tracked per request to be cancellable.
    timer_ = platform().startTimer(700, [this] { deliver(); });
  }

  void deliver() {
    if (pending_.empty()) {
      platform().stopTimer(timer_);
      timer_ = 0;
      return;
    }
    const TreeTableModel::NodeId id = pending_.front();
    pending_.erase(pending_.begin());

    // Every fourth answer fails, on purpose: a demo where the network always
    // works teaches nothing about the state the operator will actually meet.
    ++served_;
    if (served_ % 4 == 0) {
      tree_.finishLoad(id, false);
      statusChanged.emit("读取失败 —— 分支保留重试标记，不会自己重发");
      return;
    }

    const std::vector<std::string>& parent = tree_.cellsOf(id);
    const std::string prefix = parent.empty() ? std::string("X") : parent[0];
    for (int i = 0; i < 5; ++i) {
      char tag[64];
      std::snprintf(tag, sizeof(tag), "%s · TI-%03d", prefix.c_str(), 101 + i);
      const bool ok = (i != 3);
      const auto child = tree_.addNode(
          id, {tag, i % 2 ? "压力" : "温度", ok ? "运行" : "维护", ""});
      tree_.setFlag(child, 3, ok);
      // One grandchild per child, itself lazy: the point is that depth is not
      // special-cased anywhere.
      if (i == 0) tree_.setLazy(child, true);
    }
    tree_.finishLoad(id, true);
    statusChanged.emit("子节点已送达，分支自动展开");
  }

  TableView* table_ = nullptr;
  std::vector<TreeTableModel::NodeId> pending_;
  TimerId timer_ = 0;
  int served_ = 0;
};

}  // namespace

// =============================================== 固定列与合并单元格 =========
Size buildTablesFrozenPage(Widget* content) {
  BoxLayout* page = stack(content, kBandGap);

  // ---------------- 固定列 ----------------
  Widget* top = band(content, page, /*stretch=*/1);
  BoxLayout* topRow = line(top, kPanelGap);

  auto* gFrozen = top->add<GroupBox>();
  gFrozen->setTitle("固定列 · 左侧两列与右侧操作列钉住，中间横向滚动");
  topRow->addWidget(gFrozen, 1);
  BoxLayout* frozenStack = stack(gFrozen, kItemGap);

  std::vector<Field> fields = {
      Field::Index, Field::Tag,   Field::Name,    Field::Area,   Field::Type,
      Field::Tags,  Field::Range, Field::Progress, Field::Status, Field::Actions};

  auto* panel = gFrozen->add<TablePanel>(
      std::make_unique<DeviceModel>(&demoDevices(), fields));
  panel->setDesignHeight(300.0f);
  TableView* t = panel->table();

  std::vector<TableView::Column> cols;
  cols.push_back(kindColumn("#", 46.0f, CellKind::Index));
  cols.push_back(textColumn("位号", 110.0f, true));
  // Flexible, so a wide window fills the middle band instead of leaving a
  // stripe of empty grid between the last column and the frozen pane -- the one
  // thing a table with nothing but fixed columns always looks like on a 1676px
  // screen.
  cols.push_back(textColumn("名称", 0.0f));
  cols.push_back(textColumn("区域", 130.0f));
  cols.push_back(textColumn("类型", 100.0f));
  cols.push_back(textColumn("标签", 160.0f));
  TableView::Column range = textColumn("量程", 100.0f);
  range.align = HAlign::Right;
  cols.push_back(range);
  cols.push_back(kindColumn("完成度", 160.0f, CellKind::Progress));
  cols.push_back(kindColumn("状态", 90.0f, CellKind::Chip));
  TableView::Column ops = kindColumn("操作", 120.0f, CellKind::Actions);
  ops.actions = {CellAction("edit", "编辑"),
                 CellAction("delete", "删除", CellAction::Tone::Danger)};
  cols.push_back(ops);

  t->setColumns(cols);
  // Two leading, one trailing.  The counts are clamped against the column count
  // and against each other, so an arrangement with no middle band left is simply
  // not expressible.
  t->setFrozenColumns(2, 1);
  t->rowsReset();
  frozenStack->addWidget(panel, 1);

  caption(gFrozen, frozenStack,
          "Shift + 滚轮横向滚动 · 冻结列上出现的那道边影只在真的有内容藏在下面时才画");

  // ---------------- 合并单元格 ----------------
  Widget* bottom = band(content, page, /*stretch=*/1);
  BoxLayout* bottomRow = line(bottom, kPanelGap);

  auto* gMerge = bottom->add<GroupBox>();
  gMerge->setTitle("合并行 · 区域列按连续段合并");
  bottomRow->addWidget(gMerge, 1);
  BoxLayout* mergeStack = stack(gMerge, kItemGap);

  std::vector<Field> mergeFields = {Field::Area, Field::Tag, Field::Name,
                                    Field::Status};
  auto* mergePanel = gMerge->add<TablePanel>(
      std::make_unique<DeviceModel>(&demoDevices(), mergeFields));
  mergePanel->setDesignHeight(300.0f);
  TableView* mt = mergePanel->table();
  auto* mergeModel = static_cast<DeviceModel*>(mergePanel->model());
  mergeModel->setMergeArea(true);

  std::vector<TableView::Column> mcols;
  TableView::Column area = textColumn("区域", 140.0f);
  area.align = HAlign::Center;
  mcols.push_back(area);
  mcols.push_back(textColumn("位号", 120.0f));
  mcols.push_back(textColumn("名称", 0.0f));
  mcols.push_back(kindColumn("状态", 90.0f, CellKind::Chip));
  mt->setColumns(mcols);
  mt->setMergingEnabled(true);
  mt->rowsReset();
  mergeStack->addWidget(mergePanel, 1);

  caption(gMerge, mergeStack,
          "被覆盖的格子回答的是「锚点在我上面几行」的负偏移 —— 所以合并在二十万行上也是每格 O(1)");

  return content->sizeHint().preferred;
}

// ========================================================== 树形表格 =========
Size buildTablesTreePage(Widget* content) {
  BoxLayout* page = stack(content, kBandGap);

  Widget* top = band(content, page, /*stretch=*/1);
  BoxLayout* topRow = line(top, kPanelGap);

  auto* g = top->add<GroupBox>();
  g->setTitle("树形表格 · 区域 → 设备类型 → 仪表");
  topRow->addWidget(g, 1);
  BoxLayout* gStack = stack(g, kItemGap);

  // Column 2 holds the status word; the model tints the chip from it.
  auto tree = std::make_unique<StatusTreeModel>(2);
  TreeTableModel* raw = tree.get();

  // Built from the SAME sixty records the flat pages show: a tree is a different
  // way of walking one register, not a second register.
  const char* areas[] = {"一号反应区", "二号反应区", "罐区", "公用工程"};
  for (const char* areaName : areas) {
    const auto areaNode =
        raw->addNode(TreeTableModel::kRootNode, {areaName, "", "", ""});

    const char* types[] = {"温度", "压力", "流量", "液位", "阀门"};
    for (const char* typeName : types) {
      TreeTableModel::NodeId typeNode = TreeTableModel::kInvalidNode;
      for (const Device& d : demoDevices()) {
        if (d.area != areaName || d.type != typeName) continue;
        if (typeNode == TreeTableModel::kInvalidNode) {
          typeNode = raw->addNode(areaNode, {std::string(typeName) + "仪表", "", "", ""});
        }
        const auto leaf = raw->addNode(typeNode, {d.tag, d.name, d.status, ""});
        raw->setFlag(leaf, 3, d.running);
      }
    }
  }

  auto* panel = g->add<TablePanel>(std::move(tree));
  panel->setDesignHeight(460.0f);
  TableView* t = panel->table();

  std::vector<TableView::Column> cols;
  TableView::Column name;
  name.title = "层级 / 位号";
  name.width = 320.0f;
  name.kind = CellKind::Tree;
  cols.push_back(name);
  cols.push_back(textColumn("名称", 0.0f));
  cols.push_back(kindColumn("状态", 90.0f, CellKind::Chip));
  cols.push_back(kindColumn("投用", 78.0f, CellKind::Switch));
  t->setColumns(cols);
  t->setSelectionMode(TableView::SelectionMode::Single);
  t->rowsReset();
  gStack->addWidget(panel, 1);

  raw->structureChanged.connect([t] { t->rowsReset(); });

  Widget* buttons = band(g, gStack);
  BoxLayout* buttonRow = line(buttons, kItemGap);

  auto* rowsLabel = buttons->add<Label>();
  rowsLabel->addStyleClass("caption");
  rowsLabel->setPixelSize(11.0f);

  auto refresh = [t, raw, rowsLabel] {
    rowsLabel->setText("当前可见行：" + std::to_string(raw->tableRowCount()) +
                       " / 节点总数：" + std::to_string(raw->nodeCount() - 1));
    t->rowsReset();
  };

  auto* btnExpand = buttons->add<PushButton>();
  btnExpand->setText("全部展开");
  btnExpand->clicked.connect([raw, refresh] {
    raw->expandAll();
    refresh();
  });
  buttonRow->addWidget(btnExpand);

  auto* btnCollapse = buttons->add<PushButton>();
  btnCollapse->setText("全部折叠");
  btnCollapse->clicked.connect([raw, refresh] {
    raw->collapseAll();
    refresh();
  });
  buttonRow->addWidget(btnCollapse);

  buttonRow->addWidget(rowsLabel, 1);
  refresh();

  return content->sizeHint().preferred;
}

// ================================================== 异步加载树形表格 =========
Size buildTablesAsyncPage(Widget* content) {
  BoxLayout* page = stack(content, kBandGap);

  Widget* top = band(content, page, /*stretch=*/1);
  BoxLayout* topRow = line(top, kPanelGap);

  auto* g = top->add<GroupBox>();
  g->setTitle("异步加载子节点 · 展开时才去取，取的过程画在展开箭头上");
  topRow->addWidget(g, 1);
  BoxLayout* gStack = stack(g, kItemGap);

  auto* panel = g->add<AsyncTreePanel>();
  gStack->addWidget(panel, 1);

  auto* status = g->add<Label>();
  status->addStyleClass("caption");
  status->setPixelSize(11.0f);
  status->setText("点开任意一个站点：箭头变成转圈，约 700ms 后子节点到达");
  gStack->addWidget(status);
  panel->statusChanged.connect(
      [status](const std::string& s) { status->setText(s); });

  auto* note = g->add<Label>();
  note->addStyleClass("caption");
  note->setPixelSize(11.0f);
  note->setText(
      "每第四次请求故意失败 —— 分支变成重试标记，再点一次才会重发，不会自己轮询");
  gStack->addWidget(note);

  return content->sizeHint().preferred;
}

// ======================================================== 大数据量表格 =======
Size buildTablesBigPage(Widget* content) {
  BoxLayout* page = stack(content, kBandGap);

  Widget* top = band(content, page, /*stretch=*/1);
  BoxLayout* topRow = line(top, kPanelGap);

  auto* g = top->add<GroupBox>();
  g->setTitle("大数据量 · 200 000 行，模型里一行都没有存");
  topRow->addWidget(g, 1);
  BoxLayout* gStack = stack(g, kItemGap);

  auto* panel = g->add<TablePanel>(std::make_unique<SyntheticModel>(200000));
  panel->setDesignHeight(460.0f);
  TableView* t = panel->table();

  std::vector<TableView::Column> cols;
  cols.push_back(kindColumn("#", 78.0f, CellKind::Index));
  cols.push_back(textColumn("位号", 120.0f, true));
  cols.push_back(textColumn("名称", 0.0f));
  cols.push_back(textColumn("区域", 120.0f));
  TableView::Column value = textColumn("瞬时值", 110.0f);
  value.align = HAlign::Right;
  cols.push_back(value);
  cols.push_back(kindColumn("状态", 90.0f, CellKind::Chip));
  cols.push_back(kindColumn("完成度", 150.0f, CellKind::Progress));
  cols.push_back(kindColumn("投用", 78.0f, CellKind::Switch));
  t->setColumns(cols);
  t->setSelectionMode(TableView::SelectionMode::Multi);
  t->rowsReset();
  gStack->addWidget(panel, 1);

  Widget* buttons = band(g, gStack);
  BoxLayout* buttonRow = line(buttons, kItemGap);

  auto* btnTop = buttons->add<PushButton>();
  btnTop->setText("回到顶部");
  btnTop->clicked.connect([t] { t->scrollToTop(); });
  buttonRow->addWidget(btnTop);

  auto* btnEnd = buttons->add<PushButton>();
  btnEnd->setText("跳到末行");
  btnEnd->clicked.connect([t] { t->scrollToBottom(); });
  buttonRow->addWidget(btnEnd);

  auto* btnMid = buttons->add<PushButton>();
  btnMid->setText("定位到第 100 000 行");
  btnMid->clicked.connect([t] { t->setCurrentCell(99999, 1); });
  buttonRow->addWidget(btnMid);

  auto* note = buttons->add<Label>();
  note->addStyleClass("caption");
  note->setPixelSize(11.0f);
  note->setText(
      "拖到任意位置都不会卡：一次绘制只向模型问看得见的那二十来行");
  buttonRow->addWidget(note, 1);

  return content->sizeHint().preferred;
}

}  // namespace showcase
