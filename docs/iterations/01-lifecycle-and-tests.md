# 迭代 01：生命周期与测试网

> 本文件目前只包含 **REQ-6 emit 点审计** 一章，其余章节由后续提交补齐。

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

### 清单

#### `src/hmi/`

- [x] `src/hmi/AlarmList.cpp:109` `alarmAdded.emit(*added)` — **B**：其后 `return nextId_ - 1;` 读成员。
- [x] `src/hmi/AlarmList.cpp:125` `alarmAcknowledged.emit(*r)` — **B**：其后调 `rebuildVisible()`。
- [x] `src/hmi/AlarmList.cpp:135` `alarmAcknowledged.emit(r)` — **B**：处在遍历 `ring_` 的循环体内，`r` 本身就是成员容器的引用。
- [x] `src/hmi/DataHub.cpp:40` `sampleArrived.emit(s)` — **B**：循环继续遍历成员 `scratch_`，并在下一轮读 `channels_`。
- [x] `src/hmi/DataHub.cpp:42` `batchDrained.emit(n)` — **A**：其后 `return n;`，`n` 是局部量。
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

2. **`emit` 实参是成员引用时，槽内改写对后续槽可见。**
   `LineEdit.cpp:476` / `TextArea.cpp:530` 的 `editingFinished.emit(text_)` 用的是 `Signal<const std::string&>`，实参直接是成员。第一个槽调用 `setText()` 后，第二个槽读到的是**新值**。不是内存错误，但与"发射时的快照"这一直觉相反，值得在控件文档里点一句。
