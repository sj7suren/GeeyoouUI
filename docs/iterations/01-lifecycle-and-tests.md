# 迭代 01：生命周期与测试网

> 本文件目前包含 **REQ-6 emit 点审计** 一章及其 **阶段 3 后的复核**，其余章节由后续提交补齐。
>
> ⚠️ 下面的「REQ-6 附录」是**阶段 2 结束时**的快照，逐点结论保持原样不动（它是可复核的历史记录）。
> 阶段 3 之后哪些点不再可达、哪些**新变成**可达，见文末 [阶段 3 复核](#阶段-3-复核审计后仍剩什么)。

---

## REQ-6 附录：全仓 `.emit(` 逐点审计

### 审计问题

契约 D7 规定：**槽内不允许销毁"信号所属的宿主对象"**。本审计逐点核对全仓每一个 `.emit(` 调用点，回答两件事：

1. **仓库现状**：是否已经存在违反 D7 的槽（即某个 `connect` 的槽会销毁发射方宿主）？
2. **容错度**：如果**应用代码**在该点的槽里销毁了宿主，会不会立刻炸？

### 结论摘要

| 项 | 结果 |
|---|---|
| 实际 `.emit(` 调用点总数 | **72**（不含 `core/Signal.hpp` 自身的注释与实现） |
| 仓库内**已存在**的 D7 违约 | **0 处** |
| 判定 A（容错：emit 后本函数不再触碰 `this`） | 45 处 |
| 判定 B（不容错：emit 后仍读写成员 / 调成员函数 / 上游调用帧立刻触碰） | 27 处 |
| 判定 C（高危：应用最可能在此挂"关闭窗口 / 切画面并销毁"） | **16 处** |

> 施工图给的数是 73，实测 72。差额来自 `Signal.hpp` 里 `emit()` 的说明性注释被计入。72 这个数已逐行列出，可复核。

**为什么现状是 0 违约，而且是可验证的**：`Widget` **没有任何移除子节点的 API**（无 `removeChild` / `takeChild` / `clearChildren`，`children_` 也没有任何 `erase` / `clear` 调用点）。因此库内任何槽都无法销毁一个 widget——唯一能发生的销毁是「销毁整个 `Window` / `AppWindow`」，而全仓（含 showcase）没有任何槽这么做：`Shell::showPage` 是**隐藏**而非销毁旧页面，`WindowHeader::closeRequested` 接到的是 `Window::close()` → `PostMessageW(WM_CLOSE)`，**异步**，不在槽内销毁任何东西。

也就是说：**D7 目前是被"缺少 API"被动保证的，不是被设计保证的。** 阶段 3 给 `Widget` 加移除 API 的那一刻，下表 27 个 B 判定会同时变成可达路径——这是本审计最重要的一条输出。

### 判定图例

| 判定 | 含义 |
|---|---|
| **A** | `emit` 之后本函数只操作局部变量 / 事件对象，或直接返回。槽内销毁宿主不会在**本帧**立刻炸（上层派发路径仍可能炸，见下方"系统性风险"）。 |
| **B** | `emit` 返回后本函数仍读写 `this` 成员、调用成员函数，或**上游调用帧**紧接着这么做。槽内销毁宿主 = 即刻 use-after-free。 |
| **C** | 叠加在 A/B 之上的标记：该信号正是应用最自然会挂上"关掉这个窗口 / 跳到另一幅画面"的地方。 |

### 系统性风险（不属于任何单点）

`Widget::dispatchMouse`（`src/widget/Widget.cpp:215`）在调用 `w->onMouse(local)` 之后执行 `w = w->parent_`，**先解引用再取父指针**。因此**任何**从 `onMouse` / `onKey` 里发出的信号，只要槽销毁了冒泡链上的任意一个 widget，冒泡循环就会踩在已释放内存上——与该 emit 点自身是 A 还是 B 无关。下表所有"来自输入事件"的 A 判定都受这一条约束，不能单独读作"安全"。

> **阶段 3 已修**：`dispatchMouse` / `dispatchKey` 改为由 `BubbleGuard` 驱动，移除子树时会**取消**站在其上的冒泡游标。详见文末复核第 1 条。本条约束**已解除**，下表的 A 判定现在可以单独读作"本帧安全"。

### 清单

#### `src/hmi/`

- [x] `src/hmi/AlarmList.cpp:109` `alarmAdded.emit(*added)` — **B**：其后 `return nextId_ - 1;` 读成员。
- [x] `src/hmi/AlarmList.cpp:125` `alarmAcknowledged.emit(*r)` — **B**：其后调 `rebuildVisible()`。
- [x] `src/hmi/AlarmList.cpp:135` `alarmAcknowledged.emit(r)` — **B**：处在遍历 `ring_` 的循环体内，`r` 本身就是成员容器的引用。
- [x] `src/hmi/DataHub.cpp:40` `sampleArrived.emit(s)` — **B**：循环继续遍历成员 `scratch_`，并在下一轮读 `channels_`。
- [x] ~~`src/hmi/DataHub.cpp:42` `batchDrained.emit(n)` — **A**：其后 `return n;`，`n` 是局部量。~~ **阶段 3 (REQ-8) 已连同信号一并删除**，emit 点总数 72 → 71。
- [x] `src/hmi/Gauge.cpp:25` `valueChanged.emit(v)` — **B**：其后 `update()`。

#### `src/render/`

- [x] `src/render/Skin.cpp:201` `changed.emit()` — **A**：其后 `return true;`。宿主 `SkinRegistry` 是进程级单例，本就无法销毁。
- [x] `src/render/Skin.cpp:211` `changed.emit()` — **A**：函数末句，宿主同上。
- [x] `src/render/Skin.cpp:217` `changed.emit()` — **A**：函数末句，宿主同上。

#### `src/widget/` — 值类控件

- [x] `src/widget/CheckBox.cpp:19` `toggled.emit(checked_)` — **A**：`setChecked` 末句。
- [x] `src/widget/ToggleSwitch.cpp:24` `toggled.emit(checked_)` — **A**：`setChecked` 末句。
- [x] `src/widget/Slider.cpp:32` `valueChanged.emit(value_)` — **A**：`setValue` 末句。
- [x] `src/widget/SpinBox.cpp:28` `valueChanged.emit(value_)` — **A**：`setValue` 末句。
- [x] `src/widget/ScrollArea.cpp:46` `scrolled.emit(clamped)` — **A**：`scrollTo` 末句，`clamped` 是局部量。
- [x] `src/widget/RadioButton.cpp:24` `other->toggled.emit(false)` — **B**：全仓**唯一**发射方宿主不是 `this` 的点。循环仍在遍历 `p->children()`，销毁 `other` 或 `this` 都会当场破坏迭代。
- [x] `src/widget/RadioButton.cpp:38` `toggled.emit(true)` — **A**：`setChecked` 末句。
- [x] `src/widget/PushButton.cpp:45` `toggled.emit(checked_)` — **A**：`setChecked` 末句。
- [x] `src/widget/PushButton.cpp:67` `toggled.emit(checked_)` — **B / C**：紧接着执行 `clicked.emit()`，读 `this->clicked`。
- [x] `src/widget/PushButton.cpp:69` `clicked.emit()` — **A / C**：`activate()` 末句，两个调用点（`:241` / `:252`）之后都只有 `e.accept()`。**全库最高危的一个信号**——`clicked` 就是"确定 / 关闭 / 退出"按钮挂的地方，且受上面"系统性风险"那条约束。

#### `src/widget/` — 文本输入

- [x] `src/widget/LineEdit.cpp:29` `textChanged.emit(text_)` — **A**：`setText` 末句。
- [x] `src/widget/LineEdit.cpp:168` `textChanged.emit(text_)` — **A**：`emitChanged()` 末句。
- [x] `src/widget/LineEdit.cpp:475` `returnPressed.emit()` — **B / C**：下一行立刻 `editingFinished.emit(text_)`，再下一行写 `textOnFocus_`。回车提交是关对话框的经典位置。
- [x] `src/widget/LineEdit.cpp:476` `editingFinished.emit(text_)` — **B / C**：其后 `textOnFocus_ = text_;`。另注：实参是成员 `text_` 的引用，槽内改写文本对后续槽是可见的（不是 UAF，但值语义与直觉相反）。
- [x] `src/widget/LineEdit.cpp:513` `editingFinished.emit(text_)` — **B / C**：`onFocusChanged(false)` 分支，其后 `update()`。失焦提交同样是"提交并关闭"的挂点。
- [x] `src/widget/TextArea.cpp:29` `textChanged.emit(text_)` — **A**：`setText` 末句。
- [x] `src/widget/TextArea.cpp:194` `textChanged.emit(text_)` — **A**：`emitChanged()` 末句。
- [x] `src/widget/TextArea.cpp:530` `editingFinished.emit(text_)` — **B / C**：其后 `update()`。
- [x] `include/geeyoou/widget/SearchBox.hpp:22` `searchRequested.emit(text())` — **B / C**：该槽由 `LineEdit.cpp:475` 发出，返回后 `476`/`477` 立刻触碰 `LineEdit` 成员。"回车搜索 → 切画面（销毁搜索框）"是极常见的 HMI 写法，链路上没有任何一层容错。

#### `src/widget/` — 弹层与列表（施工图点名的高危区）

- [x] `src/widget/PopupList.cpp:104` `rowActivated.emit(highlighted_)` — **A**：`activateHighlighted()` 末句。宿主 `PopupList` 归 `Window` 拥有，`SelectBase` 的槽只会 `closePopup()`（隐藏），不销毁它。
- [x] `src/widget/PopupList.cpp:302` `expanderToggled.emit(i)` — **A**：其后仅 `e.accept(); break;`。
- [x] `src/widget/PopupList.cpp:304` `rowToggled.emit(i)` — **A**：同上。
- [x] `src/widget/PopupList.cpp:307` `rowActivated.emit(i)` — **A / C**：同上；但这是"点一行 → `SelectBase::close()` → 应用切画面"的入口。另注：`const PopupRow& row`（`:295`）是成员 `rows_` 的引用，槽内调用 `setRows()` 会让它悬垂——本分支恰好在 emit 之后不再使用 `row`，属于**侥幸而非设计**。
- [x] `src/widget/SelectBase.cpp:57` `openStateChanged.emit(true)` — **A**：`showCustomPopup` 末句。
- [x] `src/widget/SelectBase.cpp:66` `openStateChanged.emit(false)` — **A**：`hideCustomPopup` 末句。
- [x] `src/widget/SelectBase.cpp:106` `openStateChanged.emit(true)` — **A**：`open()` 末句。
- [x] `src/widget/SelectBase.cpp:114` `openStateChanged.emit(false)` — **B**：`close()` 本身是末句，但 `close()` 的调用点 `onFocusChanged:253` / `onEnabledChanged:258` 之后都紧跟 `update()`。
- [x] `src/widget/ComboBox.cpp:36` `currentIndexChanged.emit(current_)` — **B**：下一行 `currentValueChanged.emit(currentValue())` 同时用到成员信号和成员函数。
- [x] `src/widget/ComboBox.cpp:37` `currentValueChanged.emit(currentValue())` — **A**：`setCurrentIndex` 末句。
- [x] `src/widget/MultiSelect.cpp:27` `selectionChanged.emit()` — **A**：`setChecked` 末句。
- [x] `src/widget/MultiSelect.cpp:41` `selectionChanged.emit()` — **A**：`checkAll` 末句。
- [x] `src/widget/MultiSelect.cpp:52` `selectionChanged.emit()` — **A**：`clearAll` 末句。
- [x] `src/widget/SearchableSelect.cpp:49` `queryChanged.emit(query_)` — **B**：下一行调 `list()` 与 `this->noMatch`。
- [x] `src/widget/SearchableSelect.cpp:50` `noMatch.emit(query_)` — **A**：`setQuery` 末句。
- [x] `src/widget/TreeSelect.cpp:82` `selectionChanged.emit(selectedValue_)` — **A**：函数末句。
- [x] `src/widget/TreeSelect.cpp:170` `selectionChanged.emit(selectedValue_)` — **A**：函数末句。
- [x] `src/widget/Cascader.cpp:78` `selectionChanged.emit(value_)` — **B / C**：其后还有 `rebuildColumns(); update();` 以及 `if (atLeaf) close();`。选中叶子是切画面的典型时机。
- [x] `src/widget/MenuButton.cpp:89` `triggeredIndex.emit(mi)` — **B / C**：下一行读 `m`（成员 `items_` 的引用）与 `this->triggered`。菜单项就是"退出登录 / 关闭"。
- [x] `src/widget/MenuButton.cpp:90` `triggered.emit(...)` — **B / C**：`trigger()` 末句，但两个调用点（`:45`、`:140`）返回后立刻 `closeMenu()`。
- [x] `src/widget/ListView.cpp:46` `selectionChanged.emit()` — **A**：`clearSelection` 末句。
- [x] `src/widget/ListView.cpp:151` `selectionChanged.emit()` — **A**：`toggleSelection` 末句。
- [x] `src/widget/ListView.cpp:283` `rowClicked.emit(row)` — **B / C**：下一行读 `this->rowActivated`。点报警行→开详情页是本库的主场景。
- [x] `src/widget/ListView.cpp:284` `rowActivated.emit(row)` — **A / C**：其后仅 `e.accept(); break;`。
- [x] `src/widget/ListView.cpp:320` `rowActivated.emit(current_)` — **A / C**：其后仅 `e.accept();`（Enter 激活行）。
- [x] `src/widget/ListView.cpp:330` `selectionChanged.emit()` — **A**：其后仅 `e.accept();`（Ctrl+A）。
- [x] `src/widget/DatePicker.cpp:192` `dateChosen.emit(d)` — **A**：其后仅 `e.accept(); break;`。宿主 `CalendarView` 由 `Window` 拥有，`DatePicker` 的槽只隐藏它。
- [x] `src/widget/DatePicker.cpp:215` `dateChanged.emit(date_)` — **A**：`setDate` 末句。
- [x] `src/widget/DatePicker.cpp:221` `dateChanged.emit(date_)` — **A**：`clearDate` 末句。

#### `src/widget/` — 窗口层

- [x] `src/widget/Window.cpp:24` `closed.emit()` — **B / C（全库最高危）**：这个 emit 位于安装到 `Win32Window::onClose` 的 `std::function` **内部**。销毁 `Window` 会连带销毁 `platformWindow_`，也就是**销毁正在执行的那个 `std::function` 对象本身**；`Win32Window::handle` 的 `WM_CLOSE` 分支返回后还要执行 `PostQuitMessage(0)`。而 `closed` 恰恰是应用最自然写 `delete window` 的地方。
- [x] `src/widget/Window.cpp:27` `maximizedChanged.emit(isMaximized())` — **B**：返回后 `Win32Window::handle` 的 `WM_SIZE` 分支继续执行 `if (onResize) { ... clientSize() ... }`，全是已销毁后端对象的成员。
- [x] `src/widget/Window.cpp:130` `popupClosed.emit()` — **B**：`closePopup()` 本身是末句，但 `handleResize` 的调用点之后还有 `resized.emit(); update();`。
- [x] `src/widget/Window.cpp:297` `resized.emit(e.size)` — **B**：其后 `update()`。
- [x] `src/widget/AppWindow.cpp:77` `contentResized.emit(cs)` — **B**：其后 `update()`。
- [x] `src/widget/WindowHeader.cpp:27` `metricsChanged.emit()` — **A**：`setHeight` 末句。
- [x] `src/widget/WindowHeader.cpp:402` `minimizeRequested.emit()` — **A**：其后仅 `e.accept();`。
- [x] `src/widget/WindowHeader.cpp:403` `maximizeRequested.emit()` — **A**：其后仅 `e.accept();`。
- [x] `src/widget/WindowHeader.cpp:404` `closeRequested.emit()` — **A / C**：其后仅 `e.accept();`。**库自身是安全的**——`AppWindow` 接的是 `Window::close()` → `PostMessageW(WM_CLOSE)`，异步，不在槽内销毁任何东西。列为 C 是因为应用完全可能在这里同步析构窗口，那样会炸在 `dispatchMouse` 的冒泡上。

#### `examples/showcase/`

- [x] `examples/showcase/Shell.cpp:175` `activated.emit(...)` — **A**：其后仅 `e.accept(); break;`。宿主 `Sidebar` 是 `Shell` 的常驻子节点，`showPage` 只隐藏页面。
- [x] `examples/showcase/Shell.cpp:192` `activated.emit(next)` — **A**：其后仅 `e.accept();`。
- [x] `examples/showcase/Shell.cpp:244` `toggleRail.emit()` — **A**：其后仅 `e.accept();`。
- [x] `examples/showcase/ShowcaseWindow.cpp:58` `headerAction.emit(...)` — **B**（继承上游）：lambda 自身是末句，但它由 `MenuButton::trigger:90` 发出，返回后要 `closeMenu()`；销毁 `ShowcaseWindow` 会连带销毁那个 `MenuButton`。仓库内的槽只往日志追加文本。
- [x] `examples/showcase/ShowcaseWindow.cpp:76` `headerAction.emit(...)` — **B**（继承上游）：同上；另注 `items` 是 `language_->items()` 的引用，emit 前已用完。
- [x] `examples/showcase/ShowcaseWindow.cpp:93` `headerAction.emit(...)` — **B**（继承上游）：同上。

### 审计带出的两个越界发现（未修，留档）

审计过程中发现两处**与 D7 相反方向**的悬垂——不是"槽销毁发射者"，而是"**接收者先死，发射者还活着**"。它们都落在本阶段禁止改动的 32 个控件源文件里，因此只记录不动手：

1. **`SelectBase` / `MenuButton` / `DatePicker` 订阅了自己创建、但由 `Window` 拥有的弹层。**
   - `SelectBase::ensurePopup`（`src/widget/SelectBase.cpp:76-81`）对 `popup_->rowActivated` / `rowToggled` / `expanderToggled` 三个信号 `connect([this]...)`；
   - `popup_` 是 `w->add<PopupList>()`，**归 `Window` 所有，且从不移除**；
   - `SelectBase::~SelectBase`（`:15-22`）只调 `closePopup()`，**没有断开这三条连接**。
   - 结论：任何比其宿主 `Window` 先死的下拉控件，都会在 `PopupList` 里留下三个捕获了野指针的槽。`MenuButton`（`:44`）与 `DatePicker`（`:248`）是同一形状。
   - 这正是 `ConnectionScope` 要解决的场景；但控件目前没有"先于 Window 死"的途径（无移除 API），所以今天不可达。**阶段 3 加移除 API 时必须与这三处一起做**，否则移除 API 上线当天就是三个新的 UAF。
   - **阶段 3 已修**（例外授权）：三处改用 `ConnectionScope`，见文末复核第 3 条。

2. **`emit` 实参是成员引用时，槽内改写对后续槽可见。**
   `LineEdit.cpp:476` / `TextArea.cpp:530` 的 `editingFinished.emit(text_)` 用的是 `Signal<const std::string&>`，实参直接是成员。第一个槽调用 `setText()` 后，第二个槽读到的是**新值**。不是内存错误，但与"发射时的快照"这一直觉相反，值得在控件文档里点一句。**未修，仍然成立。**

---

## 阶段 3 复核：审计后仍剩什么

阶段 3（REQ-7 / REQ-8）落地了 `Widget::takeChild` / `removeChild` / `clearChildren`。上面那句"**D7 目前是被缺少 API 被动保证的**"从此作废——**从现在起，槽销毁 widget 是一条真实可达的路径**。本章是给安全/测试团队的输入：哪些点因为本阶段的修复而不再可达，哪些**仍然**可达。

### 一、本阶段消除的风险

| # | 原风险 | 修法 | 现状 |
|---|---|---|---|
| 1 | **系统性：冒泡踩空**（`dispatchMouse` / `dispatchKey` 先解引用再取 `parent_`） | `src/widget/Widget.cpp` 匿名命名空间的 `BubbleCursor` / `BubbleGuard`：进行中的冒泡把自己挂到一条静态链上，移除子树时按节点比对并**取消**命中的游标 | **不再可达。** 用例 `removal.a_slot_may_remove_the_widget_the_mouse_bubble_stands_on` / `..._the_key_bubble_stands_on` 固化 |
| 2 | **Window 四个观察指针悬垂**（`focus_` / `hovered_` / `pressGrab_` / `popup_`） | `Window::widgetDetached()`，由移除 API 对**被移除子树的每个节点**前序调用一次 | **不再可达。** 用例 `removal.detach_reaches_every_node_of_the_removed_subtree` 专门覆盖"指针指向孙子、移除其父"这一最易写漏的形态 |
| 2b | **`Window::handleMouse` 跨嵌套派发持有局部 `target`**（审计当时未单列，加移除 API 后与第 1 条同族）：合成 Enter/Leave 与 `setFocusWidget` 都会跑应用代码，它们销毁 `target` 后本函数继续用 | 不再用局部量做载体：`hovered_` / `pressGrab_` 是 `widgetDetached` 维护的指针，Enter/Leave 与焦点切换之后从它们**重新读回** `target` | **不再可达。** 用例 `removal.an_enter_handler_may_remove_what_the_pointer_is_over` / `removal.a_focus_out_handler_may_remove_what_was_just_clicked` |
| 3 | **三处「只关不断连」**（`SelectBase` / `MenuButton` / `DatePicker` 订阅归 Window 拥有的弹层） | 三者各加一个声明在**最后**的 `ConnectionScope conns_`，析构时先 `clear()` 再关弹层 | **不再可达。** 控件先于 Window 死不再在 `PopupList` / `CalendarView` 里留下捕获野指针的槽 |
| 4 | `batchDrained` 死信号 | REQ-8 删除信号与发射点 | emit 点总数 **72 → 71** |

关于第 1 条为什么只比对"当前节点"就够：**离开树的永远是一整棵子树**。若冒泡链上任一祖先要走，则站在其下的当前节点必然一起走。所以"剩余路径不安全" ⟺ "**当前**节点在被移除集合里"——一次指针比较，不需要向上搜祖先。这一推理是该修法成立的全部依据，改动它之前请先推翻它。

### 二、仍然不可达：27 个 B 判定中被 D7 挡住的部分

上表 27 个 B 判定的形态是"**槽销毁了发射方宿主**"。这在契约上仍然是 **D7 违约**：`Signal::~Signal` 的 `assert(!isEmitting())` 会在 Debug 构建当场抓住。

所以这 27 个点的正确读法是：**它们不是"可达路径"，而是"违约后的爆炸半径"**。阶段 3 没有、也不打算让它们变得可达。**判定不变，风险等级不变。**

### 三、仍然可达，且**本阶段新引入**：槽的接收者死在自己的槽里

这是本阶段唯一真正新增的高危类别，也是本文最重要的输出。形态：

> **发射方宿主活着**（所以不违反 D7），但**槽所属的对象**在槽内被销毁，而槽体在那次调用之后**还有语句**。

`ConnectionScope`（风险 1 的第 3 条）治不了这个——它治的是"控件死后信号还在发"，治不了"控件死在信号正在发的时候"：`Signal::emit` 用 `shared_ptr` 钉住了 callable，所以**从 lambda 返回**是安全的，但 lambda 体里此后每一次触碰 `this` 都是 UAF。

库内确定可达的三处（全部是"弹层归 Window、槽归控件"这一形状）：

| 发射点 | 槽 | 危险语句 |
|---|---|---|
| `src/widget/PopupList.cpp:104` / `:307` `rowActivated` | `SelectBase::ensurePopup` 的 lambda | `onRowActivated(row);` 之后还有 `if (closeOnActivate()) close();` |
| `src/widget/PopupList.cpp:104` / `:307` `rowActivated` | `MenuButton::ensureMenu` 的 lambda | `trigger(row);`（其中发出 `triggered`）之后还有 `closeMenu();` |
| `src/widget/DatePicker.cpp:192` `dateChosen` | `DatePicker::open` 的 lambda | `setDate(d);`（其中发出 `dateChanged`）之后还有 `close();` |

触发路径举例：操作员点下拉列表的一行 → `PopupList::rowActivated` → `SelectBase` 的 lambda → `onRowActivated` → 应用的 `currentIndexChanged` 槽 → 应用 `page->clearChildren()` 切画面 → 该 `SelectBase` 被销毁 → lambda 返回后执行 `close()` → **UAF**。

`SelectBase` 的 `rowToggled` / `expanderToggled` 两个 lambda 只有单条语句，返回即结束，**不在此列**。

**为什么本轮不修**：修法要么是弱 widget 句柄（新机制），要么是把"关闭弹层"的责任从控件挪到 `PopupList` 自身（跨 5 个禁改文件的重构）。施工图明确要求本轮只交付移除能力、不改造既有的"隐藏而非销毁"变通——同一轮里既造 API 又用 API 会放大爆炸半径。**这三处是 R4/R5 的第一优先级输入。**

### 四、应用侧仍然可达的高危点

上表 16 个 C 判定里，凡是"应用在槽里销毁**别的**子树"的写法，现在都是合法且可达的。冒泡循环（风险 1）已经被保护，Window 的观察指针（风险 2）已经被保护，但**调用栈上属于被销毁子树的中间帧仍然不受保护**——那正是第三节描述的类别，只是接收者换成了应用自己的对象。

给应用的一句话规则，建议写进控件文档：

> 在槽里销毁 widget 之后，**立刻 return**。不要在同一个槽里"先切画面、再更新几个控件"。

### 五、本阶段没有覆盖的点

- **无 Window 的游离树**：`removeChild` 在没有 Window 的子树上照常工作，冒泡游标也照常取消（游标链是进程级静态，不依赖 Window），但没有 `widgetDetached` 可发——游离树上本来也没有观察指针。
- **控件持有的弹层反向悬垂**：`SelectBase::popup_` / `MenuButton::menu_` / `DatePicker::calendar_` 是裸指针，指向 Window 的子节点。如果应用对 Window 调用 `removeChild(那个弹层)`，这三个指针会悬垂，且 `Window::widgetDetached` 无从通知它们。库内无人这样做，**但移除 API 使它第一次成为可写出来的代码**。
- `emit` 实参是成员引用（上一节第 2 条）：未修，仍然成立。

---

## 阶段 4 复核：R1 门禁整改（安全门 FAIL + QA 门 FAIL 的处置）

R1 的安全门与 QA 门都判 FAIL。本节记录本轮逐条处置、以及**明确推迟到 R2** 的条目。全部改动为行级，未引入新机制、未改架构。

### 一、本轮修掉的（安全门）

| # | 原判 | 修法 | 固化用例 |
|---|---|---|---|
| A1 | **S2 高**：`~Widget` 不取消冒泡游标。取消只发生在 `announceDetached`，即只覆盖移除 API；而 widget 还能经 `unique_ptr` 释放、栈上 Window 出作用域、任何子类析构等途径死亡 | `~Widget` 从头文件的 `= default` 移到 `Widget.cpp` 定义，体内 `cancelBubblesOn(this)`。后代无需遍历：`children_` 紧随其后销毁，每个后代都会各自走到这里 | `removal.a_slot_may_destroy_the_bubble_subtree_without_the_removal_api` |
| A2 | **S1 中**：`DepthGuard` 持 `SignalBlock&`，而 signal 的 `block_` 是唯一强引用。D7 违约时 `~Signal` 释放控制块，栈上 `~DepthGuard` 仍 `--emitDepth` → 确定性 4 字节 write-after-free；Release 下 assert 已被编译掉，这次静默堆写就是违约第一现场 | `DepthGuard` 改持 `shared_ptr<detail::SignalBlock>`。代价：每次 emit 一对原子增减，**不新增分配**（`signal.emit_allocates_nothing_within_the_inline_capacity` 仍为 0） | 无直接用例（见"未验证点"） |
| A3 | **S3+S4 中**：`announceDetached` 按下标遍历活动 vector。重读 `size()` 只防越界、不防**索引平移**——槽删掉靠前的兄弟后，其后整棵子树永不被通告；另外通告后无条件 `node->children()`，而槽可以合法地把 `node` 从别处移除 | 通告后先 `stillAChild(parent, node, hint)` 证明 `node` 还在，再对子列表**取快照**并逐个复检；`stillAChild` 带位置提示，未变动时是一次指针比较，只有真被改动时才退化为扫描。叶子节点不取快照、不分配 | `removal.detach_still_reaches_a_sibling_that_a_slot_shifted_down`（确定性）、`removal.detach_survives_a_slot_that_removes_the_node_being_announced` |
| A4 | **S6 高**：`MenuButton::trigger` 跨 `triggeredIndex.emit` 持 `const MenuItem&`，槽内 `setItems()` 重分配即悬垂 | emit 前按值取出 `const std::string id` | `popup_lifetime.menu_trigger_survives_set_items_from_its_own_slot` |
| A5 | **S12+D1 高**：`GWLP_USERDATA` 从不清除，`~Win32Window` 的 `DestroyWindow` 会同步回调 wndProc | `~Win32Window` 在 `DestroyWindow` **之前**清空 `GWLP_USERDATA`；另加 `WM_NCDESTROY` 分支，覆盖"窗口被别人销毁"的路径 | 无自动用例（见"未验证点"） |
| A6 | **高**：三处"先派发、后关闭"的 lambda | 语句对调为**先关闭、后派发**，使 emit 链成为 lambda 的最后一条语句。`SelectBase` 的两条键盘路径同形，一并对调，否则鼠标与键盘会给应用两种事件顺序 | `popup_lifetime.*_closes_before_it_dispatches*`（4 个） |
| A7 | **S8 低**：`MSG msg;` 未初始化，`GetMessageW` 返回 -1 时读未初始化栈 | `MSG msg{}`，并把 0（WM_QUIT）与 -1（队列出错）分开处理 | 无自动用例（需真实消息循环） |

**A6 的行为变更（需要写进控件文档）**：应用先收到"弹层已关闭"，再收到"已选中 / 已触发 / 日期已变"。这是刻意的取舍——安全 > 事件顺序美观。

### 二、本轮修掉的（QA 门）

- **B1 Debug 构建**：新增 `build-debug.bat`（独立的 `build-debug/` 目录，与 Release 树并存）。此前 `build.bat` 硬编码 Release，`Signal.hpp` 的 `assert(!isEmitting())` **从未被编译进任何二进制**，D7 至今零次执行。新增负向用例 `d7.destroying_the_signal_owner_from_a_slot_is_caught_in_debug`：以 `GEEYOOU_D7_SUICIDE=1` 重跑自身，Debug 下子进程必须**死在该断言上**（实测退出码 3，stderr 打印 `Assertion failed: !isEmitting() && "Signal destroyed from inside its own emit()"`），Release 下子进程必须走到底并以 42 退出——**后者不是通过，而是本套件白纸黑字承认 Release 不执行 D7**。
- **B2 裁剪继承基线**：新增 `shape/clip_inheritance`——父 100×100、子几何 (50,50,200,200) 溢出父边界。此前所有 golden 场景都是"根铺满画布、子控件全在父内"，`mine ⊆ clipInWindow` 时交集是恒等运算，删掉 `intersected(clipInWindow)` 零像素差。实测注入后 **14400 像素不符，首个在 (120,70)**（正是父的右边界）。
- **B3 第二窗口生命周期**：此前全仓无任何测试调用 `enableAnimations()`，`~Window` 的 `stopTimer(animationTimer_)` 每次都走 `stopTimer(0)` 早退分支，该修复的实际覆盖率为 0。新增 `timer.a_second_windows_animation_clock_dies_with_the_window`。
- **B4 I1 确定性断言**：新增 `signal.disconnect_removes_exactly_one_slot`，从**中间**断开并断言 `size()` 精确递减 1、其余槽照常到达——原本 I1 只能靠迭代器失效崩溃被间接发现。
- **测试基础设施**：`main()` 现在关掉 stdout 缓冲（崩溃跑也能看到最后一个用例名），并把 CRT/STL 断言从**模态对话框**改到 stderr（Debug 下无人值守跑不再挂死）。`signal.emit_*` 两个分配计数断言在 `_ITERATOR_DEBUG_LEVEL != 0` 时改为打印 `[skip]` 说明——MSVC 调试迭代器每个容器自带一次代理分配，Debug 下测的是 STL 记账而非 `emit` 的分配画像；Release 门禁强度不变。

### 三、O6 计数口径（CTO 裁定，仅记录，不改代码）

**按类计数，不按成员计数。** 新增公共类型算 1（`ConnectionScope` = 1、`TimerId` = 1），类内新增成员不单独计。按此口径当前 = 12，压线通过。

### 四、明确推迟到 R2（本轮不修）

- **S5 弹层永不回收**：`SelectBase::popup_` / `MenuButton::menu_` / `DatePicker::calendar_` 由 Window 拥有且从不移除，控件反复创建即单调增长。
- **S7 异常穿越回调**：`emit` / 冒泡 / `announceDetached` 都不是异常安全的；一个抛出的槽会跳过 `Window` 的指针清理。
- **S10 递归无深度上限**：`paintTree` / `hitTest` / `announceDetached` / `collectFocusable` 均按树深递归，深树可爆栈。
- **S13**：同批留档。
- 上一节遗留的 **`emit` 实参是成员引用**、以及 **控件持有的弹层反向悬垂**，仍然成立。

### 五、A6 之后**仍然**残留的窗口（R2 输入）

对调语句把危险面从"大概率"压到"小概率"，但没有消灭它：`close()` / `closeMenu()` 内部的 `Window::closePopup()` 会发 `popupClosed`，`SelectBase::close()` 末尾还会发 `openStateChanged(false)`。若应用在**这两个信号**的槽里销毁该控件，随后的 `onRowActivated(row)` / `trigger(row)` 仍是 UAF。

两种顺序都存在窗口，本轮选的是窗口更窄的那个：应用在"选中值"的槽里切画面是常见写法，在"弹层已关闭"的槽里销毁打开它的控件则罕见。**彻底消除需要弱 widget 句柄或把关闭责任下沉到 `PopupList`**，即第三节所述的 R4/R5 方案。
