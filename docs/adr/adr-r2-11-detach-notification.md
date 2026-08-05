# ADR-R2-11：容器持有的树内裸指针，在子树离场时由 `Widget` 统一通知失效

> 轮次：R2（布局引擎轮）· 状态：**已采纳，已实现（E14/E15/E16），门禁三条腿全绿**
> 落点：`Widget::onDescendantDetached` / `detail::notifyDetachToAncestors`（`Widget.hpp` / `Widget.cpp`）、`ScrollArea` 与 `AppWindow` 的 override
> 施工与证据记录：`../iterations/02-layout-engine.md` §11.11（机制）、§11.12（三个容器兑现）、§11.13（`AppWindow` 补漏）
> 配图：[`adr-r2-11-detach-notification-sequence.puml`](adr-r2-11-detach-notification-sequence.puml)（**源已入库，尚未渲染**，见文末）

---

## 1. 背景

**这一条能力早在 R1 就存在，只是没开放给普通容器。**

`Window` 持有四个指向树内节点的裸指针——`focus_` / `hovered_` / `pressGrab_` / `popup_`。R1 就为它们建了摘除通知：`Window::widgetDetached(Widget*)`，由 `Widget` 的移除 API 在**解链之前**、**按离场子树的每一个节点**调用一次。`Widget.hpp` 的注释把"为什么按节点而不是只按根"写死了：

> *Per node rather than just at the root, because those pointers routinely name a grandchild: a root-only notification would leave the window dereferencing freed memory on the very next event.*

**同一份能力从未开放给普通容器。** 而普通容器缓存的指针是同一个形状、同一个危险：

| 容器 | 成员 | 它指向哪儿 |
|---|---|---|
| `ScrollArea` | `viewport_` / `content_` | `content_` 是 `viewport_->add<Widget>()` ⇒ **孙子** |
| `AppWindow` | `header_` / `content_` / `fill_` | `fill_` 是 `content_->add<T>()` ⇒ **孙子** |
| `Shell`（examples） | `pageArea_` / 每页一个 `Page::host` | 同样是**孙子** |

**三个真实容器，三次都是孙子。** R2 的安全复核在这一族上连续复发五次，第五例正是"守卫救不了的那一半"：门守卫是**帧作用域**的，它保证**这一帧**安全返回，**它不修复对象状态**。`ScrollArea::content_` 在守卫触发之后仍然是悬垂成员，**下一次重绘就炸，不需要应用再调任何方法**（REM3-RES-1）。

同时，`AppWindow::relayout()` 开头的 `if (!header_ || !content_) return;` 从这个类写出来那天就在源码里，而**库里没有任何一行代码能满足它**——**契约早就承认了这个状态，实现从没兑现过。**

---

## 2. 决策

新增一个**默认空实现**的钩子：

```cpp
// Widget.hpp, protected
virtual void onDescendantDetached(Widget* node) {}
```

在 `announceDetached()` 处理**每一个**离场节点时，向该节点当时的**整条祖先链**广播，**无条件**：

```cpp
// Widget.cpp, detail::notifyDetachToAncestors
for (Widget* a = node->parent(); a; a = a->parent()) a->onDescendantDetached(node);
```

**位置是承重的，四条一起读**（时序图画的就是这四条）：

1. **在函数体的第一条语句**——早于 `win->widgetDetached(node)`，早于任何解链；
2. **早于 `widgetDetached`**：那一句会跑应用代码（`closePopup` → `popupClosed` 槽），祖先可能死在里面。**先广播就不必为广播论证祖先存活**；
3. **早于解链**：此刻整棵离场子树与整条祖先链**全部活着且完整**，`node == content_` 这种指针比较绝对安全；
4. **无条件，不受 `win != nullptr` 约束**：没挂窗口的 `ScrollArea`（测试里到处都是）有一模一样的悬垂成员。`Window` 是焦点/悬停的观察者，这一条是 **widget 修复自己**。

**不新增遍历**：广播骑在 `announceDetached` 已有的那一趟前序遍历上。

**访问控制**：`announceDetached` 是 `Widget.cpp` 匿名 namespace 的自由函数，够不到 protected 的钩子，而它自己是内部链接、无法在头文件里被 `friend`。所以广播抽成 `detail::notifyDetachToAncestors(Widget*)`（头文件声明、cpp 定义），由 `Widget` friend 它——**与 `Layout` friend `detail::parkLayout` 是同一个形状、同一个理由**，不是新发明。

---

## 3. 不变量：为什么"这一扇门"是全部的门

本决策的正确性完全建立在一条可独立复核的不变量上：

> **一棵子树离开一棵仍然活着的树，唯一的门是 `Widget::takeChild → announceDetached`。**

两条 grep 就是证明（**请自己跑，不要照抄**）：

```
$ grep -rn "children_.erase" include src tests examples
src/widget/Widget.cpp:561:  children_.erase(children_.begin() + std::ptrdiff_t(index));
（另外两处命中是注释里引用这条不变量的文字，不是语句）

$ grep -rn "parent_ =" include src
include/geeyoou/widget/Widget.hpp:84:    raw->parent_ = this;        <- add<T>
src/widget/Widget.cpp:562:  owned->parent_ = nullptr;              <- takeChild
（另外三处是成员声明本身，以及 setGeometry 里的比较与断言）
```

⇒ `children_.erase` 全库**唯一**出现在 `takeChild`；`parent_` 的**赋值**只在 `add<T>`（进）与 `takeChild`（出）。除此之外，持有者与被持有者**必然同生共死**。

**Leo 已独立扩面复核**，把候选集从"我提出的那两条 grep"扩到了整族容器改动原语——`swap` / `clear` / `pop_back` / `resize` / `insert` / `emplace_back`——在 `children_` 上**零命中**（唯一的 `push_back` 在 `Widget.hpp:84` 的 `add<T>`，那是进场不是离场）。**这一步不是形式主义**：只按我给的两条 grep 复核，等于用被复核者选定的候选集去复核，而"候选集写窄了"正是本轮方法学栏里记了两次的失败模式。

**推论：`~Widget` 不需要这个钩子。** 在那里发通知，等于告诉一个正在被释放的对象"你的成员要被释放了"。子孙无需遍历：`children_` 在析构体之后销毁，每个子节点自己到达自己的析构。

---

## 4. 拒绝的方案（逐条给决定性理由）

| 方案 | 决定性理由 |
|---|---|
| **可失效句柄**（弱引用 / 代际句柄替换裸指针） | 撞 `sizeof(Widget)` 预算（R2 有 `static_assert` 守着），并且**引入第二真相源**——句柄表与 `children_`/`parent_` 从此可以不一致，而"两份真相"是本缺陷族的成因而不是解法。 |
| **隐藏内部子节点**（`viewport_` / `content_` 不进 `children_`） | 要动全库所有权不变量的**五条遍历**（paint / hit-test / focus 收集 / 几何传播 / 析构），而且**覆盖不了 `AppWindow::fill_`**——`fill_` 是应用自己 `setContent<T>` 放进去的，按定义就是普通子节点。付了最大的代价，还留着一个洞。 |
| **每次现推指针**（`content()` 改成 `children_[0]->children_[0]`） | **会静默指向别的 widget**。摘掉内容之后下标还在，答的是另一个对象。这正是本轮反复拒绝的那笔交易：把"会崩"换成"会静默答错"。 |
| **复用 `childRemoved`** | 两处硬伤：(a) `childRemoved` 被 `if (detail::g_layoutHosts != 0)` 门控调用，而本通知必须**无条件**触发（RES-1 与 Layout 无关），复用等于破坏 **ADR-R2-01** 的门控；(b) 会把"通知我的 Layout"与"修复我自己的状态"揉进同一个虚函数，于是**新增一种失败模式：子类忘了调基类 ⇒ 布局静默失效**。默认空实现的新钩子没有"忘记调基类"这个陷阱。 |
| **只通知直接父节点** | **漏掉三个孙子指针，等于第五次复发。**（见下，这是唯一一条有判别性实验背书的拒绝理由。） |

### 4.1 "只通知直接父节点"——Leo 的 B2 判别性实验

**不是论证，是实测。** 广播**保留**、只把它收窄成 `node->parent()->onDescendantDetached(node)`，其余一字不动：

> 2 条用例 FAIL，**同一条 heap-use-after-free 原样出现**（`CachingBox::onPaint` ← `Widget::paintTree`，free site 是 `Widget::removeChild`），退出码 **2**。

对照的另一组（**B**：把广播整行删掉）给出的是同一个结果——**这正是判别性实验的意义**：B 只能证明"需要某种通知"，**B2 才证明"必须广播整条祖先链"**，因为被缓存的指针是孙子。只通知直接父节点会发给 `viewport_`（它什么都不缓存），**永远发不到 `ScrollArea`**（它缓存一切）。

---

## 5. 代价

| 项 | 值 |
|---|---|
| `sizeof(Widget)` | **0 字节/widget**。虚函数只让 vtable 多一个槽，vtable 是每类一份的静态数据；`Widget` 本来就有 `virtual ~Widget()`，vptr 早就在了。**编译期探针量过：有钩子 200 字节，把钩子整个删掉 200 字节。** |
| 分配 | **0** |
| 新遍历 | **0**（骑在已有的前序遍历上） |
| 每个离场节点 | 一次 **O(depth ≤ 64)** 的指针追逐 + 每层一次**默认空体**的虚调用 |
| 付在哪里 | **只在结构性变更路径上**。绘制、布局、事件路径**一条指令不加**。 |

`kMaxTreeDepth = 64` 是库里既有的上限（`Widget.cpp` 的 M4），所以"O(depth)"有硬上界。

---

## 6. 本决策新增的两条规则

**REM3-G9 —— 钩子内禁跑应用代码。**
`onDescendantDetached` 的重写**只准把自己的成员指针置空**，不得 `update()`、不得 `emit()`、不得改树、不得虚调用。理由是这个钩子跑在 `announceDetached` **半解链的树**上，且那趟遍历持有的是**钩子运行之前**拍下的快照。

执行形态：`takeChild` 里一条 **Debug assert**（`assert(!g_inDetachNotify)`）。

> ⚠️ **这条 assert 只拦得住 `takeChild` 一条路，理由是"够不着"，不是"不值得"。** `g_inDetachNotify` 是 `Widget.cpp` 匿名 namespace 的内部链接符号，而 `Signal::emit` 在 `core/`，core 不许依赖 widget。**从钩子里 `emit()` 出去同样内存不安全**，它只是没有任何机器看得见。反证：`update()` / `setGeometry` / `setVisible` / `invalidateSizeHint` / `performLayout` 全在同一个 TU，各加一条同样的 assert 只要 Debug 下一次布尔测试——**不加是对的，因为它们不内存不安全**。取舍按危害写，不按成本写。完整登记见 `../iterations/02-layout-engine.md` §11.11 未验证栏第 3/6/7 条。

**REM3-G7 —— 每一个指向树内节点的成员裸指针，都必须有一条使其失效的通知路径；没有路径的指针不许存在。**
这是本 ADR 的**一般化**：`Window` 的四个指针有 `widgetDetached`，容器的指针有 `onDescendantDetached`，此后任何新的树内裸指针成员在写下来的同时就要回答"谁让它失效"。这条规则是给下一个人的，不是给这一轮的。

---

## 7. 时序图

`adr-r2-11-detach-notification-sequence.puml`，泳道 = 应用 / `Widget::takeChild` / `announceDetached` / `ScrollArea` / `Window`，画清

> **广播 → `widgetDetached` → 解链 → `childRemoved` → 销毁**

的**先后顺序**——**顺序在本决策里是承重的**（§2 的四条位置理由全部是关于顺序的），所以它值一张图而不是一段话。

⚠️ **本机 `plantuml.jar` 未安装。`.puml` 源入库即可，渲染在安装后补；未渲染前不得宣称"已出图"。**

---

## 8. 实现与验收的落点

| 内容 | 位置 |
|---|---|
| 机制、完备性证明、红态 B/B2 | `../iterations/02-layout-engine.md` §11.11 |
| 三个容器的降级语义、B7 成员重读、门禁 | 同上 §11.12 |
| `AppWindow::maximizedChanged` 判空 | 同上 §11.13 |
| 用例 | `tests/widget/test_removal.cpp`（2 条）、`tests/widget/test_detach_notify.cpp`（8 条） |
| 已知残留（**本 ADR 不掩盖**） | §11.11 未验证栏第 6/7 条：`announceDetached` 的 `DeathWatch` 注册在广播**之后**、广播循环自身无守卫。**两条都 S2/W2，两条都在本决策自己的爆炸半径里。** |
