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

---

## 11. 【REM3 · E1】跨越应用代码的帧守卫 —— 设计定案（第 2 版）

> 状态：**第 1 版经 `eng-frontend-ui`（Leo）书面评审，有条件放行。本版按 B1~B7 + 登记项修订，待复签。**
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
> **判据 2（表外读者）**：链表若有**守卫自身 `alive()` 之外的读者**（任何形如"现在有没有一个 X 帧站在谁身上"的谓词），则混入异类帧会让那个谓词**静默答错** ⇒ 必须独占。

**反过来说**：两条链表若**取消策略相同**且**都没有表外读者**，它们就是同一份东西的两个名字——多一条链表 = 多一个 `~Widget` 里要记得写的 `cancelOn` = 多一个下次会忘的地方（`Widget.cpp:74-75` 自己写的那句"a second hand-rolled copy of this pattern is a second place to forget a check"，说的是机制，但同样的道理逐字适用于策略）。

**完整选项矩阵（五个候选，逐个判）**

| # | 候选 | 取消策略 | 表外读者 | 判决 |
|---|---|---|---|---|
| A | 新开第五条链表 `g_hints` | 只在 `~Widget` | 无 | **否决**（与 F 逐条相同，纯冗余；这是第 1 版的答案） |
| B | 复用 `g_geometries` | `announceDetached:282` **取消** + `~Widget:342` | 无 | 否决（判据 1） |
| C | 给 `LiveCursor` 加 kind 字段，`cancelOn` 按 kind 过滤 | — | — | 否决（见下） |
| D | 复用 `g_bubbles` | `announceDetached:281` **取消** + `~Widget:339` | 无 | 否决（判据 1；另：`moveTo()` 语义是沿父链行走，与测量帧无关） |
| E | 复用 `g_layouts` | **只在 `~Widget:343`**，`announceDetached:283-296` 明令不取消 | **有三个** | 否决（判据 2，见下） |
| **F** | **复用 E17 的 `g_detaches`，按策略改名 `g_deathWatch`** | **只在 `~Widget:347`** | **无** | **采用** |

**为什么不是 B / D（判据 1，决定性）**：`announceDetached:281-282` 对这两条链表都执行 `cancelOn`。而按 Q4 的结论，hint 帧在**仅 detach** 时**必须不取消**。共用 = 一条链表被迫执行两套策略。

**为什么不是 E（这是第 1 版真正漏掉的那个，也是 Leo B2 点名的那个）**：Leo 说得对——`g_layouts` 的**取消策略已经就是 hint 帧要的那一套**（`announceDetached:283-296` 明文写着不取消，`~Widget:343` 取消），所以第 1 版"两种取消策略 ⇒ 两条链表"这条理由**够不着 E**。杀掉 E 的是判据 2，而 `g_layouts` 的表外读者有三个，且三个都会被一个 hint 游标弄错：

* `markLayoutDirty:623` 的 `if (g_layouts) return;`——测量期间挂一个游标，等于告诉全引擎"有 pass 在跑"，**测量自己引发的重排会被这句吞掉**。这就是 `announceDetached:288-296` 已经记过一次的"子树几何永久冻结"，只是从另一个方向到达。
* `detail::layoutPassActive():722`——`Layout.cpp:77` 的停车场排空点读它（`g_measureDepth == 0 && !layoutPassActive()`），一个测量期的假 pass 会把停车的 `Layout` 对象**多押一轮**才释放。
* `detail::currentLayoutHost():724-725`——它会返回**被测量的那个 widget**，而 `tests/widget/test_layout_engine.cpp:553` 正在断言这个值。

再加一条形状理由：`LayoutGuard`（`Widget.cpp:166-193`）不只是一个游标，它还挂着 `g_layoutDepth` 与 `DrainOnUnwind`；复用它等于在每次测量的边界上排空停车场。**这三条与 E17 在 `e9c283a` 里否决 `g_layouts` 的理由逐条相同**（`Widget.cpp:113-134`）——同一个候选，第二次被同一组理由杀掉，这一致性本身是判据 2 成立的证据。

**为什么不是 C**：给一个在三条热路径上被构造的 `struct` 加字段，并在 `cancelOn` 的每个节点上加一次分支，换 8 字节 BSS。方向反了。

**为什么是 F（正面论证，两个事实都可 grep 复核）**：

1. **取消策略逐字相同**。`g_detaches` 的注释（`Widget.cpp:132-133`）自己写着 "cancelled by `~Widget` and by nothing else, which is exactly *dead, not merely detached*"。而 Q4 对 hint 帧的结论一字不差就是这句。
2. **表外读者：零**。全库对 `g_detaches` 的操作只有 `~Widget:347` 的 `cancelOn` 和三处 `DetachGuard` 的 `alive()`（`:301`、`:400`、`:431`）。grep 命令写在这里，E5 可复核：
   `grep -n "g_detaches\|DetachGuard" -r src include tests`

**所以：`g_detaches` 与 `g_hints` 是同一条链表的两个名字。** 保留两条 = 两个 8 字节全局 + `~Widget` 里两行 `cancelOn` + 两个要记住的名字，换来的是零。合并。

**改名（B1 的另一半：`HintGuard` 是第二次给错主语）**

第 1 版按"门"命名（`g_hints`），理由是 R2 翻车的根因是给错主语。这条理由是对的，但**第 1 版自己没执行到底**：一旦这条链表同时承载 detach 帧（`takeChild` 从来没调过 `sizeHint()`）与 hint 帧，"门"就不是它们的公因子了。**它们的公因子是取消策略**，所以名字必须按策略取：

| 旧名（不再使用） | 新名 | 含义 |
|---|---|---|
| `g_detaches` / `g_hints` | **`detail::g_deathWatch`** | 站在别人身上的在飞帧，**只被真实销毁取消，detach 不取消** |
| `DetachGuard` / `HintGuard` | **`detail::DeathWatch`** | 上面那条链表的唯一守卫类型 |

`g_bubbles` / `g_geometries` 仍按帧种命名，因为它们的策略是另一套（detach 也取消），名字与策略不冲突。四条链表因此读作：**两条"还在原地吗"（bubbles / geometries），一条"pass 还在跑吗"（layouts，有表外读者所以独占），一条"人还在吗"（deathWatch）**。

**一个守卫类型，不是两个。** 合并后 `DetachGuard` 与 `HintGuard` 的差别只剩访问形状，而事实是**三处 detach 现场（守卫在 `:301`、`:400`、`:431`）的四次检查（`:305`、`:318`、`:402`、`:437`）也只用 `alive()`**，没有一处用 `node()` / `moveTo()`。所以直接让 `DeathWatch` 采用 Q3 的私有继承形状：**只导出 `alive()`**。Q3 那一手（把"游标只比较、永不解引用"变成编译器强制）因此从 2 个新现场扩大到 **5 个现场**，包括 E17 刚落地的三处。这是本次合并最实在的收益。

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
2. **Debug 期 `assert(w != nullptr)`**：因为**这是调用方的 bug，不是正常状态**。理由可判定：任何一个上守卫的现场，都是因为它**马上要解引用那个指针**；指针为空时该现场**在到达守卫之前就已经解引用过它了**（`ScrollArea::relayout:157` 的 `content_->layout()` 就在门与守卫之前）。所以「空成员」必须由现场的**空检查**处理，而不是由守卫**冒充**成一次死亡。
3. **Release 期不静默**：`alive()` 为 false 会让现场走降级分支，而降级分支**必然记一次 `detail::frameDegraded()`**（§11.3 表里逐点写死）。所以 Release 下 `nullptr` 表现为「诊断计数 +1」，不是 ADR-R2-04 意义上的无痕迹。

> **REM3-G7：空成员是空检查的事，不是守卫的事。**
> 守卫回答的问题是「我记下的这个对象死了没有」；它**不**回答「这个成员现在还指着东西没有」。E14/E15 的 `onDescendantDetached` 把 `content_` 置空之后，`ScrollArea::relayout` 的入口必须先有 `if (!content_ || !viewport_) return;`，然后才轮到守卫。

**为什么不新增一个 `nullGuards` 计数器**：区分不出额外信息。Debug（测试全量跑 Debug + ASan）会在 `assert` 上直接变红并指出现场；Release 只需要"不静默、不崩"。多一个字段要多一条 E6 断言去维护它，收益为零。**这一条如果 Leo 不同意，是可以单独翻的**——加字段是纯增量改动，不影响其它任何决定。

---

### 11.2 API 形状（唯一权威；E2 照此实现，不得增删）

> 本版取代评审前的版本。与第 1 版的差异：`g_hints`→`g_deathWatch`（且它是 E17 `g_detaches` 的改名而非新建）、`HintGuard`→`DeathWatch`（合并 `DetachGuard`）、`hintCursorDepth()`→`deathWatchDepth()`、`hintFrameCancelled()`→`frameDegraded()`、`hintFramesCancelled`→`framesDegraded`、新增 `nullptr` 语义。

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
// This list has NO reader other than alive() on the guards below.  That is a
// PRECONDITION, not an observation: the day something needs to ask "is a
// detach in flight on X?", the list splits FIRST -- see REM3-G6 and the three
// readers of g_layouts that make it the counter-example.
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

**三个成员**：

| 成员 | 可见性 | 作用 |
|---|---|---|
| `explicit DeathWatch(const Widget*)` | public，inline | 构造：把栈上游标压入 `g_deathWatch`（Release 5 条指令，见 §11.5；Debug 多一条 `assert`） |
| `~DeathWatch()` | public，隐式（继承自 `LiveGuard`，非虚） | 析构：弹出游标（2 条）。非虚是对的——私有基类，永不通过基类指针 delete |
| `bool alive() const` | public，`using` 导出，inline | 查询：`cursor_.node != nullptr` |

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

> **REM3-G6**：`g_deathWatch` 不得增加 `alive()` 之外的读者。任何形如"现在有没有一个 X 帧站在谁身上"的谓词都必须**先拆链表再写**。反例已经在库里：`g_layouts` 有三个表外读者（`markLayoutDirty:623`、`layoutPassActive:722`、`currentLayoutHost:724`），这正是它不能被复用的原因（Q1 候选 E）。

> **REM3-G7**：空成员是空检查的事，不是守卫的事（Q7）。

> **REM3-G8**：`detail::frameDegraded()` **每帧最多记一次**。计数器数的是"帧"，不是"检查"；每个降级分支写一次、随即 `return`，天然满足。

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
> * **P1**：对任何 widget 的**虚成员**调用（`sizeHint()`、`onGeometryChanged()`、`onPaint()`、`styleState()`、`styleType()` …）。**限定名调用不算 P1**（`PushButton::sizeHint()` 是静态绑定），但见下面的传递性条款。
> * **P2**：调用**已知能到达应用代码的库函数**：`setGeometry` / `setVisible` / `setLayout` / `invalidateSizeHint` / `add<T>` / `takeChild` / `removeChild` / `clearChildren` / `Window::openPopup` / `Window::closePopup` / `Widget::performLayout` / `Layout::arrange` / `Layout::measure(For)`，**以及任何被本清单登记过的库函数**（传递性在这里显式展开，见下）。
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
| 1 | `GroupBox.cpp:53` | P1 `Widget::sizeHint()` | `:54` `layout_`、`:62` `title_`、`:65` 虚调用 | `this` | S2 | **W1** | ✅ CP-G1 |
| 2 | `GroupBox.cpp:65` | P1 `styleState()` | 门后 `:69-75` **只读局部量** | — | — | — | ❌ 非危险（谓词判出，不是免检） |
| 3 | `ScrollArea.cpp:158` | P1 `content_->sizeHint()` | `:159` `geometry_`；`:164` **写** viewport；`:165` **写** content | `this,viewport_,content_` | S1 | **W1** | ✅ CP-S1 |
| 4 | `ScrollArea.cpp:164` | P2 `setGeometry`（条件性，见下） | `:165` 读 `this->content_` 并写 content | `this,content_` | S1 | **W1** | ✅ CP-S2 |
| 5 | `ScrollArea.cpp:165` | P2 `setGeometry` | **无**（下一句 `return`） | — | — | — | ❌ 死代码；**加注释** |
| 6 | `ScrollArea.cpp:174` | P2 `setGeometry` | **无**（函数末） | — | — | — | ❌ 死代码；**加注释** |
| 7 | `ScrollArea.cpp:22`（`setContentSize`） | P2 `content_->setGeometry` | `:24` `relayout()`、`:26` `scrollTo(scrollOffset())`、`:27` `update()` | `this,viewport_,content_` | S1 | **W1** | ✅ CP-C1 |
| 8 | `ScrollArea.cpp:24` | P2 `relayout()`（含 #3/#4/#5） | `:26` 经 `this`→`viewport_`/`content_`；`:27` `this` | `this,viewport_,content_` | S1 | **W1** | ✅ CP-C2 |
| 9 | `ScrollArea.cpp:26` | P3 `scrolled.emit`（`scrollTo:65`） | `:27` `update()` 只读 `this` | — | — | — | ❌ **D7 豁免**；宿主是 `this` |
| 10 | `Widget.cpp:548` | P2 `layout_->measureFor` | **无**（`return` 该调用的结果） | — | — | — | ❌ 登记备查 |
| 11 | `Widget.cpp:421`（`removeChild`） | P2 `takeChild` | `:422` `doomed.reset()` **不经 `this`**，随后函数结束 | — | — | — | ❌ **看过了，不需要封**（见下） |
| 12 | `Widget.cpp:302`（`announceDetached`） | P2 `win->widgetDetached` | `:306` 起读 `parent.children()` | `parent` | S1 | — | ✅ **已修**（E17，`e9c283a`；守卫在 `:301`，检查 `:305`/`:318`） |
| 13 | `Widget.cpp:401`（`takeChild`） | P2 `announceDetached` | `:408` 起读 `children_` | `this` | S1 | — | ✅ **已修**（E17；`:400`/`:402`） |
| 14 | `Widget.cpp:436`（`clearChildren`） | P2 `removeChild` | `:437`/`:441` 读 `children_` | `this` | S1 | — | ✅ **已修**（E17；`:431`/`:437`） |
| 15 | `Widget.cpp:492`（`setGeometry`） | P1 `onGeometryChanged()` | `:495` `visible_` + `update()` | `this` | S1 | — | ✅ **已修**（R2；`GeometryGuard` `:491`/`:493`） |
| 16 | **`AppWindow.cpp:72/74/75`** | P2 `setGeometry` ×3 | `:73→:74` 读并解引用 `content_`、`:75` 读并解引用 `fill_`、`:77` 读信号成员、`:78` `update()` | `this,content_,fill_`（`header_` **不需要**：`:72` 之后没有任何语句再解引用它） | **S1（本表最高）** | **W2** | ❌ 本轮不改，**已定级** |
| 17 | `AppWindow.cpp:77` | P3 `contentResized.emit` | `:78` `update()` 只读 `this` | — | — | — | ❌ D7 豁免 |
| 18 | `AppWindow.cpp:85`（`setHeaderVisible`） | P2 `setVisible` | `:86` `relayout()` 经 `this` | `this` | S2 | W2 | ❌ 本轮不改 |
| 19 | `WindowHeader.cpp:218`（`relayoutItems`） | P2 `setGeometry`（**在 range-for 里**） | `:219-221` 继续用 `slots_` 的迭代器、`:222` `update()` | `this` + 迭代器失效 | S1 | **W2** | ❌ 本轮不改；与 BoxLayout scratch 越界同形 |
| 20 | `Cascader.cpp:143`（`relayoutColumns`） | P2 `setVisible`（循环内，按下标读 `columns_`） | `:150` 起继续按下标读（`:142` 的循环自己也按下标） | `this` + 下标 | S2 | W2 | ❌ 本轮不改 |
| 21 | `Cascader.cpp:152/159/161` | P2 `setGeometry` | `:153-155` 局部量、`:156-159` 按下标读 `columns_`、`:161` 读 `popupBox_` | `this,popupBox_` + 下标 | S2 | **W2** | ❌ 本轮不改；下标防重分配、**防不了缩短** |
| 22 | `SelectBase.cpp:60`（`showCustomPopup`） | P2 `Window::openPopup`（内含 `closePopup`→`popupClosed.emit`） | `:61` `update()`、`:62` 读 `openStateChanged`（都经 `this`） | `this` | S2 | W2 | ❌ 本轮不改；**P3 家族的样本**（宿主是 `Window` 不是 `this`，D7 不豁免） |
| 23 | `PushButton.cpp:88`（`sizeHint`） | P1 `styleState()` | `:90/:91/:92` 读 `text_`/`loadingText_`、`:93-94` 读 `icon_` | `this` | S3 | **W3** | ❌ 见 REM3-RES-2（应走契约而非守卫） |
| 24 | `IconButton.cpp:29` | 限定名 `PushButton::sizeHint()`（内含 #23） | 门后 `:30-33` **只读局部量** | — | — | — | ❌ 非危险 |
| 25 | `examples/showcase/PageIcons.cpp:633` | P1 `content->sizeHint()`，门后动 `sa` | 库外 | （库外） | S2 | W3 | ❌ R3 不改 examples；**"契约主语是调用者"的活样本** |
| 26 | showcase 四页 `return content->sizeHint().preferred;` | P1 | 门后无读 | — | — | — | ❌ 无动作 |
| 27 | **P3 家族（全库 ~60 处 `.emit(`）** | P3 | **未逐点枚举** | — | S2 | **W2（扫描任务）** | ❌ 见下 |

**#11 为什么"看过了、不需要封"（回答我自己 E17 报告的 §8.3）**：`removeChild:420-423` 只有两条语句——`:421 takeChild(child)`（门，且它自己已经在 `:400` 上了守卫）、`:422 doomed.reset()`。`reset()` 只碰局部 `unique_ptr`，**不经 `this`**；`:422` 之后函数结束。所以判它免检的理由**不是**"`reset()` 不碰 `this` 成员"（那是关于被调函数内部的推理，不可机械复核），而是 **"门之后本帧没有任何经 `this` 或成员到达的读写"**——这是谓词的第二个子句，对 diff 可判定。按 Leo 的标准，本行的存在本身就是"看过了没有"与"没看"的区别。

**#4 为什么仍然要查**：`viewport_` 是 `ScrollArea` 构造函数里 `add<Widget>()` 出来的**普通 `Widget`**，今天 `onGeometryChanged()` 是基类空实现、且没有 layout，所以 `:164` **今天**不是门。但应用能拿到它：`sa->content()->parent()->setLayout<BoxLayout>()` 一句就让它变成真门。**"今天不是门"不是不变量**——`ScrollArea.hpp:25` 的 `content()` 是 public。

**#16 为什么定级最高**：三个 `setGeometry` 无条件执行、**每帧**跑（`AppWindow::onGeometryChanged():81` → `relayout()`），`header()`/`content()` 都是 public，showcase 五页正是这么用；门后一个游标都没有。
⚠️ 而 `Widget.cpp:483-489` 的注释**就是拿这个函数当例子写的**——"*onGeometryChanged runs APPLICATION code: AppWindow::relayout emits contentResized from inside one, and a slot is entitled to destroy widgets -- this one included.*" 那条注释给 `setGeometry` **自己那一帧**加了 `GeometryGuard`（`:491`），而 `AppWindow::relayout` 的帧在它**下面**，一个游标都没有。**库里最显眼的那条注释，指着的正是本表漏掉的那一行。** 这是"表不是扫出来的"最硬的证据，我接受。

**#27 为什么按家族登记而不逐点列**：全库 `.emit(` 约 60 处，但 D7 豁免砍掉其中大半——凡是"发自有信号、门后只读 `this`"的（`PushButton::activate:107-109`、`ScrollArea::scrollTo:65`、`Cascader:78` …）都不危险。**剩下的是"发别人的信号 / 经别的对象绕一圈回来"那一类**，#22 是已确认的一个样本。逐点判定要读 60 个函数体，那是一次独立的扫描任务，不是 E1 能在设计文档里完成的事。**登记形态**：家族已识别、判定谓词已给出（P3 + D7 豁免）、定级 S2、排 W2，扫描产出物是 §11.9 那个 lint 的 allowlist。

**本轮 ✅ 的规模变化（必须让架构团队知道）**：第 1 版是 2 个检查点 / 2 个函数 / 2 个文件；本版是 **5 个检查点 / 3 个函数 / 2 个文件**（新增 `ScrollArea::setContentSize` 的 CP-C1/CP-C2，以及 CP-S2 的检查项从 3 变 2）。多出来的那个函数在**同一个文件**、**同一种形状**、上方 130 行处；不带上它，E5 复核时会立刻问"为什么隔壁那个一模一样的没改"。

---

### 11.5 开销量化

**已实测的部分**（`cl /std:c++20 /W4 /permissive- /O2`，MSVC 14.50 / VS18 x64，`/FA` 取汇编；探针在 scratchpad，不入库）：

| 段 | 实测 | 说明 |
|---|---|---|
| 单个守卫构造 | **5** | 1 次链表载入 + 2 次存字段 + 1 次存回全局（+ Debug 的 `assert` 一条 `test`/`jcc`） |
| 三个守卫构造（合并后） | **9** | 编译器把 LIFO 的三个合并了：对全局的读改写**各只有一次** |
| 守卫析构（无论几个） | **2** | `mov rax,[a.outer]` / `mov [g_deathWatch],rax` |
| 一次 `alive()` | **2** | `cmp qword ptr [rsp+k], 0` + `je` |

**关键实测事实**：三个守卫在全局变量上的争用与一个守卫**完全相同**，Q5 的加固没有为多守的两个指针付全局访问的代价。

**⚠️ 本版新增的成员重读没有重测，以下是按上表单价的推算，标注为推算**：

| 现场 | 构造 | 析构 | 检查 | 合计（推算） |
|---|---|---|---|---|
| `GroupBox::sizeHint()` 受保护区 | 5 | 2 | CP-G1：1×`alive()` = 2 | **9**（与第 1 版实测一致，未变） |
| `ScrollArea::relayout()` 受保护区 | 9 | 2 | CP-S1：3×`alive()` + 2×成员重读 ≈ 12；CP-S2：2×`alive()` + 1×成员重读 ≈ 7 | **≈30** |
| `ScrollArea::setContentSize()` | 9 | 2 | CP-C1 ≈ 12；CP-C2 ≈ 12 | **≈35** |

（成员重读按 `mov reg,[this+off]` + `cmp reg,[rsp+k]` + `jne` = 3 条计。）
**第 1 版那句"全函数除两次门调用外共 23 条指令"作废**：它按 6 次 `alive()` 计价，而正确的检查项是 5 项且其中 2 项不是 `alive()`（§11.3 CP-S2 的理由）。**重测写进 E2 的出门条件**（§11.7）。

**没有分支预测灾难**：所有条件跳转都是「本帧栈槽/寄存器 vs 0 或 vs 另一个栈槽」，生产中**恒不跳转**，稳态误预测 ≈ 0；无间接跳转、无数据相关循环、无 `switch`。对照被否决的替代方案（"用完再扫一遍 `children()` 确认指针还在"）：那是 O(N) 次指针追逐载入且分支结果数据相关——**那才是分支预测灾难。**

**同一帧的分母**：`:158` 的 `content_->sizeHint()` 要把整页 layout 测一遍，按 §10.2 每个 `Label` 至少一次 `BLGlyphBuffer` + 一次 shaping，`PageOps` 有几十个。量级 **10⁴ ~ 10⁵ 条指令**，外加 Blend2D 自己的运行时分配。守卫占 **≲0.1%**。

**`~Widget` 的增量**：**零**。HEAD 已经有第四行 `cancelOn(g_detaches, this)`（`:347`），本设计只改它的名字。（相对 E17 之前是 3 条指令，冷路径。）

**ADR-R2-01「不装 Layout 的 widget 不为引擎付代价」继续成立**，逐条：

* `ScrollArea::relayout()` 的守卫落在 `if (content_->layout())` **内部**——今天库里每一个 `ScrollArea`、以及 5 个绝对坐标 showcase 页，**一条都不执行**。
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
   a. 删掉搬走的两处定义，加最小适配（`using detail::LiveCursor;` 之类）；
   b. `g_detaches` → `detail::g_deathWatch`（**定义**移出匿名 namespace，外部链接）；`using DetachGuard = LiveGuard<g_detaches>;` **整条删除**，三处现场改用 `detail::DeathWatch`（`:301`、`:400`、`:431`）；
   c. **重写 `:113-134` 那段"第四条链表"注释**——按 §11.1 的判据 1/2 重写，不是替换标识符：新文字要说清"按取消策略命名""表外读者为零是前提不是观察"（REM3-G6）；
   d. **改掉 `:264-269` 那段已被本设计作废的理由**：`DeathWatch` 收 `const Widget*`，"非常量引用是为了避免 `const_cast`"不再成立。**只改注释，不改 `announceDetached` 的签名**（签名归 E18）；
   e. `~Widget:347` 改名 `cancelOn(g_deathWatch, this);`；
   f. 定义 `detail::g_deathWatch = nullptr;` 与 `deathWatchDepth()`。
3. `Layout.hpp` / `Layout.cpp`：`LayoutDiagnostics`（`Layout.hpp:273-280`）加 `framesDegraded`，实现 `detail::frameDegraded()`（饱和自增，与 `Layout.cpp:190-206` 的四个同写法）。
4. **E2 到此为止**——调用点（`GroupBox` / `ScrollArea`）是 E3/E4，不在同一次提交里。
5. 提交约定：**每文件一个 commit**。

**E2 明确不做**：不改 `Layout.hpp:168-174` 的既有契约文字（那条对 `Layout` 实现者仍然正确，只是不完备）；不动 examples；不改 `announceDetached` 的行为或签名；不给 `Widget` 加任何成员。

#### E2 的出门条件（**本版新增；Leo 的 E7 实证素材 #2/#3**）

E2 交测前必须逐条给出证据，**"探针通过"不算数**：

1. **从干净目录全量构建**。`Widget.hpp` 一改，依赖它的 **31 个 .cpp 全部要重编**；任何一步用了增量构建，`static_assert` 可能根本没被重新求值就报"通过"。证据形式：**新建构建目录**（不得复用 `build/`、`build-debug/`、`build-asan/`）+ **贴出实际编译的 TU 数量**。
2. **全库 `/W4 /permissive-` 零警告**。这是 **E2 的出门条件，不是 E1 的已验事实**（§11.8 已相应降级）。理由：§11.5/§11.8 的探针是**手写 `cl` 命令行**，缺 `/utf-8` 与 `/Zc:__cplusplus`（项目实际是 `CMakeLists.txt:96` 的 `/W4 /permissive- /utf-8 /Zc:__cplusplus` 外加 CMake 注入的默认 flags），也没走真实 include 链。`Widget.hpp` 是全库最热的头文件，模板搬进去会在**每一个** TU 里实例化。
3. **`Widget.cpp:36` 的尺寸 `static_assert` 在上述全量构建里被真正求值过**（0 新成员，论证上必然成立，但要编过才算）。
4. **§11.5 的指令数按新形状重测**（成员重读是推算，不是实测）。

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

---

### 11.9 门覆盖的机器校验（E5 的交付物；Leo E7 素材 #1）

**Leo 的判词我接受**：五次复发，五次都是"有人复核过、复核漏了"；§11.2 的 `static_assert` 校验的是**类型形状**，`deathWatchDepth()` 只能验链表配平，**没有一样东西能验"第 9 扇门忘了加守卫"**；而 §11.8 把完整性交给"E5 独立复核"——**一个人**。Q3 证明了评审规则可以降级成编译器规则，那一手必须也用在门覆盖上。

**先说我否决的那个方案，以及为什么**：Leo 建议的运行时信号是"在 `Widget::sizeHint()` / `setGeometry()` 入口统计**跨越时栈上没有任何游标**的次数，由 E6 断言它等于表里 ❌ 行的数量"。这个信号**不能用**，理由不是成本而是**它恒为正**：每一次**最外层**的门跨越（测试直接调 `relayout()`、事件循环第一次进 `setGeometry`、应用调 `setContentSize`）栈上都合法地没有任何游标，因为最外层的调用者**不需要**游标。于是这盏红灯在正常运行下常亮，而"常亮的红灯"是最坏的一种诊断——它会在第一周被人加进白名单，然后永远失效。（这与 `latchNaturalSize` 当年问错问题是同一类错误：`layoutPassActive()` 是"进程里有没有 pass"，要的却是"我这一帧是不是 pass"，见 `Widget.cpp:577-587`。栈上有没有游标，同样答不了"我这一帧该不该有游标"。）

**我提的方案：把 §11.4 从手工表变成 lint 的 allowlist。** 门覆盖是**代码属性**，不是执行路径属性，所以校验它的正确工具是源码 lint，不是运行时计数器。

**契约（E5 按此实现，形状可议、三条性质不可议）**：

1. **候选集由谓词生成**：脚本按 §11.4 的 P1/P2/P3 原语清单扫 `src/widget/*.cpp`（以及 `src/**` 其余目录），对每个函数体判定"含门 且 门不是本体最后一条语句 且 本体内没有 `DeathWatch`"⇒ 进候选集。允许**保守多报**（例如门后只碰局部量的），因为多报的方向是"逼一个人做决定并留档"，漏报的方向是本缺陷族本身。
2. **每个候选必须有归宿**：要么已被守卫，要么出现在 allowlist 里，且 allowlist 的每一条**必须带**：理由（对应谓词的哪个子句）、定级（S1/S2/S3）、轮次（W1/W2/W3）。**allowlist 就是 §11.4 的表**，两者不得各写一份（`Widget.cpp:74-75` 那条规矩：第二份手抄就是第二个会忘的地方）。
3. **未归档的候选让 `verify.bat` 变红**。这就是"第 9 扇门忘了加守卫"的检出点：新写的门若既没守卫也没进 allowlist，构建就红。

**本版扫描用的第一刀（E5 的起点，不是终点）**——它只生成候选行，函数体归属与"门后有没有读"仍是人判的，这也正是要把它变成脚本的原因：

```
grep -n "setGeometry(\|sizeHint()\|\.emit(\|setVisible(\|setLayout\|invalidateSizeHint\|add<\|takeChild(\|removeChild(\|clearChildren(\|openPopup(\|closePopup(" src/widget/*.cpp
```

**为什么这个能兑现 Leo 要的性质**：它检查的是**代码**而不是**被跑到的路径**（运行时计数器只能覆盖用例踩到的那些帧，而本族五次复发里至少三次是没有用例踩到的帧）；它的红/绿判据是机械的；它的 allowlist 天然逼着"看过了没有"与"没看"可区分——这正是 §11.4 #11（`removeChild`）那一行要证明的事。

**成本与风险，写明**：脚本要做的是**近似**的 C++ 函数体切分（花括号配平 + 函数头正则），不是解析。首轮会多报若干条（估计 20~40 条，主要来自 P3 家族），**这些多报的分诊本身就是表 #27 那个 W2 扫描任务**——也就是说这个 lint 不是额外工作，它是把已经必须做的那次扫描变成一个**不会退化**的产物。E5 若判定脚本形状不合适（例如决定改用 clang 的 AST 工具），**三条性质不变**即可。

---

**按团队纪律，本节到此为止：我不下"已验证"结论。** 本版是对 Leo 书面评审的答复稿，交复签；实现与测试交后续任务。
