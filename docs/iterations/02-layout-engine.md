# 迭代 02：布局引擎（R2）

本轮范围：**T-01 护栏 → T-13 文档**，即整轮 R2。

| 任务 | 内容 | 本文 |
|---|---|---|
| T-01 | 四处承重墙的几何护栏 | §1 |
| T-02 | `setGeometry` 幂等短路 | §2 |
| T-03 | `Widget` 骨架（`sizeHint` / `naturalSize_` / `depth_` / 尺寸预算） | §3 |
| T-04 | 防重入四机制 | §4 |
| T-05 | `Layout` 抽象基类 | §5 |
| T-06 ~ T-10 | `BoxLayout`、`GridLayout`、六个控件的 `sizeHint()`、视觉基线、分配门禁 | §6 |
| T-11 | showcase 三页迁移（含嵌套 `sizeHint()`、九个新 `sizeHint()`） | §7 |
| T-12 | `TagId` / `TagRegistry` | §8 |
| T-13 | 文档 | §9 |
| — | 已知限制与未完成 | §10 |

---

## 0. PRD 摘要

**问题**：R2 之前全库只有绝对坐标。showcase 的 8 个页面各自写死一个 design size，塞进自己的 `ScrollArea`；窗口放大 = 多出一片空白，窗口缩小 = 出现滚动条。而参数表单、设置页这类内容会随语言/字体/翻译长度变化的界面，坐标一写死就必然在某个客户现场重叠。

**目标**（产品团队原文的三条，本轮以它们作为验收）：

| # | 目标 | 本轮结果 |
|---|---|---|
| O1 | 迁移页面里不再出现 `a + i * b` 形式的坐标算式 | ✅ 三页 `setGeometry` **75 → 0** |
| O2 | 迁移页面的尺寸由布局算出，不再返回硬编码 design size | ✅ 三页 `return content->sizeHint().preferred;` |
| O3 | 窗口放大时内容跟着长，不再是只增加空白 | ⚠️ **页面内部成立**（`showcase_pages.*` 三个用例断言），**页面根还没有**——见 §10.1 |

**非目标**（本轮明令不做）：`FormLayout`（`GridLayout::addRow` 就是它）、四个特殊容器的迁移、另外 5 个页面的迁移、`DataHub` / `AlarmList` 的位号字段迁移。

---

## 0.5 架构裁定 ADR-R2-01 .. 11

| # | 裁定 | 落点 |
|---|---|---|
| 01 | 布局**可选**，与绝对坐标**永久共存**；不装 `Layout` 的 widget 不为引擎付任何代价 | `detail::g_layoutHosts`，§3 零开销 |
| 02 | `setGeometry` 同值短路（M3） | §2 |
| 03 | `Layout` 是策略对象：只摆放宿主的**直接子节点**，不建 widget、不跨层、不跑应用代码 | `Layout.hpp` 正文 |
| 04 | 防重入四机制，且**记录而非致命**——运行六周的画面不能因为一块面板抖半个像素而死 | §4 |
| 05 | 溢出**只报告不强制**，且**不发信号** | `LayoutOverflow`，§5 |
| 06 | `kUnbounded = 1e7f` 而不是 `infinity`（`inf - inf` = NaN，污染且不可回溯） | `Layout.hpp` |
| 07 | 分配**确定性**：份额向下取整，余数给权重最大者，同权取最后一个 ⇒ 两趟相同输入产出逐位相同的几何（否则 30fps 下读作抖动） | `BoxLayout::spread` |
| 08 | `Layout` **不得持有 `Widget*`**（`host_` 除外），子项用**子节点索引**寻址 | `Layout.hpp` 结构性规则 1 |
| 09 | `sizeHint()` **不读 `geometry()`**；无 layout 的 widget 报 `naturalSize_`，一次锁存终身不改 | §3 |
| 10 | 跨行列（span）用**升序跨度单趟**求解，不用解算器、不迭代到不动点 | `GridLayout.hpp` 正文 |
| **11** | 容器持有的**树内裸指针**，在子树离场时由 `Widget` 统一通知失效：`onDescendantDetached` 向**整条祖先链**、按**每个离场节点**、在**解链与 `widgetDetached` 之前**无条件广播 | **[`../adr/adr-r2-11-detach-notification.md`](../adr/adr-r2-11-detach-notification.md)**；实现见 §11.11 / §11.12 / §11.13 |

> **一处编号更正**：`GridLayout.hpp` 原文把升序跨度这条写成 "ADR-R2-09"，与 `Widget.hpp` / `LineEdit.cpp` / 本文 §3 对 09 的用法（`sizeHint` 不读几何）冲突。三比一，本轮把 `GridLayout.hpp` 的引用改成 **ADR-R2-10**，其余不动。

---

## 1. T-01：护栏先行

`tests/widget/test_geometry_baseline.cpp`，6 个用例，覆盖四处承重墙：

| 组 | 被测 | 关键断言 |
|---|---|---|
| `AppWindow` | `relayout()` | header / content 按 `borderWidth()` 内缩切分；`setContent<T>` 填满 content；`contentResized` 载荷；改 header 高度、隐藏 header；`borderWidth()` 三条分支（frameless+border=1、border 关=0、framed=0）；**真实最大化态**（`maximize()` → `IsZoomed()`）下内缩归零、还原后恢复 |
| `WindowHeader` | `relayoutItems()` | trailing **组**整体右对齐到窗口按钮内沿；组内间距 6、`addTrailingGap` 只加宽它前面那一处接缝；改窗宽只平移组原点；去掉三个窗口按钮后组右沿=`width - padRight` |
| `ScrollArea` | `relayout()` | viewport 排除滚动条条带（10px/条，纵向赢平局）四种组合；`content()` 挂在 viewport 下；缩小内容重新 clamp 滚动位置 |
| `Shell` | `relayout()` | sidebar / titleBar / pageArea 三分；页面 host 内缩 `kGap=16`；收起导轨后全部重排；改 Shell 尺寸走 `onGeometryChanged` 同一条路 |

`Shell` 不是库代码，为此把 `examples/showcase/Shell.cpp` 编进测试可执行文件（`tests/CMakeLists.txt`）。

**这 4 处是 6 个 showcase 页面的承重墙，而原有 88 个用例一个都不碰它们。**

### 护栏有效性（注入验证，用文件备份而非 `git checkout`）

同时注入 4 处真实几何缺陷后重跑：

| 注入 | 结果 |
|---|---|
| `AppWindow::relayout` 去掉 `.deflated(b)` | 3 个 `geom_baseline.appwindow_*` FAIL |
| `WindowHeader::relayoutItems` 改成左对齐 | `header_right_aligns_the_trailing_group` FAIL |
| `ScrollArea::relayout` viewport 用整块 `localRect()` | `scrollarea_viewport_excludes_the_scrollbars` FAIL |
| `Shell::relayout` 页面 host 去掉 `kGap` 内缩 | `shell_lays_out_rail_title_and_page_area` FAIL |

**107 个用例，恰好 6 个失败，全部落在 `geom_baseline`。** 原有 88 个用例与新增 13 个 `layout_engine` 用例在四处几何全断的情况下依然全绿——这正是护栏补上的那个洞。注入已还原。

---

## 2. T-02：`setGeometry` 幂等短路 —— 8 处 `onGeometryChanged` override 逐条复核

改动：

```cpp
void Widget::setGeometry(const Rect& r) {
  if (r == geometry_ && !layoutDirty_) return;   // 新增
  ...
}
```

这是**行为变更**：同值 `setGeometry` 不再触发 `onGeometryChanged()`、不再触发两次 `update()`。
全仓 `onGeometryChanged` override 共 8 处，逐条复核「是否依赖同值也触发」：

| # | override | 做什么 | 是否依赖同值触发 | 结论与依据 |
|---|---|---|---|---|
| 1 | `Label::onGeometryChanged`<br>`src/widget/Label.cpp:54` | `if (wrap_) wrappedFor_ = -1.0f;` | **否** | 换行缓存的键是 `(width, styleGeneration)`（`rebuildLines` 开头 `if (wrappedFor_ == width && wrappedGen_ == gen) return;`）。同值 = 同 width、同 gen ⇒ 即使强制失效，重建结果逐字相同。跳过是纯节省。 |
| 2 | `ListView::onGeometryChanged`<br>`src/widget/ListView.cpp:66` | `scrollY_ = clamp(scrollY_, 0, maxScroll())` | **否**（架构已预核） | `maxScroll()` 只依赖 `localRect().height()`、`rowCount_`、`rowHeight_`。几何未变 ⇒ `maxScroll()` 未变 ⇒ `scrollY_` 已在区间内，clamp 是恒等。行数变化那条路由 `setRowCount` 自己 clamp，不经过几何。 |
| 3 | `TextArea::onGeometryChanged`<br>`src/widget/TextArea.cpp:42` | `layoutWidth_ = -1; rebuildLayout();` | **否** | `rebuildLayout()` 用 `avail = width - 2*kPad - (kScrollbarWidth+4)` 做键，`if (avail == layoutWidth_ && !lines_.empty()) return;`。同值几何 ⇒ 同 `avail` ⇒ 重建结果逐字相同。**注意**：这里被跳过的是「强制失效 + 重建」，而重建本身是 O(文本长度)，所以跳过反而修掉了一处同值重排的浪费。 |
| 4 | `TrendChart::onGeometryChanged`<br>`src/hmi/TrendChart.cpp:58` | `update()` | **否** | 纯重绘请求。但 `setGeometry` 本身在同值时也省掉了它自己的两次 `update()`，三次一起省——几何没变则**画面没变**，脏矩形本就不该扩。全仓无「靠同值 `setGeometry` 强制重绘」的调用点（重绘一律直接调 `update()`）。 |
| 5 | `AppWindow::onGeometryChanged`<br>`src/widget/AppWindow.cpp:81` | `relayout()` | **否** | 三条会改变布局但不改变 `AppWindow` 自身几何的路径，**全部自己显式调 `relayout()`**，不依赖几何触发：`header_->metricsChanged`（改 bar 高度）、`maximizedChanged`（改 `borderWidth()`）、`setHeaderVisible` / `setBorderVisible` / `setContent<T>`。`Window::handleResize` 只在真实尺寸变化时下发。已由 `geom_baseline.appwindow_*` 三个用例钉住。 |
| 6 | `ScrollArea::onGeometryChanged`<br>`src/widget/ScrollArea.cpp:71` | `relayout()` | **否**（架构已预核） | `setContentSize` 在 `content_->setGeometry(...)` 之后**显式**调 `relayout()`，不依赖同值触发。已由 `geom_baseline.scrollarea_*` 钉住（含 `setContentSize` 同尺寸再设的场景）。 |
| 7 | `WindowHeader::onGeometryChanged`<br>`src/widget/WindowHeader.cpp:193` | `relayoutItems()` | **否** | 所有会影响 trailing 布局而不影响 header 几何的 setter——`setHeight` / `setTrailingPadding` / `setItemHeight` / `setItemGap` / `setButtons` / `setButtonWidth` / `addTrailingItem` / `setTrailingItemWidth`——**每一个都自己调了 `relayoutItems()`**。逐个核对无遗漏。 |
| 8 | `Shell::onGeometryChanged`<br>`examples/showcase/Shell.cpp:316` | `relayout()` | **否** | 另两条路径（`showPage`、`titleBar_->toggleRail`）自己显式调 `relayout()`。`relayout()` 内部先 `pageArea_->setGeometry(...)` 再**显式**读 `pageArea_->localRect()` 计算页面矩形，不靠子节点的 `onGeometryChanged` 回调链。 |

**总结论：8 处全部不依赖同值触发，T-02 对现有 32 个控件与 6 个 showcase 页面行为等价。**
两个正面收益：#3 省掉一次 O(n) 文本重排；#4 省掉一次不必要的脏矩形扩张。

`Rect` 原本没有 `operator==`，本轮在 `core/Types.hpp` 补上（`constexpr`，四字段逐位比较）。
**刻意不做 epsilon**：`setGeometry` 用它判定「没动过」，epsilon 会把亚像素漂移一次次吞掉，最后树永远差一点点且没人再纠正它。NaN 边永远不等于自身 ⇒ NaN 矩形永远走慢路径，方向是安全的。

---

## 3. T-03：`Widget` 骨架

### ADR-R2-09：`sizeHint()` 不读 `geometry()`

```cpp
Size naturalSize_{};                    // 一次锁存，终身不改
SizeHint Widget::sizeHint() const {
  return {{0,0}, naturalSize_, {kUnbounded, kUnbounded}};
}
```

> **T-11 改了这个函数的前半句**：有 layout 的 widget 现在转发到 `layout()->measure(*this)`，没有 layout 的仍然是上面这段。理由见 §7。

锁存点两个，取先发生者：

* `setGeometry` 里首次拿到**非空**几何（`Size::isEmpty()` 为假）；
* `adoptLayout`（即 `setLayout<L>`）时，对 host 及其**当时已有的**每个子节点各锁一次——这些子节点在那一刻之前是手工定位的，那个手写尺寸就是它们「想要多大」的最佳陈述。

之后 `arrange` 再怎么改 `geometry_` 都不回写。用例 `layout_engine.natural_size_is_latched_once_and_never_rewritten` 直接钉住 ADR 描述的那个塌缩：连续缩小 → hint 不动 → 放大回去 → hint 仍不动。

### `depth_`：`add<T>` 单挂接点 + **子树 rebase**

ADR 写的 `raw->depth_ = depth_ + 1;` 单独用会漏掉一类真实场景：**容器在自己的构造函数里建子树**（`ScrollArea` 建 viewport+content，`AppWindow` 建 header+content）。那些节点是在 `raw->parent_ = this` 之前、从 0 开始编号的。所以 `add<T>` 里补一步：

```cpp
raw->depth_ = std::uint16_t(depth_ + 1);
if (!raw->children_.empty()) raw->rebaseSubtreeDepth();   // 只有容器付费，且只付一次
```

叶子节点（绝大多数）只多一次 `empty()` 判断。`kMaxTreeDepth = 64` 定在 `core/Types.hpp`，Debug 下 `add<T>` 与 rebase 各断言一次。
新增只读访问器 `Widget::depth()`——否则这个成员**不可测**，而不可测的设计就是坏设计。

### `sizeof(Widget)` 预算：184 → 200，**恰好 +16**

（MSVC x64 Release；用探针 `static_assert` 实测）

怎么放进去的：

| 新成员 | 字节 | 落位 |
|---|---|---|
| `std::unique_ptr<Layout> layout_` | 8 | `children_` 之后（析构逆序 ⇒ layout 先于 children 死） |
| `Size naturalSize_` | 8 | 尾部 |
| `bool layoutRunning_` / `bool layoutDirty_` / `std::uint16_t depth_` | **0** | 塞进 `focusPolicy_` 后面本来就存在的 5 字节 padding |

`static_assert` 不写死数字，而是对着一个「R2 之前成员集合」的镜像 struct 量（`Widget.cpp` 匿名 namespace 的 `WidgetSizeBeforeR2`）——否则 Debug 下 MSVC 容器各多一个指针，手抄的数字当场失真。

> **对 ADR 的一处偏离**：ADR 把 `LayoutOverflow lastOverflow_`（12 字节）列为 `Widget` 私有成员。连同上面三项一起放会是 **+24 ~ +32 字节**，突破 ADR 自己定的 ≤16 硬约束。取舍是把它移进 `Layout` 对象（它本来就是 `Layout::arrange` 的返回值），`Widget::lastLayoutOverflow()` 转发过去，无 layout 时返回一个文件作用域常量。公开 API 签名一字未改。详见交接报告。

### 零开销

所有内容驱动的挂钩（`add<T>` / `takeChild` / `setVisible` / `invalidateSizeHint`）第一步都测 `detail::g_layoutHosts`——进程内当前**拥有 Layout 的 widget 数**。本库 32 个控件与 6 个 showcase 页面一个都不用布局，所以它们付的是一次热全局的 load 加一次必然预测正确的分支：没有父链遍历、没有函数调用。用例 `the_host_counter_returns_to_zero` 钉住这个计数器的两道门（换 layout、销毁宿主）都配平。

`layout_ == nullptr` 时 `setGeometry` 的增量只有：一次 `Rect` 比较（短路收益远大于它）、一次 `naturalSize_.isEmpty()`（锁存后恒为假）、一次 `layout_` 判空。5 张 shape golden 与 1 张 text golden 逐位不变。

---

## 4. T-04：防重入四机制

```cpp
bool Widget::runLayoutIfAny() {
  if (!layout_) return true;
  if (layoutRunning_) { layoutDirty_ = true; return true; }        // M1
  if (g_layoutDepth >= kMaxTreeDepth) {                             // M4
    detail::layoutDepthExceeded(this); return true;
  }
  LayoutGuard guard(this);
  layoutRunning_ = true;
  int rounds = 0;
  do {
    layoutDirty_ = false;
    const LayoutOverflow result = layout_->arrange(*this, contentRect());
    if (!guard.alive()) return false;        // 宿主在 arrange 里被销毁了
    layout_->lastOverflow_ = result;
  } while (layoutDirty_ && ++rounds < 2);     // 至多重跑 1 次
  if (layoutDirty_) detail::layoutNotConverged(this);
  layoutDirty_ = false;
  layoutRunning_ = false;
  return true;
}
```

| 机制 | 实现 | 用例 |
|---|---|---|
| **M1 重入闩** | `layoutRunning_`；嵌套请求只留一张便条，由 do/while 收走 | `m1_a_nested_request_defers_instead_of_recursing`（恰好 2 次 arrange，0 次嵌套）<br>`m1_a_layout_that_never_settles_is_recorded_not_fatal`（永不收敛 ⇒ 仍是 2 次，记 1 次 `notConverged`，控件之后照常可用） |
| **M2 下行单向红线** | Debug 下 `g_arrangeHost`：**只在控制流直接位于 `Layout::arrange` 内**时非空；`setGeometry` 一开始断言 `parent_ == g_arrangeHost`，随即用 `ArrangeSuspend` 把它清空，所以子节点 `onGeometryChanged` 里跑的应用代码不会被误算成「布局还在写」 | 断言型，无用例（触发即 abort） |
| **M3 幂等短路** | 即 T-02 | `setting_the_same_geometry_does_no_work` |
| **M4 pass 深度上限** | `g_layoutDepth`（`LayoutGuard` 加减）≥ `kMaxTreeDepth` 时中止本次 pass 并记 `depthExceeded`；**不 abort** | 见「未验证点」 |

**为什么 M2 的断言不能照 ADR 字面写**：`assert(!layoutPassActive() || parent_ == currentLayoutHost())` 里 `layoutPassActive()` 在**整个 pass 期间**为真，包含 arrange 通过 `setGeometry → onGeometryChanged` 间接跑起来的所有应用代码。`ScrollArea` 一旦被放进某个 Layout，它自己 `onGeometryChanged → relayout() → viewport_->setGeometry()` 就会撞上这条断言——而那是完全合法的既有代码。所以改成「直接位于 arrange 内」的精确判定，语义与 ADR 想抓的东西一致，但不会误伤。

**LayoutGuard**：完全复刻 `BubbleCursor` / `BubbleGuard`——匿名 namespace 里的侵入式链表 `LayoutCursor{host, outer}` + `g_layouts` + `cancelLayoutsOn()`，`~Widget` 与 `announceDetached` 各加一行。理由和 bubble 完全一样：几何路径上**已经存在**运行应用代码的出口（`AppWindow::relayout` 里的 `contentResized.emit(cs)`）。多一个宿主销毁的出口就多一次 UAF，而现在这个库对「对象死在自己的回调里」只有一个答案，就是这个 pattern。

`runLayoutIfAny` 返回 `bool`（宿主是否存活）是对 ADR `void` 签名的一处必要加强：`setGeometry` 拿到 `false` 就立刻 `return`，不再往下走 `onGeometryChanged()` 和 `update()`——否则 LayoutGuard 只保住了 `runLayoutIfAny` 自己那一帧，调用帧照样 UAF。用例 `the_guard_survives_a_host_destroyed_inside_its_own_arrange`。

`arrange` 遍历子节点**按索引 + 每步重读 `children()`**（测试里的 `StackLayout` 就是这么写的，作为给 T-06 的施工样板）——`announceDetached` 的 A3 索引平移问题已经被证明过一次，不重犯。

### 执行时机

* **几何驱动**（`setGeometry`）→ 同步，在 `onGeometryChanged` **之前**。显式代码最后跑、代码赢。
* **内容驱动**（`add` / `takeChild` / `setVisible` / `invalidateSizeHint` / `setLayout` / `Layout::invalidate`）→ `markLayoutDirty()` 向上标脏，当前无 pass 时从**最顶层脏宿主**跑一次。
* **不引入帧边界调度器。**

`markLayoutDirty` 只给**真的拥有 layout** 的祖先置脏位。给其余节点置位会让那个位永远清不掉（没人为它们跑 pass），进而永久废掉 `setGeometry` 的幂等短路——而那个位存在的全部理由就是这个短路。

---

## 5. T-05：`Layout` 抽象基类

`include/geeyoou/widget/Layout.hpp` + `src/widget/Layout.cpp`，签名照 ADR 实现。

**ADR-R2-08（结构性约束）**：`Layout` **不得持有任何 `Widget*`**，`host_` 除外，且一次绑定终身不变。子项一律用**子节点索引 `std::size_t`** 标识。理由写在头文件正文里，作为 code review 检查点：索引错 = 摆错位置（可见、可测）；指针错 = UAF（不可见、月级后爆）。

**`LayoutOverflow` 为什么不发信号**：发信号 = 在布局 pass 中间运行应用代码 = 刚在 M1/M2 禁掉的那种重入。调用方在 pass 结束后读 `Widget::lastLayoutOverflow()`。

**`kUnbounded = 1e7f` 而不是 `infinity`**：布局一趟里尺寸要反复加减缩放，`inf - inf` 是 NaN，之后每个碰到它的矩形都被污染，且回溯不到源头。1e7 逻辑像素约等于 5000 块 4K 屏并排，现实中够不着。

对 ADR 类定义的两处**新增**（非修改）：

* `protected: void invalidate();` —— `setMargins` / `setSpacing` 走它，子类自己的 setter 也要走它（T-06 的 `BoxLayout::setStretch` 没有它无法工作）。它做两件事：`onInvalidated()` + `host_->relayout()`。
* `public: const LayoutOverflow& lastOverflow() const;` —— `lastOverflow_` 移进 `Layout` 之后的读取口，`Widget::lastLayoutOverflow()` 转发到它。

---

## 6. T-06 ~ T-10：两个具体布局、六个 `sizeHint()`、视觉与分配门禁

（上一会话交付，本节为补记。）

### `BoxLayout`

一行或一列，两件事值得先知道：

* **spacing 只出现在两个相邻 widget 之间**。spacer（`addSpacing` / `addStretch`）两侧不吃 spacing 也不传导，所以 `addSpacing(20)` 就是 20 像素而不是 20 + 两份 spacing。隐藏的 widget 用同样的方式退出链条，**连同它那道缝一起带走**——把一行中间的控件隐藏起来，缺口会合上，而不是留一个双倍宽的洞。
* **分配是精确且确定的**（ADR-R2-07）：每项拿到 preferred 之后剩下的空间按 stretch 权重切分，份额向下取整，余数给权重最大的那一项，同权取最后一个。

`scratch_` 是每项 6 个 float 的**扁平**缓冲（不是 `vector<struct>`），只 `clear()` + `resize()`，容量只增不减——`DataHub::scratch_` 的同一个手法。跨轴的 min/max 也存在里面，好让摆放循环不必再问一次 `sizeHint()`（对 Label 来说那是一整次文本度量）。

### `GridLayout`

行、列、跨行列。跨度是这里唯一真正的设计难题：`colSpan=3` 的单元格没有告诉你其中任何一列该多宽，它陈述的是三列**之和**的要求。ADR-R2-10 的答案不是解算器也不是迭代到不动点（两者都可能在需要连续开机数月的屏幕上不终止），而是**升序跨度单趟**：span-1 定各列的 min/preferred → span-2 问它覆盖的两列加上中间的 spacing 够不够、不够就把差额摊下去 → span-3 …… 每批只读更早的批次定下来的结果，因此一趟访问每个单元格一次、按构造终止、每次答案相同。差额优先摊给**可拉伸**的列（作者说了那几列可以长），一列可拉伸的都不覆盖时才摊给全部。

`addRow(label, field)` 是每张参数表单的那对标签/字段：追加一行，列 0 放标签、列 1 放字段，并把列 1 设成会长的那列（显式 `setColumnStretch(1, n)` 仍然优先，这里只填零）。**`FormLayout` 因此被否决**——它就是这个，多一个类只是多一份要维护的对齐规则。

### 六个控件的 `sizeHint()`

`Label` / `PushButton` / `CheckBox` / `LineEdit` / `SpinBox` / `GroupBox`。三条共同的取舍，后来 T-11 的九个新 hint 全部沿用：

1. **不从当前值测量。** `SpinBox` 量的是 `[min, max]` 两端加后缀，不是此刻显示的数字——否则一屋子仪表会在泵一变速时整页重排，而操作员正在读那一页数字。
2. **文本宽度不让步。** `CheckBox` 的 `min.width == preferred.width`：把它压窄不会重排，只会把标签切一半，而"确"不是"确认"的缩小版，是另一条指令。挤不下就由 `lastLayoutOverflow` 报出来。
3. **`GroupBox` 是唯一的容器**，所以是唯一 hint 可以超出自身装饰的：它报自己 layout 需要的尺寸 + 边框。

### 视觉与分配门禁

* 3 张新 golden：`box_layout_stretch`、`grid_layout_spans`、`layout_overflow`。加上原有 5 张 shape + 1 张 text，共 **8 shape + 1 text**。
* `tests/widget/test_layout_alloc.cpp`：**首趟可以分配**（scratch 得从某处来），**子节点数量变化后可以再分配一次**（容量只增不减），**item 数不变、只有几何变的一趟必须零分配零释放**。同时数 free，因为"释放后立刻按同样大小重新分配"这种失败模式只看分配数会读成"每趟一次"，其实是两次。

---

## 7. T-11：showcase 三页迁移

### 两处引擎改动（架构裁定，本轮拍板）

**① `Widget::sizeHint()` 现在会转发到自己的 layout。**

```cpp
SizeHint Widget::sizeHint() const {
  if (layout_) return layout_->measure(*this);            // 容器为自己的内容代言
  return {{0,0}, naturalSize_, {kUnbounded, kUnbounded}};  // 其余不变
}
```

不改就迁不动：T-11 的表单必然是 GroupBox 里套 GridLayout 再放进页面的 BoxLayout，而基类原来只报 `naturalSize_`，于是**每个嵌套容器都按"某次手写几何"报尺寸**，整棵树由构造顺序而不是内容定尺寸。`GroupBox::sizeHint()` 里那段 `if (layout()) ... else ...` 随之塌成一次基类调用。

⚠️ **T-05 那条 `CHECK_EQ(lay->measures, 0)` 的语义变了**：它原来的说法是"引擎从不调用 `measure()`"，现在不成立。改成的说法是「**没有 measure pass**」——arrange 一个宿主时直接读子项的 hint 并摆放，不存在一趟自上而下、没人消费的 measure；所以处在布局树**顶端**、没人测量它的宿主，它自己的 `measure()` 被调用的次数就等于应用调用的次数，即 0。新增用例 `layout_engine.a_nested_host_reports_what_its_own_layout_needs` 把「作为别人的 item 时会被测量」这半边钉住。

**② `GroupBox` override 新的 `protected virtual Widget::layoutRect()`。**

引擎交给 layout 的矩形原本是 `localRect()` 减 layout margins，于是 GroupBox 里的第一行会画在标题上，除非每个调用点都写 `setMargins({12, 34, 12, 12})`。三种改法：

| 方案 | 否决理由 |
|---|---|
| 每个 GroupBox 手写 margins | 把一个控件的私有常量抄进每一个用到它的文件，且哪天有人忘了就画到标题上 |
| `setLayout` 时自动塞一份默认 margins | ①标题是在 `setLayout` 之后设的，而有没有标题决定顶部是 12 还是 34；②作者从此无法在边框之外再加自己的留白（一调 `setMargins` 就把边框补偿覆盖掉）；③把"边框多厚"和"作者要多少留白"这两件事揉成一个数 |
| **`layoutRect()` 虚函数**（采用） | 边框归控件、margins 归作者，两者**叠加**；`contentRect = layoutRect() − margins`，默认实现就是 `localRect()`，其余 31 个控件一字未改 |

### 九个新的 `sizeHint()`

迁移一开工就撞上：`Separator` / `RadioButton` / `ToggleSwitch` / `Slider` / `ProgressBar` / `TextArea` / `IconButton` / `ListView` / `ScrollArea` 都没有 `sizeHint()`，放进竖直 box 里高度全是 0——不是隐晦的 bug，但也不是编译错误。九个都补上了，`tests/widget/test_size_hints.cpp` 钉住的是**答案的形状**而不是像素（像素跟主题走，冻住它只会在下次换字体时假红）：

* `Separator` 是全库唯一有真正 `max` 的控件：一条 40px 高的分隔线是 39px 的虚无，而且那 39px 是从邻居那里拿走的。
* `ListView` / `ScrollArea` 报的是**视口**而不是内容。报内容会有两个后果：2000 行的报警缓冲变成 52000 像素的 hint，外层布局忠实地报出一个 51000 像素的溢出——一个真实的数字，描述的却不是问题；而且内容本身一旦也是布局出来的，这就成了循环。
* `IconButton` 取基类的**高度**做两个方向：`PushButton` 的宽度是"标签加内边距"，而这个控件不画标签（`onPaint` 从不碰 `text_`），照抄会得到一个中间飘着图标的宽药丸。取高度既保证和同一排 PushButton 齐平——那是唯一必须一致的那一维——又顺带得到正方形。

### 三页的形状

| 页 | 结构 | `setGeometry` |
|---|---|---|
| `PageWidgets` | 竖直 box：两条横带 + 状态行；带内每块面板一列；参数设定与滑块两块用 `GridLayout` | 33 → **0** |
| `PageInputs` | 同上；六个按钮变体是一行六个 stretch 相同的 item，而不是 `14 + i * 102`；12 个图标按钮是 2×6 的 `GridLayout` | 27 → **0** |
| `PageOps` | 横向 box：左列（实时值 grid + 参数表单）+ 报警列表（stretch 2）；报警列表拿 stretch，窗口变大就多显示几条报警而不是多一片毡布 | 15 → **0** |

三页都以 `return content->sizeHint().preferred;` 结束——尺寸是**算出来的**，源码里没有这个数字。

`tests/widget/test_showcase_pages.cpp` 把三页编进测试二进制（和 `Shell.cpp` 早就在里面是同一个理由：不是库代码，但它的几何回归只能靠"跑起来看一眼"发现），逐页断言 O1（没有任何 widget 停在 `(0,0,0,0)`）、O2（`design == content.sizeHint().preferred`）、O3（给更大的矩形 ⇒ 摆出来的总宽变大；再缩回去 ⇒ 逐位回到原值，即 ADR-R2-09 要防的那种塌缩没有发生）。

**另外 5 个页面一行未改**——它们是"共存真的能用"的活证据，不是待办事项。

四个不迁移的容器里，`AppWindow` / `WindowHeader` / `Shell` 一行未改；**`ScrollArea` 有一处偏离**：给它加了 `sizeHint()`（约 18 行，纯新增一个方法）。理由与影响面：`PageOps` 的参数表单是一个放进 `BoxLayout` 的 `ScrollArea`，没有 hint 它报的是空的自然尺寸，于是整块表单塌成边框。这不是"迁移 `ScrollArea`"——它的 `relayout()` / `needVBar` / `needHBar` 一个字没动，而 `sizeHint()` 只有**外层 Layout** 会去问，本轮之前全库没有任何 `ScrollArea` 的父节点装着 Layout，所以对既有代码是惰性的。若架构团队认为红线应按字面执行，删掉这一个方法即可，其余不受影响。

---

## 8. T-12：`TagId` / `TagRegistry`

纯新增，**零现有调用点改动**：`DataHub::channel` 与 `AlarmRecord::tag` 的 `std::string` 字段本轮不动（一轮里同时做布局迁移和位号迁移 = 两次大迁移的爆炸半径叠加，见 `docs/roadmap.md` R2.5）。

| 决定 | 理由 |
|---|---|
| `uint32`，`Invalid = 0` | 0 是默认构造/memset 后字段里的值，它必须永远不指向真实点位——和 `Icon::None = 0` 同一条 |
| 保留区放**最高位**（`FirstExternal = 0x8000'0000`），不是 `Icon::FirstCustom` 那样的低位空档 | 两者问的不是同一个问题。`Icon` 有内置集合，问的是"这是不是我内置的"，那是对一个每次发版都在变长的列表做区间比较；`TagId` 没有内置集合，任何人真正要问的只有"这是不是外部系统的"，那应该是一次**位测试**，且不会被将来任何一次发版作废 |
| registry **可实例化** + 一个进程默认实例（函数内 static，非单例类） | 单例会挡住"四台 PLC 三个厂商、其中两台都把第一路模拟量叫 AI0"这种正常情况，也会让每个 intern 过名字的用例依赖它前面跑过的所有用例。本仓库已经为这个错误付过一次账：`Theme` 是全局量，于是每个 golden 用例都得先 `resetStyling()` |
| **只增不删**：没有 `remove()` / `clear()`，id 不回收 | 屏幕连开数月，id 一旦可回收，所有已经躺在报警记录、趋势缓冲、待发协议帧里的 `TagId` 就在某一刻悄悄变成了别的意思。一个没用的名字占几十字节；一个错的数字是屏幕上的错误读数 |
| 名字**只存一份**，地址稳定 | map 持有文本，`byId_` 持有指向 map **键**的指针。这只在 node-based 容器上成立：rehash 移动的是节点而不是节点里的字符串。同样的写法配 `vector<string>` 会在第一次扩容时炸——短名字住在 string 对象自己肚子里（SSO），会跟着搬家。这类 bug 在开发机上一个测试都不会红 |
| 透明哈希（`is_transparent`） | 驱动每解一帧就要解析一次位号；对一个开机起就已知的名字每次查询都构造一次 `std::string`，是那种"看不见，直到帧率看得见"的浪费。用例 `tag.resolving_a_known_name_allocates_nothing` 钉住 1000 次查询零分配 |

`intern("")` 返回 `Invalid` 而不是给空串一个身份：空串是"配置项没填"，给它真身份就分不清"没配位号"和"有个点位就叫空串"了。`name()` 对未知 id / 外部 id 返回空视图而不是断言——`name()` 是显示层调的，显示空位号的屏幕仍然能读。

---

## 9. T-13：文档

* `docs/architecture.md`：新增 **§3.12 布局引擎**；§2 分层图加入 `Layout / BoxLayout / GridLayout` 与 `TagId / TagRegistry`；§3.11 补线程结论；**§4「明确不做」第一条改写**——布局引擎已实现，但原理由**只推翻了一半**：绝对坐标不是过渡期而是一等公民，组态区（仪表/趋势图/管线，按工艺流程图定位）本来就该用绝对坐标。顺带修掉一条早已与 §3.10 冲突的旧条目（"滚动与虚拟化不做"，而 `ScrollArea` / `ListView` 早就在库里）。
* `docs/roadmap.md`：R2 标记已交付并对齐实际范围；**删掉 `FormLayout`**（被 `GridLayout::addRow` 取代）；把位号字段迁移拆成 **R2.5**。

---

## 10. 已知限制 / 未完成

### 10.1 ⚠️ O3 只做到一半：页面根还不会跟着窗口长（最重要的一条）

页面**内部**已经完全弹性：给 `content` 一个更大的矩形，面板、按钮、报警列表都会跟着长（三个 `showcase_pages.*` 用例断言）。但**谁给 `content` 那个更大的矩形**，目前还是 `Shell::showPage` 里那一行：

```cpp
const Size design = pg.builder ? pg.builder(pg.host->content()) : Size{};
pg.host->setContentSize(design);   // 建页时算一次，之后再不更新
```

链路是 `Shell::relayout → ScrollArea::setGeometry → ScrollArea::onGeometryChanged → relayout() → viewport 改尺寸`，而 **content 没有人碰**。所以窗口放大时，viewport 变大、content 不变 ⇒ 还是多出一片空白，只是这片空白现在出现在页面**外面**而不是里面。

**本轮不修的原因**：修它必须动 `ScrollArea` 或 `Shell`，而这两个都在本轮明令不动的四个容器里（"其余 5 个页面 + 4 个容器 0 改动"是出门条件之一）。**这不是我判断不该修，是判断不该在本轮擅自修**。

建议的最小改法（约 5 行，**opt-in**，对现有 5 个绝对坐标页面零影响，因为它们的 `content` 没有 layout）：

```cpp
void ScrollArea::relayout() {
  const Size vp = viewportSize();
  viewport_->setGeometry({0.0f, 0.0f, vp.width, vp.height});
  // 内容自己会布局时，滚动范围就不该由调用方写死：至少铺满视口，
  // 不够时按内容要的尺寸滚动。没有 layout 的 content 一行都不受影响。
  if (content_->layout()) {
    const SizeHint h = content_->sizeHint();
    content_->setGeometry({0.0f, 0.0f, std::max(vp.width, h.preferred.width),
                           std::max(vp.height, h.preferred.height)});
  }
}
```

同一条限制在 `PageOps` 里出现了第二次：参数表单那个内层 `ScrollArea` 也得有人给它内容尺寸，所以那一页留着**唯一一处** `setContentSize`——但它的实参是 `scroll->content()->sizeHint().preferred`，是**算出来的**，不是敲进去的。上面这 5 行落地后，这一处也可以删掉。

### 10.2 文本控件每趟布局都重新度量（归 R3）

布局引擎自身零分配，但每趟 arrange 会向每个子项要一次 `sizeHint()`，而 `Label::sizeHint()` 要走 `measureText` ⇒ 一个 `BLGlyphBuffer` + 一次 shaping。拖窗口边缘时这是每帧一次。

* **别拿 `AllocGuard` 去量它**：它读出来是 0，而那是漏报——测试框架换掉的是全局 `operator new`，Blend2D 走的是自己的运行时分配器（malloc 之上）。用例 `layout_alloc.text_is_re_measured_on_every_pass_r3` 因此改数**度量次数**：10 趟 × 6 个 Label = 60 次，一趟一个一次，`BoxLayout` 已经把跨轴的值缓在 `scratch_` 里避免问第二次。
* 正确修法是控件上按 `(text, fontSize, styleGeneration)` 的宽度缓存 —— 那是文本度量的事，**R3 本来就要重做文本栈**。R3 落地时这个用例会变红，那是它预期的结局。
* 直接后果：`PageOps` 的 ticker 每秒 10 次 `Label::setText` ⇒ 每次 `invalidateSizeHint` ⇒ 整页重跑一趟布局。视觉上稳定（值标签在可拉伸列里右对齐），但是白花的 CPU。

### 10.3 建页期间的重复布局趟数

页面构建时每次 `add<T>` / `addWidget` 都会标脏并从最顶层脏宿主跑一趟，于是建一页 n 个 item 大约跑 n 趟、总计 O(n²) 次 `sizeHint()`。一页只建一次且是懒建（首次打开该页时），实测感知不到；但正确的做法是一个批量作用域（`Widget::LayoutBatch` 之类，作用域内只标脏、退出时跑一趟）。**本轮不做**：它会改变现有用例里 `arranges` 的计数语义，不适合放在一轮的最后一个任务里。

### 10.4 沿用自 T-01 ~ T-05 的边界

1. **M2 断言未被用例覆盖**：命中即 `abort`，写不出「断言应触发」的进程内用例。要覆盖须用 `d7.*` 那种子进程模式，本轮未做。
2. ~~**M4 深度上限未被用例覆盖**~~ —— **已解决（E5）**。原文的判断是"要触发需要 64 层嵌套 Layout 宿主，而 Debug 下 `add<T>` 的 `kMaxTreeDepth` 断言会先拦下建树本身"，这句话**只对某一种触发方式成立**：`g_layoutDepth` 数的是**帧**，不是**树**。八十棵互不相干的**单节点**树、每棵的 `arrange()` 去跑下一棵的 `performLayout()`，照样能把帧叠到 65 层，而每棵树的深度都是 0，`add<T>` 无话可说。用例 `layout_engine.m4_a_chain_of_unrelated_hosts_reaches_the_same_ceiling`，**三条腿全跑**。
   原来那条 Release-only 的用例（`m4_a_pass_that_nests_past_the_ceiling_is_recorded_not_fatal`）**保留**：它走的是这个上限**被设计出来时**针对的那个形状（宿主的孩子本身又是宿主），与新用例互补。
   **顺带的教训**：这条"未跑通"挂了两轮，原因是"要触发它需要 X"这句话从来没有被复核过——**它测的是树，要测的是递归**。同一条教训随即让 M-2 的用例成立（§11.10）。
3. **`naturalSize_` 的深层子孙锁存**：`setGeometry` 里的锁存无条件生效，但如果一个 widget 在**进程里还没有任何 Layout** 时拿到几何、之后祖先才装上 Layout，那么只有 `adoptLayout` 的**直接**子节点会被补锁，更深的子孙要等它们下一次 `setGeometry`。迁移的三页里不可观测（页面是建在已有 layout 的宿主下的）。
4. ~~**`Widget::relayout()` 与三个容器的 `relayout()` 同名**~~ —— **已解决**：基类那个方法改名为 `Widget::performLayout()`。理由写在 `Widget.hpp` 上：`AppWindow` / `ScrollArea` / `Shell` 各自已有 `relayout()`，其中 `ScrollArea::relayout()` 还是 `private`，因此同名的基类成员会被三个容器静态隐藏、被第四个挡在访问权限外；给唯一一个没有调用点的方法改名，比改一个已发布的 API 便宜。
5. **多线程**：与全库一致，布局引擎只在 UI 线程使用。`g_layouts` / `g_layoutDepth` / `g_layoutHosts` / `g_arrangeHost` 都是普通 `static`，非 `thread_local`——与 `g_bubbles` 同一条理由（`docs/architecture.md` §3.11）。
6. **`ScrollArea` / `ListView` 的 hint 是常数**（320×200 / 六行）。这是刻意的（见 §7），但那两个常数本身没有出处，只是"够用"。将来如果需要"最多长到内容那么大"，得先想清楚循环怎么断。

---

## 11. 【REM3 · E1】跨越应用代码的帧守卫 —— 设计定案（第 3 版）

> 状态：**第 2 版经 `eng-frontend-ui`（Leo）复签放行（Q2/Q3/Q6 无条件签字，合并判据成立）。本版按复签的一处实质异议 + 两处注释补正修订，并按放行后的范围把契约改写收进 E2。**
> 复签带来的三处改动，逐条可查：
> * **判据 2 改写**（§11.1）：「表外读者为零」→「**没有任何读者的返回值参与引擎决策；纯诊断读者不计**」。原措辞被本节自己引入的 `deathWatchDepth()` 证伪，是文档自相矛盾，不是设计错误；两条结论一字不改。同步改到 §11.2 的注释、REM3-G6、§11.7 2c。
> * **`markLayoutDirty` 的机制更正**（§11.1 候选 E 第一条）：脏标记**不会丢**（`:613-618` 在 `:623` 之前就打完了），**丢的是执行**。原文的「would be swallowed / 被吞掉」会让下一个人看到 `layoutDirty_` 还在就判定这条否决理由是错的。同步改到 `Widget.cpp:113-134` 的注释。
> * **`takeChild` 第二道门入表**（§11.4 #13b）：`:416 childRemoved(index)` 在守卫作用域**之后**，是一扇真门。它今天非危险，但唯一理由是"其后再没有任何语句解引用 `this`"——一条此前哪里都没写的隐式不变量。E2 在 `:416` 上方补注释。
>
> 另按复签补入两条正面论据：合并的**附加收益**（一帧三游标同链，§11.1）与**维护条件**（需要"detach 也取消"的新现场不得挂这条链表，§11.1）。`removeChild` 免检的理由换成 Leo 那版（§11.4 #11）。
>
> 本节只定形状与语义，不含实现。实现是 E2，落点是 E3/E4，枚举表与门覆盖校验是 E5，用例是 E6。
>
> **行号基准**：本节所有 `src/`、`include/` 行号取自 **HEAD = `06db811`**（含 E17 的 `e9c283a` / `c53fd66` / `24bbc28`），且已逐条对当前工作树复核过。
> ⚠️ 本版第一稿写的哈希（`7b2e1f8`、`d3965bb`）已随仓库的一次全历史署名重写失效，上面是重写后的。第一稿抬头记的那份「未提交的实验性回退」是那次重写把历史版本的树 checkout 进工作目录时的中间态，**不是真的回退**：`g_detaches` / `DetachGuard` 从未离开过库（`Widget.cpp:84`、`:134`、`:301`、`:347`、`:400`、`:431`）。本节按当前工作树写，没有任何一条结论建立在"E17 被回退"这个前提上。
>
> **命名**：Elena 裁定 `R3-` 前缀改 `REM3-`（roadmap 的 R3 是文本引擎轮次，撞名）。本版已全部改完，与 `Widget.cpp:264`、`:300`、`:375` 及 `tests/widget/test_removal.cpp:527` 已经在用的 `REM3-` 一致。

### 11.0 缺陷复述：契约写错了主语

R2 第 2 轮把契约写在 `Layout.hpp:168-174`：「**实现 `Layout` 的类**在每次重入应用代码后检查 `hostAlive()`」。那段文字本身是对的，形状也是对的（"AFTER EVERY CALL THAT RE-ENTERS APPLICATION CODE, sizeHint() INCLUDED"），**错的是主语**：它只约束 `Layout` 的实现者。24 扇门里 16 扇因此合格，漏掉的不在 `Layout` 子类里，而在 **`Widget` 子类**里——它们是应用代码的**调用者**，不是 `Layout` 的实现者：

| 现场 | 门 | 门后干了什么 |
|---|---|---|
| `GroupBox.cpp:53` | `Widget::sizeHint()` → `layout_->measureFor` → 子项 `sizeHint()` override → 应用代码 | `:54` 读 `layout_`、`:62` 读 `title_`、`:65` 在已释放对象上做虚调用 `style(styleState())` |
| `ScrollArea.cpp:158` | `content_->sizeHint()` → 同上 | `:159` 读 `geometry_`、`:164`/`:165` **向已释放的 viewport / content 写入 16 字节 `Rect`** |

`Widget.cpp:490-494` 的 `GeometryGuard` 保的是 **`setGeometry` 自己那一帧**；上面两帧在它**下面**（`relayout()` 由 `onGeometryChanged` 调起），不查任何游标。**守卫加在了调用者身上，真正跨越信任边界的却是被调用的那一帧。**

**为什么不能用「门前预读」了事**（`Widget.cpp:61-66` 记过这条的局限）：预读只治**读**。`ScrollArea:164/165` 是**写**——把 16 字节写进已释放的 viewport。预读一个字都救不了。

**为什么不能改成「测量期间禁止改树」**：那等于在 `sizeHint()` 期间作废 D7（`core/Signal.hpp`：槽可以销毁别的对象），且没有 `abort` 就无法执行——而 ADR-R2-04 明令**记录而非致命**。方向反了。
（注意：对 `styleState()` / `onPaint()` 这一族**能不能**立契约，是另一个问题，见 §11.6 REM3-RES-2，那里我的结论与这里相反。）

**本轮唯一可接受的形状**（eng-lead 拍板）：**一处设施 + N 处调用 + 一张枚举表 + 一条能变红的用例**。第 1 版把 N 写小了（Leo：「N 是错的」），本版 §11.4 按可机械执行的谓词重扫，N 从 2 个检查点变成 **5 个检查点 / 3 个函数 / 2 个文件**，其余命中登记定级排轮。

---

### 11.1 六问定案

#### Q1 —— 这条 hint 帧游标挂在哪条链表上？

**第 1 版答案（新开第四条链表 `g_hints` + `HintGuard`）作废。**
**本版答案：挂在 E17 已经建好的那条链表上，把它按取消策略改名为 `g_deathWatch`，守卫改名 `DeathWatch`。**

Leo 的 B1/B2 两条都成立，而且它们指向同一个结论。先把判据摆全——第 1 版的矩阵只有三行，漏掉了两个候选，其中一个恰好是对的那个。

**拆表判据（两条，缺一不可）**

> **判据 1（取消策略）**：两种取消策略 ⇒ 两条链表。`cancelOn` 是按整条链表走的，一条链表做不到两套策略。
> **判据 2（决策读者）**：链表若有**返回值参与引擎决策**的读者（任何形如"现在有没有一个 X 帧站在谁身上"、且引擎**据此改变行为**的谓词），则混入异类帧会让那个谓词静默答错，而**那个错误答案会被执行** ⇒ 必须独占。
> **纯诊断读者不计**：只被人和用例读、不出现在任何库内分支条件里的读者，答错也没有消费者。

⚠️ **本版措辞更正（Leo 复签的唯一实质异议）**：第 1 版把判据 2 写成"表外读者为零"，而本节自己又为合并后的链表引入了 `detail::deathWatchDepth()`（Q2 的可测性论证）——**按字面，那就是一个表外读者，文档自相矛盾**。实质上不矛盾，因为杀掉 `g_layouts` 的从来不是"有人读"，而是"**有人读了之后引擎据此改变行为**"：`markLayoutDirty` 因此少跑一次 pass、停车场因此晚排空一轮、`currentLayoutHost()` 因此被断言。所以判据按**读者的返回值参不参与引擎决策**来分，而不是按有没有读者来分。这样 `deathWatchDepth()` 合法（它谁也不指挥），`layoutPassActive()` 依然把 `g_layouts` 排除在外（它指挥 `Layout.cpp:77` 的排空点），两条结论一字不改。

**反过来说**：两条链表若**取消策略相同**且**都没有决策读者**，它们就是同一份东西的两个名字——多一条链表 = 多一个 `~Widget` 里要记得写的 `cancelOn` = 多一个下次会忘的地方（`Widget.cpp:74-75` 自己写的那句"a second hand-rolled copy of this pattern is a second place to forget a check"，说的是机制，但同样的道理逐字适用于策略）。

**完整选项矩阵（五个候选，逐个判）**

| # | 候选 | 取消策略 | 决策读者 | 判决 |
|---|---|---|---|---|
| A | 新开第五条链表 `g_hints` | 只在 `~Widget` | 无 | **否决**（与 F 逐条相同，纯冗余；这是第 1 版的答案） |
| B | 复用 `g_geometries` | `announceDetached:282` **取消** + `~Widget:342` | 无 | 否决（判据 1） |
| C | 给 `LiveCursor` 加 kind 字段，`cancelOn` 按 kind 过滤 | — | — | 否决（见下） |
| D | 复用 `g_bubbles` | `announceDetached:281` **取消** + `~Widget:339` | 无 | 否决（判据 1；另：`moveTo()` 语义是沿父链行走，与测量帧无关） |
| E | 复用 `g_layouts` | **只在 `~Widget:343`**，`announceDetached:283-296` 明令不取消 | **有三个** | 否决（判据 2，见下） |
| **F** | **复用 E17 的 `g_detaches`，按策略改名 `g_deathWatch`** | **只在 `~Widget:347`** | **无**（`deathWatchDepth()` 是纯诊断，不计） | **采用** |

**为什么不是 B / D（判据 1，决定性）**：`announceDetached:281-282` 对这两条链表都执行 `cancelOn`。而按 Q4 的结论，hint 帧在**仅 detach** 时**必须不取消**。共用 = 一条链表被迫执行两套策略。

**为什么不是 E（这是第 1 版真正漏掉的那个，也是 Leo B2 点名的那个）**：Leo 说得对——`g_layouts` 的**取消策略已经就是 hint 帧要的那一套**（`announceDetached:283-296` 明文写着不取消，`~Widget:343` 取消），所以第 1 版"两种取消策略 ⇒ 两条链表"这条理由**够不着 E**。杀掉 E 的是判据 2，而 `g_layouts` 的决策读者有三个，且三个都会被一个 hint 游标弄错——注意这三条的共同点不是"被读了"，而是**读到的答案立刻变成一次行为改变**：

* `markLayoutDirty:623` 的 `if (g_layouts) return;`——测量期间挂一个游标，等于告诉全引擎"有 pass 在跑"，于是**这一次重排不跑**。
  ⚠️ **机制的精确说法（第 1 版写错一个字，Leo 复签指出）**：`markLayoutDirty` 是**先**在 `:613-618` 把 `layoutDirty_` 打在从这里到根的每一个 host 上，**才**在 `:623` 遇到 `if (g_layouts) return;`。**脏标记不会丢，丢的是执行。** 在**真实** pass 下这样做是对的：那个 pass 退栈时会在 `runLayoutIfAny:699` 的 `while (layoutDirty_ && ++rounds < 2)` 上回头重读这面旗，还它一趟，然后在 `:715` 清掉。而一个**假的**游标（detach 帧或测量帧）背后**没有 pass 会退栈**，没有人回头读那面旗，于是这次重排要等下一次**不相干的**触发（那时链表恰好是空的）才跑，子树在此期间保持上一帧的几何。
  第 1 版把这写成"会被这句吞掉"，字面意思是脏标记丢了；将来有人去看 `layoutDirty_` 发现它还在，会据此判定这条否决理由是错的——而它是对的，错的只是那个动词。这就是 `announceDetached:288-296` 已经记过一次的"子树几何冻结"，只是从另一个方向到达（那一段是 `layoutRunning_` 永远为真，这一段是没有人来还债）。
* `detail::layoutPassActive():722`——`Layout.cpp:77` 的停车场排空点读它（`g_measureDepth == 0 && !layoutPassActive()`），一个测量期的假 pass 会把停车的 `Layout` 对象**多押一轮**才释放。
* `detail::currentLayoutHost():724-725`——它会返回**被测量的那个 widget**，而 `tests/widget/test_layout_engine.cpp:553` 正在断言这个值。

再加一条形状理由：`LayoutGuard`（`Widget.cpp:166-193`）不只是一个游标，它还挂着 `g_layoutDepth` 与 `DrainOnUnwind`；复用它等于在每次测量的边界上排空停车场。**这三条与 E17 在 `e9c283a` 里否决 `g_layouts` 的理由逐条相同**（`Widget.cpp:113-134`）——同一个候选，第二次被同一组理由杀掉，这一致性本身是判据 2 成立的证据。

**为什么不是 C**：给一个在三条热路径上被构造的 `struct` 加字段，并在 `cancelOn` 的每个节点上加一次分支，换 8 字节 BSS。方向反了。

**为什么是 F（正面论证，两个事实都可 grep 复核）**：

1. **取消策略逐字相同**。`g_detaches` 的注释（`Widget.cpp:132-133`）自己写着 "cancelled by `~Widget` and by nothing else, which is exactly *dead, not merely detached*"。而 Q4 对 hint 帧的结论一字不差就是这句。
2. **决策读者：零**。全库对 `g_detaches` 的操作只有 `~Widget:347` 的 `cancelOn` 和三处 `DetachGuard` 的 `alive()`（`:301`、`:400`、`:431`）——**没有一个出现在库内的分支条件里去指挥引擎干别的事**。合并后新增的 `deathWatchDepth()` 是**纯诊断**（只被用例读），按修订后的判据 2 不计。grep 命令写在这里，E5 可复核：
   `grep -n "g_detaches\|DetachGuard\|g_deathWatch\|DeathWatch" -r src include tests`

**所以：`g_detaches` 与 `g_hints` 是同一条链表的两个名字。** 保留两条 = 两个 8 字节全局 + `~Widget` 里两行 `cancelOn` + 两个要记住的名字，换来的是零。合并。

**改名（B1 的另一半：`HintGuard` 是第二次给错主语）**

第 1 版按"门"命名（`g_hints`），理由是 R2 翻车的根因是给错主语。这条理由是对的，但**第 1 版自己没执行到底**：一旦这条链表同时承载 detach 帧（`takeChild` 从来没调过 `sizeHint()`）与 hint 帧，"门"就不是它们的公因子了。**它们的公因子是取消策略**，所以名字必须按策略取：

| 旧名（不再使用） | 新名 | 含义 |
|---|---|---|
| `g_detaches` / `g_hints` | **`detail::g_deathWatch`** | 站在别人身上的在飞帧，**只被真实销毁取消，detach 不取消** |
| `DetachGuard` / `HintGuard` | **`detail::DeathWatch`** | 上面那条链表的唯一守卫类型 |

`g_bubbles` / `g_geometries` 仍按帧种命名，因为它们的策略是另一套（detach 也取消），名字与策略不冲突。四条链表因此读作：**两条"还在原地吗"（bubbles / geometries），一条"pass 还在跑吗"（layouts，有决策读者所以独占），一条"人还在吗"（deathWatch）**。

**维护条件（Leo 复签时提的，写进本节以便执行）**：这条链表的正确性**完全建立在"两类帧的取消策略相同"之上**。将来任何新现场若需要「detach 也取消」，**不得**挂到 `g_deathWatch`——它必须回到 `g_bubbles`/`g_geometries` 那一族，或者自开一条。**按策略命名而不是按门命名，正是让这条规则可执行的原因**：一个叫 `g_detaches` 的链表上挂一个测量帧要靠人记住"其实它不是按门分的"，而一个叫 `g_deathWatch` 的链表，只要问"我这帧是不是只怕死、不怕搬家"就能当场判。判错的代价见 §11.1 候选 B/D。

**一个守卫类型，不是两个。** 合并后 `DetachGuard` 与 `HintGuard` 的差别只剩访问形状，而事实是**三处 detach 现场（守卫在 `:301`、`:400`、`:431`）的四次检查（`:305`、`:318`、`:402`、`:437`）也只用 `alive()`**，没有一处用 `node()` / `moveTo()`。所以直接让 `DeathWatch` 采用 Q3 的私有继承形状：**只导出 `alive()`**。Q3 那一手（把"游标只比较、永不解引用"变成编译器强制）因此从 2 个新现场扩大到 **5 个现场**，包括 E17 刚落地的三处。这是本次合并最实在的收益。

**合并的一个附加收益（Leo 复签时提的正面论据，本版补入）**：站在 `this` 上的**一个**游标**不足以**覆盖 `ScrollArea::relayout`。`:158` 那扇门里，应用完全可以只销毁 `content_` 而**根本不碰** ScrollArea——`sa->content()->parent()->removeChild(sa->content())` 一句就够（Q5 的反例之一），此时 `this` 的游标照样是 `alive()`，而 `:165` 依然向已释放的 content 写 16 字节。**合并恰好支持一帧多游标**：`LiveGuard` 是**一个对象一个游标**，`DeathWatch` 是纯栈成员，所以一帧挂三个守卫（`this` / `viewport_` / `content_`）就是同一条链表上前后相连的三个节点，`cancelOn` 一次遍历全覆盖，§11.5 实测的"三个守卫构造合并为 9 条指令"正是这个形状。若按第 1 版每种帧一条链表，这三个守卫要么被迫同链（那就得承认按门命名是假的），要么要三条链表和 `~Widget` 里三行 `cancelOn`。

**合并的代价，写明，不藏**：

* `announceDetached` 的 `parent` 参数取**非常量引用**，`Widget.cpp:264-269` 的注释把理由写成"游标是 `Widget*`，替代方案是一次不该随便出现的 `const_cast`"。`DeathWatch` 的构造函数收 `const Widget*`（Q3）之后，这条理由不再成立，**那段注释会变成陈旧断言**。E2 必须在同一次改动里改掉它（改注释，不改签名——签名归 E18），已写进 §11.7。
* 合并后 `~Widget` 少一行、全局少一个，但 E2 的 diff 会碰到 E17 刚写的那一段（`Widget.cpp:113-134` 的"第四条链表"注释块整段重写）。这是**改名 + 重写理由**，不是改逻辑；E2 的验收要求这段注释按判据 1/2 重写，而不是简单替换标识符。

#### Q2 —— `LiveCursor` 要不要提升到 `detail::` 并进 `Widget.hpp`

**要。** 提升三样进 `Widget.hpp` 尾部的 `namespace detail`：`LiveCursor`、`LiveGuard<>` 模板、`extern LiveCursor* g_deathWatch`。

**必须提升的机械理由**：守卫是**纯栈对象**（Q6），栈对象的完整类型必须在使用点可见；而使用点在 `GroupBox.cpp` / `ScrollArea.cpp`，不在 `Widget.cpp`。

**`g_bubbles` / `g_geometries` / `g_layouts` 三条链表继续留在 `Widget.cpp` 的匿名 namespace**——`LiveGuard` 是以「变量引用」为模板参数的模板，C++11 起允许内部链接实体作模板实参，所以模板搬到头文件不强迫任何一条既有链表跟着搬。只有 `g_deathWatch` 需要外部链接。**四条链表里三条仍然是私有的**，暴露面增量 = 一个类型 + 一个模板 + 一个指针。

**可见性等级：`detail::`，不是 `protected`。**

| 方案 | 判决 |
|---|---|
| **`geeyoou::detail::DeathWatch`** | **采用** |
| `protected: class Widget::DeathWatch`（嵌套类） | 否决 |
| `friend class GroupBox; friend class ScrollArea;` + `Widget` 私有 helper | 否决 |

* **为什么不是 `friend`**：验收标准明令「不需要任何 `friend` 声明」，且 friend 是 O(N) 增长——每多一个需要守卫的调用点就要回头改一次 `Widget.hpp`，而 §11.4 已经点名了至少 4 个后续轮次的调用点。
* **为什么不是 `protected` 嵌套类（唯一有分量的对手）**：
  1. **可测性决定**。E5/E6 要断言「链表退栈后回到空」，需要读 `g_deathWatch` 的深度。`protected` 嵌套意味着诊断口只能通过继承或 `friend` 拿到；`detail::` 里放一个 `deathWatchDepth()` 与既有 `detail::parkedLayoutCount()`、`detail::layoutDiagnostics()` 完全同构。**不可测的设计就是坏设计。**
  2. **`protected` 买不到它看起来买到的封装**。库外的应用控件全都 `: public Widget`，`protected` 对它们一样可见。两个方案在「应用代码能不能拿到」这件事上结果相同，而 `detail::` 不要求应用先继承。
  3. **一致性**。`detail::g_layoutHosts` 已经是 `Layout.hpp` 里的 `extern` 变量，`detail::parkLayout` / `layoutDiagnostics` 同理。
  4. `protected` 嵌套还会让 `BubbleGuard` 等别名变成 `Widget::LiveGuard<g_bubbles>`——把 `Widget` 的调用方永远用不到的模板塞进每个读头文件的人的视野里。

**库外应用代码能不能拿到？** 技术上能——`geeyoou::detail::` 没有访问控制，和今天的 `detail::g_layoutHosts` 一模一样。这是**有意的、且被上面第 2 条论证过是免费的**。约定层面 `detail::` 就是「不对外承诺」的标记，`docs/architecture.md` 已有这条。

#### Q3 —— const 正确性

`GroupBox::sizeHint() const` 里 `this` 是 `const GroupBox*`，而 `LiveCursor::node` 是 `Widget*`（`g_bubbles` 的 `moveTo()`/`node()` 需要非常量）。

| 方案 | 判决 |
|---|---|
| a. 把 `LiveCursor::node` 改成 `const Widget*` | 否决 |
| b. 按 constness 模板化 / 另开一套 const 游标 | 否决 |
| **c. `DeathWatch` 构造函数内 `const_cast`，游标只比较不解引用** | **采用** |

* **为什么不是 a**：`dispatchMouse/dispatchKey` 要 `w->onMouse(local)`（非常量）、`bubble.moveTo(w->parent_)`。改成 `const Widget*` 就把 `const_cast` 推进**全库重入压力最大的那条路径**，还得推进两处。
* **为什么不是 b**：`cancelOn` 就得走两种节点类型，`~Widget` 里就得有八行而不是四行。**这正是"第二份手抄"。**
* **c 为什么安全**，逐条可独立验证：
  1. **游标存的是身份，不是访问路径。** 全库对游标 `node` 的操作只有三种：`cancelOn:137-141` 里的 `c->node == doomed` 比较、`c->node = nullptr` 写空、`alive()` 里的 `!= nullptr`。**没有任何地方解引用它。** 这不是新规矩——`Widget.cpp:244-246` 的 `stillAChild()` 已经写着 *"Only pointer VALUES are compared -- `w` is never dereferenced"*。
  2. **`const_cast` 只有在通过它去修改一个真正 const 的对象时才是 UB。** 被测量的 widget 从来不是 const 对象——它躺在父节点 `children_` 的 `unique_ptr<Widget>` 里，`const` 只是当前这条访问路径的属性。何况我们连写都没写。
  3. **把"不解引用"从评审规则变成编译期规则。** `DeathWatch` **私有继承** `LiveGuard<g_deathWatch>` 并只 `using` 出 `alive()`——**不导出 `node()`，不导出 `moveTo()`**。私有继承不是"另起炉灶"：零新逻辑、零新成员、对象布局逐位相同。
  4. `const_cast` 因此**全库只出现一次**，就在它的理由注释旁边；三个既有守卫的 `LiveGuard(Widget*)` 签名一字不改。
  5. **（本版新增）** 由 Q1 的合并，这条编译期事实同时覆盖 E17 的三处 detach 现场——它们此前用的是公开继承的 `LiveGuard`，`node()` 可达。合并后**全库没有任何一条路径能从这条链表里拿到指针**。

#### Q4 —— `~Widget:331` 加不加？`announceDetached:281-282` 加不加？

**`~Widget`：加（HEAD 已有 `:347`，合并后就是它本身）。`announceDetached`：不加。**

**`~Widget` 为什么加**：`~Widget` 是"每一次真实离场唯一都会经过的门"，而游标记的就是一个**指针**，指针恰恰在这里失效。子孙不需要遍历：`children_` 在函数体之后才析构，每个子孙都会自己走到这里取消自己的游标——**这一条正是 Q5 的多游标方案能免掉任何树遍历的前提。** 落位：与既有三行并排（`Widget.cpp:339-347`），在 `layout_` 停车块之前。停车块读的是 `layoutRunning_` / `buffersBusy_`，不读游标链表，先后对正确性无影响。

**`announceDetached` 为什么不加**，正面规则 + 反证：

* **正面规则（REM3-G4）：游标在指针死掉的地方取消，而 detach 不杀指针。** `takeChild` 把节点交给调用方的 `unique_ptr`，节点**活着**，它的成员**可读**，它的子树**跟着它走**。取消一个活对象上的游标，等于在没有任何安全收益的前提下伪造一个降级答案。

  ⚠️ **出处更正（Leo B-Q4a，我接受）**：第 1 版说这条是"独立论证，不引用 `g_layouts` 在 `announceDetached` 里的理由"。核对后：`Widget.cpp:283-296` 的注释有**两段**，第 1 版只刻画了第 2 段（`:288-296`，讲 `layoutRunning_` 被卡死），而**上面这条 REM3-G4 正是第 1 段（`:284-286`）的逐句改写**——"`node` is only being ANNOUNCED, not destroyed -- takeChild may still hand it back alive -- and `~Widget` is the one door every departure really goes through"，连两个分句的顺序都一样。所以正确的说法是：
  > **REM3-G4 不是新论证。它是 `Widget.cpp:284-286` 已有的规则，E1 做的是两件事：(i) 把它从"`g_layouts` 这一条链表的例外"推广成"所有帧游标的默认策略"；(ii) 补上一条可执行的判据（见下），因为原注释只说了结论没给判据。**
  E1 确实不依赖第 2 段（`layoutRunning_` 那段），本设施没有任何标志位；但"不依赖第 2 段"不等于"独立于整条注释"，第 1 版的措辞把后者说成了前者。**声称独立论证而实际是既有注释的换词版，正是 R2 那种"看起来复核过、其实没有"的形状**，所以这条按实改写而不是保留。

* **判据（Leo B-Q4b，措辞更正；结论不变）**：
  > **判据：帧的剩余工作会不会读到（或写到）已经释放的内存？会 ⇒ 取消；不会 ⇒ 不取消。**
  > detach **不释放任何东西**，所以默认答案是"不取消"；`g_bubbles` / `g_geometries` 是**例外**，因为它们的剩余工作要走一条 detach 刚刚作废的父链（气泡要读 `w->parent_` 继续往上；`setGeometry` 要 `window():882-886` 沿 `parent_` 找窗口报脏），而那条链上的节点**是别人的**、随时可能被释放。父链是**例外的原因举例，不是判据本身**。

  第 1 版把判据写成"剩余工作是否依赖刚被作废的父链"，**这条措辞被自己的例子证伪**：`GroupBox::sizeHint():65` 的 `style(styleState())` 走父链，`ScrollArea:164/165` 的 `setGeometry` 内部也走父链（`update()` → `window()`），按字面执行会把 hint 帧判成"应该取消"，与结论相反。第 1 版靠"答案可能不同但良定义"救回来，而那句用的其实是**另一条**判据。现在把那条判据扶正，把父链降级为举例。

* **判据的正式组成部分（从"一处诚实的副作用"提升上来）**：一个 detached widget 的剩余工作**允许算出与挂着时不同的答案**，只要那个答案是良定义的。`GroupBox::sizeHint()` 门后的 `style(styleState())` 会走父链（`isEffectivelyEnabled()` / `styleParentSubject()`）；detach 之后 `parent_` 已被置空，样式级联少了祖先 ⇒ **答案不同，但那正是一个 detached widget 的正确答案**。判据要分的是"不同"与"已释放"，不是"不同"与"相同"。
* **反证一（`ScrollArea`）**：若取消，一次合法的、从子项 `sizeHint()` override 里发起的 `takeChild(sa)` 会让 `relayout()` 中途 `return`，viewport 停在**上一帧**的尺寸，而 `sa` 活着、马上要被重新挂上去——**一个静默冻结的视口**。这与 `g_layouts` 当年取消后造成的"子树几何永久冻结"是同一类损害。
* **反证二（`GroupBox`）**：若取消，`sizeHint()` 会把一个只由门前局部量拼出来的 hint 交给一个**活着的** Layout，而没有任何东西标记它是假的。**从"会崩"退化成"会静默答错"，后者更难抓。**（这条反证也是 Q7 判 `nullptr` 不得静默降级的依据。）

#### Q5 —— `this` 存活是否蕴含 `viewport_` / `content_` 存活？

**不蕴含。这不是不变量，证不出来，所以走"加固"这一支。**

反例（全部只用**公开 API**）：

```cpp
sa->content()->parent()->removeChild(sa->content());   // content_ 悬垂，ScrollArea 活着
sa->removeChild(sa->children()[0].get());              // viewport_ 连同 content_ 一起悬垂
```

`content()`（`ScrollArea.hpp:25`）、`children()`、`removeChild` 都是公开的，`ScrollArea` 没有任何钩子会把这两个裸指针置空。

**加固形状**：帧要解引用哪些指针，就守哪些指针，**外加一次成员重读**（Q7）。逐个检查点写死在 §11.3 的表里。

**为什么不是"只守 `viewport_`，靠 `this` 死则 viewport 必死来省两个"**：那个蕴含要靠一次对 `takeChild`/`removeChild` 顺序的**穷举**才能成立（`sa->takeChild(viewport_)` 后再销毁 `sa`，viewport 活而 `this` 死），而本缺陷族**五次复发，每一次都是某个穷举少了一个 case**。

> **REM3-G2：守卫你将要解引用的每一个指针，并在门后重读每一个你将要解引用的成员指针。链表节点是栈上的两个指针，穷举一个 case 的成本远高于多压一个游标。**

**残留**：守卫是**帧作用域**的。它救下这一帧，但不修复 `viewport_`/`content_` 在 `ScrollArea` 余生里继续悬垂——`contentSize()`（`ScrollArea.hpp:28`）、`scrollOffset()`、`content()` 在下一次调用时照样 UAF。见 §11.6 REM3-RES-1，那是 E14 的题目。

#### Q6 —— 纯栈、零分配

**是，且是结构性的，不是靠约定。**

`LiveCursor` = 两个指针，作为 `DeathWatch` 的**成员**（不是指针、不是 `optional`、不是容器），`DeathWatch` 只以**具名局部量**出现。没有 `vector`、没有 `new`、没有 TLS 查找（普通 `static`，与 `g_bubbles` 同一条理由：树是 UI 线程独占，`docs/architecture.md` §3.4）。嵌套天然由链表承载。

soak 的 `liveAllocs` 序列（`test_layout_soak.cpp` 里 `s.liveAllocs = allocCount() - freeCount()` 那一行；**不记行号**——E8 正在重写这个文件）因此**逐位不动**。`parkedLayoutCount()` 同理不受影响：本设施不进停车场，它不持有 `Layout`。

配套禁令写进 E3 验收：**`DeathWatch` 的拷贝/移动构造与赋值全部不可用**（继承自 `LiveGuard` 的 `= delete`，私有继承后派生类隐式删除），**不得有工厂函数返回它，不得放进任何容器**。游标节点的地址就是它的身份，一旦被搬动链表就断。

#### Q7（本版新增，Leo B3）—— `DeathWatch(nullptr)` 是什么行为？

**定义为「视同已死」，且 Debug 期 `assert`。三条，缺一不可：**

1. **不是 UB，不是崩溃**：构造函数接受 `nullptr`，游标 `node` 为空 ⇒ `alive()` **自构造起恒为 false** ⇒ 该帧立刻走降级路径。
2. **Debug 期 `assert(w != nullptr)`**：因为**在这个构造函数上**，空是调用方的 bug 而不是正常状态。理由可判定：任何一个**无条件**解引用的现场，上守卫都是因为它**马上要解引用那个指针**；指针为空时该现场**在到达守卫之前就已经解引用过它了**（`ScrollArea::relayout:157` 的 `content_->layout()` 就在门与守卫之前）。所以在这类现场，「空成员」必须由现场的**空检查**处理，而不是由守卫**冒充**成一次死亡。
3. **Release 期不静默**：`alive()` 为 false 会让现场走降级分支，而降级分支**必然记一次 `detail::frameDegraded()`**（§11.3 表里逐点写死）。所以 Release 下 `nullptr` 表现为「诊断计数 +1」，不是 ADR-R2-04 意义上的无痕迹。

> **⚠️【E19 更正，Leo 复签通过】第 2 条的主语必须是"无条件解引用的现场"。** 上面原本写的是"任何一个上守卫的现场"，那对**有条件**解引用的**可选成员**为假，三条门就是证据：
> * **`AppWindow::fill_`** 在 `setContent<T>()` 跑之前**恒空**，而 `relayout()` 把它摆在 `if (fill_)` 之内；
> * **`Layout::host_`** 在 park 之后**恒空**，而 `invalidate()` 把 `performLayout()` 摆在 `if (host_)` 之内；
> * **`Window::focus_`** 在没有焦点时就是空，而 `clearFocus()` **就是** `setFocusWidget(nullptr)`——空是这个 API 的正常入参，不是漏检。
>
> 这三处仍然要为**非空那一支**问同一个问题，而它们**不能**靠构造一个"契约说这是调用方 bug"的东西去问。
> **REM3-G7 的处方（现场自己先做一次空检查、然后早退）在这三处用不上**，因为它要求现场**能整帧放弃**：没有内容的窗口仍要摆标题栏，park 掉的 layout 仍要跑 `onInvalidated()`，没有焦点的窗口仍要把焦点交出去。
> ⇒ 扩展的是**构造方式**，不是语义：`DeathWatch(p, MayBeNull{})` 走**同一条链表、同一套取消、同一个 `alive()`**，空**仍然**读作已死（第 1、3 条逐字照旧）。差别只有一处——**它不 `assert`**，因为在这些现场空不是 bug。作为交换，现场欠一句 `(p0 && !pw.alive())`（`p0` 是门前捕获值）：**门前就是空的成员不得查 `alive()`**，否则一个**健康**帧会被判成降级并跳过它本该做的事，那才是真的行为改变。见 §11.2 的 API 形状与 §11.14。

> **REM3-G7：空成员是空检查的事，不是守卫的事。**
> 守卫回答的问题是「我记下的这个对象死了没有」；它**不**回答「这个成员现在还指着东西没有」。E14/E15 的 `onDescendantDetached` 把 `content_` 置空之后，`ScrollArea::relayout` 的入口必须先有 `if (!content_ || !viewport_) return;`，然后才轮到守卫。
> **适用边界（E19 补）**：本条的"早退"形态只在**现场能整帧放弃**时可用；放弃不了的可选成员走 `MayBeNull` 游标 + `(p0 && !pw.alive())`，那**不是**本条的例外，而是本条在"不能整帧放弃"时的另一种落法——空**仍然**没有被守卫冒充成死亡，只是这一帧不为它降级。

**为什么不新增一个 `nullGuards` 计数器**：区分不出额外信息。Debug（测试全量跑 Debug + ASan）会在 `assert` 上直接变红并指出现场；Release 只需要"不静默、不崩"。多一个字段要多一条 E6 断言去维护它，收益为零。**这一条如果 Leo 不同意，是可以单独翻的**——加字段是纯增量改动，不影响其它任何决定。

---

### 11.2 API 形状（唯一权威；E2 照此实现，不得增删）

> 本版取代评审前的版本。与第 1 版的差异：`g_hints`→`g_deathWatch`（且它是 E17 `g_detaches` 的改名而非新建）、`HintGuard`→`DeathWatch`（合并 `DetachGuard`）、`hintCursorDepth()`→`deathWatchDepth()`、`hintFrameCancelled()`→`frameDegraded()`、`hintFramesCancelled`→`framesDegraded`、新增 `nullptr` 语义。
> **【E19 增补，Leo 复签通过】** 本节增列 `explicit DeathWatch(const Widget*, MayBeNull)` 与 tag 类型 `MayBeNull`——**纯新增，不改任何既有成员**。本节自称"唯一权威"，所以实现里有而这里没有的东西，本身就是一次违约；这一条补的正是那个洞。

落点：`include/geeyoou/widget/Widget.hpp` **尾部**，`class Widget` 定义之后，`namespace geeyoou::detail` 内——与 `Layout.hpp` 尾部放 `detail::g_layoutHosts` / `detail::layoutDiagnostics()` 的位置对齐。**不新建头文件**（本仓库没有 `detail/` 头文件目录，取消点又全在 `Widget.cpp`）。

```cpp
namespace detail {

// ONE in-flight stack frame standing on a widget it does not own.  Moved here
// from Widget.cpp's anonymous namespace, unchanged: the frames that need it now
// live in widget SUBCLASSES (GroupBox, ScrollArea), not in Widget.  Three of
// the four lists stay private to Widget.cpp; only g_deathWatch needs external
// linkage.
struct LiveCursor {
  Widget* node = nullptr;
  LiveCursor* outer = nullptr;
};

template <LiveCursor*& List>
class LiveGuard { /* verbatim from Widget.cpp:88-108 */ };

// Frames cancelled by DESTRUCTION and by nothing else -- "dead, not merely
// detached".  Named after the cancellation POLICY, not after a door: a
// takeChild has never called sizeHint(), a measurement has never detached
// anything, and what those frames have in common is only this policy.  The two
// lists that are cancelled by announceDetached as well (g_bubbles,
// g_geometries) are named after their frame kind because for them the two
// namings agree.
//
// NOTHING reads this list to DECIDE anything.  alive() on the guards below is
// the whole of it; deathWatchDepth() reads the depth, but only a test ever
// reads THAT, and no branch in the library turns on either.  A PRECONDITION,
// not an observation: the day something needs to ask "is a detach in flight on
// X?" in order to do something differently, the list splits FIRST -- see
// REM3-G6 and the three readers of g_layouts that make it the counter-example.
extern LiveCursor* g_deathWatch;

class DeathWatch : private LiveGuard<g_deathWatch> {
 public:
  // const_cast, in the ONE place in the library that needs it, and safe by
  // construction: this cursor is COMPARED and never dereferenced -- which is
  // why alive() is the only thing exported.  node() and moveTo() are
  // deliberately NOT re-exposed.
  //
  // A null widget is DEFINED as already dead (alive() is false from here on),
  // never undefined and never a crash -- but it is a caller bug, because every
  // site that guards a pointer is about to dereference it.  REM3-G7.
  explicit DeathWatch(const Widget* w)
      : LiveGuard<g_deathWatch>(const_cast<Widget*>(w)) {
    assert(w && "a null here means the caller's own null check is missing");
  }

  // Tag for the constructor below.  A TYPE rather than a bool, so a call site
  // cannot say `true` and leave the next reader working out what is true.
  struct MayBeNull {};

  // The same guard for an OPTIONAL member -- one whose null is a legitimate
  // state of the object rather than a missing null check (AppWindow::fill_,
  // Layout::host_, Window::focus_).  The assert above is right for a member the
  // frame dereferences UNCONDITIONALLY and simply false for one it dereferences
  // CONDITIONALLY; see Q7.
  //
  // SEMANTICS ARE UNCHANGED, which is the point of reusing the class instead of
  // writing a second one: same list, same cancellation, same alive(), and a
  // null still reads as dead from construction on.  What the site owes in
  // return is that its check must NOT consult alive() for a member that was
  // ALREADY null in front of the door -- there is nothing there to die, and a
  // frame that gave up over it would record a degradation that did not happen.
  // The shape is `(p0 && !pw.alive())`, with p0 the pre-door capture.
  explicit DeathWatch(const Widget* w, MayBeNull)
      : LiveGuard<g_deathWatch>(const_cast<Widget*>(w)) {}

  using LiveGuard<g_deathWatch>::alive;
};

// Depth of the death-watch list.  Diagnostic only, and the reason it exists is
// that a list which does not come back to zero is a guard somebody kept alive
// past its frame -- which is untestable without this.
std::size_t deathWatchDepth();

// A guarded frame found the tree moved under it and bailed.  Recorded, never
// fatal (ADR-R2-04), and it is what makes E6's regression assert a POSITIVE
// fact instead of "it did not crash".  Field lives in LayoutDiagnostics.
//
// ONE call per FRAME, not per check (REM3-G8), and it covers both reasons a
// frame bails: a cancelled cursor and a member pointer that changed across the
// door.  Deliberately NOT named after either one.
void frameDegraded();

}  // namespace detail
```

**四个成员**（第 4 个由 **E19 扩展**，见 §11.14；**语义、尺寸、取消策略均未变**）：

| 成员 | 可见性 | 作用 |
|---|---|---|
| `explicit DeathWatch(const Widget*)` | public，inline | 构造：把栈上游标压入 `g_deathWatch`（Release 5 条指令，见 §11.5；Debug 多一条 `assert`） |
| `explicit DeathWatch(const Widget*, MayBeNull)` | public，inline | 同上，但**给可选成员用**：接受空、**不 `assert`**。空仍读作已死；现场欠一句 `(p0 && !pw.alive())`（`p0` 是门前捕获值），**不得**对门前就是空的成员查 `alive()`。理由见 Q7，落点见 §11.14 的 CP-A1/CP-A2 与 N1/N4。指令数与上一行相同（Release 5 条，Debug 少一条 `assert`） |
| `~DeathWatch()` | public，隐式（继承自 `LiveGuard`，非虚） | 析构：弹出游标（2 条）。非虚是对的——私有基类，永不通过基类指针 delete |
| `bool alive() const` | public，`using` 导出，inline | 查询：`cursor_.node != nullptr` |

**`struct MayBeNull {}`**：空的 tag 类型，嵌在 `DeathWatch` 内（调用形态 `detail::DeathWatch::MayBeNull{}`）。**是类型而不是 `bool`**——`DeathWatch(p, true)` 会让下一个读者去猜"什么为真"。两个构造函数**都是 `explicit`**：双参构造没有隐式转换风险，标它只为与上一行一致。`sizeof(DeathWatch)` 不变（空基类之外没有新成员）。

拷贝/移动：**四个全部删除**（`LiveGuard` 已删，私有继承后派生类隐式删除）。

**`Widget` 本体改动**：`~Widget` 的第四行 `cancelOn(g_detaches, this)`（HEAD `:347`）改名为 `cancelOn(g_deathWatch, this)`。**不加任何成员** ⇒ `Widget.cpp:36` 的 `static_assert(sizeof(Widget) <= sizeof(WidgetSizeBeforeR2) + 16)` **原样成立**。
**`LayoutDiagnostics` 加一个 `std::uint32_t framesDegraded = 0;`**——它是 `Layout.hpp:273-280` 里的诊断结构，不在 `Widget` 里，与尺寸预算无关。饱和自增，与既有四个计数器同写法（`Layout.cpp:190-206`）。

**两个使用点都不需要 `friend`**：`GroupBox` / `ScrollArea` 公开继承 `Widget`，`const GroupBox*` → `const Widget*` 与 `Widget*` → `const Widget*` 都是隐式转换，`detail::` 无访问控制。

**`assert` 的头文件依赖**：`Widget.hpp:10` 已经 `#include <cassert>`，`Layout.hpp` 已被 `Widget.hpp:23` 包含，所以 `frameDegraded()` 的声明可见。零新 include。

---

### 11.3 语义规则与逐检查点降级表（**E3/E4 的验收依据，写死**）

#### 八条规则

> **REM3-G1（核心）**：守卫触发之后，该帧**不得读取任何**通过 `this` 或任何被守卫指针到达的存储——成员、虚函数、`localRect()`、`geometry()`、`style()`、`layout()` 一律禁止。返回值**只能**由两类东西构成：
> **(a)** 在**门之前**已经拷贝到本帧栈上的局部量；**(b)** 文件作用域常量、函数标量参数。
> 返回 `void` 的帧直接 `return;`。

> **REM3-G2**：守卫你将要解引用的**每一个**指针；并且门后**重读**每一个你将要解引用的**成员**指针，与门前捕获值比对，**不等就降级**（不采纳新值——新值上没有游标）。理由见 Q7 与 §11.6 REM3-RES-1；形状与 `stillAChild()`（`Widget.cpp:240-254`）和 `takeChild:405-409` 的"re-found rather than reused"同构。

> **REM3-G3**：检查**紧贴门之后**，在下一条语句**之前**；一个检查点写成**一条短路 `||` 链**，且**第一项必须是 `this` 的游标**——链上的成员重读要经 `this` 解引用，顺序是承重的，不是风格。

> **REM3-G4**：游标在**指针死掉**的地方取消（`~Widget`），detach 不取消（Q4）。

> **REM3-G5（本版重写，Leo B4）**：**E3/E4 的改动只允许"插入"。** 允许插入：守卫的声明、门前捕获的局部量、检查块。**不允许**移动、合并、拆分、重排、新增或删除任何一条原有语句。
> **为什么这么写**：第 1 版的 G5 是按**动机**表述的（"不得为了让降级答案更好看而把门后的计算上提到门前"）。动机不可复核，下一个人的动机不一样规则就失效；而且它逐字适用于 `GroupBox.cpp:49` 那行门前的 `top`，于是同一个变量被 G1（`frameH` 必须在门前）和 G5（读 `title_` 的计算应该下沉到门后）拉向相反方向——Leo 说他因此不知道 `:49` 该不该动，这是对的。现在的 G5 对 **diff 可判定**：E3 的 patch 里不得出现"删除行"（除非是被同一处插入的重写注释）。`titleW` 不得上提**仍然成立**，但它现在是 G5 的**推论**（上提 = 移动语句），不是一条要靠动机去解释的独立规则。

> **REM3-G6**：`g_deathWatch` 不得增加**决策读者**。任何形如"现在有没有一个 X 帧站在谁身上"、**且库里有分支据此改变行为**的谓词，都必须**先拆链表再写**。纯诊断读者（只被人和用例读，不进任何库内条件）不受此限，`deathWatchDepth()` 就是一个，它存在的理由是可测性（Q2）。反例已经在库里：`g_layouts` 有三个决策读者（`markLayoutDirty:623` 决定这一趟重排跑不跑、`layoutPassActive:722` 决定停车场排不排空、`currentLayoutHost:724` 喂 M2 的断言），这正是它不能被复用的原因（Q1 候选 E）。

> **REM3-G7**：空成员是空检查的事，不是守卫的事（Q7）。**【E19 补】** 早退这个形态要求现场**能整帧放弃**；放弃不了的**可选**成员（`AppWindow::fill_` / `Layout::host_` / `Window::focus_`）走 `DeathWatch(p, MayBeNull{})` 加 `(p0 && !pw.alive())`——空仍不被冒充成死亡，只是这一帧不为它降级。

> **REM3-G8**：`detail::frameDegraded()` **每帧最多记一次**。计数器数的是"帧"，不是"检查"；每个降级分支写一次、随即 `return`，天然满足。

> **REM3-G8 的例外条款（Leo 评审 E5/E6 后补进规则本体；此前它只写在代码注释里）**：
> **一个帧的放弃若在返回值里对调用者可见，则不记。**
> 计数器存在的理由是"放弃否则是一个**缺席**"（`Layout.hpp` 计数器旁的原话）——返回 `void`、或返回一个编造出来的 hint 的帧不留痕迹；而一个交回给调用者的 `false` 不是缺席。
> **全库唯一实例是 `Widget::runLayoutIfAny`。** 它有**三个**调用者：`setGeometry`（`Widget.cpp:529`，`if (layout_ && !runLayoutIfAny()) return;` —— 消费）、`performLayout`（`:612` —— 丢弃）、`markLayoutDirty`（`:722` —— 丢弃）。两处丢弃是安全的，但理由**不同**：它们在这次调用之后**函数立刻结束**，门后没有任何成员访问。
> ⚠️ **所以豁免的依据是"放弃出现在返回值里"，不是"调用者消费了它"**——后者三取一。第 1/2 版把理由写成"`setGeometry` 消费这个 `false`"，那是三分之一的事实。
> **不补这一条的后果**：下一个只读契约的人会给 `runLayoutIfAny` 补一次记录，于是 soak 的 `× 5`（`test_layout_soak.cpp` 末尾）与 rem3_doors 的 `+2` 同时改变含义，而没有任何东西说明为什么。

> **REM3-G8 的推论（E3/E4 落地后由实测补正；这是 E6 的验收依据，第 1 / 2 版哪里都没写过）**：
> **"每帧最多一次"不等于"每次操作最多一次"，更不等于"每扇门一次"。**
> 一次 `ScrollArea::setContentSize` 可以**合法地记两次**：`relayout()` 是**它自己的一帧**，它的 CP-S1 / CP-S2 记一次；返回之后 `setContentSize` 是**另一帧**，它从头到尾站在同样那三个指针上，它的 CP-C2 再记一次。按 G8 这是**正确的**——计数器数的是帧，两个帧就是两条记录。
> ⚠️ **因此 E6（以及此后任何人）不得写 `framesDegraded == 门的数量`**，也不得写"== 死掉的对象数"或"== 失败的操作数"。
> soak 的实测是 `framesDegraded = 403 × 5 = 2015`（`GY_SOAK_CYCLES=400` 加 3 轮热身）。**那个 5 是因为 soak 的五组各自都在自己那一帧的第一个检查点上就被打死**，不是因为库里有五扇门；把其中一组的钩子往后挪一扇门，这个数就会变——而且是**正确地**变。
> 用例：`tests/widget/test_rem3_doors.cpp` 的 `one_operation_can_degrade_two_frames`（一次 `setContentSize`，两条记录），以及 `tests/widget/test_layout_soak.cpp` 末尾那条常驻断言（连同它为什么恰好是 5 的逐组推导）。

> **REM3-G9（E14 新增；⚠️ 编号见下）**：**`Widget::onDescendantDetached()` 的实现只允许把自己的成员指针置空。禁止在其中运行任何应用代码——不得发信号、不得 `update()`、不得 `removeChild` / `takeChild` / `clearChildren`、不得调用虚函数。**
> **理由**：这一趟正走在 `announceDetached` 的**递归中间**，树处于**半解链**状态——离场子树还挂在原处、Window 还没被通知、外层循环正在遍历一份**在钩子跑之前就拍好的快照**。在这里跑应用代码，等于把刚关掉的那扇门重新打开。
> **执行**：Debug 下由 `Widget.cpp` 匿名 namespace 的 `g_inDetachNotify` 标志（RAII 保存/恢复）+ **一条 `assert`** 拦截，落点在 `Widget::takeChild` 的函数首。与 M2 的断言同级同风格：`#ifndef NDEBUG`、命中即 abort、Release 里一个字节都不生成。
> **为什么落点是 `takeChild` 而不是别处**：它是 `announceDetached` 的**唯一**调用者，也是全库 `children_.erase` 的**唯一**出现处，所以它是钩子回到那趟遍历的**唯一**通路。断言**不覆盖** G9 禁止的其余各项（`update()`、`emit()`、虚调用）——那些是契约，**只有会破坏遍历的那一项值得付运行时代价**。这一句是有意写下的边界，不是遗漏。
> **⚠️ 编号冲突，请架构团队复签**：本条在 E14 任务书里被称作 **REM3-G6**，但 `REM3-G6` 已被"`g_deathWatch` 不得增加决策读者"占用（见上）。同一张表里两个 G6 会让两条都无法被引用，所以本轮按**下一个空号 G9** 落地。**若架构团队要的是另一种编号（例如把旧 G6 改名），请裁定，我改。**

#### 逐检查点降级表（**这是 E3/E4 的施工图；按检查点写死，不按门**）

约定：`self` = `this` 的守卫；`vpw`/`ctw` = `viewport_`/`content_` 的守卫；`vp0`/`ct0` = 门前捕获的成员指针值。**记**一列写"是"表示该分支必须调用 `detail::frameDegraded()`。

| 检查点 | 现场（HEAD 行号） | 紧贴在哪扇门后 | 检查项（顺序即代码顺序） | 降级动作 | 记 |
|---|---|---|---|---|---|
| **CP-G1** | `GroupBox::sizeHint():53` 之后 | `Widget::sizeHint()` | `!self.alive()` | `return` `{min=preferred={frameW,frameH}, max` 默认 `}` | 是 |
| **CP-S1** | `ScrollArea::relayout():158` 之后 | `content_->sizeHint()` | `!self.alive()` → `viewport_!=vp0` → `content_!=ct0` → `!vpw.alive()` → `!ctw.alive()` | `return;` | 是 |
| **CP-S2** | `ScrollArea::relayout():164` 之后 | `viewport_->setGeometry` | `!self.alive()` → `content_!=ct0` → `!ctw.alive()` | `return;` | 是 |
| **CP-C1** | `ScrollArea::setContentSize():22` 之后 | `content_->setGeometry` | `!self.alive()` → `viewport_!=vp0` → `content_!=ct0` → `!vpw.alive()` → `!ctw.alive()` | `return;` | 是 |
| **CP-C2** | `ScrollArea::setContentSize():24` 之后 | `relayout()` | 同 CP-C1 | `return;` | 是 |

**逐点理由**（"门后还会经谁读写"，这是检查项的唯一来源）：

* **CP-G1**：门后 `:54` 读 `layout_`、`:62` 读 `title_`、`:65` 虚调用 `style(styleState())`——**全部经 `this`**，没有一个成员指针被解引用 ⇒ 只需 1 个守卫、1 项检查。
  **降级返回值逐字写死**：`h.min = h.preferred = {frameW, frameH}`，`h.max` 保持默认 `{kUnbounded, kUnbounded}`；即"只剩这个框本身"，`preferred == min`，单调、不虚报。`inner`（`:53-58`）与 `titleW`（`:62-67`）**一律按 0 计，不得计算**。
  代价是降级答案丢掉标题下限——可接受，因为按 REM3-G4 这条路只在对象**已经死了**时才走，那个下限没有消费者。
* **CP-S1**：门后 `:159` 读 `this->geometry_`（`localRect()`）、`:164` 写 `viewport_`、`:165` 写 `content_` ⇒ 三个指针全要，且两个是**成员**，所以要重读。
* **CP-S2**：门后只剩 `:165`，它 (i) 读 `this->content_`（要 `this` 活）、(ii) 解引用 `content_`（要 `ctw` 活 + 值没变）。**不查 `viewport_`**——`:165` 之后没有任何东西再碰它。这一点第 1 版是错的：§11.5 按 6 次 `alive()` 计价，等于假定第二次也查三个；正确答案是 5 项（其中 2 项是成员重读，不是 `alive()`）。
* **CP-C1 / CP-C2**：`setContentSize:24 relayout()` 与 `:26 scrollTo(scrollOffset())` **都是门**（前者内部有 `:158`/`:164`/`:165`，后者 `:65` 发 `scrolled` 信号）。门后 `:26` 经 `this` 调 `scrollTo`，其内部 `maxScroll()→contentSize()`（`ScrollArea.hpp:28`）解引用 `content_`、`scrollOffset()` 解引用 `viewport_` ⇒ 三个都要。
* **`:26 scrollTo(...)` 之后为什么没有 CP-C3**：门后只剩 `:27 update()`，它只读 `this`（`Widget::update():872-880` → `window():882-886` 沿 `parent_` 走）。而 `scrolled` 的宿主就是 `this`，**契约 D7 禁止槽销毁信号宿主**（`Widget.hpp:105-108`、`core/Signal.hpp`），所以这扇门对 `P = this` 不危险。**这是 D7 在本设计里唯一一次做实事的地方，也是门谓词必须把 D7 写进去的原因**（见 §11.4）。

**不加检查的门，必须留注释**（否则下一个人追加语句时无从知道）：

* `ScrollArea.cpp:165`：门后紧接 `return;` ⇒ 检查会是死代码。注释写"此后追加任何语句都必须先补一次检查（REM3-G3）"。
* `ScrollArea.cpp:174`：函数最后一条语句，同上。

**⚠️ 登记：`GroupBox::sizeHint()` 门前门后各读一次 `title_`（REM3-RES-5）。** `:49` 的 `top` 由 `title_.empty()` 算出、在门**前**；`:62-67` 的 `titleW` 读 `title_` 在门**后**。门内的应用代码完全可以调 `setTitle("")`，于是**健康路径**会返回一个内部自相矛盾的 hint（门前认为有标题所以 `frameH` 含 34px 的 `kTopTitled`，门后 `titleW` 为 0，或反过来）。
**这是既有缺陷，不是 E1 引入的**，但 §11.3 把 `frameH` 钉成降级返回值的组成部分，等于替它背书，所以必须写明：
1. 本轮**不修**——修它要么把 `:49` 下沉到门后（G5 禁止移动语句），要么在门后重算（那是行为改变，要一条自己的用例）；
2. 降级路径用门前的 `frameH` **不是**对这条撕裂的认可，而是 G1 的必然结果：对象已死，门前快照是唯一还合法的输入；
3. 定级 S2、排 W2 轮，见 §11.6 REM3-RES-5。

---

### 11.4 门的定义、谓词、枚举表（E5 的输入）

#### 门谓词（**可机械执行**；这是本版对第 1 版最大的一处改动）

第 1 版把门定义成 D-a/D-b 两类，是**围着两个已知缺陷点画的圈**，不是扫出来的（Leo 的判词，我核对后同意：它收录了更弱的 `viewport_->setGeometry`，却漏了 `AppWindow::relayout` 这个每帧都跑的无条件命中）。本版改成谓词 + 封闭原语清单：

> **门（door）**：语句 `S` 是帧 `F` 的一扇门，当且仅当 `S` 能把控制权交给**应用代码**。判定用**封闭的原语清单**（可 grep，可维护）：
> * **P1**：对**任何应用可派生的库多态基类**的**虚成员**调用。主语的封闭清单：**`Widget` / `Layout` / `SelectBase` / `StyleSubject`**（`sizeHint()`、`onGeometryChanged()`、`onPaint()`、`styleState()`、`styleType()`、**`layoutRect()`**、**`Layout::onInvalidated()` / `onChildAppended()` / `onChildRemoved()`**、`SelectBase::buildRows()` / `onOpened()` / `onClosed()` …）。**限定名调用不算 P1**（`PushButton::sizeHint()` 是静态绑定），但见下面的传递性条款。
>   ⚠️ **`layoutRect()` 是 E5 复核时补进这个括号的。它一直在谓词的覆盖范围内，只是从来没有被扫到过**——原因见 §11.9 的更正：本节的第一刀是一张 grep **名字表**，而 P1 说的是"任何虚调用"，**虚调用是按名字 grep 不出来的**。
>   ⚠️⚠️ **主语是 E9 复核时从"widget"改宽的，这是与上一条不同的第二个洞。**`Layout` 不是 widget，于是 `Layout::onInvalidated()` / `onChildAppended()` / `onChildRemoved()`（`Layout.hpp` 的 protected virtual，注释白纸黑字写着是给子类用的扩展点）**P1 管不着、P2 没登记**，两头落空——而它们是货真价实的门，见下面的 N1/N2/N3。
>   **两个洞的区别值得写死**（Leo 的判词，我核对后同意）：`layoutRect()` 漏掉是因为「**虚调用 grep 不出名字**」，`Layout` 三个钩子漏掉是因为「**主语写窄了**」。补第一个（§11.9 的声明侧生成）**不会**自动补上第二个——声明侧扫描的根目录如果只有 `Widget.hpp`，`Layout.hpp` 的三个钩子照样扫不到。所以 §11.9 lint 第四条的扫描根同时改成 `include/geeyoou/**/*.hpp`。
> * **P2**：调用**已知能到达应用代码的库函数**：`setGeometry` / `setVisible` / `setLayout` / `invalidateSizeHint` / `add<T>` / `takeChild` / `removeChild` / `clearChildren` / `Window::openPopup` / `Window::closePopup` / `Widget::performLayout` / `Layout::arrange` / `Layout::measure` / `Layout::measureFor` / `AppWindow::relayout`，**以及任何被本清单登记过的库函数**（传递性在这里显式展开，见下）。
>   ⚠️ **这一行就是 lint 的 P2 名字集合本身，脚本里没有第二份。** §11.9 性质 2 早就为 allowlist 定了这条规矩（"两者不得各写一份"），P2 清单是同一种东西——人维护、机器消费——所以守同一条规矩。**在 R2 第 4 轮之前它没有守**：`tools/lint-door-coverage.ps1` 里有一份手抄的 `$P2Names`，旁边写着"你在这里加一个，记得也去那边加"，而两份**已经漂了**——脚本有 `relayout`，本行没有。这次漂的方向是红的（多扫一个名字），所以什么都没漏；**反方向的同一次漂移会静默删掉一整类候选，并且是让门禁变绿的那个方向**。⇒ 脚本改成解析本行，手抄那份已删除；`relayout` 按"取两者的并集、不取交集"补进本行（它确实是门：`AppWindow::relayout` 里三个 `setGeometry`，就是 #16）。
>   ⚠️ **本行的书写格式是承重的**（脚本按此归一）：每个原语一个反引号 token，`add<T>` 去掉模板实参取 `add`，`Window::openPopup` 去掉限定名取 `openPopup`。**原先的 `Layout::measure(For)` 这种缩写已展开成两条**——它省下四个字符，代价是机器读不出来。本行上任何一个反引号 token 若归一不出一个标识符，lint **退 3**（fail closed），不是跳过：跳过意味着文档里的一个笔误静默删掉一个原语。
> * **P3**：`signal.emit(...)`。
>
> **危险（hazard）**：门 `S` 对指针 `P` 危险，当且仅当 `S` **之后本帧**还有一次经 `P` 到达的读或写。`P` 取自 `{this}` ∪ `{本帧要解引用的成员指针}`。
> **唯一豁免（D7）**：若 `S` 是 P3 且信号的宿主就是 `this`，则契约 D7 禁止槽销毁信号宿主（`Widget.hpp:105-108`），故 `S` 对 `P = this` **不**危险；对任何**其它** `P` 仍然危险。
> **动作**：对每一个危险的 `P` 上一个游标 + 在紧贴 `S` 之后的检查点里查一次（REM3-G2/G3）。
>
> **传递性怎么收敛**：门不做隐式传递。一个库函数若含门，它要么在自己帧内把门关好，要么**被登记进 P2**。发现新的一个 ⇒ 加进 P2 清单 ⇒ **重扫**。这个不动点是手工推进的，但每一步都留在清单里，所以"扫到哪了"是可查的事实而不是记忆。

**D-a / D-b / D-c 保留为 P 的具名实例**，方便与既有文字对齐：
**D-a** = `Widget::sizeHint()`（P1）；**D-b** = `Widget::setGeometry()`（P2）；**D-c** = **任何会运行 `Window::widgetDetached` / 槽 / 子类钩子的 detach 帧**（P2 的 `takeChild`/`removeChild`/`clearChildren`）。
**D-c 正式进门的定义**（第 1 版只认 D-a/D-b，把 D-c 漏在残留里）。D-c 的取消策略与 hint 帧相同 ⇒ 按 Q1 的判据它**不**另开链表，与 hint 帧同挂 `g_deathWatch`。

#### 枚举表

定级：**S1** = 无条件或主流用法下必然执行、且门后有写；**S2** = 条件性 / 低频，或门后只有读；**S3** = 理论暴露面，无已知触发路径。
轮次：**W1** = 本轮（E3/E4）；**W2** = 紧接下一轮（编号待架构团队映射）；**W3** = 需要一次架构裁定之后才能排。

| # | 位置（HEAD） | 门原语 | 门后经 `this`/成员的读写 | 需守卫 | 级 | 轮 | 动作 |
|---|---|---|---|---|---|---|---|
| 1 | `GroupBox.cpp:53`（`sizeHint`） | P1 `Widget::sizeHint()` | `:54` `layout_`、`:62` `title_`、`:65` 虚调用 | `this` | S2 | **W1** | ✅ CP-G1 |
| 2 | `GroupBox.cpp:98-103`（`sizeHint` 的 `titleW`） | P1 `styleState()` | **同一条语句内、门之后**：`style(...)` 读 `styleCache_`/`styleCacheGen_` 并走 parent 链；随后 `measureText(title_, ...)` 读 `title_` | `this` | **S3** | **W3** | ❌ **判词已更正**，见下；并入 #23 的族（REM3-RES-2，走契约不走守卫） |
| 3 | `ScrollArea.cpp:158`（`relayout`） | P1 `content_->sizeHint()` | `:159` `geometry_`；`:164` **写** viewport；`:165` **写** content | `this,viewport_,content_` | S1 | **W1** | ✅ CP-S1 |
| 4 | `ScrollArea.cpp:164`（`relayout`） | P2 `setGeometry`（条件性，见下） | `:165` 读 `this->content_` 并写 content | `this,content_` | S1 | **W1** | ✅ CP-S2 |
| 5 | `ScrollArea.cpp:165`（`relayout`） | P2 `setGeometry` | **无**（下一句 `return`） | — | — | — | ❌ 死代码；**加注释** |
| 6 | `ScrollArea.cpp:174`（`relayout`） | P2 `setGeometry` | **无**（函数末） | — | — | — | ❌ 死代码；**加注释** |
| 7 | `ScrollArea.cpp:22`（`setContentSize`） | P2 `content_->setGeometry` | `:24` `relayout()`、`:26` `scrollTo(scrollOffset())`、`:27` `update()` | `this,viewport_,content_` | S1 | **W1** | ✅ CP-C1 |
| 8 | `ScrollArea.cpp:24`（`setContentSize`） | P2 `relayout()`（含 #3/#4/#5） | `:26` 经 `this`→`viewport_`/`content_`；`:27` `this` | `this,viewport_,content_` | S1 | **W1** | ✅ CP-C2 |
| 9 | `ScrollArea.cpp:26`（`setContentSize`） | P3 `scrolled.emit`（`scrollTo:65`） | `:27` `update()` 只读 `this` | — | — | — | ❌ **D7 豁免**；宿主是 `this` |
| 10 | `Widget.cpp:548`（`sizeHint`） | P2 `layout_->measureFor` | **无**（`return` 该调用的结果） | — | — | — | ❌ 登记备查 |
| 11 | `Widget.cpp:421`（`removeChild`） | P2 `takeChild` | `:422` `doomed.reset()` **不经 `this`**，随后函数结束 | — | — | — | ❌ **看过了，不需要封**（见下） |
| 12 | `Widget.cpp:302`（`announceDetached`） | P2 `win->widgetDetached` | `:306` 起读 `parent.children()` | `parent` | S1 | — | ✅ **已修**（E17，`e9c283a`；守卫在 `:301`，检查 `:305`/`:318`） |
| 13 | `Widget.cpp:401`（`takeChild`） | P2 `announceDetached` | `:408` 起读 `children_` | `this` | S1 | — | ✅ **已修**（E17；`:400`/`:402`） |
| 13b | **`Widget.cpp:416`（`takeChild` 的第二道门）** | P2 `childRemoved(index)` → `Layout::onChildRemoved` + `markLayoutDirty` → `runLayoutIfAny` → `arrange` → `setGeometry` → `onGeometryChanged` | **无**（`:417 return owned;` 只读局部量） | — | — | — | ❌ 非危险，**但理由是隐式的 ⇒ 本轮加注释**（见下） |
| 14 | `Widget.cpp:436`（`clearChildren`） | P2 `removeChild` | `:437`/`:441` 读 `children_` | `this` | S1 | — | ✅ **已修**（E17；`:431`/`:437`） |
| 15 | `Widget.cpp:492`（`setGeometry`） | P1 `onGeometryChanged()` | `:495` `visible_` + `update()` | `this` | S1 | — | ✅ **已修**（R2；`GeometryGuard` `:491`/`:493`） |
| 16 | **`AppWindow.cpp:72/74/75`**（`relayout`） | P2 `setGeometry` ×3 | `:73→:74` 读并解引用 `content_`、`:75` 读并解引用 `fill_`、`:77` 读信号成员、`:78` `update()` | `this,content_`（`header_` **不需要**：`:72` 之后没有任何语句再解引用它；`fill_` 见 §11.14 —— 它取 `MayBeNull` 游标） | **S1（本表最高）** | W2 | ✅ **已封**（E19；CP-A1 / CP-A2 / CP-A3，§11.14） |
| 17 | `AppWindow.cpp:77`（`relayout`） | P3 `contentResized.emit` | `:78` `update()` 只读 `this` | — | — | — | ❌ D7 豁免 |
| 18 | `AppWindow.cpp:85`（`setHeaderVisible`） | P2 `setVisible` | `:86` `relayout()` 经 `this` | `this` | S2 | W2 | ❌ 本轮不改 |
| 19 | `WindowHeader.cpp:218`（`relayoutItems`） | P2 `setGeometry`（**在 range-for 里**） | `:219` 读引用 `s`、下一轮 `:218` 再读、`:222` `update()` | **不是游标**：按下标迭代 + 每轮重读 `size()`（或先快照）；**外加** `this` | S1 | **W2** | ❌ 本轮只改表（处方原写错了，见下）；实修归 R2.4 |
| 19b | **`WindowHeader.cpp:183-191`（`setTrailingItemWidth`）** | P2 `relayoutItems()`（内含 #19） | **无**——门后紧跟 `return`，`s` 不再被读 | — | **S3** | **W2** | ❌ **同形，靠一个 `return` 侥幸活着**；与 #19 同批读，见下 |
| 20 | `Cascader.cpp:143`（`rebuildColumns`） | P2 `setVisible`（循环内，按下标读 `columns_`） | `:150` 起继续按下标读（`:142` 的循环自己也按下标） | `this` + 下标 | S2 | W2 | ❌ 本轮不改；**主语已更正**（原写作 `relayoutColumns`，见下） |
| 21 | `Cascader.cpp:152/159/161`（`rebuildColumns`） | P2 `setGeometry` | `:153-155` 局部量、`:156-159` 按下标读 `columns_`、`:161` 读 `popupBox_` | `this,popupBox_` + 下标 | S2 | **W2** | ❌ 本轮不改；下标防重分配、**防不了缩短**；**主语已补上**（原行只有文件名，归档不到任何东西） |
| 22 | `SelectBase.cpp:60`（`showCustomPopup`） | P2 `Window::openPopup`（内含 `closePopup`→`popupClosed.emit`） | `:61` `update()`、`:62` 读 `openStateChanged`（都经 `this`） | `this` | S2 | W2 | ❌ 本轮不改；**P3 家族的样本**（宿主是 `Window` 不是 `this`，D7 不豁免） |
| 23 | `PushButton.cpp:88`（`sizeHint`） | P1 `styleState()` | `:90/:91/:92` 读 `text_`/`loadingText_`、`:93-94` 读 `icon_` | `this` | S3 | **W3** | ❌ 见 REM3-RES-2（应走契约而非守卫） |
| 24 | `IconButton.cpp:29`（`sizeHint`） | 限定名 `PushButton::sizeHint()`（内含 #23） | 门后 `:30-33` **只读局部量** | — | — | — | ❌ 非危险 |
| 25 | `examples/showcase/PageIcons.cpp:633` | P1 `content->sizeHint()`，门后动 `sa` | 库外 | （库外） | S2 | W3 | ❌ R3 不改 examples；**"契约主语是调用者"的活样本** |
| 26 | showcase 四页 `return content->sizeHint().preferred;` | P1 | 门后无读 | — | — | — | ❌ 无动作 |
| 27 | **P3 家族（全库 ~60 处 `.emit(`）** | P3 | **未逐点枚举** | — | S2 | **W2（扫描任务）** | ❌ 见下 |
| **28** | **`Widget.cpp` 的 `contentRect()`，`layoutRect()` 之后** | **P1 `layoutRect()`（protected virtual）** | 门后 `if (!layout_)` 与 `layout_->margins()`——**两次经 `this` 读成员** | `this` | **S1** | **E6** | ✅ **已修**（`DeathWatch` + `frameDegraded`；见下） |
| **28b** | **`Widget.cpp` 的 `runLayoutIfAny()`，`contentRect()` 之后** | P2 `contentRect()`（内含 #28） | `layout_.get()`，随后把 `*this` 交给 `arrangeFor` | `this` | **S1** | **E6** | ✅ **已修**（复用早已在栈上的 `LayoutGuard`，一行） |
| **29** | **`GroupBox.hpp` 的 `layoutRect()` 覆写** | — | — | — | — | — | ❌ **当前实现安全**，理由见下；按新契约的字面它必须在表里 |

**#11 为什么"看过了、不需要封"（回答我自己 E17 报告的 §8.3；理由按 Leo 复签时给的更强版本重写）**：`removeChild:420-423` 只有两条语句——`:421 takeChild(child)`（门，且它自己已经在 `:400` 上了守卫）、`:422 doomed.reset()`。

判它免检的理由**不是**"`reset()` 不碰 `this` 成员"——那是关于被调函数内部的推理，不可机械复核，而且它把结论挂在了一个会变的实现细节上。**正确的理由是：`doomed` 是一个局部量。** 门之后本帧碰到的**唯一**存储就是这个栈上的 `unique_ptr`，`:422` 之后函数结束，`this` 和任何成员都不再出现。于是两条死亡路径**都**安全，且**都不依赖 `takeChild` 返回了什么**：

* 宿主（`this`）死在门里 ⇒ `takeChild` 按契约返回 `nullptr`，`doomed` 是空的，`reset()` 是 no-op，函数结束；
* 宿主活着、`child` 被门里的槽先摘走 ⇒ 同样返回 `nullptr`，同上；
* 正常路径 ⇒ `doomed` 持有子树，`reset()` 销毁它，函数结束。

这条推理**只用到"局部量的生命期"和"门后没有成员访问"**两件事，两件都对 diff 可判定。按 Leo 的标准，本行的存在本身就是"看过了没有"与"没看"的区别。

**#13b —— `takeChild` 的第二道门（Leo 独立重走"不泄漏"论证时发现，第 1 版只写了路径 A）**：守卫作用域在 `:403` 就**关掉**了，而 `:416 childRemoved(index)` 在它**之后**，并且它是一扇货真价实的门——`Layout::onChildRemoved` 是应用可重写的虚函数，`markLayoutDirty` 可能当场起一趟 pass，pass 里的 `setGeometry` 会跑 `onGeometryChanged`。所以问一次"宿主死在这里会怎样"是必须的，答案是**不泄漏也不 UAF**，但理由是精确的、而且是**隐式的**：

* 此时 `owned` 已经 `move` 出 `children_`（`:411-412`），`owned->parent_` 已置空（`:413`），所以宿主的析构**不会**带走这棵子树；
* `owned` 是局部 `unique_ptr`，`:417` 把它 `return` 给调用者，**所有权有归宿**（若宿主真死在 `:416`，调用者拿到的是一棵合法的、已脱离的子树）；
* 它成立的**唯一**理由是：`childRemoved(index)` 之后**再没有任何语句解引用 `this`**。

第三条是一条**隐式不变量，此前哪里都没写**，而 `:395-399` 的注释读起来像这个函数只有一道门（"this one covers the rest of this function"）。下一个人在 `:416` 后面追加一行成员访问，就把 #13b 从"非危险"变成 S1，而没有任何东西会提醒他。**E2 因此在 `:416` 上方补一条注释**：这是本函数的第二道门，其后不得再出现任何成员访问，否则 `:394-403` 的守卫作用域必须延长到函数末尾。（只加注释，不加检查：按谓词判定它今天确实非危险，加一个恒真的检查是死代码，与 §11.3 对 `ScrollArea:165`/`:174` 的处理同一条规矩。）

**#2 的判词是错的，本轮更正（E9 复核；这是本表里第一处"同一个形状两个相反结论"）**。第 1/2 版写的是"门后 `:69-75` 只读局部量"。现场不是这样——`GroupBox.cpp:98-103` 是**一条**语句：

```cpp
const float titleW =
    title_.empty()
        ? 0.0f
        : measureText(title_, style(styleState()).fontSizeOr(Theme::current().fontBody))
                  .width +
              kTitlePad;
```

`styleState()` 是门（P1，protected virtual）。`style(...)` 是**同一条语句里、门之后**对 `this` 的成员调用——它读 `styleCache_` / `styleCacheState_` / `styleCacheGen_`，未命中还要沿 parent 链上溯；`measureText(title_, …)` 跟在它后面，又读一次 `title_`。**这不是"只读局部量"。** CP-G1 的游标此刻确实还在栈上（`self` 覆盖整个函数），但游标不等于检查：**`styleState()` 与这两次读之间没有任何 `alive()`**，REM3-G3 要的"紧贴门之后的检查"在这里一条都没有。

**这与 #23（`PushButton::sizeHint():88`，同样是 `styleState()` 之后接着读自己的成员）是完全相同的形状，却得到了相反的结论**——#23 登记成 S3/W3，#2 判成"非危险"。改判：**#2 定级 S3、排 W3、并入 #23 的族**（REM3-RES-2：这一族的处理方向是**收紧契约**——`styleState()` 的覆写不得销毁 widget——而不是在每个 `sizeHint()` 里加守卫）。今天没有已知触发路径（库内 `styleState()` 的覆写都不跑应用代码），所以是 S3 而不是 S1。

**#4 为什么仍然要查**：`viewport_` 是 `ScrollArea` 构造函数里 `add<Widget>()` 出来的**普通 `Widget`**，今天 `onGeometryChanged()` 是基类空实现、且没有 layout，所以 `:164` **今天**不是门。但应用能拿到它：`sa->content()->parent()->setLayout<BoxLayout>()` 一句就让它变成真门。**"今天不是门"不是不变量**——`ScrollArea.hpp:25` 的 `content()` 是 public。

**#16 为什么定级最高**：三个 `setGeometry` 无条件执行、**每帧**跑（`AppWindow::onGeometryChanged():81` → `relayout()`），`header()`/`content()` 都是 public，showcase 五页正是这么用；门后一个游标都没有。
⚠️ 而 `Widget.cpp:483-489` 的注释**就是拿这个函数当例子写的**——"*onGeometryChanged runs APPLICATION code: AppWindow::relayout emits contentResized from inside one, and a slot is entitled to destroy widgets -- this one included.*" 那条注释给 `setGeometry` **自己那一帧**加了 `GeometryGuard`（`:491`），而 `AppWindow::relayout` 的帧在它**下面**，一个游标都没有。**库里最显眼的那条注释，指着的正是本表漏掉的那一行。** 这是"表不是扫出来的"最硬的证据，我接受。

**#19 的处方原本是错的，本轮更正（R2 第 4 轮安全评审 C5；只改表，实修归 R2.4）**。这一行原来写的是「需守卫：`this` + 迭代器失效」——**前半句对，后半句会把下一个人带进沟里**：它读起来像"再上一个游标就行了"，而**一个 `DeathWatch` 关不掉这一条**。游标回答的是"我记下的那个对象死了没有"；这里出事的**不是对象死亡**，是 `std::vector` 重分配，`slots_` 那块缓冲区换了地址，而 `this` 从头到尾好好活着。游标答不了这个问题。

```cpp
for (const Slot& s : slots_) {                 // :215
  s.widget->setGeometry({x, y, s.width, h});   // :218  <- 门
  x += s.width;                                // :219  <- 读已失效的引用
}
update();                                       // :222
```

**触发路径走得通，而且不需要任何对象死亡**——这一点让它在本表里独一份：应用的尾部项在自己的 `onGeometryChanged()` 里回调 `header->addTrailingItem<T>(...)`（**`README.md:170/176` 演示的正是这个 API**）⇒ `slots_.push_back` ⇒ **vector 重分配** ⇒ range-for 的迭代器与引用 `s` 全部失效 ⇒ `:219` 读已释放内存，下一轮 `:218` 再读一次。**本表其余每一条 S1 都要求"某个对象死在门里"；这一条只要求"应用又加了一个尾部项"。**

**正确的处方**：按下标迭代 + **每轮重读 `slots_.size()`**（下标能防重分配），或者在循环前把要摆的 (widget, x, y, w, h) 快照出来、门全部开在快照上。⚠️ 下标**防不了缩短**（#21 已经写过这句），所以每轮都要重读 `size()` 并重新取 `slots_[i]`，不能把引用提到循环外。**`this` 的游标仍然要，那是另一件事**：`:222` 的 `update()` 经 `this`。

**#19b —— `setTrailingItemWidth` 同形，靠一个 `return` 活着（本轮补登记）**：`WindowHeader.cpp:183-191` 也是 `for (Slot& s : slots_)`，循环里改 `s.width` 之后调 `relayoutItems()`（同一扇门，同一条回调链），然后**立刻 `return`**。门后不再读 `s`，所以按谓词它今天**非危险**——与 #13b 完全同一条规矩，也与 #13b 同一个风险：**这条命是那个 `return` 给的，而这件事此前哪里都没写**。哪天有人把它改成"批量设宽度"（把 `return` 换成 `continue`，或在循环外再 `relayoutItems()` 一次），它当场变成第二个 #19，而没有任何东西会提醒他。**定级 S3、排 W2，与 #19 同批读**——修 #19 的人手就在这个文件里，两处一起改是一次改动，分两轮改是两次。

**#27 为什么按家族登记而不逐点列**：全库 `.emit(` 约 60 处，但 D7 豁免砍掉其中大半——凡是"发自有信号、门后只读 `this`"的（`PushButton::activate:107-109`、`ScrollArea::scrollTo:65`、`Cascader:78` …）都不危险。**剩下的是"发别人的信号 / 经别的对象绕一圈回来"那一类**，#22 是已确认的一个样本。逐点判定要读 60 个函数体，那是一次独立的扫描任务，不是 E1 能在设计文档里完成的事。**登记形态**：家族已识别、判定谓词已给出（P3 + D7 豁免）、定级 S2、排 W2，扫描产出物是 §11.9 那个 lint 的 allowlist。

**#28 / #28b —— `layoutRect()` 这扇门，以及为什么两个帧都要封**：`layoutRect()` 是 `protected virtual`，它 protected 恰恰是为了让应用覆写——它自己的声明就是这么邀请的（"画自己装饰的容器覆写它，交回装饰的内侧"）。它按 P1 的字面就是一扇门，而第 1/2 版的表里没有它。

**"由调用者保证"在这里不成立**，这一条值得写死：`contentRect()` 在门之后**自己还有两次经 `this` 的读**（`layout_`、`layout_->margins()`），这两次读发生在控制权回到 `runLayoutIfAny` **之前**。调用者的守卫覆盖调用者的帧，它**覆盖不了一次已经发生的读**。所以调用侧的检查**必要而不充分**，而一句"此处存活性由调用者保证"会是一句**假话**。两个帧因此各封各的：`contentRect()` 自己上游标，`runLayoutIfAny` 用它本来就有的 `LayoutGuard` 多问一次。

**为什么不改成"门前预读 margins"**：这是本缺陷族里**唯一**一处预读真能奏效的地方——门后全是读，§11.0 对预读的否决理由（"预读救不了写"）在这里不适用。否决它的是**另一条**理由：`layoutRect()` 是应用代码，它有权调 `setMargins()` / `setLayout()`，预读之后这一趟就会拿**门前**的 margins 去排布。那是一次**行为改变**，没有任何缺陷在推动它，而且它是**新增**一处 REM3-RES-5 那种跨门撕裂读，不是消除一处。守卫在一条马上要跑一整趟 arrange 的路径上花 9 条指令（§11.5）。

**#28 记一次、#28b 不记，这是按站点定的，不是通则**：`frameDegraded()` 存在的理由是"放弃否则是一个**缺席**"（`Layout.hpp` 计数器旁边的原话）——返回 `void`、或者返回一个编造出来的 hint 的帧不留痕迹。`runLayoutIfAny` 的放弃**不是缺席**：它把放弃写进了**返回值**（`false`）。这就是 §11.3 REM3-G8 例外条款的**全库唯一实例**，规则本体现已写明。

⚠️ **本段第 1/2 版把理由写成"`setGeometry` 消费这个 `false`"，那是三分之一的事实**（Leo 评审 E5/E6 指出）：`runLayoutIfAny` 有**三个**调用者——`setGeometry`（`:529` 消费）、`performLayout`（`:612` 丢弃）、`markLayoutDirty`（`:722` 丢弃）。两处丢弃安全的理由是"调用后函数即结束、门后无成员访问"，与"被消费"完全无关。**豁免依据是返回值可见，不是调用者消费。**

再记一次还会让这个检查与它下面那个同形检查（`arrangeFor` 之后那个，自 R2 起一直沉默）自相矛盾。

**#29 —— `GroupBox::layoutRect()` 为什么是"当前实现安全"**：`GroupBox::contentRect()` 里**一扇门都没有**——它读 `title_`、调 `localRect()`（非虚，读 `geometry_`）、做算术，P1 / P2 / P3 **一条都不匹配**，到不了应用代码。这是**这一个覆写**的性质，不是这个钩子的性质，所以调用侧照样上守卫：`Widget::contentRect` 是照着**钩子**写的，不是照着今天恰好只有一个的那份实现写的。

#### 声明侧复扫的九扇新门 N1–N9（E9 登记；**本轮只登记，一处不修**）

上面那张表是按 `src/widget/*.cpp` 的**调用点**扫出来的，主语还是"widget"。按更正后的 P1 主语（`Widget` / `Layout` / `SelectBase` / `StyleSubject`）**从声明侧**重扫全部 66 条非析构 `virtual`，多出下面九扇。**行号是本轮改动后的 HEAD**（`Widget.cpp` 因 E14 与几处注释整体下移约 100 行，与第 2 版的编号对不上是正常的；**语句是表要说的东西，行号不是**——这句话 GroupBox 那段已经写过一次了）。

| # | 位置（HEAD） | 门 | 门后经 `this`/成员的读写 | 级 | 轮 |
|---|---|---|---|---|---|
| **N1** | `Layout.cpp:27` `Layout::invalidate()` | P1 `onInvalidated()` | `:31` `if (host_) host_->performLayout()` | **S1** | W2 → ✅ **已封**（E19；CP-L1，守 `host_`。两条残留见 §11.14） |
| N2 | `Widget.cpp:821` `childAppended()` | P1 `layout_->onChildAppended()` | `:822` `markLayoutDirty()` | S2 | W2 |
| N3 | `Widget.cpp:826` `childRemoved()` | P1 `layout_->onChildRemoved(index)` | `:827` `markLayoutDirty()` | S2 | W2 |
| **N4** | `Window.cpp:165` `setFocusWidget()` | P1 `old->onFocusChanged(false)` | `:166` `focus_->onFocusChanged(true)` | **S1** | W2 → ✅ **已封**（E19；CP-W1。⚠️ 触发形态与本表原文不同，见 §11.14） |
| **N5** | `Widget.cpp:1195` `animationTickTree()` | P1 `onAnimationTick()` | `:1196` `for (children_)` | **S1** | **W2** |
| N6 | `Widget.cpp:1131` `paintTree()` | P1 `onPaint()` | `:1136` `for (children_)` | S3 | W3 |
| N7 | `SelectBase.cpp:106`（`refreshRows`）、`:121`/`:125`（`open`） | P1 `buildRows()` / `onOpened()` | `:110-112` 与 `:126-127` 共 4 处经 `this` 的读（`popupWidth_`、`geometry()`、`update()`、`openStateChanged`） | S2 | W2 |
| N8 | `MenuButton.cpp:134` `onMouse()` | P1 `inArrowZone(e.pos)` | `:135` `isMenuOpen()` / `openMenu()` / `closeMenu()` | S3 | W3 |
| N9 | `Widget.cpp:1104` `window()` | P1 `w->asWindow()`（`:1107`） | `:1108` `w = w->parent_` | S3 | W3 |

**三条要点，必须跟着表一起读：**

* **N1 是这九条里唯一一条会真正 `delete` 而不是 park 的，park list 在这条路径上救不了它。** `Layout::invalidate()` 由 `setMargins` / `setSpacing` / 任何子类 setter 调用。走这条路时 `layoutRunning_` 与 `buffersBusy_` **都是 false**（没有 pass、没有 measure），于是 `~Widget` 里 `if (layoutRunning_ || layout_->buffersBusy_) parkLayout(...)` 的条件不成立，`unique_ptr<Layout> layout_` 直接把 Layout **删掉**——而 `Layout::invalidate()` 的栈帧还在，`:31` 的 `if (host_)` 就是一次对已释放对象的读。§11.3 反复引用的"停车场兜底"在这里**不成立**，这是它第一次不成立。定级 S1。
* **N3 是 E6 那个形状，早了一轮，而且在同一个文件里。** 本表 #13b 分析的是 `takeChild` **那一帧**：`childRemoved(index)` 之后只剩 `return owned;`，不碰 `this`，所以那一帧非危险——**这个结论是对的**，本轮不改。但它**停在了 `childRemoved()` 的门口**：`Widget::childRemoved()` **自己那一帧**里，门（`layout_->onChildRemoved()`）之后还有一句 `markLayoutDirty()`，那是一次经 `this` 的成员访问。**外帧看着干净不等于内帧干净**——这与 E6 抓 `contentRect` / `runLayoutIfAny` 是同一件事（#28 / #28b），只是这一次内外两帧都在 `Widget.cpp` 里。N2 是它的孪生。
* **N4 今天就能走到应用代码，不需要任何假设。** 路径是现成的：`SelectBase::onFocusChanged(false)` → `close()` → `w->closePopup()` + `openStateChanged.emit(false)`。挂在 `openStateChanged` 上的槽只要销毁那个**即将获得焦点**的控件，`Window.cpp:166` 就在悬空的 `focus_` 上做虚调用。注意 `focus_` 在 `:162` 已经被写成新值了，所以这不是"读旧指针"，是**读一个刚写进去、又在门里被销毁的成员**——REM3-G2 的成员重读正是为这个形状写的。

  ⚠️ **上面这段的最后一句对了，前面的路径错了。E19 封门时逐步走过一遍，本轮更正**：那个槽若用**树内移除**（`removeChild`/`takeChild`/`clearChildren`）去"销毁那个即将获得焦点的控件"，`announceDetached` → `Window::widgetDetached` 会**先**把 `focus_` 清空——R1 就有，且是**按离场子树的每个节点**发的，所以孙子也覆盖。于是 `:166` 的 `if (focus_)` 根本不成立，**这条路径今天不会崩**。E19 把它写成了用例的第一块，在无守卫的构建上实测**存活**。
  **真正的洞是另外两个形状**（两条都实测出了红态，见 §11.14）：(i) **不走 `takeChild` 的销毁**——`~Widget` 对 Window 不发任何通知（ADR-R2-11 §3 的有意设计），所以一个孤儿控件、或应用早先摘下现在才释放的子树，`focus_` 无人清空；(ii) **窗口自己死在门里**，那是 `widgetDetached` 从来就管不着的。
  **登记这条更正本身**：`widgetDetached` 的记账**没有洞**，洞在"哪些死亡会经过它"。把"能到达应用代码"当成"能崩"，是本族第七次同形错误——**门是必要条件，不是充分条件**。

#### "看过了、不需要封"清单（E9 复核的免检项；与 #11、#13b 同一条规矩：可复核的理由 > 沉默）

* **`platform/Platform.hpp` 的 21 条非析构 `virtual`（`Platform` / `PlatformWindow` 的接口方法）—— 免检，理由是结构性质而不是用法观察。** `Platform& platform()`（`Platform.hpp:151`）是全库取得实现的**唯一**入口，而**库里没有任何 setter / 注册点**能把应用自己的实现装进去：`grep -rn "setPlatform\|installPlatform" include src` **输出为空**。所以这 21 条虚函数的动态类型只可能是库自己的那一个实现，覆写它们**没有接口**。
  ⚠️ **这不是"今天没人这么用"，是"没有安装接口"**——两句话的分量差着一个数量级，本族六次复发里"今天没人这么用"输了六次。
  **⚠️ 触发条件，写进表里**：**哪天出现任何形式的实现安装点**（`setPlatform` / `installPlatform` / 构造函数注入 / 工厂注册 / 测试替身钩子），**这 21 条一起进表**，按 P1 逐条判危险性。这条 grep 应当进 §11.9 的 lint：它是一条**两行的机器判据**，比一段免检说明可靠。
  ⚠️ 注意这条免检**只覆盖虚函数**。同一个头文件里 `PlatformWindow` 的 7 个公有 `std::function` 成员是**另一回事**，它们是应用可赋值的，见 §11.9 末尾登记的第四类原语。
* **`core/Event.hpp` 的 1 条、`widget/Window.hpp` 的 1 条、`widget/GroupBox.hpp` 的 1 条、`widget/MenuButton.hpp` 的 2 条**：`GroupBox::layoutRect()` 已是 #29；`MenuButton` 的两条已是 N8；其余两条是 `asWindow()` 家族（N9）与事件类型标签，无门后成员访问。
* **`render/StyleSheet.hpp` 的 5 条（`StyleSubject`）**：主语已按 P1 新定义纳入，但库内调用点（`style()` / `styleState()` / `styleType()`）已由 #2 / #23 覆盖，无第三处。

**本轮 ✅ 的规模变化（必须让架构团队知道）**：第 1 版是 2 个检查点 / 2 个函数 / 2 个文件；本版是 **5 个检查点 / 3 个函数 / 2 个文件**（新增 `ScrollArea::setContentSize` 的 CP-C1/CP-C2，以及 CP-S2 的检查项从 3 变 2）。多出来的那个函数在**同一个文件**、**同一种形状**、上方 130 行处；不带上它，E5 复核时会立刻问"为什么隔壁那个一模一样的没改"。

---

#### 【E20】lint 首扫产出的候选登记（`tools/lint-door-coverage.ps1`，机器生成）

> 状态：**登记，不是判定。** 本小节的每一行都是 §11.9 那条 lint 用 §11.4 的谓词**扫出来**的，不是人挑出来的；**逐点危险性（门后有没有经 `this`/成员的读写）一条都没有判**。定级与轮次是**家族级**的，与 #27 对 P3 家族的处理方式同一条规矩（"逐点判定要读 60 个函数体，那是一次独立的扫描任务"）。
> **它存在的唯一理由**：让每一个候选**有归宿**（§11.9 性质 2），从而让**第 76 个**候选——下一扇没人注意到的新门——能把门禁打红。**任何人把某一行的定级往下改、或把行删掉来让 lint 变绿，就是把 lint 关掉。**

**这张表是怎么来的，以及它证明了什么。** §11.4 上面那张表是**人**按调用点扫出来的，N1–N9 是**人**按声明侧重扫出来的。lint 第一次跑，在**同一份代码**上，扫出 **76** 个候选，其中 **13** 个能在上面两张表里找到归宿（`Widget.cpp` 的 `childAppended`/`childRemoved`/`paintTree`/`animationTickTree`/`window`/`removeChild`、`WindowHeader::relayoutItems`、`PushButton::sizeHint`、`MenuButton::onMouse`、`AppWindow::setHeaderVisible`、`SelectBase` 的 `open`/`refreshRows`/`showCustomPopup`），**11** 个已经带着游标（`DeathWatch` / `BubbleGuard` / `GeometryGuard` / `LayoutGuard`），**其余 75 个此前哪张表里都没有**（`Widget::sizeHint` 补上行号侧的函数名后归入 #10）。

**"这 75 个都是真门吗？" 不是这张表要回答的问题，也不该由这张表回答。** 谓词是**保守多报**的（§11.9 白纸黑字），多报的方向是"逼一个人做决定并留档"。但**其中有几条不需要等到 W2 就该被读一遍**：

* **L75-X `Window::widgetDetached`**——**这是本次扫描最该被带走的一条**。它的门是 `closePopup()`（→ `popupClosed.emit` → 应用槽），门后紧跟着 `if (focus_ == w) focus_ = nullptr;` 等**三次经 `this` 的写**，而**一个游标都没有**。函数自己的注释甚至写着"Re-tested after that emit, which can move the focus or open another popup"——**想到了信号会动状态，没想到信号会把 `this` 拆掉**。
  ⚠️ 而 §11.14 里 N4 的整条论证（"`widgetDetached` 的记账没有洞"）**正是站在这个函数身上**。记账没有洞是对的；**记账的那一帧自己没有游标**，是另一件事。定级 **S1**，与 N1/N4/#16 同级。
  ⚠️⚠️ **L75-X 的触发路径本轮被收窄了一半（R2 第 4 轮安全评审 C6），登记下来免得下一轮照着错的那一支去修**：`closePopup()` 里有**两支**能到应用代码——`:126 p->setVisible(false)` 和 `:130 popupClosed.emit()`。**emit 那一支到不了"`this` 死在门里"**：`popupClosed` 的宿主就是这个 `Window`，而契约 D7 明令禁止槽销毁信号自己的宿主（`Widget.hpp:105-108`），这正是 §11.4 hazard 条款那条唯一豁免。**真正走得通的只有 `setVisible` 那一支**：`setVisible` → `markLayoutDirty` → 一趟 pass → `setGeometry` → 应用的 `onGeometryChanged` → 销毁这个 `Window`。⇒ 下一轮写用例的人**别从 `popupClosed` 的槽入手**，那条走不通（这就是 §12.5 教训 12 说的"门是必要条件不是充分条件"，只是这次省的是下一轮的时间而不是本轮的）。
* **L54-C `Window::closePopup` 与 L75-X 是同生共死的一对，定级本轮拉平到 S1（C6）**。`widgetDetached:141` 的第一句就是 `if (popup_ == w) closePopup();`——**L54-C 就是 L75-X 那扇门的里面**，而且 **`closePopup` 才是发 emit、真正把控制权交出去的那一帧**。两条同形（门后都有经 `this` 的写：`closePopup:127-129` 写 `hovered_` / `pressGrab_` 并 `update()`），却一条 S2 一条 S1，**这个不一致会让下一轮先做高的那条、然后发现低的那条才是根**——修 `widgetDetached` 而不修 `closePopup`，等于在门外面上锁。⇒ **L54-C 提为 S1，与 L75-X 同批做，一条用例应当同时覆盖两帧。**
* **家族 F（6 条）**是平台事件入口：`Win32Platform::handle` / `paint` 与 `Window::handleMouse` / `handleKey` / `handlePaint` / `handleResize`。这些是**每一次输入事件的最外层帧**，门后继续读成员。它们与 N5（`animationTickTree`，S1）是同一形状，只是站在树的更外面。
* **家族 A（21 条）**是 #2 / #23 的族（`styleState()` 之后接着读自己的成员），**REM3-RES-2 已裁定走契约不走守卫**，所以整族 S3/W3。**它的规模是新信息**：表里原本只有 2 个站点，实际是 **21** 个，遍布每一个控件的 `onPaint` / `sizeHint`。"收紧 `styleState()` 覆写的契约"这个处方的**收益面**因此比表里看起来大一个数量级。
* **家族 G（2 条）是谓词的名字碰撞误报**，登记而不掩盖：`Painter::fillArcRing` 与 `VectorPath::fromSvg` 里的 `close()` 是 `VectorPath` / `BLPath` 的路径闭合，不是 `SelectBase::close()`。**虚调用按名字扫就是会这样**，这正是 §11.9 承认的"近似"的代价；把它写进表里，比在脚本里加一条"忽略 `Painter.cpp`"的暗规则可复核。

**家族与定级对照：**

| 族 | 是什么 | 级 | 轮 | 依据 |
|---|---|---|---|---|
| **A**（21） | `styleState()` / `displayText()` / `arrowWidth()` 等只读渲染与度量帧里的虚调用 | S3 | W3 | 并入 #2 / #23 的族，REM3-RES-2：处方是收紧契约 |
| **B**（19） | P3 `.emit(` | S2 | W2 | 就是 #27 登记的那个家族，本表把它逐点展开 |
| **C**（15） | popup / 菜单的生命周期帧（`openPopup` / `closePopup` / `ensurePopup` / `open` / `close` …） | S2（**L54-C 除外：S1**） | W2 | 与 #22 同形，宿主不是 `this`，D7 不豁免。⚠️ **家族级定级压不住成员级事实**：`Window::closePopup`（L54-C）是 L75-X 那扇门的里面，与它同生共死，本轮按 C6 提为 S1 |
| **D**（5） | 布局与度量帧（`arrange` / `gather` / `measureAxis` / `measureFor`） | S2 | W2 | R2 的停车场 + `hostAlive()` 覆盖了其中一部分，覆盖到哪未逐点核对 |
| **E**（3） | 构造函数帧（`AppWindow` / `ScrollArea` / `Window`） | S3 | W3 | 对象尚未交给应用，门是 `add<T>()`；无已知触发路径 |
| **F**（6） | 平台与窗口的事件入口 | S2 | W2 | 与 N5 同形，见上 |
| **G**（2） | **谓词误报**（名字碰撞） | S3 | W3 | 不是门；登记以免下一个人重新发现一次 |
| **H**（3） | 其余单点（`invalidateSizeHint` / `onExpanderToggled` 之后） | S2 | W2 | 无家族，逐点判定归 W2 |
| **X**（1） | `Window::widgetDetached` | **S1** | W2 | 见上 |

⚠️ **顺带扫出来的一条表内不一致（已核实并更正）**：#20 / #21 两行的主语原本写作 `Cascader.cpp` 的 `relayoutColumns`，而**今天的 `Cascader.cpp` 里没有这个函数**。

**核实结论：是主语写错了，不是指别处。** 两条依据，都对 diff 可判定：

1. **行号自己指认了函数**——`:143`（`columns_[i]->setVisible(false)`）、`:152` / `:159`（`col->setGeometry` / `columns_[level]->setGeometry`）、`:161`（`popupBox_->setGeometry`）**全部落在 `Cascader::rebuildColumns()` 的函数体内**（`Cascader.cpp:90-164`），而且表里描述的"循环内按下标读 `columns_`"逐字对得上 `:142` 与 `:149/:156` 那三个循环。
2. **`relayoutColumns` 在本仓库的历史里从未存在过**——`git log -S relayoutColumns` 只命中文档提交，一次源码提交都没有。它不是"改过名没同步"，是**从来就没有过这个名字**：写表的人按 `WindowHeader::relayoutItems`（#19，紧挨着的上一行）的形状顺手编了一个。

⇒ **#20 / #21 的主语已改为 `rebuildColumns`**（#21 原来连函数名都没有，一并补上）。**只改文档，`Cascader.cpp` 一个字未动**——本轮的判定是"表错了"，不是"代码该改名"。改完之后这两行与本表 **L42-C**（`Cascader.cpp`（`rebuildColumns`），P2 add ×7）归档同一个 (文件, 函数) 键；**L42-C 保留**，它记的是 lint 独立扫出来的门数，与 #20/#21 手工记的两处不是同一份事实。

⚠️ **顺带更正首扫记录自己的一处措辞**：当时写的是"lint 因此把 `rebuildColumns` 当作未归档候选登记"——**这半句不对**。实测（`0 UNARCHIVED`）它一直是**已归档**的，归宿是 L42-C；真正成立的只有另外半句——**#20 / #21 归档不到任何东西**。所以这次不一致不是被"红灯"抓到的，是被**归宿表**抓到的：一个编造出来的函数名不会让门禁变红，它只会让两行手工登记**静悄悄地什么都不管**。

**这正是"表不是扫出来的"的第三个实例，只是这次是机器发现的**——而它能被发现，恰恰是因为 lint 的键是 (文件, **函数**) 而不是 (文件, 行号)：一个编造出来的函数名归档不到任何东西，一个偏了一百行的行号却看不出来。

| # | 位置（文件 :: 函数） | 首个门原语 | 门数 | 级 | 轮 |
|---|---|---|---|---|---|
| L01-A | `CheckBox.cpp`（`onPaint`） | P1 styleState | 1 | S3 | W3 |
| L02-A | `CheckBox.cpp`（`sizeHint`） | P1 styleState | 1 | S3 | W3 |
| L03-A | `GroupBox.cpp`（`onPaint`） | P1 styleState | 1 | S3 | W3 |
| L04-A | `Label.cpp`（`color`） | P1 styleState | 1 | S3 | W3 |
| L05-A | `Label.cpp`（`pixelSize`） | P1 styleState | 1 | S3 | W3 |
| L06-A | `LineEdit.cpp`（`onPaint`） | P1 styleState | 1 | S3 | W3 |
| L07-A | `MenuButton.cpp`（`onKey`） | P1 arrowWidth | 1 | S3 | W3 |
| L08-A | `MenuButton.cpp`（`onPaint`） | P1 arrowWidth | 1 | S3 | W3 |
| L09-A | `ProgressBar.cpp`（`onPaint`） | P1 styleState | 1 | S3 | W3 |
| L10-A | `PushButton.cpp`（`onPaint`） | P1 styleState | 1 | S3 | W3 |
| L11-A | `PushButton.cpp`（`palette`） | P1 styleState | 1 | S3 | W3 |
| L12-A | `RadioButton.cpp`（`onPaint`） | P1 styleState | 1 | S3 | W3 |
| L13-A | `RadioButton.cpp`（`sizeHint`） | P1 styleState | 1 | S3 | W3 |
| L14-A | `SelectBase.cpp`（`onPaint`） | P1 displayText | 4 | S3 | W3 |
| L15-A | `Separator.cpp`（`onPaint`） | P1 styleState | 1 | S3 | W3 |
| L16-A | `Separator.cpp`（`sizeHint`） | P1 styleState | 1 | S3 | W3 |
| L17-A | `Slider.cpp`（`onPaint`） | P1 styleState | 1 | S3 | W3 |
| L18-A | `SpinBox.cpp`（`onPaint`） | P1 displayText | 1 | S3 | W3 |
| L19-A | `StyleSheet.cpp`（`resolve`） | P1 styleMatchesType | 6 | S3 | W3 |
| L20-A | `ToggleSwitch.cpp`（`onPaint`） | P1 styleState | 1 | S3 | W3 |
| L21-A | `WindowHeader.cpp`（`onPaint`） | P1 styleState | 1 | S3 | W3 |
| L22-B | `AlarmList.cpp`（`acknowledge`） | P3 emit | 1 | S2 | W2 |
| L23-B | `AlarmList.cpp`（`acknowledgeAll`） | P3 emit | 1 | S2 | W2 |
| L24-B | `AlarmList.cpp`（`add`） | P3 emit | 1 | S2 | W2 |
| L25-B | `Cascader.cpp`（`chooseAt`） | P3 emit | 1 | S2 | W2 |
| L26-B | `ComboBox.cpp`（`setCurrentIndex`） | P3 emit | 1 | S2 | W2 |
| L27-B | `DataHub.cpp`（`drain`） | P3 emit | 1 | S2 | W2 |
| L28-B | `DatePicker.cpp`（`onMouse`） | P3 emit | 1 | S2 | W2 |
| L29-B | `Gauge.cpp`（`setValue`） | P3 emit | 1 | S2 | W2 |
| L30-B | `LineEdit.cpp`（`onFocusChanged`） | P3 emit | 1 | S2 | W2 |
| L31-B | `LineEdit.cpp`（`onKey`） | P3 emit | 2 | S2 | W2 |
| L32-B | `ListView.cpp`（`onKey`） | P3 emit | 2 | S2 | W2 |
| L33-B | `ListView.cpp`（`onMouse`） | P3 emit | 2 | S2 | W2 |
| L34-B | `MenuButton.cpp`（`trigger`） | P3 emit | 1 | S2 | W2 |
| L35-B | `PopupList.cpp`（`onMouse`） | P3 emit | 3 | S2 | W2 |
| L36-B | `PushButton.cpp`（`activate`） | P3 emit | 1 | S2 | W2 |
| L37-B | `SearchableSelect.cpp`（`setQuery`） | P3 emit | 1 | S2 | W2 |
| L38-B | `Skin.cpp`（`apply`） | P3 emit | 1 | S2 | W2 |
| L39-B | `TextArea.cpp`（`onFocusChanged`） | P3 emit | 1 | S2 | W2 |
| L40-B | `WindowHeader.cpp`（`onMouse`） | P3 emit | 3 | S2 | W2 |
| L41-C | `Cascader.cpp`（`open`） | P2 add | 2 | S2 | W2 |
| L42-C | `Cascader.cpp`（`rebuildColumns`） | P2 add | 7 | S2 | W2 |
| L43-C | `DatePicker.cpp`（`open`） | P2 add | 3 | S2 | W2 |
| L44-C | `MenuButton.cpp`（`closeMenu`） | P2 closePopup | 1 | S2 | W2 |
| L45-C | `MenuButton.cpp`（`ensureMenu`） | P2 add | 1 | S2 | W2 |
| L46-C | `MenuButton.cpp`（`openMenu`） | P2 setGeometry | 2 | S2 | W2 |
| L47-C | `SelectBase.cpp`（`close`） | P2 closePopup | 2 | S2 | W2 |
| L48-C | `SelectBase.cpp`（`ensurePopup`） | P2 add | 6 | S2 | W2 |
| L49-C | `SelectBase.cpp`（`hideCustomPopup`） | P2 closePopup | 1 | S2 | W2 |
| L50-C | `SelectBase.cpp`（`onEnabledChanged`） | P1 close | 1 | S2 | W2 |
| L51-C | `SelectBase.cpp`（`onFocusChanged`） | P1 close | 1 | S2 | W2 |
| L52-C | `SelectBase.cpp`（`onKey`） | P1 open | 10 | S2 | W2 |
| L53-C | `SelectBase.cpp`（`onMouse`） | P1 close | 2 | S2 | W2 |
| L54-C | `Window.cpp`（`closePopup`） | P2 setVisible | 1 | **S1** | W2 |
| L55-C | `Window.cpp`（`openPopup`） | P2 closePopup | 3 | S2 | W2 |
| L56-D | `BoxLayout.cpp`（`arrange`） | P2 setGeometry | 1 | S2 | W2 |
| L57-D | `BoxLayout.cpp`（`gather`） | P1 sizeHint | 1 | S2 | W2 |
| L58-D | `GridLayout.cpp`（`arrange`） | P2 setGeometry | 1 | S2 | W2 |
| L59-D | `GridLayout.cpp`（`measureAxis`） | P1 sizeHint | 1 | S2 | W2 |
| L60-D | `Layout.cpp`（`measureFor`） | P2 measure | 3 | S2 | W2 |
| L61-E | `AppWindow.cpp`（`AppWindow`） | P2 add | 5 | S3 | W3 |
| L62-E | `ScrollArea.cpp`（`ScrollArea`） | P2 add | 2 | S3 | W3 |
| L63-E | `Window.cpp`（`Window`） | P2 setGeometry | 4 | S3 | W3 |
| L64-F | `Win32Platform.cpp`（`handle`） | P1 onMouse | 3 | S2 | W2 |
| L65-F | `Win32Platform.cpp`（`paint`） | P1 onPaint | 1 | S2 | W2 |
| L66-F | `Window.cpp`（`handleKey`） | P2 closePopup | 1 | S2 | W2 |
| L67-F | `Window.cpp`（`handleMouse`） | P2 closePopup | 1 | S2 | W2 |
| L68-F | `Window.cpp`（`handlePaint`） | P1 onPaint | 1 | S2 | W2 |
| L69-F | `Window.cpp`（`handleResize`） | P2 setGeometry | 3 | S2 | W2 |
| L70-G | `Painter.cpp`（`fillArcRing`） | P1 close | 1 | S3 | W3 |
| L71-G | `VectorPath.cpp`（`fromSvg`） | P1 close | 1 | S3 | W3 |
| L72-H | `LineEdit.cpp`（`emitChanged`） | P2 invalidateSizeHint | 1 | S2 | W2 |
| L73-H | `LineEdit.cpp`（`setText`） | P2 invalidateSizeHint | 1 | S2 | W2 |
| L74-H | `TreeSelect.cpp`（`onRowActivated`） | P1 onExpanderToggled | 1 | S2 | W2 |
| L75-X | `Window.cpp`（`widgetDetached`） | P2 closePopup | 1 | S1 | W2 |

---

#### 【E21】候选侧扫描根扩到 `include/` 之后多出来的两条（family T）

> 状态：**登记，不是判定**，与上面 E20 那张表同一条规矩。**代码本身定级 S3**（今天没有已知触发路径）；**lint 漏掉它们是 S1**，那一条已在本轮关闭，见 §11.9 的 E21 记录。

**这是同一个洞的第三次出现，而且前两次的修都没有碰到它。** §11.4 P1 那条更正（`layoutRect()` 漏掉是因为"虚调用 grep 不出名字"）与 §12.5 教训 2 的第二层（"扫描根写成 `Widget.hpp` 而不是 `include/geeyoou/**`"）**两条都修在了声明侧**——lint 拿 `include/geeyoou/**` 生成 P1 名字表，这一条从 E20 起就是对的。**候选侧的根却一直是 `src/`**（`tools/lint-door-coverage.ps1` 里的 `$SrcDirs`），而这个库的代码**不全在 `src/`**：头文件里的模板与 inline 函数体，从来没有被 `Split-CppFunctions` 切过。

**"哪里有 `.cpp`" 和 "哪里有代码" 是两个问题，扫描根要按后一个写。** 这句话是本轮唯一值得从这段里带走的东西。

**扩根之后一并暴露的两个机械陷阱**（都实测，都记在脚本的修复处；**只扩根不修这两条等于什么都没做**）：

1. **`Get-ChildItem -LiteralPath <dir> -Recurse -File -Include *.cpp` 根本不过滤。** 实测（PowerShell 5.1.19041）：`-LiteralPath include` 返回 **59** 个文件（全是 `.hpp`），`-Path include` 返回 **0**。也就是说旧扫描里那个"只扫 `.cpp`"从来不是一个过滤器，只是 `src/` 里恰好几乎没有别的东西——**恰好有一个**（`src/render/VectorPathImpl.hpp`），它一直在被当作 TU 扫，纯属侥幸对了。现改成直接判扩展名。
2. **`template` 在 `NotAFunctionHead` 里，于是每一个模板函数体都被读成"不是函数"。** 切分器遇到 `template <class T, class... Args>` 开头的片段就判"不是函数、往里降一层找函数"，进去找不到（模板体内没有嵌套函数定义），**整个函数体连同它的门一起从未被扫过**。实测：`AppWindow.hpp` 切出 `header` / `content` / `isBorderVisible` 三个，**没有 `setContent`**。现改成在 `NotAFunctionHead` 判定**之前**按尖括号配平剥掉 `template <...>` 前缀（不是 `.*?>`：`template <class T, std::vector<int> V>` 有嵌套，惰性匹配会停在第一个 `>`）。

**扩根后的实测规模**：源文件 47 → **106**（多出 59 个头文件），头文件里切出 **318** 个函数体，新增候选 **2** 个，其余 316 个函数体全部落选（绝大多数是一行访问器，门后无代码）。**没有出现"为了变绿要收窄谓词"的压力**，两条照单登记。

| # | 位置（文件 :: 函数） | 首个门原语 | 门数 | 级 | 轮 |
|---|---|---|---|---|---|
| L76-T | `AppWindow.hpp`（`setContent`） | P2 add | 2 | S3 | W3 |
| L77-T | `WindowHeader.hpp`（`addTrailingItem`） | P2 add | 1 | S3 | W3 |

**family T = 头文件里的模板便利构造器**：门是 `add<T>(...)`（P2，经 `addChild` → `childAppended` → `Layout::onChildAppended` + `markLayoutDirty`，够得到应用代码），门后**有经 `this` 的写**——

* **L76-T `AppWindow::setContent<T>`**（`AppWindow.hpp:78-84`）：门 `:80 content_->add<T>(...)`；门后 `:81 fill_ = w;`（**写**）、`:82 relayout()`（**第二扇门**，就是 #16 那三个 `setGeometry` 所在的帧）。
* **L77-T `WindowHeader::addTrailingItem<T>`**（`WindowHeader.hpp:108-115`）：门 `:110 add<T>(...)`；门后 `:111 slots_.push_back({w,...})`（**写**）、`:112 pendingGap_ = 0.0f`（**写**）、`:113 relayoutItems()`（**#19 那一帧**）。

**为什么定 S3 而不是 S1**：两处的门后确有写，形状与 #16 同级；**但今天没有已知触发路径**——要走通得让 `add<T>` 里那趟 `markLayoutDirty` 当场起一趟 pass、pass 里的 `onGeometryChanged` 再销毁这个 `AppWindow` / `WindowHeader` 自己，而 `setContent` / `addTrailingItem` 的主流用法是在窗口构造期（README:170/176 的样例正是如此），那时对象还没交给应用。**这是"今天没有已知触发路径"，不是"没有接口"**——按 §12.5 教训 11 的分量，它只值 S3，不值免检；两条 API 都是 public 模板，轮次 W3 与 family E（构造函数帧）同批。

⚠️ **L77-T 与 #19 是同一条链上的两环，读的时候要一起读**：`addTrailingItem` 的门后调 `relayoutItems()`，而 #19 说的正是 `relayoutItems()` 里 range-for 中的 `setGeometry`。#19 的触发路径（见下面 §11.4 对 #19 处方的更正）**反过来又要经过 `addTrailingItem` 的 `slots_.push_back`**。**两条的定级不同是对的**：L77-T 要求"窗口自己死在门里"（S3），#19 只要求"应用在几何回调里再加一个尾部项"（S1，无需任何对象死亡）。

---

### 11.5 开销量化

**已实测的部分**（`cl /std:c++20 /W4 /permissive- /O2`，MSVC 14.50 / VS18 x64，`/FA` 取汇编；探针在 scratchpad，不入库）：

| 段 | 实测 | 说明 |
|---|---|---|
| 单个守卫构造 | **5** | 1 次链表载入 + 2 次存字段 + 1 次存回全局（+ Debug 的 `assert` 一条 `test`/`jcc`） |
| 三个守卫构造（**探针形状**：三个守卫站在同一个已在寄存器里的指针上） | **9** | 编译器把 LIFO 的三个合并了。⚠️ **这不是落点的形状**——见下方 E3/E4 落地后的实测更正 |
| 守卫析构（无论几个） | **2** | `mov rax,[a.outer]` / `mov [g_deathWatch],rax` |
| 一次 `alive()` | **2** | `cmp qword ptr [rsp+k], 0` + `je` |

**⚠️ 上面那句"三个守卫的全局争用与一个守卫完全相同"只对探针成立**，落点上不成立；正确的说法见下。

#### 落点实测（E3/E4 落地后，Leo 在真实 TU 上 `/FAsc` 取汇编——出门条件 §11.7 第 4 条，本轮兑现）

真实 include 链、真实 CMake flags（`/W4 /permissive- /utf-8 /Zc:__cplusplus /O2` 加 CMake 注入的默认项），不是手写 `cl` 命令行：

| 现场 | 构造 | 检查 | 析构 | **实测合计** | 曾经的推算 |
|---|---|---|---|---|---|
| `GroupBox::sizeHint()` 受保护区 | 5 | CP-G1 = 2 | 2 | **9** | 9 ✅ |
| `ScrollArea::relayout()` 有 layout 的那条分支 | **17** | CP-S1 = 11、CP-S2 = 7 | 2 | **≈38** | ≈30 ⚠️ |
| `ScrollArea::setContentSize()` | **18** | CP-C1 = 10、CP-C2 = 11 | 2 | **≈43** | ≈35 ⚠️ |

**为什么推算低估了三守卫的构造（9 → 17/18）——这条方法学教训比数字本身值钱。**

第 1 版的"三个守卫构造合并为 9 条"**是实测的**，但测的是**另一个形状**：探针里三个守卫站在同一个**已经在寄存器里**的指针上，中间没有任何别的代码，编译器于是把对 `g_deathWatch` 的三次读改写合并成一次。真实落点不是这个形状——`DeathWatch vpw(viewport_)` / `DeathWatch ctw(content_)` 各自要先 `mov reg,[this+off]` 把成员载进来，这些**成员载入插在三次构造之间**、把它们隔开，编译器无法再合并。对 `g_deathWatch` 是 **3 次**独立的读改写，不是 1 次。

改正后的成本模型：**全局争用与守卫个数成正比，一个守卫一次读改写**；三守卫的现场付三次。这仍然可以忽略（分母见下），但理由不能再是"编译器帮我们合并了"。

**教训（这是本条更正真正的产出）**：探针能证明"这样写能编译、类型形状对、语义对"——§11.8 那六条仍然全部成立。探针**不能**证明落点的指令数，因为**落点的形状由周围的代码决定，而探针里没有周围的代码**。指令数这一类断言只有一种有效测法：**在真实 TU、真实 flags、真实 include 链上取 `/FAsc`**。写进出门条件的那一条就是为此存在的，本轮是它第一次真正被兑现。

**成员重读比推算便宜**：实测 **2~3 条**——编译器把 `content_ != ct0` 折成 `cmp [rbx+256],rdi` + `jne`，不必先把成员载进寄存器。推算按 3 条，方向对、量级对。

**第 1 版那句"全函数除两次门调用外共 23 条指令"作废**：它按 6 次 `alive()` 计价，而正确的检查项是 5 项且其中 2 项不是 `alive()`（§11.3 CP-S2 的理由）。

#### ADR-R2-01 的指令层面实证（正面结论，本轮新增）

`ScrollArea::relayout()` 开头的 `if (content_->layout())` 编译成一条 `je $LN2`，**直接跳过全部三个守卫的构造**——不是"守卫很便宜"，是**一个游标都不取**。库里今天的每一个 `ScrollArea`、以及五个绝对坐标 showcase 页走的正是这条路径。ADR-R2-01 在这里不是被引用的，是被汇编证明的。

#### E5/E6 两处新护栏的成本（§11.10）

| 现场 | 成本 | 说明 |
|---|---|---|
| M-2（`Layout::measureFor` 的度量深度上限） | **2**：`cmp` 一个热全局 + `jb` | 与它上面那条 `if (buffersBusy_)` 同量级；每次 `sizeHint()` 到达 layout 时付一次 |
| M-1（`Widget::contentRect` 的 `layoutRect()` 门） | **5 + 2 + 2 = 9** | 一个 `DeathWatch` 加一次 `alive()`，每趟 arrange 一次。它在 `runLayoutIfAny` 内部，**不装 layout 的 widget 一条都不执行**——与 ADR-R2-01 同一条理由 |
| M-1 的调用侧（`runLayoutIfAny`） | **2** | 复用已在栈上的 `LayoutGuard`，只多一次 `alive()`；那个守卫本来就要构造 |

**没有分支预测灾难**：所有条件跳转都是「本帧栈槽/寄存器 vs 0 或 vs 另一个栈槽」，生产中**恒不跳转**，稳态误预测 ≈ 0；无间接跳转、无数据相关循环、无 `switch`。对照被否决的替代方案（"用完再扫一遍 `children()` 确认指针还在"）：那是 O(N) 次指针追逐载入且分支结果数据相关——**那才是分支预测灾难。**

**同一帧的分母**：`:158` 的 `content_->sizeHint()` 要把整页 layout 测一遍，按 §10.2 每个 `Label` 至少一次 `BLGlyphBuffer` + 一次 shaping，`PageOps` 有几十个。量级 **10⁴ ~ 10⁵ 条指令**，外加 Blend2D 自己的运行时分配。守卫占 **≲0.1%**。

**`~Widget` 的增量**：**零**。HEAD 已经有第四行 `cancelOn(g_detaches, this)`（`:347`），本设计只改它的名字。（相对 E17 之前是 3 条指令，冷路径。）

**ADR-R2-01「不装 Layout 的 widget 不为引擎付代价」继续成立**，逐条：

* `ScrollArea::relayout()` 的守卫落在 `if (content_->layout())` **内部**——今天库里每一个 `ScrollArea`、以及 5 个绝对坐标 showcase 页，**一条都不执行**。**指令层面已实证**：那个 `if` 是一条 `je $LN2`，直接跳过三次构造（见上）。
* `ScrollArea::setContentSize()` 的守卫是新增的无条件开销，但该函数只在应用改内容尺寸时调用（showcase 里是 `PageIcons.cpp:633` 的 layoutChanged 回调），不在每帧路径上。
* `GroupBox::sizeHint()` 的守卫只在有人问 hint 时才执行，而问的人只有 Layout 或应用。
* `sizeof(Widget)` **不变**（0 新成员）；`g_deathWatch` 是 8 字节 BSS，**且不是新增的**（E17 已有）。

---

### 11.6 残留（登记待裁；`REM3-` 前缀按 Elena 裁定）

* **REM3-RES-1（重要，E14 的题目）**：守卫是**帧作用域**的。`ScrollArea::viewport_` / `content_` 在守卫触发后**仍然是悬垂成员**，`contentSize()` / `scrollOffset()` / `content()` 下一次被调用时照样 UAF。
* **REM3-RES-2**：`PushButton::sizeHint():88` 的 `style(styleState())` 是 P1 门，门后 `:90-94` 读三个成员（表 #23）。**本轮不加守卫，而且我不建议下一轮加守卫**——`styleState()` / `onPaint()` 这一族门在库里有几十处，逐个上游标是把一条契约的代价摊到每个调用点。**正确的形状是立契约**：`onPaint()` / `styleState()` 的重写**不得修改控件树**（与 `Layout.hpp:168-174` 对 `Layout` 实现者立契约同形）。`sizeHint()` **不能**用这个办法（§11.0 已论证：那等于作废 D7），但 `styleState()`/`onPaint()` 可以，因为它们是纯查询语义。**这需要一次架构裁定**（会不会有应用在 `onPaint` 里改树？），所以排 W3 而不是 W2。
* **REM3-RES-3**：examples 侧的调用者义务（表 #25）未落地。
* **REM3-RES-4**：`takeChild` / `announceDetached` / `clearChildren` 的宿主帧——**已由 E17（`e9c283a`，配套 `c53fd66` / `24bbc28`）修复**，表 #12/#13/#14。E2 只改名不改逻辑。
* **REM3-RES-5（本版新增，Leo B4）**：`GroupBox::sizeHint()` 跨门撕裂读 `title_`（`:49` 门前 / `:62` 门后），**健康路径**可返回内部自相矛盾的 hint。既有缺陷，非 E1 引入；定级 S2，排 W2。修法二选一（门后重算 `top`，或把整个 hint 明确定义为"门前快照 + 门后增量"并写进 `sizeHint()` 的契约），二者都是行为改变，都要一条自己的用例。
* **REM3-RES-6（本版新增）**：`g_bubbles` 与 `g_geometries` **取消策略相同**（`announceDetached:281-282` + `~Widget:339/342`）且**都没有表外读者**——按 Q1 的判据，它们也是同一条链表的两个名字。本轮**不合并**：动的是全库重入压力最大的两条路径（`dispatchMouse`/`dispatchKey`/`setGeometry`），且没有任何缺陷推动它。定级 S3（纯整洁性），排 W3。**写在这里是因为我用来合并 `g_hints`/`g_detaches` 的判据，同样指向这两条**——只登记不执行是一个决定，不是一个疏忽。
* **REM3-RES-7（本版新增，给 E14/E15 的评审输入）**：见下。

#### 给 E14 / E15 的评审输入（Leo B7；这一条是本版必须由我拍板的）

**冲突链条**（Leo 的推演，我逐步核过，成立）：
1. Q4 定死：**detach 不取消死亡守卫**；
2. Elena 裁定 `onDescendantDetached` 向整条祖先链广播，`ScrollArea` 收到后处理悬垂的 `content_`——**把成员置空是这个钩子唯一说得通的实现**（不置空就没解决 REM3-RES-1）；
3. 于是在 `relayout` 的门（`:158`）**之内**发生一次 `takeChild(content_)`：`DeathWatch(content_).alive()` **仍为 true**（对象活着，只是被 detach 了，这正是 Q4 要的），但 `this->content_` 已是 `nullptr`；
4. 只查 `alive()` 的检查**全部通过**，然后 `:165 content_->setGeometry(...)` → **空指针解引用**。

**裁定：走方向 (a) —— 门后重读成员并与门前捕获值比对（已写进 REM3-G2 与 §11.3 的每个检查点）。方向 (b)（要求 E14/E15 的钩子"只标记不置空"）否决。**

理由三条：
1. **(b) 是让 E14 为 E1 的检查形状让路，而且让的是错的那一步。** E14 的任务就是消灭悬垂成员（REM3-RES-1）；"只标记不置空"留下的是**一个悬垂指针加一个必须与它保持同步的标志位**，每个访问器都得记得查那个标志位——同一个缺陷族的第六种形态。置空是对的实现。
2. **(a) 有既有形状可抄，不是新发明。** `stillAChild()`（`Widget.cpp:240-254`）与 `takeChild:405-409` 的"Re-found rather than reused: announceDetached can run popupClosed slots, and one of those may already have removed `child`"就是这条规则，只是用在下标上。E1 把它用在成员指针上。
3. **两者都需要，不是二选一。** 只重读成员：漏掉"成员没变、但指向的对象被销毁了"（**这正是今天的真实缺陷**，因为今天没有任何东西会去更新 `content_`）。只查游标：漏掉 B7 这条。**所以检查点是 `this` → 成员重读 → 游标，三段都要**，代价是每个成员多 3 条指令。

**E14 因此获得的自由与约束**：
* **自由**：钩子可以把 `content_`/`viewport_` 置空，E1 的检查点会把它读成"树在我脚下动了"并降级，不会崩。
* **约束（E14 必须遵守）**：(i) 钩子把成员置空**之后**，所有会解引用它的函数入口必须有空检查（REM3-G7），`ScrollArea::relayout:157` 的 `content_->layout()` 首当其冲；(ii) 钩子本身**是一扇门**（它跑子类代码），所以广播循环自己要按 §11.4 的谓词上守卫——E14 的设计里要有它自己的检查点表。

**REM3-RES-7 —— E17 两条新用例的覆盖假设要重审**（回答我自己 E17 报告的 §8.4）：`test_removal.cpp` 里 `take_child_survives_a_slot_that_destroys_the_host` 与 `clear_children_survives_...` **都只走一条门**：`widgetDetached` → `closePopup` → `popupClosed.emit`（`test_removal.cpp:526` 与 `:580` 两条用例，门分别在 `:556` 与 `:601` 的 `win.popupClosed.connect`）。**若 E14 的 `onDescendantDetached` 广播新增了一条会跑应用代码的门，这两条用例的覆盖假设就要重审**——它们证明的是"popupClosed 这条门后 takeChild/clearChildren 不崩"，不是"所有 detach 门后都不崩"。E14 要么复用同一组用例并把新门接进去，要么各加一条。

---

### 11.7 E2 施工清单（复签放行后才开工）

1. `Widget.hpp` 尾部新增 `detail` 块：`LiveCursor`（从 `Widget.cpp:76-79` 原样搬）、`LiveGuard<>`（从 `:88-108` 原样搬）、`extern LiveCursor* g_deathWatch`、`DeathWatch`、`deathWatchDepth()`、`frameDegraded()`。
2. `Widget.cpp`：
   a. 删掉搬走的两处定义，加最小适配：匿名 namespace 顶部 `using detail::LiveCursor;` **与 `using detail::LiveGuard;`**，其余各处一字不改。
      ⚠️ **两条 `using` 都是必需的，第二条容易漏**：`LiveGuard` 在本文件有**三个**使用点，其中 `BubbleGuard`/`GeometryGuard` 两个别名很显眼，而第三个是 `LayoutGuard` 的**成员声明** `LiveGuard<g_layouts> live_;`（HEAD `:191`），它躲在一个类体里。E2 施工时正是漏了它——只加 `using detail::LiveCursor;`、把两个别名逐个改成 `detail::LiveGuard<...>`，于是 `:191` 成了唯一没跟上的那处，编译器报 C7568「缺少参数列表」。用 `using` 而不是逐处加 `detail::` 限定，除了少改两行，更重要的是**让"搬家"在 diff 里读作换地址而不是换含义**，也就不存在"还有第几处没改"这个问题。
   b. `g_detaches` → `detail::g_deathWatch`（**定义**移出匿名 namespace，外部链接）；`using DetachGuard = LiveGuard<g_detaches>;` **整条删除**，三处现场改用 `detail::DeathWatch`（`:301`、`:400`、`:431`）；
   c. **重写 `:113-134` 那段"第四条链表"注释**——按 §11.1 的判据 1/2 重写，不是替换标识符：新文字要说清"按取消策略命名""**决策读者**为零是前提不是观察"（REM3-G6），并按本版更正把 `markLayoutDirty` 那条的机制写对（脏标记不丢，丢的是执行）；
   d. **改掉 `:264-269` 那段已被本设计作废的理由**：`DeathWatch` 收 `const Widget*`，"非常量引用是为了避免 `const_cast`"不再成立。**只改注释，不改 `announceDetached` 的签名**（签名归 E18）；
   e. `~Widget:347` 改名 `cancelOn(g_deathWatch, this);`；
   f. 定义 `detail::g_deathWatch = nullptr;` 与 `deathWatchDepth()`。
   g. **（本版新增，Leo 复签必改 3）** `:416 childRemoved(index)` 上方补注释：这是本函数的**第二道门**，其后不得再出现任何成员访问，否则 `:394-403` 的守卫作用域必须延长到函数末尾。见 §11.4 #13b。
3. `Layout.hpp` / `Layout.cpp`：`LayoutDiagnostics`（`Layout.hpp:273-280`）加 `framesDegraded`，实现 `detail::frameDegraded()`（饱和自增，与 `Layout.cpp:190-206` 的四个同写法）。
4. **契约改写（本版新增，取代下面那条"E2 明确不做"）**：`Layout.hpp:168-174` 的适用范围从"**实现 `Layout` 的类**的义务"扩写成"**任何调用 `sizeHint()` / `setGeometry()` 之后仍要读自身的代码**的义务"，点名四类调用者（Layout 实现 / 容器自身的 `sizeHint()` 转发 / 容器自身的 `relayout()` 等几何维护方法 / `protected virtual` 几何钩子的调用者），并写明**死亡后的行为约定**（REM3-G1 的降级形状）。同时在 `Widget.hpp` 的 `sizeHint()` 与 `setGeometry()` 声明处**各留一句反向引用**——下一个踩雷的人读的是 `Widget.hpp`，不是 `Layout.hpp`。**这是 §11.0 定的缺陷根因（契约写错了主语）的正面修复，不是文档整理。**
5. **一条正向用例**：守卫在**没有人死**时不改变任何行为——几何逐位相同、`Layout::measure` 次数不变、`framesDegraded` 保持 0、`deathWatchDepth()` 退栈回零。
   落点 `tests/widget/test_layout_engine.cpp`，紧接 `the_host_counter_returns_to_zero` 之后——同一个位置已经放着"引擎的记账回不回零"那一条，本设施的链表是同一类事实。实际落了**三条**，因为一条盖不住三件独立的事：
   * `a_death_watch_is_reachable_from_a_widget_subclass` —— 出门条件 5。在**库外的 TU** 里，从一个 `Widget` 子类的 **const 成员函数**（`GroupBox::sizeHint()` 的形状）和一个**非 const 私有方法**（`ScrollArea::relayout()` 的形状）各取一个守卫，零 `friend`；顺带钉住链表会嵌套、会退栈回零。**这条不编译就是设计错了**，而它现在编译。
   * `a_death_watch_survives_a_detach_and_not_a_destruction` —— Q4 的取消策略，也是 E2 唯一动过的那行行为（`~Widget` 的 `cancelOn`）：`takeChild` 之后守卫**仍然 alive**，`unique_ptr` 释放之后**才**翻假；子孙的守卫由子孙自己的 `~Widget` 取消，所以没有任何遍历。另钉一条容易被误读的实现事实：取消是把 `node` 置空，**不是**把节点摘链，所以 `deathWatchDepth()` 不会因为取消而变小。
   * `a_death_watch_costs_a_healthy_pass_nothing` —— 施工清单第 5 条本身。两棵**结构相同**的树跑同一串 50 个几何，一棵挂三个守卫、一棵不挂；比对每个孩子的 `geometry()` 逐位相同、`onGeometryChanged` 次数相同、`StackLayout::measures`/`arranges` 相同、`framesDegraded` 为 0。**两棵树而不是同一棵树跑两遍**：`naturalSize_` 会锁存、`layoutDirty_` 会沉淀，同一棵树的第二遍根本不是同一个实验。
6. **E2 到此为止**——调用点（`GroupBox` / `ScrollArea`）是 E3/E4，不在同一次提交里。
7. 提交约定：**每文件一个 commit**。

**E2 明确不做**：不动 examples；不改 `announceDetached` 的行为或签名；不给 `Widget` 加任何成员；不在 `GroupBox`/`ScrollArea` 里加任何检查点。
（第 1 版这里写的"不改 `Layout.hpp:168-174` 的契约文字"**已被上面第 4 条取代**：那条文字对 `Layout` 实现者仍然正确、但不完备，而"不完备"正是 §11.0 记的那个缺陷本身，留到 E3/E4 之后再改等于让两个落点先按一份写错主语的契约施工。）

#### E2 的出门条件（**本版新增；Leo 的 E7 实证素材 #2/#3**）

E2 交测前必须逐条给出证据，**"探针通过"不算数**：

1. **从干净目录全量构建**。`Widget.hpp` 一改，依赖它的每一个 .cpp 都要重编；任何一步用了增量构建，`static_assert` 可能根本没被重新求值就报"通过"。证据形式：**新建构建目录**（不得复用 `build/`、`build-debug/`、`build-asan/`）+ **贴出实际编译的 TU 数量**。
   ⚠️ **"31 个 .cpp"这个数字是错的，E2 实测后更正**：31 是 `src/widget/*.cpp` 的个数，而 `Widget.hpp` 的依赖面比 `src/widget/` 大得多。按 ninja 的依赖库（`ninja -t deps`，在干净 Release 树上）实测：**70 个目标文件**依赖 `include/geeyoou/widget/Widget.hpp`，来自 **61 个不同的 .cpp**——库 35 个（`src/widget/` 31 + `src/hmi/` 4）、showcase 14 个、tests 20 个，其中 6 个 showcase 页因为同时进 `showcase` 和 `geeyoou_tests` 两个目标而被编译两次。写下 31 会让下一个人以为看到 31 行 `Building CXX object` 就是全量了。
   **E2 的实测**：三份全新构建目录（均在仓库外的 scratchpad，`build/` 三兄弟一个都没碰），每份 `Building CXX object` 行数 —— Release **271**（我们 88 + blend2d/asmjit 183）、Debug **271**（同上）、ASan **257**（我们 74 + 183；ASan 腿 `GEEYOOU_BUILD_EXAMPLES=OFF`，少 14 个 showcase TU）。三份里 `src/widget/Widget.cpp.obj` 都在列，所以第 3 条成立。
   （唯一复用的是 blend2d/asmjit 的**已下载源码目录**，靠 `FETCHCONTENT_SOURCE_DIR_*` 指过去，省的是一次 clone；它们的 183 个 TU 仍然是从零编的，我们的一个 .obj 都没复用。）
2. **全库 `/W4 /permissive-` 零警告**。这是 **E2 的出门条件，不是 E1 的已验事实**（§11.8 已相应降级）。理由：§11.5/§11.8 的探针是**手写 `cl` 命令行**，缺 `/utf-8` 与 `/Zc:__cplusplus`（项目实际是 `CMakeLists.txt:96` 的 `/W4 /permissive- /utf-8 /Zc:__cplusplus` 外加 CMake 注入的默认 flags），也没走真实 include 链。`Widget.hpp` 是全库最热的头文件，模板搬进去会在**每一个** TU 里实例化。
3. **`Widget.cpp:36` 的尺寸 `static_assert` 在上述全量构建里被真正求值过**（0 新成员，论证上必然成立，但要编过才算）。
4. ~~**§11.5 的指令数按新形状重测**~~ —— **已交（E5/E6 轮）**，见 §11.5 的"落点实测"小节。结论不是"推算基本对"：三守卫构造的**实测是 17/18，推算是 9**，因为探针里那三个守卫站在同一个已在寄存器里的指针上，而落点上成员载入把它们隔开了、编译器合并不了。**方法学教训写在那一节里**，它比数字本身值钱。（成员重读的推算方向和量级是对的：推算 3 条，实测 2~3 条。）
5. **设施可被 `GroupBox`（const 成员函数内）与 `ScrollArea`（非 const 私有方法内）直接使用，零 `friend`**（§11.2 的两条可见性论证要在真实 include 链上兑现，不是探针上）。
6. **零分配**：soak 的 `liveAllocs` 序列证明（`test_layout_soak.cpp` 的四条采样序列，末值不得高于热身后的值）。
7. **一条正向用例**（施工清单第 5 条）：没有人死时，几何、度量次数、诊断计数**逐个不变**。
8. **门禁红的位置与形态不变**。E8 的复现器已提交而 E3/E4 未落地，所以 E2 落地后门禁**仍然是红的**——这是设计中的，不是回归。验收标准因此不是"三条腿全绿"，而是：**E2 前 / E2 后两份完整门禁日志逐条对比，红的位置和形态完全一致，没有新增任何红。**

   ⚠️ **E2 实测发现：Release 腿是双峰的，一份日志不足以做这个对比。** 复现器命中的是 UAF，而 Release 下一次 UAF 崩不崩取决于那块内存有没有被拿去复用，所以同一个二进制反复跑会给出两种结果。实测（`GY_SOAK_CYCLES=400`，每档 10 次）：

   | 树 | Release 腿 |
   |---|---|
   | HEAD（E2 前） | 9 崩 / 1 过 |
   | E2 只落设施（测试文件留在 HEAD） | 8 崩 / 2 过 |
   | E2 完整（含三条正向用例） | 8 崩 / 2 过 |

   三档在统计上分不开，**这不是 E2 引入的，也不是 E2 消除的**。**这条出门条件因此按下面的方式判，而不是按"两份日志逐字相同"**：
   1. **ASan 腿逐条对比**——它是三条腿里唯一确定性的，报告种类、条数、每条的 use/free 归属帧必须完全一致。这是主判据。
   2. **Debug 腿对比崩溃点**——落在哪个用例之后（`layout_engine` 套件跑完、`layout_soak` 的复现器开始处）。
   3. **Release 腿按崩溃率对比**，不按单次结果；同时要求它崩在与 Debug 腿相同的位置。
   4. 三条腿的**通过用例集合**只允许增加（E2 新增的三条正向用例），不允许减少或由 PASS 变 FAIL。

   **这条要写进后续每一轮**：在 E3/E4 把门关上之前，任何人拿一份"Release 腿是绿的"日志来主张"我没弄坏"或"我修好了"，都可能只是抽到了那 10~20% 的一面。E2 前的第一份基线日志（`Release tests [ok]`）就正是那一面，差点被当成"E2 把 Release 弄红了"。

---

### 11.8 可行性探针：哪些是**实测过**的，哪些还只是论证

E1 只出设计不写实现，但设计里有几条"编译器说了算"的断言，靠读代码断不了，所以在 scratchpad 里编了一份**形状等价**的独立探针跑过（`cl /nologo /std:c++20 /W4 /permissive- /EHsc /O2`，MSVC 14.50 / VS18 x64）。**仓库源码一个字未改**，探针不入库。

**已实测通过（Leo 可复核，不必采信我的话）：**

| 断言 | 结果 |
|---|---|
| `LiveGuard<>` 放头文件后，仍能以 `Widget.cpp` **匿名 namespace** 里的 `g_bubbles`（内部链接）作模板实参实例化 ⇒ 四条链表里**三条继续私有** | ✅ 探针零警告通过 |
| `DeathWatch : private LiveGuard<...>` + `using ...::alive;` 后，**`node()` 在类型层面不可达**（SFINAE 探测 `HasNode<DeathWatch>` 为 false，对 `LiveGuard<>` 为 true 作对照） | ✅ `static_assert` 通过 —— Q3 的 `const_cast` 安全性因此是**编译期事实** |
| 子类的 **const 成员函数**能用 `this` 直接构造，**零 `friend`**；非 const 私有方法用 `Widget*` 同样直接构造 | ✅ 两处都通过 |
| `sizeof(DeathWatch) == sizeof(LiveCursor) == 2 * sizeof(void*)`；拷贝/移动构造与拷贝赋值**全部不可用**；`nothrow` 可析构 | ✅ 五条 `static_assert` 全过 |
| 运行时：游标链表进出配平回 0；`~Widget` 的 `cancelOn` 确实把 `alive()` 翻成 false | ✅ 探针 `main` 打印 `depth 0 → 0`，`alive 1 → 0` |
| 单个守卫构造 5 / 析构 2 / `alive()` 2 条指令；三守卫构造合并为 9 | ✅ `/FA` 实测汇编 |

**⚠️ 上表的适用范围（本版降级，Leo E7 素材 #2）**：以上全部只证明**探针形状**成立。探针用手写 `cl` 命令行，**缺 `/utf-8` 与 `/Zc:__cplusplus`**，也没走真实 include 链，因此**"全库 `/W4` 零警告"不在已实测之列**——它是 E2 的出门条件（§11.7 第 2 条）。第 1 版把它写在"已实测"栏里，是把探针的结论冒充成了真实 TU 集的结论。

**仍然只是论证、未实测（复签请重点看这几条）：**

1. **Q4 的两个取消策略**（`~Widget` 加、`announceDetached` 不加）是**语义论证**，探针没有 detach 语义。E6 必须各写一条用例把两侧钉住：detach 后 hint 帧**继续正常返回**、destroy 后 hint 帧**降级返回且 `framesDegraded` +1**。
2. **§11.3 的降级返回值与五个检查点**是我定的，还没有任何代码消费过。E3/E4 落地后由 E6 断言逐位。
3. **B7 的成员重读**（REM3-G2 后半）没有对应用例，因为触发它需要 E14 的钩子先存在。E6 可以用一条**手工模拟**的用例代替：在 `sizeHint()` override 里 `takeChild(content_)` 并手动把 `content_` 置空——但那要 friend 或测试子类，**由 E6 决定值不值得**；不做的话，这条规则在 E14 落地前是**只有代价没有证据**的，我认这个账。
4. **§11.5 的成员重读指令数**是按实测单价推算的，未重测（§11.7 出门条件第 4 条）。
5. **枚举表 §11.4 的完整性**。本版按谓词重扫了 `src/widget/*.cpp`（扫描命令见 §11.9），但"完整"这个词的分量应由 E5 独立复核后再下——**这正是 R2 判 FAIL 的那个位置，我不自评。**

#### 证据标准：**一次被丢弃的读不是一次读**（E18 补记，Leo 判定的归属地）

本节的题目是"哪些是实测过的、哪些还只是论证"，所以这条**证据标准**归在这里，而不是 §11.9——§11.9 是门覆盖 lint 的契约，它那几条性质被写死为不可议，往里塞一条"怎么写用例"的经验会稀释一个必须机械的判据。

**标准**：一条断言"某处会 use-after-free"的用例，只有当**那次读活到了 sanitizer**，它才是关于 ASan 腿的证据。

**实测出处（E14 的红态，§11.11 有现场记录）**：用例里那次会 UAF 的读最初写成 `(void)cached_->geometry();`。RelWithDebInfo `/O2` 把这个**没有消费者**的载入整个删掉，于是红态里**用例照样 FAIL**（`touchedCache` 断言命中）、**ASan 一条报告都没有**。把值存进可观测成员（`test_removal.cpp` 的 `lastCachedWidth`）之后，UAF 才浮出来。

**推论，三条，都按"证据"而不是"技巧"读**：

1. **「用例 FAIL」与「ASan 报告」是两个独立信号**，前者红不蕴含后者红。本缺陷族**六次复发全部只有后者看得见**——所以只拿到前者，等于**没有**拿到关于这一族的证据。
2. 任何"我构造了红态、它确实红了"的主张，必须说清**红在哪条腿、以什么形态**。退出码 2（用例失败）与一条 heap-use-after-free 报告，是两个不同的结论。
3. **每一次对可能已释放对象的读都要被消费**（传进 `CHECK_*`）。这不是风格，是这条腿的可观测性前提。

**落点**：`build-asan.bat` 与 `verify.bat` 的 ASan 腿抬头注释各有一段（下一个写这类用例的人真正会读到的地方），`tests/widget/test_removal.cpp` 的 `lastCachedWidth` 旁边有现场版本，`tests/widget/test_detach_notify.cpp` 的文件头把它当成纪律执行。

---

### 11.9 门覆盖的机器校验（E5 的交付物；Leo E7 素材 #1）

**Leo 的判词我接受**：五次复发，五次都是"有人复核过、复核漏了"；§11.2 的 `static_assert` 校验的是**类型形状**，`deathWatchDepth()` 只能验链表配平，**没有一样东西能验"第 9 扇门忘了加守卫"**；而 §11.8 把完整性交给"E5 独立复核"——**一个人**。Q3 证明了评审规则可以降级成编译器规则，那一手必须也用在门覆盖上。

**先说我否决的那个方案，以及为什么**：Leo 建议的运行时信号是"在 `Widget::sizeHint()` / `setGeometry()` 入口统计**跨越时栈上没有任何游标**的次数，由 E6 断言它等于表里 ❌ 行的数量"。这个信号**不能用**，理由不是成本而是**它恒为正**：每一次**最外层**的门跨越（测试直接调 `relayout()`、事件循环第一次进 `setGeometry`、应用调 `setContentSize`）栈上都合法地没有任何游标，因为最外层的调用者**不需要**游标。于是这盏红灯在正常运行下常亮，而"常亮的红灯"是最坏的一种诊断——它会在第一周被人加进白名单，然后永远失效。（这与 `latchNaturalSize` 当年问错问题是同一类错误：`layoutPassActive()` 是"进程里有没有 pass"，要的却是"我这一帧是不是 pass"，见 `Widget.cpp:577-587`。栈上有没有游标，同样答不了"我这一帧该不该有游标"。）

**我提的方案：把 §11.4 从手工表变成 lint 的 allowlist。** 门覆盖是**代码属性**，不是执行路径属性，所以校验它的正确工具是源码 lint，不是运行时计数器。

**契约（E5 按此实现，形状可议、三条性质不可议）**：

1. **候选集由谓词生成**：脚本按 §11.4 的 P1/P2/P3 原语清单扫 `src/widget/*.cpp`（以及 `src/**` 其余目录），对每个函数体判定"含门 且 门不是本体最后一条语句 且 **本体内没有 REM3 游标**"⇒ 进候选集。
   **⚠️「REM3 游标」= `DeathWatch` / `BubbleGuard` / `GeometryGuard` / `LayoutGuard` 四个 `LiveGuard` 实例中的任意一个（E20 实测后放宽，编排者裁定采纳；本条原文只写 `DeathWatch`）。理由是实测而不是口味**：只认 `DeathWatch` 会把 `Widget::dispatchMouse` 与 `dispatchKey` 判成缺陷，而这两帧在门后**重测了自己的游标**（`if (bubble.node() != w) return;`）——那正是 REM3-G2/G3 要的形状，只是写在 REM3 之前。**把正确的帧报成缺陷，是 lint 被静音的第一步**，而被静音的 lint 就是本节下面否决运行时计数器时说的那个下场。**代价原样登记在 §12.4 A′ 第 1 条**：本检查分不清"持有游标"与"检查游标"（S2/W2）。允许**保守多报**（例如门后只碰局部量的），因为多报的方向是"逼一个人做决定并留档"，漏报的方向是本缺陷族本身。
2. **每个候选必须有归宿**：要么已被守卫，要么出现在 allowlist 里，且 allowlist 的每一条**必须带**：理由（对应谓词的哪个子句）、定级（S1/S2/S3）、轮次（W1/W2/W3）。**allowlist 就是 §11.4 的表**，两者不得各写一份（`Widget.cpp:74-75` 那条规矩：第二份手抄就是第二个会忘的地方）。
3. **未归档的候选让 `verify.bat` 变红**。这就是"第 9 扇门忘了加守卫"的检出点：新写的门若既没守卫也没进 allowlist，构建就红。

**本版扫描用的第一刀（E5 的起点，不是终点）**——它只生成候选行，函数体归属与"门后有没有读"仍是人判的，这也正是要把它变成脚本的原因：

```
grep -n "setGeometry(\|sizeHint()\|\.emit(\|setVisible(\|setLayout\|invalidateSizeHint\|add<\|takeChild(\|removeChild(\|clearChildren(\|openPopup(\|closePopup(" src/widget/*.cpp
```

**⚠️ E5 复核的更正：上面那一刀漏掉的是 P1，而且是结构性地漏掉的。** 那条 grep 是一张**名字表**，表里每一项都是 P2（具名库函数）或 P3（`.emit(`）。P1 说的却是"对任何 widget 的**虚**成员调用"，而**虚调用按名字是 grep 不出来的**——`c->foo()` 是不是虚调用，取决于 `foo` 在**头文件**里有没有 `virtual`，不取决于调用点长什么样。`layoutRect()`（表 #28）正是这么漏掉的：它一直在谓词的覆盖范围之内，扫描却结构性地看不见它。

**因此 lint 的契约加一条（三条性质之外的第四条，E5 判定它同样不可议）**：候选集的 P1 部分必须**从声明侧生成**——扫 `include/geeyoou/**/*.hpp` 收集所有 `virtual` 成员函数名（含 `override`），再拿这张**生成出来的**名字表去扫 `src/`——而不能由人手写一张调用点名字表。手写的那一张，本节第一刀已经证明会漏，而且漏的方式是"看起来很全"。

**⚠️ 扫描根就是 `include/geeyoou/**/*.hpp` 的全部，不是 `Widget.hpp`（E9 复核补正）。** 这一条现在是承重的，不是措辞：`Layout.hpp` 的 `onInvalidated` / `onChildAppended` / `onChildRemoved` 三个钩子只有把根开到整个 `include/` 才会出现在候选集里。写死这个根的理由，是 §11.4 P1 主语那条更正的直接推论——**声明侧生成只解决"grep 不出虚调用"，解决不了"扫描根写窄了"**，两个洞要两条修。今天全库非析构 `virtual` 共 **66** 条（`grep -rn "virtual" include/geeyoou --include=*.hpp | grep -v "virtual ~"`），分布：`platform/Platform.hpp` 21、`widget/Widget.hpp` 15、`widget/SelectBase.hpp` 13、`widget/Layout.hpp` 7、`render/StyleSheet.hpp` 5、其余 5。这 66 条就是 lint 第一刀的 P1 输入，`Platform.hpp` 那 21 条的归宿见 §11.4 的免检清单。

**登记：第二个谓词缺口，P1/P2/P3 都不覆盖的第四类原语（E9 复核发现，本轮只登记不修）。** `platform/Platform.hpp` 的 `PlatformWindow` 有 7 个 **公有 `std::function` 成员**（`:88-102` 的 `onPaint` / `onMouse` / `onKey` / `onResize` / `onClose` / `onHitTest` / `onWindowStateChanged`），它们是应用可以直接赋值覆盖的回调。调用它们**能到达应用代码**，但既不是虚成员调用（P1）、不是登记过的库函数（P2）、也不是信号发射（P3）。**谓词漏了整整一类原语**，不是漏了一个站点。定级 S2、排 W2（与 #27 的 P3 家族扫描同一批），处理形态：要么给谓词加一条 **P4 = 调用一个公有可赋值的可调用成员**，要么把这 7 个成员逐一登记进 P2。**这是登记，不是判定**——判定归架构团队。

**为什么这个能兑现 Leo 要的性质**：它检查的是**代码**而不是**被跑到的路径**（运行时计数器只能覆盖用例踩到的那些帧，而本族五次复发里至少三次是没有用例踩到的帧）；它的红/绿判据是机械的；它的 allowlist 天然逼着"看过了没有"与"没看"可区分——这正是 §11.4 #11（`removeChild`）那一行要证明的事。

**成本与风险，写明**：脚本要做的是**近似**的 C++ 函数体切分（花括号配平 + 函数头正则），不是解析。首轮会多报若干条（估计 20~40 条，主要来自 P3 家族），**这些多报的分诊本身就是表 #27 那个 W2 扫描任务**——也就是说这个 lint 不是额外工作，它是把已经必须做的那次扫描变成一个**不会退化**的产物。E5 若判定脚本形状不合适（例如决定改用 clang 的 AST 工具），**三条性质不变**即可。

---

#### 【E20】落地记录：`tools/lint-door-coverage.ps1`

> 状态：**已实现、已自检（22 例）、三条腿门禁全绿、双向证明实测。** 结论由测试团队下。

**四条不可议性质逐条对照：**

| 性质 | 落地形态 | 怎么验的 |
|---|---|---|
| 1 谓词生成候选，不是名字表 | 括号配平切函数体 → 逐体找 P1/P2/P3 门 → "有门 ∧ 门不是本体最后一条语句 ∧ 体内无游标" ⇒ 候选 | 自检 `a_door_that_is_the_last_statement_is_not_a_candidate`、`a_guarded_frame_is_not_a_candidate` |
| 2 每条候选带理由 + 定级 + 轮次 | allowlist 的行**必须**有非空的「级」「轮」两列，且位置格必须点出**函数名**；缺一样 ⇒ 归档不到 | 自检三例：`a_row_with_no_grade_…` / `…no_round_…` / `…naming_only_a_file_…` |
| 3 未归档 ⇒ `verify.bat` 红 | 步骤 [1/6] 内 `call :lint_doors`；退出码 1 或任何非 0 ⇒ `GY_RED=1` | 自检 `deleting_a_real_row_names_the_frame_that_lost_its_home`（**在真文档的副本上删一行，差分比对**）+ `spawned_unarchived_returns_1`（退出码穿过进程边界）+ 一次**整门禁实跑变红** |
| 4 P1 从**声明侧**生成，根是 `include/geeyoou/**` | 扫 59 个头文件收 58 个虚成员名（含只写 `override` 的），减去 20 个 `Platform.hpp` 独有的 ⇒ 38 个 | 自检 `a_virtual_declared_outside_the_widget_base_is_still_found`（**第二个洞**：根写成 `Widget.hpp` 就漏 `Layout` 三个钩子） |

**allowlist 就是 §11.4，一份。** 脚本解析本文档 `### 11.4` 到 `### 11.5` 之间的**全部**表格行，键是 **(文件, 函数)** 而不是 (文件, 行号)——理由是本节自己写过的那句话："**语句是表要说的东西，行号不是**"。E14 之后表里的行号已经整体偏了一百行；键在行号上的 lint 会在每一次无关编辑上变红，而那正是 §11.9 否决运行时计数器时用的理由（**常亮的红灯**）。自检 `doc-full.md` 里在 §11.4 **之前和之后**各放了一张能解析的表，两张都必须归档不到东西。

**首扫的三处实测更正，写下来给下一个人：**

1. **`~Foo() override` 不带 `virtual`。** 只过滤 `virtual ~` 会让 `AppWindow` / `Widget` / `Window` / `MenuButton` / `SelectBase` **五个类名变成门名**，于是每一个 `new Widget(` 都是门。自检 `a_destructor_written_as_override_is_not_a_door_name`。
2. **`Platform.hpp` 的免检必须落到名字集合上，而不只是写在文档里。** `PlatformWindow` 声明了 `restore()` / `show()` / `invalidate()` / `close()`，把它们留在 P1 集合里，`painter.restore()` 和 `Layout::invalidate()` 就都成了门。**免检是有闸门的**：`setPlatform` / `installPlatform` 的 grep（§11.4 点名要求进 lint 的那两行）每次都跑，一旦出现安装点，免检当场失效、门禁变红。自检两例，一正一反。
3. **空的名字集合会让正则退化成"匹配一切"。** `(?<n>)` 是空的交替分支，`\b(?<n>)\s*\(` 匹配每一个左括号。是 `Platform.hpp` 免检那个夹具**把 P1 集合合法地清空**之后暴露的——输出是 `first: P1 ` 后面什么都没有。**方向是红的，但内容是无意义的**，而无意义的红灯和常亮的红灯是同一个下场。现用 `(?!)` 兜底。

**一处放宽，以及它的代价（**编排者已裁定采纳；契约正文已按此改写，见本节上面的第 1 条**）**：§11.9 的原文是"本体内没有 `DeathWatch`"，实现读作"本体内没有 **REM3 游标**"，即 `DeathWatch` / `BubbleGuard` / `GeometryGuard` / `LayoutGuard` 四个 `LiveGuard` 实例中的任意一个。**理由是实测而不是口味**：字面读法把 `Widget::dispatchMouse` 与 `dispatchKey` 判成缺陷，而这两个帧在门后**重测了自己的游标**（`if (bubble.node() != w) return;`）——REM3-G2/G3 要的形状，写在 REM3 之前。**把正确的帧报成缺陷，是 lint 被静音的第一步**，而被静音的 lint 正是 §11.9 否决运行时计数器时说的那个下场。**代价登记在 §12.4 A′**：本检查分不清"持有游标"与"检查游标"。

**性能，因为它决定这条 lint 会不会被删掉。** 第一版按名字逐个扫（38 + 15 = 53 遍全树），实测 **14.9 秒**；折成每类一条编译后的交替正则 + 注释剥离结果按「路径 + 修改时间」记忆化后，**2.0 秒**（冷）/ **0.95 秒**（热）。自检 22 例 **6.4 秒**（含 3 次真进程 spawn 覆盖退出码路径、2 次真仓库全扫）。**门禁里合计约 8.5 秒。**

**顺带兑现的第二条机器判据（§11.4 免检清单点名要求的）**：`setPlatform|installPlatform` 的 grep 现在每次门禁都跑，剥掉注释之后再匹配——否则一句提到这两个名字的注释就能让门禁红，那是把一条判据换成一个笑话。

**顺带兑现的第三条（本轮 D4b 的连带产物，见任务 B）**：所有 `*.bat` 必须是**纯 ASCII**。`cmd.exe` 按**字节偏移**定位、按**字符**计消耗量，实测真值表——`CRLF+UTF-8` 对、`CRLF+GBK` 对、`LF+纯 ASCII` 对、**`LF+多字节` 错**（解析器从 160 行之上的行中间续跑，把 `ine` 当命令执行）。**要两个条件同时成立**；本树四个 `.bat` 全是 LF，所以 ASCII 是唯一撑着的那一半，也就是值得机器守的那一半。**⚠️ 这条检查保护不了 `verify.bat` 自己**——它由 `verify.bat` 调起，而漂移的 `verify.bat` 在跑到这一步之前就已经错行执行了。它保护的是**下一次**运行。

---

#### 【E21】第四轮安全评审：门禁自身的四个缺口，以及第四条性质的第三次修

> 状态：**四条全部关闭，逐条有能变红的自检夹具。** 自检 22 → **30** 例，ASan 分类器自检 21 → **22** 例；lint 三条腿门禁全绿。

**性质 4 的扫描根，这是它第三次被修，而前两次都没有碰到这一处。** §11.4 P1 那条更正修的是"虚调用 grep 不出名字"（⇒ 声明侧生成），§12.5 教训 2 的第二层修的是"根写成 `Widget.hpp` 而不是 `include/geeyoou/**`"（⇒ 声明侧的根）。**两条都在声明侧。候选侧的根一直是 `src/`**，而这个库的代码**不全在 `src/`**——头文件里的模板与 inline 定义从来没有被 `Split-CppFunctions` 切过。⇒ 候选侧的根改成 `src/` + `include/`，实测多出两条候选（L76-T / L77-T，§11.4 E21），**没有出现"为了变绿收窄谓词"的压力**。

**只扩根买不到任何东西，还得修两个机械陷阱**（都实测，都记在脚本的修复处）：

1. **`-Include` 在 `-LiteralPath` + `-Recurse` 下不过滤**（PowerShell 5.1.19041：`-LiteralPath include` 返回 59 个 `.hpp`，`-Path` 返回 0）。旧扫描那句"只扫 `.cpp`"从来不是一个过滤器。
2. **`template` 在 `NotAFunctionHead` 里**，于是每一个模板函数体都被读成"不是函数"，切分器往里降一层找不到东西，**整个函数体连同它的门一起从未被扫过**。实测：`AppWindow.hpp` 切出三个函数，**没有 `setContent`**。

**P2 清单不再有第二份**（C2）。脚本里那份手抄的 `$P2Names` 旁边写着"你在这里加一个，记得也去那边加"，而两份**已经漂了**——脚本有 `relayout`，§11.4 那行没有。⇒ 脚本改成解析 §11.4 的 P2 行（与 allowlist 同一条规矩，性质 2 的字面）；**那一行的书写格式从此是承重的**，归一不出标识符的反引号 token ⇒ **退 3**。自检三例：narrowed（差分证明脚本真的在读文档）、无 P2 行（退 3）、token 归一不了（退 3）。**报告行现在打印 P2 原语条数**——丢一条 allowlist 行会让门禁更红，丢一条 P2 原语会让门禁**更绿**，而后者没有任何别的东西看得见，所以它要在日志里有个数字。

**P2 不再套 `(?<!::)`**（C3）。那条 lookbehind 的依据是 §11.4 的"限定名调用不算 P1"，而那句话讲的是**虚分派**——`PushButton::sizeHint()` 静态绑定，落不进应用的覆写。**P2 不是分派**：`Widget::setGeometry(r)` / `Base::relayout()` 写成限定名照样跑到应用代码，限定名选的是实现，挡不住实现去调应用的 `onGeometryChanged`。**实测全库候选数不变**（今天的限定名调用全部是 P1 家族），所以它不是活缺陷，是**潜伏的漏报**——**而漏报的方向就是本缺陷族本身**，只能靠读论证而不是靠红灯抓。自检一例（限定名 P2 仍是门），并与既有的"限定名 P1 不是门"那一例并排放着。

**门禁有没有插上电，现在有机器守**（C4）。剥掉 `rem` 之后 grep `verify.bat`，缺任一 `call :lint_doors` / `call :classify_asan`、或缺任一对应标签，即退 1。**剥注释是必需的**：`verify.bat` 的注释比代码长，而且**它们逐字引用这两行**，不剥就等于被"描述那条刚被删掉的调用"的句子满足。

**C4 的自指，以及它是怎么关掉的**：这条检查由 `:lint_doors` 调起，**抓不到自己那条 `call` 被删**——它没在跑。⇒ **一份实现、两个调用者**：lint 抓 `call :classify_asan` 丢失；`tools/test-classify-asan.ps1` 通过 `-GateWiringOnly` 起一个进程抓 `call :lint_doors` 丢失。**两个守门的机器互相看着对方的电源线。** 两个方向都实测（各注释掉一行，两次都拿到非 0 退出码，且第二次是从 ASan 腿红的）。**为什么是 spawn 不是 dot-source**：两个脚本各有自己的 `$script:Emitted` 与输出函数，共享作用域是留给某个安静下午的缺陷；0.6 秒买两份互不相干。

**这条 lint 不做什么，写清楚免得下一个人以为它做了：** 它**不判危险性**。§11.4 的 hazard 条款（"门 S 对 P 危险，当且仅当 S 之后本帧还有一次经 P 到达的读或写"）要求知道一个名字指的是什么，那需要编译器。它回答的是便宜的那个问题——**有没有一扇门，后面还有代码，而这一帧没有游标**——然后逼一个人在表里回答贵的那个，并留档。

### 11.10 【E5 · E6】两条护栏的落地记录

> 状态：**已实现、已自检，未由测试团队验证。** 本小节记录做了什么、判定位置的论证、以及每条能变红的证据；结论由测试团队下。

#### M-2（E5）：度量深度上限 —— `src/widget/Layout.cpp`

`g_measureDepth` 被正确加减、被停车场第二个排空点正确读，**却从来没有与任何上限比较过**。M4 只管住排布半边（`Widget.cpp` 的 `g_layoutDepth >= kMaxTreeDepth` 是全库唯一的上限判定，且只统计 `LayoutGuard` 帧）。

**漏的是哪一类**，精确到形状：

| 形状 | 谁挡住 |
|---|---|
| 自递归（A 量 A） | `buffersBusy_` ✓ |
| 二元环 A → B → A | `buffersBusy_`（回到 A 时 A 自己的闩已合上）✓ |
| **链 A → B → C → … → N** | **没有任何东西挡** ⚠️ |

链上每一级是**不同的** `Layout` 对象、`buffersBusy_` 各自为 false；一级都不是布局 pass，所以 `g_layoutDepth` 全程为 0，M4 看不见。触发它只需要一个 `sizeHint()` 覆写去问**另一棵树**的 `sizeHint()`——而"问别的 widget 要 hint"正是 `sizeHint()` 存在的理由。终点是栈耗尽：**是 DoS，不是内存破坏**，但与 R1/R2 同形——护栏只按排布半边设计。

**做法**：`Layout::measureFor` 里，`if (buffersBusy_)` 之后、`MeasureFrame` 构造**之前**，`g_measureDepth >= kMaxTreeDepth` 即 `detail::layoutDepthExceeded(&host)` 并返回 `lastMeasure_`。**绝不 abort、绝不抛**（ADR-R2-04）；沿用 M4 的 `depthExceeded` 计数器而不是新开第六个，因为是同一个事实、同一个读者。

**判定位置的两条论证**（代码里也写着，这里是摘要）：

* **计数配平**：被拒的调用**根本没碰过** `g_measureDepth`，没有加就不需要配对的减；`MeasureFrame` 析构（`Layout.cpp` 停车场的**第二个排空点**）语义**一字不变**——这一帧既没停放任何东西、也没成为最外层度量，栈上仍在的那些帧照旧在退栈时排空。若把判定放进 `MeasureFrame` 的构造函数，"这一帧到底计没计数"就变成析构函数也必须回答的问题，于是要多一个只为让两半同步而存在的标志位——而"一个计数器记在两个地方"正是这个文件当初出现两个排空点的原因。
* **位置必须在递归发生之前**（这是决定性的一条，Leo 评审后补正）：把判定往下挪到 `measure(host)` 之后，上限不是"更难论证"，而是**功能上直接作废**——`measure()` 正是下沉到链条下一环的那一步，等它返回时，要拦的那条失控链已经走完、栈已经花掉了。事后判定的上限记录的是一件它没有阻止过的事。

* **不读已释放对象是另一个问题，答案不是"本帧还没跨门"**。第 1 版就是这么写的，**它对自己描述的那一帧判断错了**：下面那个作用域里门后**已经有两处经 `this` 的读写**——`lastMeasure_ = measure(host)` 是门后的写，`measured = lastMeasure_` 是紧接着的读，而 `measure()` 是应用代码。让它们安全的是**停车场**，不是"没跨门"：进 `MeasureFrame` 作用域后 `buffersBusy_` 为真 ⇒ `~Widget` / `adoptLayout` 走 `parkLayout()` 分支而不是 `delete`；`releaseParkedLayouts()` 在 `g_measureDepth != 0` 时直接返回。这条路径的用例是 `r2_remediation.a_box_survives_its_host_dying_inside_a_pure_measure`。
  往下挪真正会悬空的是 **`host` 这个引用**——停车场救的是 `Layout`，没有任何东西救那个 `Widget`。而判定只对它**取地址**（`layoutDepthExceeded` 存指针、从不解引用），所以连这一条都不构成 UAF。**这恰恰是重点：留在这里的理由是上面那条递归顺序，不是存活性论证。**

**顺序**：放在 `buffersBusy_` **之后**，于是既有行为逐位不变——两个分支都返回 `lastMeasure_`，差别只在记不记；而同一个 layout 的重入式度量是**有文档、预期之内、自终止**的，不是失控链条，不该被记成失控链条。

**能变红的用例**：`layout_engine.a_chain_of_measurements_stops_at_the_same_ceiling_a_pass_does`。**八十棵互不相干的单层树**，不是一棵八十层的树——所以 §10.4.2 里那条"Debug 建不出这么深的树"的限制根本不适用，本用例**三条腿全跑**。红态两种形态都实跑过：

* 去掉上限判定 ⇒ 本用例在第一条断言就 FAIL（`depthExceeded` 读到 **0**，且 `layouts[64]->measures` 是 1——链条真的一路跑穿了）；
* 去掉上限判定并把链长提到 20 000 ⇒ 进程**栈耗尽**（SIGSEGV，没有报告、没有可读的退出码）；
* **装回上限判定后，同样 20 000 链长本用例照过**——上限落在第 64 层，与链长无关，所以门禁里留 80 就够。

**顺带解决 §10.4.2 的 M4**：同一手法（互不相干的浅宿主，每个的 `arrange()` 跑下一个的 `performLayout()`）让排布半边的上限也能在 **Debug** 里跑通，用例 `layout_engine.m4_a_chain_of_unrelated_hosts_reaches_the_same_ceiling`。原来那条 Release-only 的用例保留，两者形状互补。

#### M-1（E6）：`layoutRect()` 这扇未枚举的虚函数门 —— `src/widget/Widget.cpp`

见 §11.4 的 #28 / #28b / #29 与那三段说明。两处落点：`Widget::contentRect()` 自己上一个 `DeathWatch` 并在降级分支记一次 `frameDegraded()`；`Widget::runLayoutIfAny()` 用早已在栈上的 `LayoutGuard` 多问一次 `alive()`（一行，不记）。`LayoutGuard` 的 `DrainOnUnwind` 顺序语义**未被触碰**——新增的只有一条 `if (!guard.alive()) return false;`，和它下面那条同形检查一样从 `guard` 的作用域里返回。

**能变红的用例**：`layout_engine.a_layout_rect_override_that_destroys_its_host_is_survived`（应用覆写 `layoutRect()` 并在其中销毁宿主）。**封门之前**它在 ASan 腿上产出 **3 条报告**，分类器判 **OURS**、退出码 1（= 门禁红）：

```
heap-use-after-free  READ 8   #0 geeyoou::Widget::contentRect      Widget.cpp
heap-use-after-free  READ 8   #0 geeyoou::Widget::runLayoutIfAny   Widget.cpp
access-violation              #0 geeyoou::Layout::arrangeFor       Layout.cpp   <- 进程在此死掉
```

free site 两条都是 `SuicidalRect::~SuicidalRect`。第三条是**进程当场死**，不是"只有 ASan 看得见"那一类——与 E8 复现器同级。

**⚠️ 但上面那个实验只证明了"两处检查里至少有一处是必要的"**（Leo 评审 E5/E6 的判词，我接受）。两处一起拆，得到的是一个**合取**的反证，它无法区分"`contentRect` 自己那个游标必要"和"`runLayoutIfAny` 那个检查必要"。而 §11.4 #28/#28b 主张的偏偏是更强的一条：**调用侧的检查必要而不充分**。要证明它，必须做**判别性**实验——只拆一处，保留另一处，正好构造出"由调用者保证"那个方案的精确形状。

**判别性实验（本轮实测，取代上面那条作为 #28 的承重证据）**：只拆掉 `Widget::contentRect()` 自己的 `DeathWatch`（连同它的降级分支），`runLayoutIfAny` 的 `if (!guard.alive()) return false;` **原样保留**。结果：

```
==37176==ERROR: AddressSanitizer: heap-use-after-free
READ of size 8
    #0 geeyoou::Widget::contentRect     src\widget\Widget.cpp:788   <- 读 layout_
    #1 geeyoou::Widget::runLayoutIfAny  src\widget\Widget.cpp:869
    #2 geeyoou::Widget::setGeometry     src\widget\Widget.cpp:624
freed by:
    #3 `anonymous namespace'::SuicidalRect::`scalar deleting destructor'
    #4 geeyoou::Widget::removeChild     src\widget\Widget.cpp:565
    #6 `anonymous namespace'::SuicidalRect::layoutRect  test_layout_engine.cpp:1270
    #7 geeyoou::Widget::contentRect     src\widget\Widget.cpp:774   <- 门
```

**报告恰好一条**，用例 `a_layout_rect_override_that_destroys_its_host_is_survived` 单独 FAIL，进程退出码 **1**（门禁红）。

这一条报告就是那句话的全部内容：门在 `:774`，读在 `:788`，中间**没有回到调用者**——调用者的守卫此刻还在栈上、还是好的，`runLayoutIfAny` 后面那次 `alive()` 也确实拦住了它自己那一帧（所以**没有第二条**报告）。**调用者的检查覆盖不了一次已经发生的读**，这是实测出来的，不是论证出来的。

#### Leo 留给 E6 的两条常驻断言

* **CP-G1 的降级返回值逐位断言**：`tests/widget/test_rem3_doors.cpp` 的 `a_dead_group_box_answers_with_its_frame_and_nothing_else`。**有标题 / 无标题各一份**——两份的差值就是标题栏那 22px，而这正好钉死了"`frameH` 用的是门**前**的 `top`"（门后重算要读一个已释放的 `std::string`）。断言 `min == preferred == {24, 46}` / `{24, 24}`、`max` 保持 `{kUnbounded, kUnbounded}`。四个常量在用例里**重写一遍而不是从实现导出**：它们是 §11.3 的契约，从实现导出的用例只会同意实现自己。
* **`framesDegraded` 的常驻断言**：`tests/widget/test_layout_soak.cpp` 末尾，`== (cycles + 3) × 5`，**并附上五组各自命中哪个检查点的推导**，以及一句"这个 5 不是门的数量"。见 §11.3 REM3-G8 的推论。

#### 本轮的用例增量与门禁

**新增 5 条**（186 → 191），无一条既有用例被改动或删除：

| 用例 | 文件 | 覆盖 |
|---|---|---|
| `a_chain_of_measurements_stops_at_the_same_ceiling_a_pass_does` | `test_layout_engine.cpp` | M-2 |
| `m4_a_chain_of_unrelated_hosts_reaches_the_same_ceiling` | `test_layout_engine.cpp` | §10.4.2 的 M4，三条腿 |
| `a_layout_rect_override_that_destroys_its_host_is_survived` | `test_layout_engine.cpp` | M-1（含健康路径对照） |
| `a_dead_group_box_answers_with_its_frame_and_nothing_else` | `test_rem3_doors.cpp`（新文件） | CP-G1 逐位 |
| `one_operation_can_degrade_two_frames` | `test_rem3_doors.cpp`（新文件） | REM3-G8 的推论 |

#### 未验证 / 留给下一轮

* **源码改动的行为中性**是用"整份 Release stdout 与改动前逐字节相同"自检的：只在源码改完、用例尚未加入时比对，diff **完全为空**；加入用例后 diff 只有 5 行新增的 PASS 加一行总数。这不能替代测试团队的判断。
* **§11.5 的 M-2 / M-1 成本是按 §11.5 的实测单价推算的**，两处**都没有单独取 `/FAsc` 复测**。按本节自己刚写下的教训，这一条**就是推算**，标注在此。
* `Widget::contentRect()` 的降级分支返回门前那个 `r`，今天**没有任何消费者**（唯一调用者下一行就问自己的守卫）。这是一条**隐式不变量**：将来若出现第二个调用者，它要么自己检查，要么这个返回值的语义要重新定义。
* REM3-RES-1 / RES-2 / RES-3 / RES-5 / RES-6 / RES-7 **一条都没动**。

---

**按团队纪律，本节到此为止：我不下"已验证"结论。** 本版是对 Leo 书面评审的答复稿，交复签；实现与测试交后续任务。

---

### 11.11 【E14】`Widget::onDescendantDetached` —— 摘除通知钩子

> 状态：**已实现、已自检，未由测试团队验证。** 本小节记录机制、完备性证明、以及能变红的证据；结论由测试团队下。
> ⚠️ **本轮只交核心机制。** `ScrollArea` / `AppWindow` / `Shell` 的 override、降级语义与它们各自的用例是 **E15/E16**，不在本轮。

#### 为什么需要它（守卫解决不了的那一半）

守卫是**帧作用域**的：它保证**这一帧**安全返回，**它不修复对象状态**。`ScrollArea` 的 `viewport_` / `content_` 在守卫触发之后仍然是悬垂成员，下一次 `onPaint` → `bars()` → `contentSize()` → `content_->geometry()` 就是 UAF——**摘除之后的下一次重绘就炸，不需要应用再调任何方法**。这是 REM3-RES-1。

`AppWindow::relayout()` 里那三处 `if (!header_ || !content_) return;` **今天全是死代码**（库里没有任何一行会把它们置空）——**契约早就承认了这个状态，实现从没兑现过**。钩子就是兑现它的机制。

#### 机制形状（照 Elena 的裁定实现，逐条对应）

* **新增 `protected virtual void onDescendantDetached(Widget* node)`，默认空实现**（`Widget.hpp`）。**不复用 `childRemoved`**：后者被 `if (detail::g_layoutHosts != 0)` 门控调用，而这个钩子必须**无条件**触发（RES-1 的触发根本不需要 Layout）；复用还会把"通知我的 Layout"和"修复我自己的状态"揉进同一个虚函数，子类忘了调基类就让布局引擎静默停止更新——**凭空多一种失败模式**。默认空实现 ⇒ 没有"忘记调基类"这个陷阱。
* **落点：`Widget.cpp` 的 `announceDetached()`**，复用已经存在的那一趟前序遍历，**不新增任何遍历**。
* **位置：函数体第一条语句**，在 `if (win) win->widgetDetached(node);` 之前、任何解链之前。
* **广播整条祖先链**：`for (Widget* a = node->parent(); a; a = a->parent()) a->onDescendantDetached(node);`。**不是只通知直接父节点**——`ScrollArea::content_` 是**孙子**（`viewport_->add<Widget>()`），只通知直接父节点会发给 `viewport_` 而**永远发不到 `ScrollArea`**。同样的漏在 `AppWindow::fill_` 和 `Shell::PageEntry::host` 上各再出现一次：**三个真实容器，三次都是孙子**。（这一条是**实测**过的，见下面的红态 B2。）
* **无条件**，不受 `win != nullptr` 约束（没挂窗口的 `ScrollArea`——测试里到处都是——同样要修复状态）。
* **在 `win->widgetDetached` 之前**：那一句会跑应用代码（`closePopup` → `popupClosed` 槽），祖先可能死在里面；先发通知就不必论证祖先存活。
* **时机是解链之前**：整棵离场子树与整条祖先链此刻全部活着且完整，指针比较绝对安全。

**一处实现细节，因为它是唯一一处需要动到访问控制的地方**：`announceDetached` 是 `Widget.cpp` 匿名 namespace 里的自由函数，够不到 protected 的钩子，而它自己是内部链接、无法在头文件里被 `friend`。所以广播被抽成 `detail::notifyDetachToAncestors(Widget*)`（`Widget.hpp` 声明、`Widget.cpp` 定义），由 `Widget` `friend` 它——**与 `Layout` friend `detail::parkLayout` 是同一个形状、同一个理由**，不是新发明。

#### §11.6 约束 (ii) 的处置：**以 REM3-G9 替代，登记为一处有意的偏离**（Leo E18 复签必改 1）

§11.6 给 E14 立的约束 (ii) 原文是：**钩子本身是一扇门（它跑子类代码），所以广播循环自己要按 §11.4 的谓词上守卫——E14 的设计里要有它自己的检查点表。**

**E14 没有兑现它，兑现的是另一件事**：用 **REM3-G9 契约 + `takeChild` 里一条 Debug assert**，把这个钩子**窄化成非门**，而不是给一扇门上守卫。**这是一处偏离，第一版一个字都没提，本版补记。**

**为什么可以这样换**（理由，不是事后合理化）：

* §11.4 的谓词判的是"这一段代码会不会跑到应用代码"。给门上守卫，是承认门存在、然后活下来；把门取消，是让那一段**根本不跑应用代码**。后者的爆炸半径严格更小：守卫只保证**这一帧**安全返回，取消了门则连"钩子里改了树、广播的快照失效"这一类**守卫救不了的**破坏也一并没有了（`announceDetached` 的快照语义见 `Widget.cpp` 的 `takeChild` 头部注释）。
* 检查点表要求"门后重读、门后比对"，而一个**只准把自己的成员指针置空、不准做别的**的钩子，门后**没有任何东西**要重读——表会是空的。用一张空表去满足一条形式要求，不如把"为什么它是空的"写出来。
* 契约是**可判定的**：G9 的形状是"钩子体内只允许成员指针赋空"，`ScrollArea` / `AppWindow` 两个 override 逐行可核（各只有指针比较 + 置空），不是"请小心"。

**这个替换换来的代价，必须一并写下：契约不是运行时执行的。** G9 在 `takeChild` 里有一条 Debug assert，那是**唯一**被执行的部分；其余（`update()` / `emit()` / 虚调用）**只是文字**。真正的后果不是"钩子作者可能违约"，而是**广播循环本身至今没有任何守卫**——见下面"未验证"栏第 6/7 条，那是本节最重的两笔账。

#### 尺寸预算：实测，不是论证

`Widget.cpp:36` 的 R2 预算 `static_assert` **实编通过**（三条腿全绿）。但那条断言是 `<=`，它挡得住增长、说不出"没变"，所以额外用一次编译期探针取了确切数字（`template <int> struct GyShowSize; GyShowSize<int(sizeof(Widget))>;`，靠 C2079 的错误信息把数字打出来，跑完即从备份还原）：

| 构型 | `sizeof(Widget)`（Release, MSVC x64） |
|---|---|
| **有**钩子 | **200** |
| 把 `virtual void onDescendantDetached(...)` 整个删掉 | **200** |

**一字节没动。** 虚函数只让 vtable 多一个槽，vtable 是**每类一份的静态数据**、不进对象——而 `Widget` 本来就有 `virtual ~Widget()`，vptr 早就在了。所以"0 新成员"是**量出来的**。

#### 完备性证明（可独立复核，请自己 grep，不要照抄）

```
$ grep -rn "children_.erase" include src tests examples
src/widget/Widget.cpp:444:  children_.erase(children_.begin() + std::ptrdiff_t(index));

$ grep -rn "parent_ =" include src
include/geeyoou/widget/Widget.hpp:71:    raw->parent_ = this;          <- add<T>
src/widget/Widget.cpp:445:  owned->parent_ = nullptr;                   <- takeChild
（其余两条是 setGeometry 里的比较与断言，不是赋值）
```

⇒ **一棵子树离开一棵仍然活着的树，唯一的门是 `takeChild → announceDetached`；除此之外持有者与被持有者必然同生共死**（`~Widget` 会先析构 `children_`，每个子节点自己到达析构）。所以 **`~Widget` 不需要加这个钩子**：在那里发通知，等于告诉一个正在被释放的对象"你的成员要被释放了"。
（行号是本轮改动**前**的 HEAD。改动后重跑同两条 grep：`children_.erase` 的**语句**下移到 `Widget.cpp:539`，`grep` 另外命中的 `:409` / `:501` 两行**是注释里引用这条不变量的文字**，语句仍然只有一处；`parent_ =` 的**赋值**是 `Widget.hpp:84`（`add<T>`）与 `Widget.cpp:540`（`takeChild`），另两条命中是 `setGeometry` 里的比较与断言、以及成员声明本身。结论不变。）

#### 与 E1/E2 守卫的关系（B7，方向 (a)）

钩子把成员置空 ⇒ 出现"**对象活着但成员被置空**"的第三态。E3/E4 的检查点已经是 **`this` → 成员重读 → 游标**三段短路链。

⚠️ **但"成员重读那两个子分支 E14 落地后第一次有覆盖"这句话本轮不成立，必须更正**：那两个子分支在 `ScrollArea.cpp` 的 CP-S1 / CP-C1 里，读的是 `ScrollArea` 的**私有**成员 `viewport_` / `content_`。能把它们改成 `nullptr` 的**只有 `ScrollArea` 自己的 override**，而 `ScrollArea.hpp` 也没有任何 `setContent`/`setViewport` 之类的外部写入点。**所以在 E15/E16 给 `ScrollArea` 写出 override 之前，这两个子分支仍然是死代码。** E14 让它们**可能**被覆盖，没有让它们**已经**被覆盖。这一条记在"未验证"栏。

#### 能变红的证据（三个红态，全部实跑）

三次实验都用**文件备份**还原，未跑任何 git 历史改写命令。用例是 `removal.a_cached_grandchild_is_cleared_before_the_next_paint` / `removal.every_node_of_a_departing_subtree_announces_itself`（`tests/widget/test_removal.cpp`）。

| 实验 | 改动 | 结果 |
|---|---|---|
| **B** | 把 `announceDetached` 里那一行广播整个删掉 | 2 条用例 FAIL，**1 条 heap-use-after-free**（`CachingBox::onPaint` ← `Widget::paintTree`，free site 是 `Widget::removeChild`），退出码 **2** |
| **B2（判别性）** | 广播**保留但收窄成只通知直接父节点** | 2 条用例 FAIL，**同一条 UAF 原样出现**，退出码 **2** |
| — | 装回整条祖先链广播 | 三条腿全绿，200 用例 0 失败，ASan 0 条我方报告 |

**B2 是这里的承重实验**（形状取自 Leo 对 M-1 的判别性实验）：B 只能证明"需要某种通知"，B2 才证明"**必须广播整条祖先链**"——因为被缓存的指针是孙子。

**⚠️ 一条实测出来的方法学教训，值得单独记：** 用例里那次会 UAF 的读，最初写成 `(void)cached_->geometry();`。**RelWithDebInfo /O2 把这个没有消费者的载入整个删掉了**，于是红态里用例照样 FAIL（`touchedCache` 断言）、**ASan 却一条报告都没有**。把值存进一个可观测的成员（`lastCachedWidth`）之后，UAF 才浮出来。
**推论，写给下一个写这类用例的人：一次被丢弃的读不是一次读。** 「用例 FAIL」和「ASan 报告」在这里是**两个独立的信号**，前者绿了不代表后者也会红；本族六次复发全都只有后者看得见。

#### REM3-RES-7 的处置：**两条 E17 用例的覆盖假设不重审**（Leo E18 复签必改 3）

§11.6 的 REM3-RES-7 留了一个待处置项：`take_child_survives_a_slot_that_destroys_the_host` 与 `clear_children_survives_...`（`test_removal.cpp:526` / `:580`）**都只走 `widgetDetached → closePopup → popupClosed.emit` 这一条门**；**若 E14 的广播新增了一条会跑应用代码的门**，这两条用例证明的东西（"popupClosed 这条门后 takeChild/clearChildren 不崩"）就不再等于它们被当成的东西（"所有 detach 门后都不崩"）。

**处置：不重审，因为前提不成立。** 按 REM3-G9，`onDescendantDetached` **不是那种门**——它跑的是子类代码，但那段子类代码被契约限死为"只准把自己的成员指针置空"，不得 `update()`、不得 `emit()`、不得改树。**广播因此没有给这两条用例新增任何一条会跑应用代码的门**，它们的覆盖假设原样成立。

**这条写在这里，是因为"答案是对的"和"答案被写下来了"是两件事。** 第一版的隐含回答就是上面这句，但没有落笔，于是 §11.6 那条留待处置项在文档里永远悬着，下一轮的人只能重新推一遍。

**它同时是一张欠条**：这个处置**完全建立在 G9 上**。哪一天有人给 `onDescendantDetached` 的 override 里写进一次 `emit()` 或一次 `update()`，这条结论**当场作废**，那两条用例就真的要各加一条新门的版本。而今天**没有任何机器**能在那一天变红（G9 的 assert 只拦 `takeChild`，见未验证栏第 3 条）——所以这条依赖被显式记在这里。

#### 本轮的用例增量与门禁

**新增 2 条（198 → 200）**，无一条既有用例被改动或删除：

| 用例 | 文件 | 覆盖 |
|---|---|---|
| `a_cached_grandchild_is_cleared_before_the_next_paint` | `test_removal.cpp` | 钩子存在、可覆写、在下一次重绘之前生效；含健康路径对照；**无 Window** |
| `every_node_of_a_departing_subtree_announces_itself` | `test_removal.cpp` | 广播是**按离场子树的每个节点**发的，不只是被点名的那个 |

门禁：Release / Debug / ASan 三条腿全绿，200 用例 0 失败；Release 独立跑 3 次退出码全 0 且 stdout **逐字节相同**。

#### 未验证 / 留给下一轮

**⚠️ 本栏第一版只有 5 条，而 E14 的交接报了 7 条**（Leo E18 复签必改附 2）。**文档才是下一轮的输入，交接不是**，所以补全在此。

> 一句诚实的话：交接原文已不可回溯，**第 6/7 条是按 Leo 同一份评审里点名要求登记的两条 UAF 路径重建的**，不是从交接里抄回来的。若当初那两条另有所指，它们仍然缺席——**而这正是"只写进交接不写进文档"的代价**，本条一并作为教训留在这里。

1. **B7 的成员重读子分支仍未被覆盖**，理由见上；覆盖它是 E15/E16 的事。（**已于 §11.12 兑现**，本条留档以便读出顺序。）
2. **`ScrollArea` / `AppWindow` / `Shell` 今天仍然带着 RES-1 的缺陷**：钩子存在了，但这三个容器还没有 override，所以"摘除 content 后重绘"在真实容器上仍然是 UAF。**本轮没有修它，也没有为它加用例**——加一条断言"现在还会炸"的用例不是门禁该有的东西。这是 E15/E16 的验收对象。（`ScrollArea` / `AppWindow` **已于 §11.12 收口**；`Shell` 编排者裁定不补，登记 W2，见 §12.4 的 B 组。）
3. **REM3-G9 的 assert 只拦住了 `takeChild` 一条路**。`update()` / `emit()` / 虚调用仍然只是契约，没有运行时执行。

   **⚠️ 这一条的理由，第一版写错了，本版更正**（Leo E18 复签必改 2）。第一版写的是"只有会破坏遍历的那一项值得付运行时代价"——**这个理由不成立**：从钩子里 `emit()` 出去**同样内存不安全**，而且更难发现，它并不是"不值得"的那一类。

   **真正的理由是够不着，不是不值得**：

   > **`takeChild` 是唯一一个既内存不安全、又能被这个 TU 的标志位够到的原语。** `g_inDetachNotify` 是 `Widget.cpp` 匿名 namespace 的内部链接符号；`Signal::emit` 在 `core/`，而 **core 不许依赖 widget**。

   **这一改，残留就从「选择不执行」变成「够不着」——两种完全不同的东西**（Leo 判词）。反证说明它确实不是成本问题：`update()` / `setGeometry` / `setVisible` / `invalidateSizeHint` / `performLayout` **全在 `Widget.cpp` 这同一个 TU**，各加一条 `assert(!g_inDetachNotify)` 只要 Debug 下一次布尔测试。**不加是对的——因为它们不内存不安全。**

   **方法学**：**取舍要按危害写，不能按成本写。** 按成本写的取舍读起来像已经权衡过，实际上把一条同样危险的路径藏在了"性价比"后面。同一条更正已同步到 `src/widget/Widget.cpp` 里 assert 上方的注释。
4. **REM3-G9 的编号**待架构团队复签（见 §11.3）。
5. **成本未取 `/FAsc` 复测**：广播是 O(深度) 的指针追逐 + 每层一次默认空体的虚调用，只在真实 detach 路径上付，绘制/布局/事件路径一条指令不加；**这是论证，不是实测**，按 §11.8 的教训标注在此。
6. **`announceDetached` 的 `detail::DeathWatch host(&parent)` 注册在广播之后 ⇒ 那个游标对"钩子槽销毁 `parent`"恒为 true。** 【**S2 / W2**，本轮登记不修，编排者裁定】

   路径（可逐行复核）：广播在 `Widget.cpp:340`（`detail::notifyDetachToAncestors(node)`），`host` 在 `:361`。钩子跑的槽**销毁 `parent` 是合法的**——D7 只禁止销毁**信号自己的**宿主。而 `~parent` 里的 `cancelOn(detail::g_deathWatch, this)` 在 `:361` **之前**就跑完了，此后注册的游标**没有人再去取消它**⇒ `:365` 的 `host.alive()` **恒为 true** ⇒ `:366` 的 `stillAChild(parent, ...)` 直接读**已释放内存**（它读 `parent.children()`）。

   **修法是两行**（Leo 给的）：把 `detail::DeathWatch host(&parent);` 提到广播**之前**，广播后立刻补一句 `if (!host.alive()) return;`。

   **本轮不做，编排者裁定**：它改的是**契约已禁止的路径**上的运行时行为（G9 禁止钩子跑任何应用代码，销毁 `parent` 更在其外），属评审外的范围蔓延。**但它落在 E14 自己的爆炸半径里**——是 E14 把广播插到守卫**之前**的——所以**必须登记，且定级不低于 S2**。

   ✅ **【E21 / R2 第 4 轮 C7】已修。** 裁定翻转的理由：**这是 G9 三件套里唯一一条修补代价小于登记代价的**——守卫本来就在这一帧里，它的论证也早就写在它上面（那段注释是对的），**它只是被装在了门的另一侧**。而"守卫在 diff 里读起来是有的、事实上是没有的"比"根本没有守卫"更坏：后者会被 lint 扫成候选，前者不会（§12.4 A′ 残留 2：带游标的函数整体豁免）。

   **红态先行，实测原文**（改之前，`build-asan`，探针是一个把自己从 `onDescendantDetached` 里释放掉的宿主）：

   ```
   ==16940==ERROR: AddressSanitizer: heap-use-after-free
   READ of size 8 ...
       #0 ... std::vector<...>::size
       #1 geeyoou::`anonymous namespace'::stillAChild        Widget.cpp:284
       #2 geeyoou::`anonymous namespace'::announceDetached   Widget.cpp:366
       #3 geeyoou::Widget::takeChild                         Widget.cpp:550
   ```

   一次跑出 **5 条**报告：**4 条**是本条（`stillAChild` 的 `:284` / `:285` 两处读，各两次），**1 条**是下面第 7 条那个广播循环自己的 `a = a->parent()`（`Widget.cpp:432`）。

   ⚠️⚠️ **而这 5 条分不开，这是本轮最该带走的一条新事实**：广播走的是离场节点的**祖先链**，`parent` 按构造是链上**第一个**（`announceDetached` 只以 `parent == node->parent()` 被调用）⇒ **唯一能销毁 `parent` 的钩子帧就是 `parent` 自己**，而之后循环的自增就读它；从**更高**的祖先去销毁 `parent` 则必须走 `takeChild`，那被 G9 的 assert 挡住（Debug 直接 abort）。**没有第三种形状。** ⇒ **本条的 UAF 用例必然同时打红第 7 条**，所以它不能进门禁。

   **落进门禁的是一条确定性的顺序探针**（`tests/widget/test_removal.cpp` 的 `the_announcement_arms_its_cursor_before_the_broadcast`）：钩子里读一次 `detail::deathWatchDepth()`——**修之前是 1**（只有 `takeChild` 自己那个），**修之后是 2**。不碰任何已释放内存，不违反 G9 的任何一条，三条腿都能跑，而且**恰好在守卫站错边时变红**。两个方向都实测过。
7. **`notifyDetachToAncestors` 的祖先链循环一个守卫都没有。** 【**S2 / W2**，本轮登记不修】

   `for (Widget* a = node->parent(); a; a = a->parent()) a->onDescendantDetached(node);`（`Widget.cpp:425-433`）：钩子若销毁了自己所在的 `a`、或它上面的任何一层，**下一次 `a->parent()` 就是 UAF**。

   这正是 §11.6 约束 (ii) 要的那张检查点表所指向的东西。**本轮以 G9 替代 (ii)**（理由见上面"§11.6 约束 (ii) 的处置"），于是**广播循环本身无守卫**这件事从"设计缺口"变成了"依赖契约的已知残留"——**它依然是残留，本条就是它的登记。**

   ⚠️ **【E21】本条与第 6 条共用同一个触发器**（论证见第 6 条末尾：`parent` 是祖先链上的第一个，没有第三种形状）。实测第 6 条的红态时，本条**跟着红了一条**（`notifyDetachToAncestors`，`Widget.cpp:432`）。**后果是可延期性变了**：第 6 条已修，但**它的 UAF 形态验收要等本条一起做**——今天进门禁的只是那条顺序探针。下一轮排期时这两条是**一块**，不是两条。

   ⚠️ **第 6/7 条与 REM3-RES-7 的处置是同一根链条**：三者都建立在"G9 让钩子不跑应用代码"这一条契约上。契约一旦被违反，三条同时作废，而**今天没有任何机器能在那一天变红**。这是本节最重的一笔账，写在最后而不是省略。

---

### 11.12 【E15 · E16】三个容器兑现钩子 —— `ScrollArea` / `AppWindow` 的降级语义与用例

> 状态：**已实现、三条腿门禁全绿、红态证据齐备，未由测试团队复核。** 结论由测试团队下。
> 本小节收口 **REM3-RES-1**：E14 给了通知，E15 让容器听，E16 是验收。

#### 缺陷在 E14 之后仍然存在的形状

E14 落地那天，下面这两行在库里的每一个 `ScrollArea` 上仍然是 heap-use-after-free：

```cpp
sa->content()->parent()->removeChild(sa->content());
paintTree(...);   // <- 中间没有任何应用调用
```

守卫是**帧作用域**的，它不修复对象状态；钩子存在但**没有容器覆写它**。所以 E14 之后 `content_` 照样悬垂，下一次重绘照样炸。

#### 降级语义（Elena 裁定，实现照此）

| 方法 | 摘除后的答案 |
|---|---|
| `ScrollArea::content()` | **`nullptr`** —— ⚠️ **公开 API 契约变更，已写进 `ScrollArea.hpp` 的注释** |
| `contentSize()` | `{0,0}` |
| `scrollOffset()` / `maxScroll()` | `{0,0}` |
| `viewportSize()` | `localRect().size()`（无 content ⇒ 无滚动条） |
| `bars()` / `needVBar()` / `needHBar()` | 双 false |
| `relayout()` / `setContentSize()` / `scrollTo()` / `ensureVisible()` | 立即 `return`，不写任何几何 |
| `onPaint` | 照常画边框，不画任何 track/thumb |
| `onMouse` / `onKey` | 不接受任何滚动输入（**不 `accept()`**，让事件继续冒泡给外层） |

**⚠️ 明令不做自愈，这是正确答案不是遗憾。** 不得重建 viewport/content：应用亲手摘掉了内容，静默复活一个新对象会让 `content()` 悄悄换成**另一个 widget**——"从会崩退化成会静默答错"，正是本轮整治要拒绝的那笔交易。降级是**永久**的。（"摘掉后还想接回去"是一个 API 缺口 `ScrollArea::setContentWidget`，**登记备查，本轮不做**。）

**实现上只有真正解引用成员的入口需要自己的空检查**，其余全部由一处推导出来：`bars()` 答"没有条" ⇒ `viewportSize()` 是整个 `localRect()` ⇒ 四个 bar/thumb 矩形全空 ⇒ `onPaint` 的 `bar` lambda 首行返回、`maxScroll()` 算术上就是 `{0,0}`。需要自己那一条的是 `setContentSize` / `scrollOffset` / `scrollTo` / `ensureVisible` / `relayout` / `bars` / `onMouse` / `onKey`，统一走一个私有谓词 `hasParts()`，而不是每个方法各写一遍空检查。

#### `AppWindow`：**`relayout()` 一个字都没改**，这是方案正确性的旁证

`AppWindow::relayout()` 开头的 `if (!header_ || !content_) return;` 与 `if (fill_)` 从这个类被写出来那天就在源码里，而**库里没有任何一行代码能满足它们**——它们是长得像防御性编程的死代码。**E15 什么都没改，只是让它们描述的状态第一次可达。** `setHeaderVisible` / `isHeaderVisible` / `hitZoneAt` 同样早就是这么写的，同样一字未改。

`AppWindow.cpp` 的 diff 因此是**纯新增**：一个三行的 override，没有一行修改。

**唯一的新增判空**：`setContent<T>()` 原本直接解引用 `content_`，现在 `content_ == nullptr` 时返回 `nullptr`（`AppWindow.hpp`）。

#### REM3-G9 遵守情况

两个 override 各只做**指针比较 + 成员置空**。没有 `update()`、没有信号、没有 `removeChild`、没有虚调用。重绘没有丢：摘除方走的是 `Widget::takeChild`，它在广播**之前**就把腾出的区域标脏了。

#### 红态先行（E15 落地**之前**跑的，原文摘录）

用例先写、先入门禁，在 `ScrollArea` / `AppWindow` 的 override **不存在**时跑：

* **Release**：`Segmentation fault`，退出码 **139**（进程直接死在 `taking_the_viewport_clears_both_of_a_scrollareas_pointers`）。
* **ASan（RelWithDebInfo /O2）**：**143 条 AddressSanitizer 报告**，随后一条 deadly signal 中断整个 run，退出码 1。

第一条报告，逐字：

```
==5028==ERROR: AddressSanitizer: heap-use-after-free on address 0x1292065a3754
READ of size 4 at 0x1292065a3754 thread T0
    #0 geeyoou::ScrollArea::contentSize include\geeyoou\widget\ScrollArea.hpp:28
    #1 geeyoou::ScrollArea::bars        src\widget\ScrollArea.cpp:167
    #2 geeyoou::ScrollArea::needVBar    src\widget\ScrollArea.cpp:176
    #3 geeyoou::ScrollArea::vBarRect    src\widget\ScrollArea.cpp:287
    #4 geeyoou::ScrollArea::vThumbRect  src\widget\ScrollArea.cpp:298
    #5 geeyoou::ScrollArea::onPaint     src\widget\ScrollArea.cpp:336
    #6 geeyoou::Widget::paintTree       src\widget\Widget.cpp:1140
    #7 gy_case_detach_notify_a_scrollarea_that_lost_its_content_survives_paint_wheel_and_resize
freed by thread T0 here:
    #4 geeyoou::Widget::removeChild     src\widget\Widget.cpp:574
```

143 条报告的**使用点**分布（`#0` 帧去重计数）：

| 使用点 | 条数 |
|---|---|
| `ScrollArea::contentSize`（`ScrollArea.hpp:28`） | 108 |
| `Rect::operator==`（`Widget::setGeometry` 的幂等短路读的是**已释放**的 `geometry_`） | 16 |
| `Widget::setGeometry`（`Widget.cpp:607`） | 4 |
| `Widget::window` / `Widget::add<Widget>`（`AppWindow::setContent` 走进已释放的 `content_`） | 5 |
| `ScrollArea::relayout`（`ScrollArea.cpp:215`，即 `content_->layout()`） | 1 |

装上 override 之后：**三条腿全绿，207 个用例 0 失败，ASan 0 条我方报告**（日志里剩下的报告全部是本机 SogouPY.ime 的，分类器判 `[known]`）。

#### 用例增量：**新增 7 条（200 → 207）**，`tests/widget/test_detach_notify.cpp`

| 用例 | 覆盖的验收条 |
|---|---|
| `a_scrollarea_that_lost_its_content_survives_paint_wheel_and_resize` | 反例①逐字 + 连续 3 次 `paintTree` + `dispatchMouse(Wheel)` + `setGeometry` + 不自愈 + 整张降级表 |
| `taking_the_viewport_clears_both_of_a_scrollareas_pointers` | 反例②逐字 + **两个指针都为空** + 不自愈 + `ensureVisible`/`setContentSize` 不写几何 |
| `the_announcement_climbs_the_whole_ancestor_chain` | **正面**断言：深度 ≥3 的容器收到孙子的通知，且 `node` 参数就是那个孙子 |
| `destruction_announces_nothing` | `~Widget` **不**触发钩子（含"探针本身是好的"的对照组） |
| `an_appwindow_that_lost_its_content_lays_out_and_paints` | `AppWindow` 的 override、`relayout()` 死分支变活路径、`setContent<T>` 答 `nullptr` |
| `a_measurement_that_steals_the_content_degrades_on_the_member_re_read` | **B7**：CP-S1 的 `content_ != ct0` 子分支 |
| `an_arrange_that_steals_the_content_degrades_on_the_member_re_read` | **B7**：CP-C1 的 `content_ != ct0` 子分支 |

**⚠️ 用例里每一次会 UAF 的读都有消费者**（`CHECK_NEAR` / `CHECK_EQ`），照 §11.11 那条方法学教训写的：`(void)sa->contentSize();` 在 `/O2` 下会被整个删掉，用例照样 FAIL 而 ASan 一条报告都没有。

**踩到过的一个陷阱，写下来给下一个人**：两条 B7 用例里的"窃贼"最初没有 `arm()`，结果它在**搭场景阶段**就偷走了 content —— `add<T>` 会调 `childAppended()`、`addWidget()` 会标脏，任何一个都会跑一趟测量，也就是被测的那扇门。症状是 `contentBefore == nullptr`。**建场景的语句本身就是门。**

#### B7：那两个成员重读子分支，这次真的被走到了

§11.11 更正过"E14 本轮不成立"，理由是 `viewport_` / `content_` 是 `ScrollArea` 的私有成员、只有它自己的 override 能置空。**E15 提供了那个 override，两条子分支现在确实被走到。**

不是靠论证，是靠一次**判别性实验**（形状取自 §11.11 的 B2）：把 CP-S1 / CP-C1 / CP-C2 / CP-S2 里的 `viewport_ != vp0 || content_ != ct0` 两项**删掉**、其余不动，两条 B7 用例立刻从"降级 1 次"变成**进程崩溃**：

```
==36788==ERROR: AddressSanitizer: access-violation on unknown address 0x0000000008
    #0 geeyoou::Rect::operator==        include\geeyoou\core\Types.hpp:73
    #1 geeyoou::Widget::setGeometry     src\widget\Widget.cpp:607
    #2 geeyoou::ScrollArea::relayout    src\widget\ScrollArea.cpp:355   <- content_ 是 nullptr
    #3 geeyoou::ScrollArea::onGeometryChanged
    #5 gy_case_detach_notify_a_measurement_that_steals_the_content_degrades_on_the_member_re_read
```

⇒ 删掉成员重读，这一帧就会跟着一个**刚刚被置空**的成员走下去。**成员重读那一段今天不再是死代码。** 实验用文件备份还原，未跑任何 git 历史改写命令。

**断言取的是 `framesDegraded == 1`（精确值，不是 `>=`）**：REM3-G8 说计数单位是**帧**，一次 `setContentSize` 可以诚实地记两次（`test_rem3_doors.cpp`），`>=` 会在这个检查点开始因为第二个原因触发的那天继续通过。
**这两条用例只能证明"该条件为假并降级了"，不能直接证明"是五项里的哪一项"**——它靠的是构造：`sa` 是本帧的栈对象、viewport 未动、被摘的 content **活着**（parked 在用例自己的 `unique_ptr` 里），所以三个游标半边全为真，唯一能为假的就是成员重读。上面的判别性实验是这个论证的实测背书。

#### 门禁

* 基线 **200 用例 / 三条腿全绿** → 改动后 **207 用例 / 三条腿全绿**，0 失败。
* **Release 独立跑 3 次**，退出码全 0，三次 stdout **逐字节相同**。
* **整份 Release stdout 与基线 diff**：只有三处差异 —— 7 条新用例的 `PASS` 行、`200 个用例` → `207 个用例`、以及下面这一条：
  * `[soak] live-allocs 17/17/17` → `18/18/18`。**已定位并解释**：逐条二分实验（把新用例逐个禁用后重跑 soak）证明这 +1 完全来自 `an_appwindow_that_lost_its_content_lays_out_and_paints`，而且只来自它**绘制**的那一段——套件里在此之前没有任何用例**画过 `WindowHeader`**，于是它的标题字号第一次进了 `src/render/Painter.cpp` 那张**进程级、按字号分桶**的 `FontRegistry` 缓存，多出一个常驻块。**不是泄漏**：同一个用例画 3 次也只多 1 个块，soak 400 圈里 `base == peak == last` 依旧成立，soak 的四条断言全部通过。四条序列的**平坦性**（唯一被断言的性质）未变。
* **9 张 golden 逐字节不变**（SHA-256 逐个比对）。
* `AppWindow::relayout` 的三处判空**一字未改**（对 HEAD 做过 `diff`，`AppWindow.cpp` 是纯新增）。

#### 未验证 / 留给下一轮

1. **`Shell`（showcase）今天仍然带着 RES-1 的缺陷。** `Shell::pageArea_` 与 `Page::host`（每个页面一个 `ScrollArea*`，存在 `std::vector<Page>` 里）没有 override，摘掉页面宿主之后 `Shell::relayout()` / `showPage()` 仍然会走悬垂指针。**Elena 的降级表没有覆盖 `Shell`**，而它的降级语义（`showPage` 遇到 host 为空该怎么办）是需要裁定的产品问题，不是机械改造，**本轮据此没有做**，登记待裁。
   **⇒ 已裁（E18，编排者）：不补，登记 W2/S2。** 两条理由：(a) `showPage` 遇到 host 为空该怎么办是**产品裁定**不是机械改造；(b) `Shell` 在 `examples/` 里，不是库。见 §12.4 的 B 组。
2. ~~**`AppWindow` 的 `maximizedChanged` 槽（`AppWindow.cpp:34-40`）无条件解引用 `header_`。**~~ —— **已在 E18 修复，见下面的补记。** 原文保留：`header_` 现在**可以**是 `nullptr`，所以"摘掉 header 后切换最大化"是一次空指针解引用（确定性崩溃，不是 UAF）。裁定书写明"唯一新增 `setContent<T>` 的判空"，E15 **据此没有动它**，登记待裁。
3. ~~**showcase 真实渲染未验证**~~ —— **已由编排者实测（E18 轮）**：showcase 四页共 **60 次拖动 + 144 次滚轮**全部存活、渲染正确，滚轮走的正是 E15 新加的降级早退路径。原文保留：无法在本环境自动化，需编排者人工确认；改动的可见面是任何 `ScrollArea` 在内容被摘除后不再画滚动条。存活路径的行为另有"整份 Release stdout 与基线 diff"与 9 张 golden 逐字节相同背书。
4. **`onMouse` 降级后残留的 `hoverV_` / `hoverH_`** 不会被清掉（早退发生在 `Leave` 分支之前）。看不见——两个 bar 矩形都是空的，读它们的东西一个都不画——但它是状态而不是不变量，写在这里备查。
5. **`sizeHint()` 未纳入降级表**：`ScrollArea::sizeHint()` 不读任何成员，摘除后照样答那个默认窗口尺寸（320x200）。这是**故意不改**，但裁定表里没有这一行，登记以免下一个人以为是漏的。

---

### 11.13 【E18】`AppWindow::maximizedChanged` 的判空

> 状态：**已实现、已自检，未由测试团队验证。** 结论由测试团队下。

本轮**唯一**的代码行为改动（其余是文档与注释）。它是 §11.12 未验证栏第 2 条的收口。

#### 为什么这一条要做，而 `Shell` 那一条不做

两条都是 §11.12 登记的待裁项，处置不同，判据是**这是不是一次机械改造**：

* `maximizedChanged` 的槽：**降级语义已经存在**，`AppWindow` 的其余部分（`relayout` / `setHeaderVisible` / `isHeaderVisible` / `hitZoneAt`）从写出来那天就是"没有 header 就什么都不做"。补上一条判空是**把这个类里唯一没跟上的一行拉齐**，没有任何新语义要裁。
* `Shell::showPage` 遇到 host 为空该怎么办：**是产品裁定**（跳过？换页？空白页？），不是机械改造，而且 `Shell` 在 `examples/` 里不是库。**编排者裁定：不补，登记 W2。**

#### 它落在 E15 自己的爆炸半径里

`header_` 在 E15 之前**永不为空**——它在构造函数里赋值、此后再无写入点——所以这个槽无条件解引用它是**对的**。**是 E15 让它可空的**，而 E15 只补了 `setContent<T>` 的判空，没有回头看这个槽。

从悬垂指针降级成空指针是**改善**（确定性崩溃优于 UAF），但**留一个"每次最大化都会走到"的空解引用不可接受**——用户按一下最大化按钮就到。

#### 改动本身

`src/widget/AppWindow.cpp` 构造函数里那个槽：

* `header_->setMaximized(on);` → `if (header_) header_->setMaximized(on);`
* `relayout()` 与 `update()` **保持无条件**：边框宽度与有没有 header 无关，`relayout()` 从第一行起就判空，`update()` 不碰这两个成员。

**遵守 E15 的降级纪律**：不自愈、不重建 header。摘掉标题栏的应用不会被静默塞回一个新的——状态只是停止被维护。

#### 能变红的证据（实跑，文件备份还原）

新增用例 `detach_notify.an_appwindow_that_lost_its_header_survives_a_maximize_change`（`tests/widget/test_detach_notify.cpp`）。

| 实验 | 改动 | 结果 |
|---|---|---|
| **红态** | 把 `if (header_)` 删掉、其余不动 | Release 腿 **Segmentation fault，退出码 139**，进程死在这条用例上（它按字典序紧跟 `an_appwindow_that_lost_its_content_lays_out_and_paints`，日志最后一行 PASS 的就是后者） |
| 装回判空 | — | 见下面的门禁 |

**这条用例与本文件其余部分不同：它在三条腿上都能红**，因为它是空解引用而不是 UAF——不需要 sanitizer 就看得见。用例里仍然按 §11.8 的证据标准把每一次读都消费掉（`CHECK_NEAR` 比对 content 几何），所以它同时是一条关于"降级后 `relayout()` 停在第一行"的正面断言。

**对照组在同一条用例里**：摘 header **之前**先 `emit(true)` / `emit(false)` 各一次并断言 `header()->isMaximized()` 跟着变——**没有它，一个"把整个槽体删掉"的假修复也能让这条用例变绿**。

#### 一处同步更正的注释（无行为改动）

`src/widget/Widget.cpp` 里 REM3-G9 assert 上方的理由，与 §11.11 未验证栏第 3 条**写着同一个已被判定为错的理由**（"只有会破坏遍历的那一项值得付运行时代价"）。文档改了而源码没改，等于留一份会被继续引用的错抄本，所以一并改成"**够不着，不是不值得**"。**只动注释**，assert 与它的作用域一个字未改。

---

### 11.14 【E19】§12.4 A 组的三条 S1：N1 / N4 / #16

> 状态：**已实现、已自检、三条腿门禁全绿、四条红态先行且逐条留档，未由测试团队验证。** 结论由测试团队下。
> 本小节把 §12.4 A 组的三条 **S1** 关掉，并**更正**其中两条的判词——两条的现场都与表里写的不一样，且不一样的方向相反：一条比表里说的更糟，一条比表里说的更窄。

#### 三条各自的失败形态不同，这是本轮最该被带走的一条事实

同一族、同一个定级、同一轮，**三种模式**，所以证据的腿也不同：

| 门 | 今天真正会发生什么 | 哪条腿看得见 |
|---|---|---|
| **N1** `Layout::invalidate` | Layout 对象自己被 `delete`，`:31` 读已释放的 `this->host_`，随后 `performLayout()` 对已释放的**宿主又读又写** | ASan |
| **N4** `Window::setFocusWidget` | `focus_` 指向的控件被释放，`:166` 经它做**虚调用** | ASan（Release 双峰） |
| **#16** `AppWindow::relayout` | E15 的钩子**先把成员置空**，所以是**空解引用**不是悬垂 | **三条腿都红** |

**#16 这一行值得单独读**：E15 给 `AppWindow` 装了 `onDescendantDetached`，广播在任何释放**之前**跑，于是这条 S1 从 UAF 降级成了确定性空解引用。**降级不是关闭**——它每帧路径上仍然是一次进程当场死，只是从"只有 sanitizer 看得见"变成了"谁跑谁看得见"。E15 的收益是把一个隐形缺陷变成了一个显形缺陷，这正是它当初被判为正确方向的理由。

#### N1 —— 需要几个守卫，守谁，以及一个守不了的对象

**两个对象有风险，只有一个能上游标**：`host_` 是 `Widget*`，而 `Layout` 自己也会被删，可 `DeathWatch` 的游标是 `Widget*`，`~Layout` 不在任何一条链表的取消点上。**结论是一个守卫，守 `host_`**，理由是两条存活性事实并不独立：

1. **`this` 活 ∧ `host_` 非空 ⇒ 宿主活。** 宿主的死一定跑 `~Widget`，它要么 park 这个 layout（park 会把 `host_` 置空），要么把它删掉。所以"活过宿主之死的 layout"必然 `host_` 为空；反过来 `host_` 非空就意味着 `~Widget` 还没跑。
2. **本路径上删掉 `this` 的正是宿主之死。** 游标一翻假，`this` 要么已释放要么已 park，两种都必须一个字节都不碰——而"一个字节都不碰"与无守卫版本**逐位同义**（park 过的 layout `host_` 为空，`if (host_)` 本来就不成立）。

**没有成员重读**，这是 `host_` 的性质而不是疏忽：写它的只有两处，`adoptLayout`（private，且**接管所有权**，无法把一个已有宿主的 layout 重新绑到别处）与 `parkLayout`（置空）。`host_` **只会变空，不会变成别的对象**，而"变空"正是函数本来就有的那个 `if (host_)`。

**两条残留，登记不掩盖**（两条都需要一条以 `Layout*` 为键的游标链表 + 会取消它的 `~Layout`，那是机制改动不是检查点）：

* **RES-N1a（S2/W2）**：`onInvalidated()` 里 `host()->setLayout<Other>()`——宿主活着，`this` 被换掉并释放，游标仍读真。
* **RES-N1b（S3/W2）**：进函数时就**已经 park** 的 layout 没有宿主也就没有游标，钩子里任何退栈到深度零的布局趟都会排空停车场并释放 `this`。

**明确否决的廉价替代**：门后重读 `host_->layout()` 并与 `this` 比较。它拿一个**已释放的指针值**去比一次新分配——一个钩子里连着两次 `setLayout` 就能让新对象落在旧地址上，于是它会**静默答对**。这正是本轮整治反复拒绝的那笔交易（§12.5 第 10 条）。

#### N4 —— 先判断再动手：`widgetDetached` 到底覆盖了没有

**覆盖了，而且没有洞；洞在"哪些死亡会经过它"。** 结论与 §11.4 N 表原文相反，更正已写在那张表下面的要点里。

* `Window::widgetDetached` 自 R1 就清 `focus_`，它由 `announceDetached` 调用，而 `announceDetached` 只有 `Widget::takeChild` 一个调用者——**那是一棵子树离开活树的唯一一扇门**（ADR-R2-11 §3，两条 grep + Leo 的扩面复核）。广播**按离场子树的每个节点**发，所以孙子也在内。⇒ **一切"树内移除"形状的销毁都已被覆盖**，包括 §12.4 点名的那条 `SelectBase` 路径。
* 用例的第一块就是那条路径，**在无守卫的构建上实测存活**。

**剩下的两个形状才是 N4**：

1. **不经 `takeChild` 的销毁**。`~Widget` 对 Window 不发任何通知（ADR-R2-11 §3 的有意设计，理由是"告诉一个正在被释放的对象它的成员要没了"没有意义）。而 `setFocusWidget` 从来不要求参数在树里——一个孤儿控件，或者应用早先 `takeChild` 下来、现在才丢掉的子树，`focus_` 无人清空。
2. **窗口自己死在门里**。从 focus-out 处理里关掉窗口是应用会写的东西，而这是 `widgetDetached` 的记账从来管不着的。

**检查点 CP-W1**：`!self.alive() || focus_ != w || (w && !incoming.alive())`——`this` → 成员重读 → 游标，顺序承重（见下面的判别性实验 D5）。

**不修复状态，登记**：守卫是帧作用域的，触发后 `focus_` 仍指着那个死掉的控件。置空是一次经 `this` 的写，而 REM3-G1 明令降级帧不得写；真正的修法是给 `focus_` 补上"不经 takeChild 的死亡"那条通知路径，那是 ADR-R2-11 的题目。**RES-N4a（S2/W2）。**

#### #16 —— 三扇门、三个检查点，以及 `header_` / `fill_` 的两处更正

| 检查点 | 紧贴哪扇门后 | 检查项 |
|---|---|---|
| **CP-A1** | `header_->setGeometry` | `!self.alive()` → `content_ != ct0` → `fill_ != fl0` → `!ctw.alive()` → `(fl0 && !flw.alive())` |
| **CP-A2** | `content_->setGeometry` | `!self.alive()` → `fill_ != fl0` → `(fl0 && !flw.alive())` |
| **CP-A3** | `fill_->setGeometry` | `!self.alive()` |

* **`header_` 不守**——Leo 在 E9 抽查时的判词成立，本轮复核确认：`:82` 与 `:84` 两次解引用都在**第一扇门之前**（`borderWidth()` / `localRect()` / `WindowHeader::height()` / `isVisible()` 全是非虚），`:84` 之后再没有任何语句碰它。
* **CP-A2 只有三项，CP-A3 只有一项**：`content_` 在 `:86` 之后不再出现，`fill_` 在 `:87` 之后不再出现——与 `ScrollArea::relayout` 的 CP-S2 同一条理由。
* **`fill_` 走 `MayBeNull` 游标**：它可以合法为空（`setContent<T>` 之前的每一个 `AppWindow`），而 `DeathWatch` 的契约把空定义为已死。空时不得查 `alive()`，否则一个**健康**帧会被判成降级并跳过 `contentResized.emit` 与 `update()`——那才是真的行为改变。
* **`:89 contentResized.emit` 不危险**：宿主是 `this`，D7 豁免（表 #17），门后只剩 `update()`。这是 D7 在本文件里唯一一次干实事。

#### 设施的一处扩展：`DeathWatch(const Widget*, MayBeNull)`

**Q7 的 `assert` 前提对"可选成员"不成立，这三条门是证据。** 原话是"任何一个上守卫的现场，都是因为它**马上要解引用那个指针**"——对**无条件**解引用的成员完全正确（`ScrollArea::content_` 在 `hasParts()` 之后，`AppWindow::content_` 在 `relayout()` 第一行之后），对**有条件**解引用的成员是错的：`AppWindow::fill_`、`Layout::host_`、`Window::focus_` 三个都是"没有它也是正常状态"。

REM3-G7 给的处方（现场自己先做空检查）只在"现场可以整帧放弃"时可用，而这三处**都不能**：没有内容的窗口仍要摆标题栏，没有宿主的 layout 仍要跑 `onInvalidated()`。所以扩展的是构造方式而不是语义——**同一条链表、同一套取消、同一个 `alive()`，空仍然读作已死**；现场欠的是一句 `(p0 && !pw.alive())`，用门前捕获的局部量做短路，而不是让守卫去冒充一次死亡。

**代价**：`Widget.hpp` 纯新增 39 行（一个 tag 类型 + 一个构造函数 + 理由），`sizeof(Widget)` 与 `sizeof(DeathWatch)` 均不变，原来那个带 `assert` 的构造函数一个字未改。**已由架构团队复签通过**（四条理由：语义零变更、tag 类型优于 `bool`、它偿还的是 Q7 判词里的**真实错误**而非加一个便利、纯新增回滚代价为零）。⇒ **§11.2 的 API 形状表与 §11.1 Q7 的判词已随之改到位**——本条动的是那节自称的"唯一权威"，**正文与实现不一致，本身就是那节存在的理由被违反**。双参构造已补 `explicit`（无隐式转换风险，只为与单参构造一致）。

#### 红态先行（四条，全部在封门**之前**实跑并留档）

用例落在 `tests/widget/test_rem3_doors.cpp`，每一次可能 UAF 的读都有消费者（§11.8 的证据标准）。**逐条给出最内层非运行时栈帧**：

**N1** —— `a_layout_that_loses_its_host_in_on_invalidated_stops_there`，ASan 腿 **10 条报告**，退出码 1：

```
heap-use-after-free  READ 8   #0 geeyoou::Layout::invalidate      src\widget\Layout.cpp:31
                     freed by #3 SuicidalLayout::~SuicidalLayout  (经 ~Widget 的 unique_ptr)
heap-use-after-free  READ 8   #0 geeyoou::Widget::performLayout   src\widget\Widget.cpp:727
heap-use-after-free  WRITE 1  #0 geeyoou::Widget::performLayout   src\widget\Widget.cpp:728
```

⚠️ **第三条是写，不是读**——§11.4 的 N 表把 N1 的门后只记成 `if (host_)`，实际的爆炸半径包含一次对已释放宿主的**写**。定级 S1 是对的，描述是轻了。

**N4** —— `a_focus_handover_that_loses_the_winner_stops_there`（第二块，孤儿获焦者）：

```
heap-use-after-free  READ 8   #0 geeyoou::Window::setFocusWidget  src\widget\Window.cpp:166
                     freed by #3 FocusHook::~FocusHook
                              #5 FocusHook::onFocusChanged
                              #6 geeyoou::Window::setFocusWidget  src\widget\Window.cpp:165
access-violation              #0 geeyoou::Window::setFocusWidget  src\widget\Window.cpp:166  <- 进程在此死掉
```

**#16 之一** —— `an_appwindow_relayout_that_loses_its_content_stops_there`，**三条腿都红**（Release 退出码 139）：

```
access-violation on 0x000000000008   <- content_ 是 nullptr
    #0 geeyoou::Rect::operator==     include\geeyoou\core\Types.hpp:73
    #1 geeyoou::Widget::setGeometry  src\widget\Widget.cpp:620
    #2 geeyoou::AppWindow::relayout  src\widget\AppWindow.cpp:86
```

**#16 之二** —— `an_appwindow_relayout_that_loses_itself_stops_there`：

```
heap-use-after-free  READ 8   #0 geeyoou::Signal<Size>::emit      include\geeyoou\core\Signal.hpp:209
                              #1 geeyoou::AppWindow::relayout     src\widget\AppWindow.cpp:89
                     freed by ~AppWindow（从 fill_->onGeometryChanged 里）
```

#### 判别性实验（五个，每个只拆一项）

**合取反证只能证明"至少有一处必要"**（§12.5 第 6 条）。逐条：

| # | 只拆掉什么 | 结果 |
|---|---|---|
| **D1** | N1 的**检查块**（守卫与门前捕获**保留**） | 原 UAF 原样回来（`Layout.cpp:98` 读 `host_`），11 条报告，退出码 1 ⇒ **救命的是检查，不是守卫在场** |
| **D2** | N4 的 `(w && !incoming.alive())`，其余两项保留 | 只有孤儿那一块红：`Window.cpp:216` heap-use-after-free ⇒ **获焦者的游标单独必要，`widgetDetached` 替代不了** |
| **D3** | CP-A1 的 `content_ != ct0`，其余四项保留 | Release 段错误 139 / ASan `AppWindow.cpp:143` 空解引用 ⇒ **成员重读单独必要** |
| **D4** | **整个 CP-A3**（CP-A1 / CP-A2 保留） | `AppWindow.cpp:153` heap-use-after-free（`contentResized.emit`）⇒ **第三个检查点单独必要** |
| **D5** | N4 的 `!self.alive()`，其余两项保留 | `Window.cpp:212` heap-use-after-free —— **报告出在 `focus_ != w` 这一项自己身上**，因为它要经 `this` 解引用。用例**照样 PASS**（帧随后在游标上降级了），**ASan 红** |

⚠️ **D5 是本轮最值钱的一条，两个理由**：

1. 它把 REM3-G3 的"第一项必须是 `this` 的游标"从一条风格规则变成了**实测事实**——顺序错了，检查链自己就是那次 UAF。
2. 它是 §11.8 那条证据标准的又一个现场：**退出码 0、用例全绿、ASan 红**。任何人拿"套件是绿的"来主张这一项可以省掉，都会漏掉它。

⚠️ **一处方法学教训，写下来给下一个人**：第一版的 #16 用例是"有 fill + `removeChild`"，五项检查里**同时**有四项会触发，于是 D3 拆掉任何单独一项都不会红——实测过，套件全绿。**判别性实验做不出来，往往说明用例的构造不够分离，而不是那一项不必要。** 现在的第一块用 `takeChild`（**摘下但不销毁**，游标按 Q4 的策略仍读真）加上**不调 `setContent`**（`fill_` 前后都是空，它那两项恒静默），把可能触发的检查项压到**恰好一项**。**分离的构造是判别性实验的前提。**

#### 每帧路径的开销：`/FAsc` 实测，不是推算

`src/widget/AppWindow.cpp` 单独取汇编，**真实 CMake flags**（`/O2 /Ob2 /W4 /permissive- /utf-8 /Zc:__cplusplus -DNDEBUG` 加 CMake 注入项）、**真实 include 链**，与 `build/build.ninja` 里那一行逐字一致；对照组是改动前的同一文件。

| | `AppWindow::relayout` 自身指令数 |
|---|---|
| 改动前 | **77** |
| 改动后 | **137** |

**+60**，拆开来看：

| 段 | 实测 |
|---|---|
| 门前捕获 `ct0` / `fl0` | 2 |
| 三个游标构造 | **17** |
| CP-A1 | 13 |
| CP-A2 | 10 |
| **CP-A3** | **2**（`test rax,rax` + `je`——编译器把 CP-A2 载入的 `self.node` 留在 `rax` 里复用了） |
| 三个守卫析构 | **2**（LIFO 折叠成一次 `mov rax,[self.outer]` + `mov [g_deathWatch],rax`，与 §11.5 记的"无论几个都是 2"一致） |

三守卫构造实测 **17**，与 §11.5 在 `ScrollArea` 落点上量到的 17/18 吻合——**同一个形状、同一个数**，那条"探针里的 9 是另一个形状"的更正因此又被独立复现了一次。分项加起来是 46，与整函数的 +60 差 14：那 14 是多出来的活跃值造成的**寄存器压力/溢出**，不是守卫代码本身。**这正是 §11.5 那条教训——落点的形状由周围的代码决定——所以本表以整函数的 77→137 为准，分项只作说明。**

**分母，以及守卫落在哪里**：

* 三个检查点**全部落在既有的 `if (!header_ || !content_) return;` 之内**。丢了标题栏或内容区的窗口**一条都不执行**——与 ADR-R2-01 对 `ScrollArea` 的论证同形。
* 这一帧要发出**三次 `Widget::setGeometry`**（自身实测 **92** 条指令，外加两次调用：虚的 `onGeometryChanged` 与 `Widget::update`，后者自身 **49** 条再加一趟父链行走），其中第一次的 `onGeometryChanged` 是 `WindowHeader::relayoutItems`，它**每个尾部项再发一次 `setGeometry`**；末尾还有一次进应用代码的 `contentResized.emit`。**+60 对 10³ 量级**。
* 没有分支预测灾难：六个条件跳转全是"本帧栈槽/寄存器 vs 0 或 vs 另一个栈槽"，生产中恒不跳转。

#### 门禁

* 基线 **208 用例 / 三条腿全绿**（196 提交、工作树干净）→ 改动后 **212 用例 / 三条腿全绿**，0 失败。**新增 4 条，既有用例一条未改、一条未删。**
* **Release 独立跑 3 次**，退出码全 0，三次 stdout **逐字节相同**。
* **整份 Release stdout 与基线 diff**：**只有 4 行新增的 PASS 和 `208 个用例` → `212 个用例`，此外一个字符都没有。** Debug 腿的 diff 逐字相同。特别地 `[soak] 400 cycles ... live-allocs 18/18/18` **一位没动**，也就是说 soak 末尾那条 `framesDegraded == (cycles+3) × 5` 的常驻断言**照旧成立**——三个新检查点在既有路径上一次都没有触发。
* ASan 腿：**1 条报告，且与基线是同一条**（本机 SogouPY.ime，use / free 两端都无我方帧，分类器判 `[known]`），退出码 0。
* **9 张 golden 逐字节不变**（SHA-256 逐个比对，`tests/visual/baseline/**` 全量）。
* **REM3-G5 机械判据**：五个改动文件 `git diff -U0 | grep -c "^-[^-]"` **全部为 0**，合计 **587 行纯新增、0 行删除**。**没有例外项要说明**——本轮一条既有语句都没有移动、合并、拆分或重写，注释也没有。
* 三条腿都是从既有目录增量构建，但 `Widget.hpp` 一改，ninja 重编了 **73 个目标文件**，`Widget.cpp.obj` / `Layout.cpp.obj` / `Window.cpp.obj` / `AppWindow.cpp.obj` 全部在列——所以 `Widget.cpp:36` 的 `sizeof(Widget)` 预算 `static_assert` **被真正重新求值过**。全库 `/W4 /permissive-` **零警告**（三条腿各自 `grep -c "warning C"` 为 0）。

#### 未验证 / 留给下一轮

1. ~~**`DeathWatch` 的 `MayBeNull` 构造函数是对 §11.2「唯一权威」API 形状的扩展**，需要架构团队复签。~~ **【已复签通过】** 它不改语义、不改尺寸、不改取消策略；**§11.2 的 API 形状表与 Q7 的判词已随之改到位**（构造函数与 tag 类型进表，Q7 第 2 条的主语收窄为"无条件解引用的现场"）。复签同时提的一处小瑕疵——双参构造未标 `explicit`——**已补**：无隐式转换风险，标它是为了与紧邻其上的单参构造一致。
2. **RES-N1a / RES-N1b**（见上）：`Layout::invalidate` 的另外两条删除路径没关，两条都要一条以 `Layout*` 为键的游标链表。**S2 / S3，均排 W2。**
3. **RES-N4a**：`focus_` 在降级后仍是悬垂成员。守卫是帧作用域的，这是 REM3-RES-1 在 `Window` 上的又一个实例。**S2 / W2。**
4. **N4 的第一块（`widgetDetached` 覆盖的那条路径）现在多记一次 `framesDegraded`**：`focus_` 被清空 ⇒ 成员重读为假 ⇒ 帧降级。**行为逐位不变**（原本 `if (focus_)` 也不成立），变的只有诊断计数。按 REM3-G8 记录是对的（这一帧确实没把 `onFocusChanged(true)` 送出去，而放弃在返回值里不可见），但它是本轮唯一一处让既有路径**多记一次**的地方。既有门禁未受影响（Release stdout 逐字节相同证明了这一点），但**下一个改 soak 断言的人要知道有这条**。
5. **`setFocusWidget` 的重入语义有一处收紧**：门里若有人重入 `setFocusWidget(other)`，本帧的 `focus_ != w` 会为真而降级，于是外层**不再**对 `other` 第二次调用 `onFocusChanged(true)`。原行为是通知两次。**这更像修复而不是回归，但它是一次行为改变。**
   **今天不可达，两条独立依据**：(i) 整份 Release/Debug stdout 与基线逐字节相同；(ii) **静态复核**——库内 `onFocusChanged` 的覆写共 **4 个**，逐个看过，**没有一个重入 `setFocusWidget`**。(ii) 比 (i) 强，因为 (i) 只说明用例没踩到。⇒ **登记备查足够，不需要为它写用例**；哪天有人在 `onFocusChanged` 里改焦点，这条就是他要读的那一段。
6. **`Layout::onInvalidated()` 在库、examples、tests 里的覆写数是 0**（本轮的用例是进程史上第一个）。所以 N1 的门此前从未真正跨越过——这解释了为什么它能活到第九次复扫才被发现，也意味着**这条门的红态完全依赖那条新用例存在**。
7. **`/FAsc` 只取了 `AppWindow.cpp`。** N1 与 N4 的成本按 §11.5 的单价推算（各一到两个守卫 + 一次检查，冷路径），**未实测**——按 §11.5 自己的教训，这一条就是推算，标注在此。

---

## 12. 【E13】R2 关帐

> 状态：**本节是关帐记录，不是验收结论。** 本轮全部改动"已实现、已自检"，**结论由测试团队下**。
> 第 12.4 节（已识别未封）是**下一轮唯一的输入**——每一条都带定级与轮次，没有"待办"这种没有主语的词。

---

### 12.1 本轮封了什么

**同一缺陷族（"门后继续读自己"）的五例中的三例**：

| 例 | 位置 | 落点 |
|---|---|---|
| 第 1 例 | `GroupBox::sizeHint()` | CP-G1（§11.4 #1） |
| 第 2 例 | `ScrollArea` 的**四扇门** | CP-S1 / CP-S2 / CP-C1 / CP-C2（#3 / #4 / #7 / #8） |
| 第 5 例 | 核心 `takeChild` / `clearChildren` / `announceDetached` | E17（#12 / #13 / #14） |

**两条护栏**：

* **M-1** —— `Widget::contentRect()` 的 `layoutRect()` 门（#28）与 `runLayoutIfAny` 的调用侧（#28b）。`layoutRect()` 是 `protected virtual`，**按 P1 的字面它一直是门**，前两版的表里没有它——这是"表不是扫出来的"最硬的一条证据。
* **M-2** —— `Layout::measureFor` 的**度量深度上限**。`g_measureDepth` 被正确加减、被停车场读，**却从来没有与任何上限比较过**；M4 只管住排布半边。

**REM3-RES-1（守卫解决不了的那一半）**：守卫是**帧作用域**的，它不修复对象状态。ADR-R2-11 的 `onDescendantDetached` 给了通知（E14），`ScrollArea` / `AppWindow` 听了通知（E15），验收是 E16。**`AppWindow::maximizedChanged` 的判空**（E18，§11.13）是这条线上最后一处补漏。

**设施与诊断**：

* `detail::DeathWatch` / `LiveCursor` / `LiveGuard<>` 搬进 `Widget.hpp`，**零 `friend`** 即可被库外子类在 const 成员函数里使用；
* `LayoutDiagnostics::framesDegraded` 计数器（REM3-G8：**单位是帧**）；
* **契约按调用者改写**：`Layout.hpp:168-174` 从"实现 `Layout` 的类的义务"扩写成"**任何调用 `sizeHint()` / `setGeometry()` 之后仍要读自身的代码**的义务"，并在 `Widget.hpp` 的两处声明各留一句反向引用。**这是 §11.0 定的根因（契约写错了主语）的正面修复，不是文档整理。**

---

### 12.2 本轮的机制产出

比缺陷修复更耐久的那一半：

| 产出 | 内容 | 状态 |
|---|---|---|
| **门谓词** | **P1** 对任何 `Widget` / `Layout` / `SelectBase` / `StyleSubject` 的**虚**成员调用（主语已改宽）、**P2** 登记过的库函数、**P3** 信号发射、**D7 豁免**（发自有信号且门后只读 `this`） | 已定 |
| **REM3-G1 .. G9** | 九条规则，形状**按可判定写**（例：G5 的判据是一条 `git diff -U0` 加一次 `grep -c` 数删除行） | 已定；G9 编号待架构团队复签 |
| **§11.4 枚举表** | 29 行 + N1–N9 声明侧复扫；每行带"门后读什么 / 需要什么守卫 / 定级 / 轮次 / 动作" | 已定 |
| **§11.9 lint 契约** | 四条**不可议**性质：候选集由谓词生成、每个候选必须有归宿、未归档候选让 `verify.bat` 变红、**P1 部分必须从声明侧生成** | **契约已定，脚本未实现** |
| **ADR-R2-11** | `docs/adr/adr-r2-11-detach-notification.md` + 时序图源 | 已落笔；**图未渲染** |
| **REM3-G7** | 每一个指向树内节点的成员裸指针，都必须有一条使其失效的通知路径；**没有路径的指针不许存在** | 已定 |

⚠️ **§11.9 的 lint 是本轮机制产出里唯一"只有契约没有实现"的一条。** 它正是为"第 9 扇门忘了加守卫"设计的检出点，而这一族**五次复发，五次都是有人复核过、复核漏了**。它没实现，就意味着**今天仍然没有任何机器**在守这条线。定级见 12.4 节 D 组。

---

### 12.3 证据

| 证据 | 内容 |
|---|---|
| **五组复现器先红后绿** | 每一组都在修复**之前**先跑出红态并留档；红态形态（退出码 / 哪条腿 / 报告种类）逐条记在各自小节 |
| **门计数器** | soak 实测 `framesDegraded = 403 × 5 = 2015`（403 = 400 圈 + 3 轮热身）。**它证明守卫幸存于调用，而不是压掉了调用**——降级路径被真正走到 2015 次，而几何仍然逐位稳定 |
| **门禁** | **207 用例、三条腿全绿**（E16 收口时）；E18 新增 1 条 ⇒ **208**；**E19（§11.14）新增 4 条 ⇒ 212**；**E21（C7）新增 1 条 ⇒ 213**。lint 自检 22 → **30** 例，ASan 分类器自检 21 → **22** 例 |
| **Release 双峰问题已消失** | E2 期间 Release 腿是**双峰的**（同一个二进制 10 次里崩 8~9 次），那是活跃 UAF 的指纹。收口后 **10 次 0 崩溃、stdout 逐字节相同** |
| **golden** | 9 张逐字节不变（SHA-256 逐个比对） |
| **零分配** | soak 四条采样序列的**平坦性**（唯一被断言的性质）未变；末值不高于热身后的值 |
| **渲染实测** | 编排者在 showcase 四页做了 **60 次拖动 + 144 次滚轮**，全部存活、渲染正确；滚轮走的正是 E15 新加的降级早退路径 |

⚠️ **"Release 腿单次绿色不能作证"这条要带进下一轮**：在门关上之前，任何人拿一份绿色 Release 日志主张"我没弄坏"或"我修好了"，都可能只是抽到了双峰的那一面。判据是 ASan 腿逐条对比（三条腿里唯一确定性的那条）+ Release 按崩溃率。

---

### 12.4 已识别未封（**下一轮唯一的输入**）

**每一条都带定级与轮次。** 定级：**S1** 无条件或主流用法下必然执行且门后有写；**S2** 条件性/低频，或门后只有读；**S3** 理论暴露面，无已知触发路径。轮次：**W2** 紧接下一轮；**W3** 需要一次架构裁定之后才能排；**R5** 已被判到第五轮。

#### 12.4.0 出门声明：**R2 出门时，库内存活的 S1 共 4 条**

> **为什么要有这么一句（R2 第 4 轮产品验收条件 P-C3）**：这个数字此前散在三处——A 组的表头写着"仍有两条 S1"，A′ 组的表里另有一条，家族对照表里还有一条——**读者要自己把它数出来。一个必须被数出来的数字，就是一个迟早会数错的数字。** 所以它在这里，是一句声明，不是一次统计练习；下一轮任何人改动这四条中的任何一条，**必须同时改这一句**，否则两处就开始漂（这条规矩本轮刚在 P2 清单上付过一次学费，见 §11.4 P2 那行的 ⚠️）。

**逐条点名，`库内` = `src/` + `include/`，不含 `examples/`，不含工具：**

| # | 位置 | 触发条件 | 备注 |
|---|---|---|---|
| 1 | **`WindowHeader::relayoutItems`**（§11.4 #19） | **不需要任何对象死亡**——应用在 `onGeometryChanged()` 里再加一个尾部项即可 | **四条里唯一一条不需要对象死亡的**，因此排第一。处方本轮更正：**不是游标**，是按下标 + 重读 `size()`。同形的 `setTrailingItemWidth`（#19b）靠一个 `return` 活着，同批读 |
| 2 | **`Window::closePopup`**（§11.4 L54-C） | `setVisible` → pass → `setGeometry` → 应用 `onGeometryChanged` 销毁这个 `Window` | 本轮由 S2 提为 S1（C6）。**它是第 3 条那扇门的里面**，两条同生共死 |
| 3 | **`Window::widgetDetached`**（§11.4 L75-X） | 同上，经 `closePopup()` | 触发路径本轮收窄：**`popupClosed.emit` 那一支到不了**（D7 禁止槽销毁信号宿主），只有 `setVisible` 那一支走得通 |
| 4 | **`Widget::animationTickTree`**（§11.4 N5） | 应用的 `onAnimationTick()` 覆写销毁子节点 | 门后 `for (children_)` |

**第 2 条与第 3 条是同一个修法的两半，第 1 条与它们无关，第 4 条独立。** 所以 R2.4 的门覆盖工作面是**三块**，不是四条。

⚠️ **本轮新登记的两条（L76-T / L77-T，§11.4 E21）都是 S3，不进这个数**；**C7 关掉的是一条 S2**（§12.4 C 组第一条），也不影响这个数。**这个数字在本轮没有减少，减少的是"表看不见的地方"**——lint 的候选侧扫描根、P2 清单的第二份手抄、P2 的错误 lookbehind、以及"没人守着门禁有没有插上电"。

#### A. 门覆盖（§11.4 与 N1–N9）

> **【E19 更新】本组减三条：`#16` / `N1` / `N4` 三条 S1 已封，见 §11.14。** 本组**从此不再有 S1 以外的更高定级项，但仍有两条 S1**（`#19`、`N5`）。**【E21】本组的两条 S1 不变；另外两条 S1 在下面的 A′ 组（`L75-X`、`L54-C`），全库合计 4 条，逐条点名见 §12.4.0。**
> 连带产生的新残留（**RES-N1a / RES-N1b / RES-N4a**）不在本组，登记在下面的 **F 组**——它们是状态与机制缺口，不是未封的门。

| 项 | 位置 | 级 | 轮 | 备注 |
|---|---|---|---|---|
| **#19** | `WindowHeader::relayoutItems`，range-for 里的 `setGeometry` | **S1** | **W2（排第一）** | 门后继续用 `slots_` 的迭代器 ⇒ **迭代器失效**，与 BoxLayout scratch 越界同形。⚠️ **E19 之后它是本组最高的一条**，而且 §11.14 的 #16 用例正是从一个 header 尾部项的 `onGeometryChanged` 里发起的——**那条用例每跑一次就从这扇门里过一次**。⚠️⚠️ **【E21 / C5】处方本轮更正，原来那句"需守卫：`this` + 迭代器失效"会误导下一个人**：一个 `DeathWatch` 关不掉它——**游标只回答"我记下的对象死了没有"，答不了"`slots_` 重分配了没有"**，而这里 `this` 从头到尾活着。正确处方是**按下标迭代 + 每轮重读 `size()`**（或先快照）。**触发路径不需要任何对象死亡**：应用在尾部项的 `onGeometryChanged()` 里回调 `addTrailingItem<T>`（README:170/176 的样例 API）⇒ `push_back` ⇒ 重分配。**这是全库唯一一条不需要对象死亡就能触发的 S1，所以它排 W2 的第一位**。逐条论证见 §11.4 #19 那一段 |
| **#19b** | `WindowHeader::setTrailingItemWidth`，同一形状 | **S3** | **W2（跟 #19 同批）** | **【E21 / C5 新登记】** 同样是 `for (Slot& s : slots_)` 里调 `relayoutItems()`，靠门后紧跟的 `return` 侥幸非危险——**与 #13b 同一条规矩，也同一个风险：这条命是那个 `return` 给的，而此前哪里都没写**。改成批量设宽度就当场变成第二个 #19。**修 #19 的人手就在这个文件里** |
| **#20** | `Cascader::rebuildColumns` 的 `setVisible`（循环内按下标读 `columns_`） | S2 | W2 | **主语已更正**（原写作 `relayoutColumns`，全库无此函数），见 §11.4 |
| **#21** | `Cascader::rebuildColumns` 三处 `setGeometry`（`:152/159/161`） | S2 | **W2** | 下标能防重分配，**防不了缩短**。主语同上 |
| **#22** | `SelectBase::showCustomPopup` 的 `Window::openPopup` | S2 | W2 | **P3 家族的已确认样本**：宿主是 `Window` 不是 `this`，**D7 不豁免** |
| **#23 / #2** | `PushButton::sizeHint` / `GroupBox::sizeHint` 的 `styleState()` 族 | **S3** | **W3** | **REM3-RES-2**：处理方向是**收紧契约**（`styleState()` / `onPaint()` 的覆写不得修改控件树），不是逐点加守卫——这一族在库里几十处。需要一次架构裁定（会不会有应用在 `onPaint` 里改树？）。⚠️ **裁定时按 21 个站点算收益，不是 2 个**：本表只点了 `PushButton::sizeHint` / `GroupBox::sizeHint` 两处，E20 的 lint 在同一个 HEAD 上扫出**这一族共 21 个站点**（L-A 组，逐点表在 §11.4 末尾 L01-A…L21-A），遍布**每一个控件**的 `onPaint` / `sizeHint`。**收益面差一个数量级，而这正是"逐点加守卫 vs 收紧契约"这道选择题的分母** |
| **#27** | **P3 家族，全库约 60 处 `.emit(`** | S2 | **W2（扫描任务）** | D7 豁免砍掉大半；剩下的是"发别人的信号 / 经别的对象绕一圈回来"那一类。产出物就是 §11.9 lint 的 allowlist |
| **N2 / N3** | `Widget::childAppended()` / `childRemoved()`，门后 `markLayoutDirty()` | S2 | W2 | **外帧看着干净不等于内帧干净**：#13b 证明的是 `takeChild` 那一帧安全，它停在了 `childRemoved()` 的门口 |
| **N5** | `Widget::animationTickTree()`，门后 `for (children_)` | **S1** | **W2** | |
| **N6** | `Widget::paintTree()`，门后 `for (children_)` | S3 | W3 | |
| **N7** | `SelectBase::refreshRows` / `open` | S2 | W2 | 门后 4 处经 `this` 的读 |
| **N8** | `MenuButton::onMouse` | S3 | W3 | |
| **N9** | `Widget::window()` | S3 | W3 | |
| **#25** | `examples/showcase/PageIcons.cpp:633` 的调用者义务 | S2 | W3 | **"契约主语是调用者"的活样本**；R3 不改 examples |

#### A′. 【E20】门覆盖的机器校验上线，以及它第一次扫出来的东西

> §11.9 那条 lint 已实现（`tools/lint-door-coverage.ps1`，`verify.bat` 步骤 [1/6] 内调用），四条不可议性质逐条兑现，双向证明实测。**本小节只登记它带来的新事实**；逐点候选表在 §11.4 末尾的「E20 lint 首扫产出的候选登记」。

**一句话结论：上面 A 组这张表，是同一份代码上人扫出来的那一半。** lint 在**同一个 HEAD** 上扫出 **100 个**候选（`src/**` 全部 47 个 TU），其中 **11 个**已带游标、**13 个**能在 A 组或 §11.4 主表里找到归宿、**其余 76 个此前哪张表里都没有**（`Widget::sizeHint` 补上主语后归 #10，余 75 条逐点登记在 §11.4 末尾）。**A 组不是"下一轮唯一的输入"的全部，它是其中被人看见的那一部分。**

| 项 | 位置 | 级 | 轮 | 备注 |
|---|---|---|---|---|
| **L75-X** | **`Window::widgetDetached`**，门是 `closePopup()`（→ `popupClosed.emit` → 应用槽），门后 `focus_` / `hovered_` / `pressGrab_` **三次经 `this` 的写**，一个游标都没有 | **S1** | **W2** | ⚠️ **本次扫描最该被带走的一条。** §11.14 里 N4 的整条论证（"`widgetDetached` 的记账没有洞"）**正是站在这个函数身上**——记账没有洞是对的，**记账的那一帧自己没有游标**是另一件事。函数自己的注释写着"Re-tested after that emit, which can move the focus or open another popup"：**想到了信号会动状态，没想到信号会把 `this` 拆掉** |
| **L54-C** | **`Window::closePopup`**，门是 `:126 p->setVisible(false)`，门后 `:127-129` 写 `hovered_` / `pressGrab_` 并 `update()` | **S1**（本轮由 S2 提级） | **W2（与 L75-X 同批）** | **【E21 / C6】** 与上一行**同形、同生共死**：`widgetDetached:141` 的第一句就是 `if (popup_ == w) closePopup();`，**L54-C 就是 L75-X 那扇门的里面，而且它才是发 emit 的那一帧**。两条定级不同会让下一轮**先做高的那条、然后发现低的那条才是根**——修外面不修里面等于在门外上锁。⚠️ 顺带收窄触发路径：**`popupClosed.emit` 那一支到不了"`this` 死在门里"**（宿主就是这个 `Window`，D7 明令禁止槽销毁信号自己的宿主），走得通的只有 `setVisible` → `markLayoutDirty` → pass → `setGeometry` → 应用 `onGeometryChanged` → 销毁 `Window` 这一支。**下一轮写用例的人别从 `popupClosed` 的槽入手** |
| **L-F 组（6）** | `Win32Platform::handle` / `paint`，`Window::handleMouse` / `handleKey` / `handlePaint` / `handleResize` | S2 | W2 | 每一次输入事件的**最外层帧**，门后继续读成员；与 **N5** 同形，只是站在树的更外面 |
| **L-C 组（15）** | popup / 菜单生命周期帧（`Window::openPopup` / `closePopup`、`SelectBase` 的 `ensurePopup` / `close` / `onKey` / `onMouse` …、`MenuButton::openMenu` / `closeMenu`、`Cascader::open` / `rebuildColumns`、`DatePicker::open`） | S2 | W2 | 与 **#22** 同形：宿主不是 `this`，**D7 不豁免**。#22 是这一族里唯一被人点过名的一个 |
| **L-A 组（21）** | `styleState()` / `displayText()` 之后接着读自己成员的只读渲染与度量帧 | S3 | W3 | **就是 #2 / #23 的族，但规模是新信息**：表里 2 个站点，实际 **21** 个，遍布每一个控件的 `onPaint` / `sizeHint`。**"收紧 `styleState()` 覆写契约"这个处方的收益面因此大一个数量级**——请架构团队在裁 #23 时按 21 个站点而不是 2 个站点算 |
| **L-B 组（19）** | P3 `.emit(` | S2 | W2 | **#27 那个"扫描任务"的产出物，现在有了**：不是"全库约 60 处"这个估数，而是 19 个**门后确有后续代码**的具体帧 |
| **L-D / E / G / H（14）** | 布局度量帧 5、构造函数帧 3、谓词名字碰撞误报 2、其余单点 4 | S2/S3 | W2/W3 | 见 §11.4 末尾 |

**同时扫出的一条表内不一致（已裁定并落笔，本条从"只报不改"关闭）**：#20 / #21 两行的主语原写作 `Cascader.cpp` 的 **`relayoutColumns`**，而今天的 `Cascader.cpp` 里**没有这个函数**。核实结论是**主语写错了**：那几个行号（`:143` / `:152` / `:159` / `:161`）全部落在 `rebuildColumns()`（`Cascader.cpp:90-164`）体内，而 `relayoutColumns` 在本仓库历史里**从未存在过**（`git log -S` 只命中文档提交）——它是照着紧邻上一行的 `WindowHeader::relayoutItems` 编出来的名字。⇒ **两行的主语已改为 `rebuildColumns`（#21 一并补上函数名），`Cascader.cpp` 未动一字**；逐条依据见 §11.4 候选表上面那一段。**这是"表不是扫出来的"第三个实例，只是这次是机器发现的**——而它能被发现，是因为 lint 的键是 (文件, **函数**)：编造的函数名归档不到东西，偏了一百行的行号却看不出来。

**lint 自身的三条残留，登记不掩盖：**

1. **它分不清"持有游标"与"检查游标"。**（**编排者已裁定采纳这次放宽**，§11.9 第 1 条的契约正文已按此改写；**代价不因裁定而消失，原样登记在这里**。）§11.9 的原文是"本体内没有 `DeathWatch`"；实现把它读作"本体内没有 REM3 游标"（四个 `LiveGuard` 实例：`DeathWatch` / `BubbleGuard` / `GeometryGuard` / `LayoutGuard`）。**这是一次放宽，理由是实测**：字面读法把 `Widget::dispatchMouse` 与 `dispatchKey` 判成缺陷，而这两个帧都在门后**重测了自己的游标**（`if (bubble.node() != w) return;`）——比 REM3 早，形状一样。**代价是**：一个因为别的原因构造了 `LayoutGuard`、然后不问 `alive()` 就跨门的帧，在这里读作"已守卫"。**S2 / W2。** 闭合它要把"检查"绑到"门"上，那需要知道门对哪个指针危险（§11.4 的 hazard 条款）⇒ 需要一个编译器。
2. **带游标的函数整体豁免。** 谓词按 §11.9 的原文是函数级的，所以 `AppWindow::relayout` 这种**已经封好**的帧从此不再是候选——**往它里面加第四扇门而不加检查，lint 看不见**。**S2 / W2。**
3. **P1 是按名字匹配的**，所以 `VectorPath::close()` 撞上 `SelectBase::close()`（L71-G / L72-G 两条误报），反过来一个虚函数若被 `std::function` / 指针间接调用也扫不到（§11.9 末尾登记的第四类原语 `PlatformWindow` 的 7 个公有 `std::function` 成员仍未覆盖）。**S2 / W2，与 P4 谓词那条同批。**

#### A″. 【E21】门禁自身的四个缺口（R2 第 4 轮安全评审 C1–C4，**本轮全部关闭**）

> **上面 A′ 登记的是 lint 扫出来的东西；本小节登记的是 lint 自己身上的东西。** 四条都不是"lint 报错了"，而是**lint 结构上看不见 / 没人守**——**判据全部是机器判据，四条都有能变红的自检夹具**（自检 22 → **30** 例，ASan 分类器自检 21 → **22** 例）。

| 项 | 缺口 | 定级（**门禁的**） | 状态 |
|---|---|---|---|
| **C1** | **候选侧的扫描根只有 `src/`**，`include/geeyoou/**` 从来没有被 `Split-CppFunctions` 切过函数体 | **S1** | ✅ 关闭。扩根 + 修两个机械陷阱，逐条见 §11.4 E21。**代码本身两条 S3**（L76-T / L77-T） |
| **C2** | **P2 原语清单是第二份手抄，而且已经漂了**（脚本有 `relayout`，文档没有） | S2 | ✅ 关闭。脚本改成解析 §11.4 的 P2 行，手抄那份已删除；文档那行的书写格式变成承重的 |
| **C3** | **`(?<!::)` 套在 P2 上是错的**——那条 lookbehind 讲的是虚分派，P2 不是虚分派 | S2 | ✅ 关闭。P2 去掉 lookbehind，**P1 保留**。实测：全库候选数不变（今天限定名调用全部是 P1 家族），**是潜伏漏报不是活缺陷** |
| **C4** | **没有机器检查门禁还在调 lint 和分类器** | S2 | ✅ 关闭。剥掉 `rem` 之后 grep `verify.bat`，缺任一 `call` 或任一标签即红 |

**C1 的判词，值得单独留一句**：`layoutRect()` 那条更正（"虚调用 grep 不出名字"）与 §12.5 教训 2 的第二层（"扫描根写成 `Widget.hpp` 而不是 `include/geeyoou/**`"）**两条都修在了声明侧**，而候选侧的同一个洞**没人修**。⇒ **同一个洞的第三次出现，而且前两次的修都没有碰到它。** 教训写进 §12.5 第 15 条。

**C4 的自指问题，以及它是怎么关掉的**：这条检查由 `verify.bat` 的 `:lint_doors` 调起，所以**它抓不到自己那条 `call` 被删**——它没在跑。⇒ 谓词**一份实现、两个调用者**：lint 抓 `call :classify_asan` 丢失，**ASan 腿的分类器自检（`tools/test-classify-asan.ps1`）通过 `-GateWiringOnly` 抓 `call :lint_doors` 丢失**。**两个守门的机器现在互相看着对方的电源线。** 两个方向都实测过（各注释掉一行，两次都拿到非 0 退出码）。

**顺带修掉的一条，它自己就是一个"过滤器不过滤"**：`Get-ChildItem -LiteralPath <dir> -Recurse -File -Include *.cpp` **在本机（PowerShell 5.1.19041）根本不过滤**——`-LiteralPath include` 返回 59 个文件（全是 `.hpp`），换成 `-Path` 返回 0。所以旧扫描里那句"只扫 `.cpp`"从来不是过滤器，只是 `src/` 里恰好几乎没有别的东西；恰好有的那一个（`src/render/VectorPathImpl.hpp`）一直在被当作 TU 扫，纯属侥幸对了。现改成直接判扩展名。**同一个 API 用法在 `Test-NoPlatformInstallPoint` 里也有一处，一并改掉**（那处方向是安全的——多扫不会漏掉安装点——但"不过滤的过滤器"不该留着）。

**本轮新增的一条 lint 残留（登记，不掩盖）**：**扩根之后，`include/` 下的头文件同时是 P1 名字表的输入和候选集的输入。** 今天没有冲突（`Get-VirtualNames` 只读声明行，`Split-CppFunctions` 只读函数体），但两条路径共用一份 `Get-CleanText` 缓存，**任何一边将来改动剥离规则，另一边会跟着变而没有任何用例会红**。**S3 / W3。**

#### B. 状态修复（RES-1 家族）

| 项 | 级 | 轮 | 备注 |
|---|---|---|---|
| **RES-1b popup 家族** | — | **R5** | 已判 |
| **`Shell` 的 RES-1** | S2 | **W2** | `Shell::pageArea_` 与每页的 `Page::host` 没有 override，摘掉页面宿主之后 `Shell::relayout()` / `showPage()` 仍然会走悬垂指针。**编排者裁定：不补，登记 W2。** 两条理由：`showPage` 遇到 host 为空该怎么办是**产品裁定**不是机械改造；且 `Shell` 在 `examples/` 不是库 |

#### C. ADR-R2-11 自己的爆炸半径（**本轮新登记**）

| 项 | 级 | 轮 | 备注 |
|---|---|---|---|
| **`DeathWatch` 提前到广播之前** | **S2** | — | ✅ **【E21 / C7】已修**。`announceDetached` 的 `detail::DeathWatch host(&parent)` 原本注册在广播**之后**（`Widget.cpp:340` 广播 / `:361` 注册）⇒ 钩子槽销毁 `parent` 时 `host.alive()` **恒为 true**，`:366` 的 `stillAChild` 直接读已释放内存。**守卫在 diff 里读起来是有的，事实上是没有的**——比没有守卫更坏，因为它是让人不再看的那种状态。修法两行：守卫提到广播之前 + 广播后立刻 `if (!host.alive()) return;`。**红态先行**：改之前实测拿到 **4 条** `stillAChild` 的 heap-use-after-free（原文见 §11.11 E21 记录） |
| **广播循环本身无守卫** | **S2** | **W2** | `notifyDetachToAncestors` 的 `for (a = node->parent(); a; a = a->parent())` 一个守卫都没有；钩子销毁自己所在的 `a`（或上面任何一层），下一次 `a->parent()` 就是 UAF。这是 §11.6 约束 (ii) 被 REM3-G9 替代之后留下的那一半。详见 §11.11 未验证栏第 7 条。⚠️⚠️ **【E21】本条与上一条共用同一个触发器，这是本轮实测出来的新事实，它改变了本条的可延期性**：广播走的是**离场节点的祖先链**，而 `parent` 按构造就是链上的**第一个**（`announceDetached` 只以 `parent == node->parent()` 被调用），所以**唯一能销毁 `parent` 的钩子帧就是 `parent` 自己**——之后循环的自增 `a = a->parent()` 就读它；换成从**更高**的祖先去销毁 `parent`，那需要 `takeChild`，而 REM3-G9 的 assert 在walk 内部禁止它（且理由正当）。**没有第三种形状。** ⇒ **一条能把上一条打红的用例，必然同时把本条打红**，因此上一条的验收用例**无法**做成"只测它自己"的形态——本轮改用确定性的**顺序探针**（钩子里读游标链深度：修之前 1，修之后 2）落进门禁，见 `tests/widget/test_removal.cpp` 的 `the_announcement_arms_its_cursor_before_the_broadcast`。**本条与上一条只能一起验收**，下一轮排期请按"一块"而不是"两条" |

#### D. 谓词与工具

| 项 | 级 | 轮 | 备注 |
|---|---|---|---|
| **`PlatformWindow` 的 7 个公有 `std::function` 成员** | **S2** | **W2** | **第四类原语**：`onPaint` / `onMouse` / `onKey` / `onResize` / `onClose` / `onHitTest` / `onWindowStateChanged` 应用可直接赋值，调用它们能到达应用代码，但 **P1/P2/P3 一条都不覆盖**。**谓词漏的是整整一类，不是一个站点**。处理形态：加一条 P4（调用一个公有可赋值的可调用成员），或把这 7 个逐一登记进 P2。**判定归架构团队** |
| **§11.9 的 lint 脚本** | **S1（按后果定级）** | — | **【E20】已实现**（`tools/lint-door-coverage.ps1`，`verify.bat` 步骤 [1/6] 内调用，四条性质逐条兑现）。它是"第 9 扇门忘了加守卫"的唯一机器检出点，而本族五次复发全部是人工复核漏掉的。**它自身的三条残留见 A′**，仍是 W2 |
| **`.bat` 的行尾没有钉死（CRLF）** | S2 | **需架构裁定** | **本轮不做，登记。** §11.9 末尾那条真值表要**两个**条件同时成立（行尾 + 编码），今天撑着的只有 **ASCII 那一半**，它已经做成机器判据（`Test-BatchFileHygiene`）；**另一半（CRLF）本轮落不了地**。实测：本仓库 `core.autocrlf=true` 且**无 `.gitattributes`**，把 `.bat` 转成 CRLF 之后 `git status` 显示已修改而 `git diff --stat` **显示 0 行**——**这不是"diff 太大"，是根本不构成一个 diff**：评审不了、提交不了、下一次 checkout 就没了。唯一能落地的形态是一行 `.gitattributes`（`*.bat text eol=crlf`），但它会在**所有人下一次 checkout 时静默重写每一个 `.bat`**，那是**仓库级行为变更**，不是一次文件修改 ⇒ **归架构团队裁定**。⚠️ 连带事实（§11.9 已写，此处复述以免裁定时漏掉）：**ASCII 那条检查保护不了 `verify.bat` 自己**——它由 `verify.bat` 调起，而一个已经漂移的 `verify.bat` 在跑到这一步之前就已经错行执行了；**它保护的是下一次运行** |
| **`Platform.hpp` 的 21 条虚函数免检** | — | — | 免检**理由是结构性质**（库里没有任何实现安装点，`setPlatform` / `installPlatform` 两个名字全库零命中），不是"今天没人这么用"。⚠️ **触发条件已写进表**：哪天出现任何形式的实现安装点（setter / 构造注入 / 工厂注册 / 测试替身），这 21 条一起进表 |

#### E. 已登记的整洁性与行为项

| 项 | 级 | 轮 | 备注 |
|---|---|---|---|
| **REM3-RES-5**：`GroupBox::sizeHint()` 跨门撕裂读 `title_` | S2 | W2 | **健康路径**即可返回内部自相矛盾的 hint；既有缺陷，非本轮引入。修法二选一（门后重算，或把 hint 定义成"门前快照 + 门后增量"并写进契约），都是行为改变，都要一条自己的用例 |
| **REM3-RES-6**：`g_bubbles` 与 `g_geometries` 可合并 | S3 | W3 | 取消策略相同、都没有表外读者 ⇒ 按 Q1 的判据它们是同一条链表的两个名字。**本轮不合并是一个决定，不是一个疏忽**（动的是全库重入压力最大的三条路径，且没有任何缺陷推动它） |
| **REM3-RES-3**：examples 侧的调用者义务（#25） | S2 | W3 | R3 不改 examples |
| `ScrollArea::setContentWidget`（摘掉之后想接回去） | — | W3 | API 缺口，登记备查 |
| `ScrollArea` 降级后残留的 `hoverV_` / `hoverH_` | S3 | W3 | 看不见（两个 bar 矩形都是空的），但它是状态而不是不变量 |
| ADR-R2-11 时序图**未渲染** | — | W2 | 本机 `plantuml.jar` 未安装。`.puml` 源已入库；**未渲染前不许宣称已出图** |

#### F. 【E19 新登记】封 N1 / N4 / #16 自己的爆炸半径

**三条都不是"没做完的门"，而是这三扇门关上之后剩下的、需要新机制或新裁定的东西。** 详见 §11.14。

| 项 | 级 | 轮 | 备注 |
|---|---|---|---|
| **RES-N1a**：`onInvalidated()` 里换掉宿主的 layout | **S2** | **W2** | 宿主活着、`this` 被 `setLayout<Other>()` 释放，`host_` 的游标仍读真。`host()` 与 `setLayout` 都是 public。**廉价替代（比较 `host_->layout()` 与 `this`）已被明确否决**：它比的是一个已释放的指针值，两次 `setLayout` 就能让新对象落在旧地址上 ⇒ 静默答对 |
| **RES-N1b**：进 `invalidate()` 时已 park 的 layout | S3 | **W2** | 没有宿主就没有游标，钩子里任何退栈到深度零的布局趟都会排空停车场并释放 `this`，`if (host_)` 就是那次读 |
| **RES-N4a**：`focus_` 在降级后仍悬垂 | **S2** | **W2** | 守卫是帧作用域的，不修复对象状态——REM3-RES-1 在 `Window` 上的实例。**修法不是在守卫里置空**（那是 REM3-G1 禁止的、经 `this` 的写），而是给"不经 `takeChild` 的死亡"补一条通知路径，那是 ADR-R2-11 / REM3-G7 的题目 |
| **`DeathWatch(const Widget*, MayBeNull)`** | — | **已关闭（复签通过）** | §11.2 的 API 形状扩展，**架构团队已复签通过**。Q7 的 `assert` 前提（"上守卫就是马上要解引用"）对**有条件解引用的可选成员**不成立，三条门都是证据。语义、尺寸、取消策略均未变。**§11.2 的形状表与 Q7 判词已同步改到位，双参构造已补 `explicit`** |
| **RES-N1c**：以 `Layout*` 为键的游标链表 | — | **W2（裁定）** | RES-N1a / RES-N1b **两条都只能靠它关**。要一条第五链表 + 一个会取消它的 `~Layout`（今天是头文件里的 `= default`）。按 §11.1 判据 1/2 先判它的取消策略与决策读者，再决定是新开还是复用 |

---

### 12.5 本轮的方法学产出

**这些比缺陷本身更值钱**，所以单独成节。每一条都有本轮的现场出处，不是格言。

1. **契约要挂在"调用者"这个名词上，不是实现者。**
   §11.0 定的根因：`Layout.hpp` 的存活性契约写的是"实现 `Layout` 的类"的义务，而真正会踩雷的是**任何调用 `sizeHint()` / `setGeometry()` 之后还要读自身的代码**——容器自己的 `sizeHint()` 转发、容器的 `relayout()`、几何钩子的调用者，一个都不在原文的主语里。**主语错了，整族缺陷就落在契约的字面之外。**

2. **只按名字 grep 的复核找不到虚调用；候选集必须从声明侧生成。**
   `c->foo()` 是不是虚调用，取决于**头文件**里有没有 `virtual`，不取决于调用点长什么样。`layoutRect()`（#28）一直在谓词的覆盖范围之内，扫描却**结构性地**看不见它。⇒ §11.9 的第四条不可议性质。
   **同一条教训还有第二层**：扫描根写成 `Widget.hpp` 而不是 `include/geeyoou/**`，`Layout.hpp` 的三个钩子就永远不会进候选集。**"grep 不出虚调用"和"扫描根写窄了"是两个洞，要两条修。**

3. **谓词的主语写窄了会漏掉一整类。**
   P1 的主语原本是"widget"，而 **`Layout` 不是 widget** ⇒ N1（九条里唯一一条会真正 `delete` 的）在主语改宽之前根本不在候选集里。同一形状的第三次出现是 `PlatformWindow` 的 7 个 `std::function` 成员：**漏的是整整一类原语，不是一个站点。**

4. **一次被丢弃的读不是一次读；用例 FAIL 与 ASan 报告是两个独立信号。**
   `(void)cached_->geometry();` 在 `/O2` 下被整个删掉 ⇒ 红态里**用例照样 FAIL、ASan 一条报告都没有**。本族六次复发**全都只有后者看得见**。⇒ 已落到 `build-asan.bat` / `verify.bat` 的 ASan 腿抬头注释、§11.8 的证据标准、以及两个测试文件的现场注释里。

5. **红态先行：今天的代码就是缺陷本身，不需要人为注入。**
   E15 的用例**先写、先入门禁**，在 override 还不存在时跑：Release 段错误退出码 139、ASan **143 条报告**。人为注入缺陷证明的是"我的注入生效了"；**先写用例证明的是"这个缺陷现在就在这里"**。

6. **判别性实验优于合取反证。**
   "两处都拆掉 ⇒ 崩了"只能证明**需要某种东西**；"只拆一处 ⇒ 仍然崩"才能证明**必须是这一处**。本轮三次用到：M-1（只拆调用侧）、B2（广播**收窄**成只通知直接父节点，**同一条 UAF 原样出现**）、B7（只删成员重读那两项）。

7. **门禁判据在有活跃 UAF 时 Release 腿是双峰的，单次绿色不能作证。**
   同一个二进制反复跑给出两种结果（实测 10 次里崩 8~9 次）。⇒ 判据改成：**ASan 腿逐条对比为主**，Debug 腿对比崩溃位置，Release 腿**按崩溃率**。E2 前的第一份基线日志正好抽到了绿的那一面，差点被读成"E2 把 Release 弄红了"。

8. **规则要按可判定的形状写，不按动机写。**
   G5 的判据是"`git diff -U0` 里删除行的条数"，不是"尽量少删既有代码"。按动机写的规则，复核时只能靠讨论；按形状写的规则，复核时是一条命令。

9. **取舍要按危害写，不按成本写。**
   G9 的落点：第一版把"只给 `takeChild` 上 assert"的理由写成"只有会破坏遍历的那一项值得付运行时代价"——**按成本写的取舍读起来像已经权衡过，实际上把一条同样危险的路径（钩子里 `emit()`）藏在了性价比后面**。真正的理由是**够不着**：`g_inDetachNotify` 是 `Widget.cpp` 匿名 namespace 的内部链接符号，而 `Signal::emit` 在 `core/`，core 不许依赖 widget。**这一改，残留就从「选择不执行」变成「够不着」——两种完全不同的东西。**

10. **宁可显式失败，不要静默给出看起来合理的错答案。**
    本轮五次拒绝同一笔交易：**不自愈**（摘掉 content 的 `ScrollArea` 永久答 `nullptr`，不复活一个"另一个 widget"）、**不现推指针**（`children_[0]->children_[0]` 会静默指向别的对象）、**不预读 margins**（会拿门前的值排完一整趟）、**不按符号名消噪**（ASan 分类器问"谁负责"而不是"谁出现"）、**不把放弃变成沉默**（`framesDegraded` 让每一次降级都留痕，除非放弃本身已经写进返回值——全库唯一实例是 `runLayoutIfAny`）。

11. **免检要给结构性理由，不给用法观察。**
    `Platform.hpp` 那 21 条虚函数免检，理由是"**没有安装接口**"（一条两行的 grep），不是"今天没人这么用"。两句话的分量差着一个数量级——**本族六次复发里，"今天没人这么用"输了六次。**

12. **【E19】门是必要条件，不是充分条件——"能到达应用代码"不等于"能崩"。**
    §11.4 的 N 表给 N4 写的触发路径（`SelectBase::onFocusChanged(false)` → `openStateChanged.emit(false)` → 槽销毁即将获焦的控件）**逐步走一遍就不成立**：那个"销毁"若走树内移除，`Window::widgetDetached` 会先把 `focus_` 清空。真正的洞是**不经 `takeChild` 的销毁**与**窗口自己死在门里**——两个都不在原判词里。
    谓词扫描给的是**候选**；把候选当成结论，方向与本族前六次相反（前六次是漏判），但**同样是"看起来复核过、其实没有"**。⇒ 定级可以照候选给，**触发路径必须实际走通并留下一条会红的用例**。

13. **【E19】判别性实验做不出来，通常说明用例的构造不够分离。**
    第一版的 #16 用例（有 fill + `removeChild`）里，CP-A1 的五项检查**同时**有四项会触发，于是单独拆掉任何一项都不红——实测过，套件全绿。换成 `takeChild`（**摘下但不销毁**，游标按 Q4 的策略仍读真）加上**不调 `setContent`**（`fill_` 前后皆空，两项恒静默）之后，能触发的检查项恰好剩一项，实验立刻有了判别力。
    **"拆了没红"有两种读法——那一项多余，或者用例分不开它们——先排除第二种。**

14. **【E19】检查链的顺序是承重的，而且这条现在有实测。**
    只拆掉 N4 的 `!self.alive()`、其余两项原样保留，ASan 报告出在**紧跟着的那一项自己身上**（`focus_ != w` 要经 `this` 解引用）。**用例照样 PASS、退出码 0、ASan 红**——第 4 条那个"两个独立信号"又演了一遍。REM3-G3 的"第一项必须是 `this` 的游标"从此不是风格规则。

15. **【E21】一个洞修在了声明侧，不等于它在候选侧也修了——"哪里有 `.cpp`"和"哪里有代码"是两个问题。**
    §11.4 的 `layoutRect()` 更正（虚调用 grep 不出名字）与本节第 2 条的第二层（扫描根写成 `Widget.hpp` 而不是 `include/geeyoou/**`）**两条都落在声明侧**；lint 的**候选侧**扫描根一直是 `src/`，而库的代码不全在 `src/`——头文件里的模板与 inline 定义从来没被切过函数体。**同一个洞的第三次出现，前两次的修都没有碰到它。** 下一个改扫描根的人：问题不是"这个目录里有没有 `.cpp`"，是"代码在哪"。
    **连带的第二层**：扩根之后仍然一个都扫不出来，因为 `template` 在切分器的"不是函数头"表里，模板体连同它的门一起从未被读过。**"根写窄了"和"切分器看不见这种函数"又是两个洞，还是要两条修。** 这条教训到这里已经是同一句话的第三个变奏，所以它值一条自己的编号。

16. **【E21】一份人维护、机器消费的清单，只能有一份。**
    §11.9 性质 2 为 allowlist 定过这条规矩（"allowlist 就是 §11.4 的表，两者不得各写一份"），**而 P2 原语清单是同一种东西，却被漏在规矩外面**：脚本里一份手抄，文档里一份散文，旁边写着"你在这里加一个，记得也去那边加"——**而它们已经漂了**。这次漂的方向是红的（脚本多一个 `relayout`），所以什么都没漏；**反方向的同一次漂移会静默删掉一整类候选，而且是让门禁变绿的那个方向。** ⇒ 判据不是"提醒大家同步"，是"只留一份"。
    **推论，写给下一个加检查的人**：一条会让门禁**更红**的漂移可以靠红灯发现；一条会让门禁**更绿**的漂移只能靠"没有第二份"或"日志里有个数字"。**两种漂移不是同一件事，不能用同一种手段对付。**

17. **【E21】守门的机器，自己有没有被守着？**
    删掉 `verify.bat` 里的 `call :lint_doors` 或 `call :classify_asan`，门禁照样跑完六步、照样打印 `[ok] gate is GREEN`——因为这两个检查器都是**在一个没人调用的子例程里**去设那个变红的变量。**一个没被调用的检查器是 fail-open 的，而且那一趟看起来和干净的一趟一模一样。**
    **自指是这条的难点，也是它的答案**：由 `:lint_doors` 调起的东西抓不到自己那条 `call` 被删。⇒ **两个检查器互相看着对方的电源线**，一份实现两个调用者。递归到此为止：再往上一层就是"有人蓄意同时掏空两个文件"，那已经不是机器该回答的问题了。
