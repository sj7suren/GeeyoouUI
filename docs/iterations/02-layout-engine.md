# 迭代 02：布局引擎（R2）

本轮范围：**T-01 护栏 → T-05 `Layout` 抽象基类**。
**不含** `BoxLayout` / `GridLayout` / 具体 `sizeHint()` 实现 / 现有控件迁移 / `TagId`。

对应架构裁定：**ADR-R2**（含 ADR-R2-08 索引寻址、ADR-R2-09 `naturalSize_` 锁存）。

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

## 6. 本轮未验证 / 已知边界

1. **M2 断言未被用例覆盖**：命中即 `abort`，写不出「断言应触发」的进程内用例。要覆盖须用 `d7.*` 那种子进程模式，本轮未做。
2. **M4 深度上限未被用例覆盖**：要触发需要 64 层嵌套 Layout 宿主，而 Debug 下 `add<T>` 的 `kMaxTreeDepth` 断言会先拦下建树本身。计数器与记录路径已实现（`detail::layoutDiagnostics().depthExceeded`），但**未跑通**。
3. **`naturalSize_` 的深层子孙锁存**：`setGeometry` 里的锁存无条件生效，但如果一个 widget 在**进程里还没有任何 Layout** 时拿到几何、之后祖先才装上 Layout，那么只有 `adoptLayout` 的**直接**子节点会被补锁，更深的子孙要等它们下一次 `setGeometry`。R2 没有具体 Layout，不可观测；T-06 若需要，加一次一次性子树遍历即可。
4. **`Widget::relayout()` 与既有 `AppWindow::relayout()` / `ScrollArea::relayout()` / `Shell::relayout()` 同名**。基类版本非虚，被派生类静态隐藏；`ScrollArea::relayout()` 还是 `private`，因此 `ScrollArea*` 上写 `->relayout()` 会编译失败（访问权限）。目前无调用点，但 T-06 迁移这三个容器时必须先处理。已在交接报告里提请架构决策。
5. **多线程**：与全库一致，布局引擎只在 UI 线程使用。`g_layouts` / `g_layoutDepth` / `g_layoutHosts` / `g_arrangeHost` 都是普通 `static`，非 `thread_local`——与 `g_bubbles` 同一条理由（`docs/architecture.md` §3.11）。
6. **`measure()` 目前无调用方**：引擎只用 `arrange`。用例显式断言 `measures == 0`，防止将来偷偷加一趟没人消费的 measure pass。
