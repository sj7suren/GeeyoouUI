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

## 0.5 架构裁定 ADR-R2-01 .. 10

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
2. **M4 深度上限未被用例覆盖**：要触发需要 64 层嵌套 Layout 宿主，而 Debug 下 `add<T>` 的 `kMaxTreeDepth` 断言会先拦下建树本身。计数器与记录路径已实现（`detail::layoutDiagnostics().depthExceeded`），但**未跑通**。
3. **`naturalSize_` 的深层子孙锁存**：`setGeometry` 里的锁存无条件生效，但如果一个 widget 在**进程里还没有任何 Layout** 时拿到几何、之后祖先才装上 Layout，那么只有 `adoptLayout` 的**直接**子节点会被补锁，更深的子孙要等它们下一次 `setGeometry`。迁移的三页里不可观测（页面是建在已有 layout 的宿主下的）。
4. ~~**`Widget::relayout()` 与三个容器的 `relayout()` 同名**~~ —— **已解决**：基类那个方法改名为 `Widget::performLayout()`。理由写在 `Widget.hpp` 上：`AppWindow` / `ScrollArea` / `Shell` 各自已有 `relayout()`，其中 `ScrollArea::relayout()` 还是 `private`，因此同名的基类成员会被三个容器静态隐藏、被第四个挡在访问权限外；给唯一一个没有调用点的方法改名，比改一个已发布的 API 便宜。
5. **多线程**：与全库一致，布局引擎只在 UI 线程使用。`g_layouts` / `g_layoutDepth` / `g_layoutHosts` / `g_arrangeHost` 都是普通 `static`，非 `thread_local`——与 `g_bubbles` 同一条理由（`docs/architecture.md` §3.11）。
6. **`ScrollArea` / `ListView` 的 hint 是常数**（320×200 / 六行）。这是刻意的（见 §7），但那两个常数本身没有出处，只是"够用"。将来如果需要"最多长到内容那么大"，得先想清楚循环怎么断。
