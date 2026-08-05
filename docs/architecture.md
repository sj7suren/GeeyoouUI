# GeeyoouUI 架构设计

面向**工控 HMI / 上位机**的跨平台 C++ 自绘控件库。

| 维度 | 决策 |
|---|---|
| 渲染 | 自绘，Blend2D（CPU JIT 光栅化） |
| 平台 | 抽象层三平台预留，v1 只实现 Win32 |
| 语言 | C++20，MSVC / Clang / GCC |
| 元对象 | **无 moc**，信号槽用模板 + `std::function` |
| 重绘 | 脏矩形增量重绘（非全帧重绘） |
| 领域 | 工控 HMI：仪表、实时曲线、状态指示、参数表单 |

---

## 1. 为什么工控 HMI 的架构和通用 UI 库不同

这是整个设计的出发点，其余决策都由它推导：

1. **界面绝大部分是静止的。** 一屏 HMI 上通常只有曲线区和几个数值在变，其余 90% 的像素每帧完全相同。通用 UI 库（尤其 GPU 路线）默认全帧重绘，因为 GPU 便宜；CPU 光栅化下全帧重绘 1920×1080 是不可接受的浪费。→ **脏矩形是架构级要求，不是优化。**

2. **必须长时间无人值守运行。** 上位机开机跑几个月。任何每帧分配、任何缓存无上限增长，几周后就是事故。→ **热路径零分配**，实时数据用固定容量环形缓冲。

3. **帧率的稳定性比峰值更重要。** 60fps 偶尔掉到 20fps，在监控曲线上表现为肉眼可见的卡顿和数据错位感。→ 宁可稳定 30fps，不要抖动的 60fps。

4. **数据源是异步的。** 串口 / Modbus / CAN / OPC UA 在别的线程产生数据。→ **UI 线程与数据线程必须有明确边界**：数据线程只投递到队列，绝不直接碰 Widget。

5. **控件谱系窄而深。** 不需要富文本编辑器、不需要 WebView，但仪表和曲线要做到极致。→ 砍掉通用性，把预算花在领域控件上。

---

## 2. 分层

```
┌─────────────────────────────────────────────────┐
│  examples/  hmi_demo  widget_gallery             │
├─────────────────────────────────────────────────┤
│  hmi/     StatusLed  Gauge  TrendChart           │  领域控件
├─────────────────────────────────────────────────┤
│  widget/  AppWindow  WindowHeader（窗口层 / 自绘chrome）│ 窗口外壳
│           Widget  Window（树 / 焦点 / 键盘 / 动画）│  通用控件与树
│           Layout  BoxLayout  GridLayout           │  布局引擎（可选，见 §3.12）
│           Label PushButton IconButton CheckBox   │
│           RadioButton ToggleSwitch Slider        │
│           ProgressBar GroupBox Separator SpinBox │
│           LineEdit PasswordEdit SearchBox        │
│           TextArea                               │
│           PopupList SelectBase ComboBox          │
│           SearchableSelect MultiSelect TreeSelect│
├─────────────────────────────────────────────────┤
│  render/  Painter（Blend2D 门面） VectorPath      │  渲染与视觉
│           Theme  Skin  StyleSheet                 │  主题 / 皮肤 / 样式
│           Icon  IconRegistry                      │  内置图标 + 注册扩展
├─────────────────────────────────────────────────┤
│  core/    Types Signal Event（Key） Utf8          │  无依赖基础设施
│           TagId  TagRegistry                      │  位号标识（R2 交付，尚无调用方）
├─────────────────────────────────────────────────┤
│  platform/  Platform / PlatformWindow (纯虚)      │  ← 移植边界
│             └─ win32/  x11/(TODO)  cocoa/(TODO)  │
└─────────────────────────────────────────────────┘
```

> `Theme` 放在 `render/` 而不是 `hmi/`：`widget/` 和 `hmi/` 都要用它，而 `widget/` 在 `hmi/` 下方——放 `hmi/` 会逼得每个通用控件向上依赖领域层。

**依赖方向严格向下**。`core/` 不 include 任何上层，也不 include Blend2D 和 `<windows.h>`。

### 移植边界有多窄

新平台只需实现两个接口共约 12 个方法：给一个窗口、一块可写像素缓冲、一串输入事件。上面所有代码不需要改动一行。

```cpp
class PlatformWindow {
  virtual Size   clientSize() const = 0;    // 逻辑像素
  virtual float  scaleFactor() const = 0;   // DPI 缩放
  virtual void   invalidate(const Rect&) = 0;
  // 无边框窗口需要的窗口命令
  virtual void   minimize() = 0;
  virtual void   maximize() = 0;
  virtual void   restore() = 0;
  virtual bool   isMaximized() const = 0;
  // onPaint / onMouse / onKey / onResize / onClose 回调
  // onHitTest：问上层「这个像素是标题区还是内容区」
  // onWindowStateChanged：最大化 <-> 还原
};
class Platform {
  virtual std::unique_ptr<PlatformWindow> createWindow(..., const WindowOptions&) = 0;
  virtual int  runEventLoop() = 0;
  virtual void quit(int) = 0;
};
```

---

## 2.5 窗口层：无边框窗口与自绘 chrome

`AppWindow` 是应用窗口基类，默认**无边框**：Windows 不绘制标题栏、边框和主题色，标题栏由 `WindowHeader` 用同一套 `Painter` / `Theme` 画出来。这样同一份组态画面在 Win7 面板机、Win11 工作站上外观完全一致，不会因为 IT 换了桌面主题而变样。

### 为什么必须动 platform 层

自绘标题栏不是控件问题，是**窗口管理器**问题：

| 能力 | 谁决定 | 自己用鼠标事件模拟的后果 |
|---|---|---|
| 拖动移动窗口 | `WM_NCHITTEST` 返回 `HTCAPTION` | 拖动有延迟、跨 DPI 显示器会跳 |
| 贴边分屏 / Snap Layouts | 同上 | 完全没有 |
| 双击最大化 | 同上 | 要自己判定双击间隔 |
| 右键系统菜单 | 同上 | 完全没有 |
| 边框拖拽改大小 | 同上返回 `HTLEFT` 等 | 光标形状不对、抖动 |

所以 `PlatformWindow` 增加了一个 `onHitTest` 回调：**上层回答"这个像素是标题区还是内容区"，其余全部交还给系统**。窗口层只负责"报告哪里是标题区"，一行拖动逻辑都不用写。

### Win32 实现的三个关键点

1. **保留 `WS_OVERLAPPEDWINDOW`，不改成 `WS_POPUP`**。窗口仍是普通受管顶层窗口——贴边分屏、最小化动画、Alt+Tab 缩略图、任务栏预览全都还在。`WM_NCCALCSIZE` 直接 `return 0`，客户区吃掉整个窗口矩形，系统只是**不画**而已。
2. **最大化时要手工内缩**。最大化的窗口尺寸是「工作区 + 边框」，本假设边框画在屏幕外；边框被吃掉后那部分变成真实像素，会挂到显示器外面。所以 `WM_NCCALCSIZE` 在 `IsZoomed()` 时按 `SM_CXFRAME + SM_CXPADDEDBORDER` 缩回去。
3. **`DwmExtendFrameIntoClientArea` 留 1px** 才有系统投影——DWM 只给「有边框」的窗口画阴影。这 1px 永远看不见：Blend2D 在整个客户区写的 alpha 都是 255。

### 顺带修掉的一个老 bug

以前没有 `TrackMouseEvent` / `WM_MOUSELEAVE`：鼠标滑出窗口时，最后 hover 的控件永远停在高亮态。以前不明显，现在标题栏按钮就贴在窗口边缘，一拖窗口关闭按钮就会一直亮着红色——所以一并补上，`MouseAction::Leave` 现在也会从平台层发上来。

---

## 2.6 样式层：主题 / 皮肤 / 类 QSS 选择器

三层，能力递增，**下层能干的事绝不交给上层**：

| 层 | 是什么 | 覆盖面 | 代价 |
|---|---|---|---|
| `Theme` | token 结构体（21 个颜色 + 圆角 + 字号） | **全部 32 个控件** | 一次结构体赋值 |
| `Skin` | 注册表里的 `Theme` + 一段样式表，按名字切换 | 同上 | 一次整窗重绘 |
| `StyleSheet` | 类 QSS 选择器 + 级联 | 已接入的控件（见下表） | 每控件每皮肤解析一次 |

### 为什么 Theme 层就已经够用了 80%

库里 **59 处取色全部走 `Theme::current()`**，而且是每次绘制现取、不缓存。所以换皮肤不需要逐控件通知、不需要失效遍历、不需要任何控件配合——`Theme::current() = 新的; 整窗 update()` 就完事了。这不是运气，是"token 结构体而不是每控件存一份颜色"这个决定的直接回报。

样式表只负责 token 表达不了的那部分：「这个厂要方角按钮」「这个急停键要单独变红」。

### 选择器子集

```
*                任意控件
PushButton       按类型，且**匹配子类**（IconButton / MenuButton 都吃这条）
.danger          按 style class
#emergencyStop   按 objectName
GroupBox Label   后代
A, B             分组
:hover :pressed :checked :focus :disabled :read-only :invalid :open :selected
```

特异度按 CSS 规则（id > class+状态 > 类型），同级按书写顺序，**逐属性决胜**——两条规则可以各自贡献一部分，这才叫级联。

状态匹配是**子集**判定而非相等：`:hover` 对「既 hover 又 focus」的控件仍然成立，否则单状态规则根本没法用。

### `@token`：样式表跟着皮肤走

```qss
PushButton:hover { border-color: @accent; }
```

`@accent` 在**解析时**对着当前主题求值。换皮肤 / 改主题色都会重新解析，所以一份样式表在四套皮肤下都成立，不用为每套皮肤写一份十六进制。

### 分层约束：`render/` 不能认识 `Widget`

级联要遍历祖先来判定后代选择器，但 `render/` 在 `widget/` **下方**。解法是在 `render/` 里声明抽象接口 `StyleSubject`（4 个虚函数：类型匹配 / class 匹配 / objectName / 父节点），由 `Widget` 实现。依赖箭头仍然向下。

类型选择器要认子类，又不想引入 RTTI，所以用宏在每个控件里织入一条虚函数链：

```cpp
#define GEEYOOU_STYLE_TYPE(Self, Base)                        \
  const char* styleType() const override { return #Self; }    \
  bool styleMatchesType(std::string_view n) const override {  \
    return n == #Self || Base::styleMatchesType(n); }
```

一次虚调用 + 若干字符串比较，没有 `typeid`、没有 `dynamic_cast`。

### 缓存：级联不能进热路径

`Widget` 存一个**单槽**缓存（`StyleProps` + 状态 + 全局代数）。控件一帧只用一个状态，单槽命中率接近 100%，用 map 反而比它省下的级联还贵。任何改动（换皮肤、改主题色、改 objectName / class）都只是 `bumpStyleGeneration()`，下次绘制惰性重算。

### 已接入样式表的控件

| 控件 | 生效属性 |
|---|---|
| `PushButton`（含 `IconButton` / `MenuButton` / `SplitButton` / `HeaderMenu` / `HeaderAvatar`） | accent / background / color / border-color / border-width / border-radius / font-size / icon-color |
| `Label` | color / font-size |
| `GroupBox` | background / border-color / border-width / border-radius / color / font-size |
| `LineEdit`（含 `PasswordEdit` / `SearchBox`） | background / border-color / border-width / border-radius |
| `CheckBox` / `RadioButton` / `ToggleSwitch` | accent / background / border-color |
| `Slider` / `ProgressBar` | accent / background / border-color / border-radius |
| `Separator` | color / border-width |
| `WindowHeader` | background / border-color / border-width / color / accent / icon-color |

**其余控件只跟随 `Theme`**，不吃选择器——这不是遗漏，是有意的：把 30 个控件的绘制代码全部改成属性查询，收益远小于风险。上表之外的控件想定制，改 `Theme` token 或提 issue。

### 优先级：代码 > 样式表 > 主题

和 Qt 反过来。理由是本库既有代码在到处 `setColor()` / `setAccent()`，若让样式表压过去，等于所有现存调用一夜之间变成"看运气"。所以规则是：**为某个实例显式指定的颜色，比为某一类控件写的选择器更具体**。

### 解析错误不抛异常

样式表是**内容**，不是代码。一处写错只丢那一条规则，其余照常生效，错误进 `StyleSheet::errors()` 供界面显示。跑着的工控画面不能因为工程师在设置页打错一个分号就白屏。

---

## 2.7 图标层：收集与扩展

### 原来卡在哪

`enum class Icon` 是**封闭**的，绘制是 `Icon.cpp` 里一个没有 `default` 的 `switch`，作画网格 `Grid` 藏在匿名 namespace 里。三条加起来 = 库外无法新增图标，也没有名字，因此无法枚举、无法做选择器、配置文件也没法写 `icon: "pump"`。

### 关键决定：`Icon` 从「枚举」变成「句柄」，而不是换类型

诱惑是把 `Icon` 换成类或字符串。**不要**。`Icon` 已经作为值类型穿过 19 个公开 API（`PushButton::setIcon`、`MenuItem::icon`、`PopupRow::icon`、`Sidebar::Item::icon`…）。只要注册表也产出 `Icon`，**所有现存控件零改动直接支持自定义图标**。

```cpp
enum class Icon : std::uint16_t {
  None = 0, /* …39 个内置… */ Moon,
  FirstCustom = 0x1000,   // 注册表从这里往上发 id
};
```

`drawIcon()` 开头加一次判断：id ≥ `FirstCustom` 就转注册表，否则落进原来的 switch。留 0x1000 的空档是为了以后加内置图标时**不用给任何人已存盘的 id 重新编号**。

### 作画网格必须公开，这才是真正的使能点

原来的私有 `Grid` 变成公开的 `IconCanvas`。不公开的话，外部作者得自己重新推导「单位→设备」映射、描边权重、以及「取能放下的最大正方形并居中」三条规则，还得三条都对。

`Icon.cpp` 里那 39 个 case **一行没改**：把私有 `Grid` 改成 `IconCanvas` 的薄适配器（`at` / `len` / `stroke` 转发过去），几何映射就只有一份来源，不会内置和自定义各算各的。

### 为什么 SVG 路径是性价比最高的录入方式

Lucide / Feather / Tabler / Material 全都是 **24×24 网格 + 2 单位描边 + 圆头圆角**——和本库的作画网格、`Icon.cpp` 现有约定**完全一致**。所以它们的 `d` 属性不需要换算，直接就能用。

而且 `Painter.cpp` 本来就在内部构造 `BLPath`（`strokeArc` / `fillArcRing` / `strokePolyline` 都用），Blend2D 又直接提供 `smooth_cubic_to` / `smooth_quad_to` / `elliptic_arc_to`——**`elliptic_arc_to` 收的就是 SVG 的端点参数化**，连圆弧转中心参数化那段最容易写错的数学都省了。所以这是个**解析器**，不是渲染器，约 200 行。

顺带一个反直觉的结论：**SVG 图标比手写图标画得更快**。路径在注册时解析一次存下来，之后每帧只是变换+描边；手写图标每次绘制都在重新构造几何。

### 分层：`render/` 依旧不认识 `Widget`

`VectorPath` 用 pimpl 把 `BLPath` 藏起来（和 `Canvas` 同一套做法），所以 GeeyoouUI 的使用者不需要把 Blend2D 放进 include 路径。库内有两个 TU 要看到 `Impl`（`VectorPath.cpp` 造路径、`Painter.cpp` 画路径），所以 `Impl` 定义放在私有头 `src/render/VectorPathImpl.hpp`——两处各定义一份即使内容相同也是 ODR 违规。

### 几条刻意的取舍

- **描边宽度要除回缩放**。`IconCanvas::path()` 用 `Painter::scale()` 把路径映射进方框，而 context 缩放会连描边一起放大。不除回去的话，14px 和 48px 的同一个图标会是**不同笔画粗细**——这是引入图标集时最显眼的翻车方式。
- **`PathStyle::Stroke` / `Fill` 必须由调用方给**。描边集（Lucide）和填充集（Material）混用会画成毛线或色块，而路径数据本身看不出是哪种。
- **重名注册保留 id**。替换画法但换 id，会让所有已经拿着旧句柄的控件当场变空白。
- **未注册的 id 画空白，不画占位框**。工控画面上，一个长得像真符号的替身比空白更危险。
- **解析失败不抛异常**，进 `errors()`。和样式表同一条理由：图标数据是内容。

### 没做的（本轮范围之外）

- **构建期代码生成**（SVG 目录 → 生成 `.cpp`）。运行时注册已经够用；真要零运行时解析再加，解析器可以复用。
- **图标库页面 / `IconPicker` 控件**。`IconRegistry::all()` / `categories()` 已经把地基留好了。
- **样式表 `icon: "name"` 属性**。需要 `StyleProps` 携带 `Icon`，是后续一小步。
- **光栅缓存**。图标每帧重画，但 HMI 是脏矩形增量重绘，图标很少进重绘区；等测出来是瓶颈再说。

---

## 3. 关键设计决策

### 3.1 逻辑像素 vs 物理像素

**规则：`platform/` 之上的所有代码只认逻辑像素，永远不接触物理像素。**

DPI 缩放只在两处发生，其余地方一律不感知：
- 输入：Win32 收到的物理坐标除以 `scaleFactor` 后再向上分发
- 输出：`Painter` 构造时对 `BLContext` 施加一次全局 `scale(dpr)` 变换

这样 Widget 的 `geometry()` 永远是设备无关的，跨显示器拖窗口时不需要任何重算逻辑。

> 这是**必须第一天就做对**的决策。见过太多项目先按物理像素写完，再回头补 DPI，最终布局代码里散落着 `* dpi / 96` 而且总有几处漏掉。

### 3.2 无 moc 的信号槽

Qt 的 moc 存在于 1990 年代 C++ 没有 lambda、没有 `std::function`、没有变参模板的年代。今天：

```cpp
Signal<double> valueChanged;                    // 声明即可，无需宏、无需代码生成
gauge.valueChanged.connect([&](double v){ ... }); // 任意可调用体
```

代价是失去了运行时反射（无法按字符串名字查属性），但 HMI 场景不需要 QML 那种动态绑定，这个交换是划算的——省掉整套构建期工具链。

**重入安全**：`emit` 时先拷贝槽列表再逐个调用，允许槽函数内部断开连接或销毁对象。

### 3.3 对象树：Widget 即树节点

不做 Qt 的 `QObject` 通用基类。父节点用 `std::vector<std::unique_ptr<Widget>>` 独占持有子节点，子节点持裸指针回指父节点。

**刻意的简化**：非可视对象（数据通道、定时器、协议解析器）**不进树**，由调用方自己管理生命周期。Qt 把一切塞进 QObject 树是为了 parent 自动析构，但代价是所有东西都被迫继承一个重基类。HMI 上位机的非可视对象通常本来就有自己的生命周期管理（连接池、采集线程），强行入树反而是负担。

### 3.4 脏矩形重绘

```
Widget::update(rect)
   → 转成窗口绝对坐标
   → 与窗口的累积脏区求并集（这里用 bounding box，不做 region 合并）
   → PlatformWindow::invalidate() → 平台触发一次重绘
   → 绘制时深度优先遍历，与脏区不相交的子树整棵跳过
```

**为什么用 bounding box 而不是精确 region**：精确 region（Qt 的 QRegion）需要矩形集合的并/交/减运算，实现和维护成本高。HMI 的脏区通常在空间上聚集（曲线区 + 旁边的数值），bounding box 的浪费很小。这是一个明确记录在案的取舍，若将来实测证明浪费显著，再升级为 region。

### 3.5 与 Blend2D 的耦合边界

`Painter` 是门面而非完全封装：提供 HMI 需要的图元（矩形、圆角矩形、圆、弧、折线、文本），同时开放 `raw()` 拿到 `BLContext&`。

**理由**：完全封装 Blend2D 的 API 面需要几千行且永远追不上，而完全暴露又让上层无法替换后端。折中方案是——**控件库自身只用 `Painter` 的门面 API**（保证后端可替换），用户代码若需要高级效果可以走 `raw()` 逃生口（自担后端锁定风险）。

### 3.5b 平台边界（C 轮已修正）

早期版本里 `PlatformWindow::onPaint` 的签名带着 `Painter`，Win32 后端自己构造 `BLContext`——**平台层在装配渲染器**，与本文档 §2 声称的"平台层只给一块像素缓冲"不符。已修正：

```cpp
// core/Surface.hpp —— 放 core/ 而非 platform/ 或 render/：
// 它是两层之间的契约，放进任一层都会让那层依赖另一层；放 core/ 则两层都
// 已依赖它，不新增任何依赖边。
struct Surface { void* pixels; int width, height; intptr_t stride; float dpr; };

std::function<void(const Surface&, const Rect& dirtyPhysical)> onPaint;
```

`render/Canvas` 负责把 `Surface` 绑到 Blend2D 并交出 `Painter`。结果：

| | 修正前 | 修正后 |
|---|---|---|
| 上行依赖 | `platform → render` | **无** |
| 含 blend2d 的文件 | `Painter.cpp` `Win32Platform.cpp` | `Painter.cpp` `Canvas.cpp`（均在 render/） |
| 新后端要写的 | 窗口 + **Blend2D 装配** | 仅窗口 |

X11/Cocoa 后端只需提供同样布局的像素缓冲（两者都能），完全不必知道 Blend2D 存在。

### 3.6 Blend2D 依赖风险（实测记录）

选定 Blend2D 后实测发现两件必须记录在案的事：

1. **Blend2D 和 AsmJit 都没有任何 release tag**，只有滚动的 master。所以"锁一个稳定版"这条常规做法不存在，只能钉 commit hash——见 `cmake/Dependencies.cmake`。
2. **master 正在做全局 camelCase → snake_case 重命名**：`fillRect` → `fill_rect`、`clipToRect` → `clip_to_rect`、`createFromFace` → `create_from_face`……而网上几乎所有教程和示例仍是旧写法。

这直接抬高了第 3.5 节那条规则的价值：**`Painter` 是全库唯一接触 Blend2D 命名的地方**（`src/render/Painter.cpp` + Win32 后端的 4 行）。上游再改一轮命名，受影响的只有这一个文件；如果当初让控件直接调 `BLContext`，这次就是几十处散弹式修改。

升级依赖的流程：改 `cmake/Dependencies.cmake` 里的 commit → 重新构建 → 只可能在 `Painter.cpp` 报错 → 用 `Select-String` 在 `_deps/blend2d-src/blend2d/core/*.h` 里查新名字。

### 3.7 焦点与键盘路由

控件层的地基不是"控件"，而是焦点。规则四条：

1. **`Key` 枚举定义在 `core/`，不是虚拟键码。** 控件里出现 `VK_SPACE` 的那天，就是 X11 后端报废的那天。平台层负责映射（`Win32Platform.cpp: mapVirtualKey`），上层只认 `Key::Space`。
2. **Tab 遍历由 `Window` 独占处理，键永不下发给控件。** 否则每个控件都得记得"把 Tab 传出去"，漏一个就困住焦点。
3. **Tab 顺序 = 构造顺序**（树的前序遍历），不提供 tab-index。固定布局的组态画面里，"你搭建的顺序"就是操作员的阅读顺序。
4. **禁用子树整棵跳过**：`collectFocusable` 遇到 `enabled_ == false` 直接返回，不递归。所以禁用一个 `GroupBox`，里面的控件既不可点也不会被 Tab 停留。

键盘事件与鼠标一样**向上冒泡**（焦点控件 → 各级祖先），让容器层能实现快捷键。

### 3.8 文本输入与中文 IME（v1 边界已修订）

原先 §4 写的是"v1 不做 IME"。加入输入框后这条必须重划——中文上位机的输入框不能打中文是不可接受的。**实际做到的层级**：

| 能力 | 状态 |
|---|---|
| 中文/日文/韩文输入 | ✅ 可用。IME 提交的文本经 `WM_CHAR` 到达，与普通按键同一条路径 |
| UTF-16 代理对合并 | ✅ 平台层缓存高位代理，凑齐后再上抛（emoji、CJK 扩展区） |
| 候选窗跟随光标 | ✅ `PlatformWindow::setImeCaret` → `ImmSetCompositionWindow`（`CFS_EXCLUDE`，空间不够时候选框翻到字段上方） |
| **内联预编辑渲染** | ❌ **未做**。组合中的拼音由 Windows 自己的 IME 窗口绘制，不是在我们字段里带下划线显示 |
| 候选列表自绘 | ❌ 未做，用系统的 |

**为什么光标定位这么要紧**：不调 `ImmSetCompositionWindow`，候选框会固定出现在窗口某个角落，操作员边打字边要把视线甩到屏幕另一头。这是"输入框感觉坏掉了"最常见的单一原因。

**为什么不做内联预编辑**：需要处理 `WM_IME_STARTCOMPOSITION` / `WM_IME_COMPOSITION` / `WM_IME_ENDCOMPOSITION`，自己渲染组合串与分段下划线，还要把候选窗与我们的排版对齐。这是独立一块工作，而系统 IME 窗口已经可用。

**光标必须落在码点边界**：`core/Utf8.hpp` 是唯一被认可的光标移动方式。退格删掉一个字节会把汉字劈成半个，产生再也修不回来的乱码——所以 `Backspace` 走 `utf8::prevBoundary`，`maxLength` 按**码点**计数而非字节。

### 3.9 弹层（overlay）

下拉框的真正门槛不是"下拉"，是 **`paintTree` 会把每个 widget 裁剪到自身边界**。`GroupBox` 里的下拉框弹出来会被父容器切掉——这不是样式问题，是架构问题。

**做法**：弹层是 **`Window` 的直接子节点**，而不是打开它的那个控件的子节点。
- 绘制：跳过正常子节点循环，**最后单独画**，因此覆盖一切
- 命中：**最先测试**，因此吃掉所有落在它身上的输入
- 关闭：`Esc`、`Tab`、失焦、点击外部

**为什么不用独立的 HWND**：Qt 用原生弹出窗口，弹层可以溢出到桌面。工控上位机基本全屏或 kiosk 运行，多一层平台表面不划算。代价是**弹层被限制在窗口内**——空间不足时向上翻转，而不是溢出屏幕。

**两个刻意的取舍**：
1. **关闭弹层的那一次点击被吞掉**，不透传给下面的控件。工控画面上，操作员只是想收起一个列表，不该顺带启动一台泵。
2. **点击弹层内部不改变焦点**，焦点留在打开它的控件上——这是键盘能继续驱动列表的前提。

**`PopupList` 是纯视图**：它不认识树、不认识过滤、不认识多选。每个下拉控件自己把模型压平成行，再交给它渲染。这个切分让**一个列表同时服务扁平单选、过滤搜索、复选多选和可展开树**，而不用为每种模式长一个标志位。渲染只画可见行，所以上万位号和十个位号一样快。

### 3.10 滚动与裁剪（B 轮修正）

**先纠正一处早期文档与代码不符**：本文档曾暗示子节点被裁剪到父节点。实际上直到 B 轮之前，`paintTree` 只把每个 widget 裁剪到**自身**边界——弹层必须挂 `Window` 的真正理由是 **z 序**与**命中测试**（`hitTest` 先检查父节点矩形，组外的点击到不了组内的子节点），不是裁剪。

滚动**必须**有真正的父子裁剪，所以现在 `paintTree` 携带一个累积裁剪矩形：

```
paintTree(p, dirty, clipInWindow)
  visible = windowRect() ∩ clipInWindow
  visible 为空 → 整棵子树跳过
  子节点继承 visible 作为它们的 clipInWindow
```

滚动本身是 `Widget::contentOffset_`：子节点渲染在 `(geometry − 父节点的 contentOffset)`。这个偏移折进 `mapToWindow`，于是**所有派生坐标**——`windowRect`、命中测试、脏矩形、IME 光标位置——自动跟随滚动，不需要任何一处单独处理。

`ScrollArea` 是三层：`ScrollArea > viewport > content`。中间层的存在是因为 widget 先画自己再画子节点——若 content 铺满整个 ScrollArea，滚动条会被埋在内容下面。viewport 的尺寸排除了滚动条条带，于是它既是裁剪者也是滚动者。

### 3.11 线程模型

```
数据采集线程 ──push──> 无锁/加锁队列 ──> UI 线程 drain ──> Widget::setValue()
                                              ↑
                                     每帧或定时器触发一次
```

**Widget 不是线程安全的，且永不会变成线程安全的。** 跨线程只能通过队列。这条规则写在这里就是为了以后不被"加个 mutex 就好了"说服——细粒度锁会毁掉帧率稳定性（第 1 节第 3 条）。

布局引擎与全库一致：只在 UI 线程使用。`g_layouts` / `g_layoutDepth` / `g_layoutHosts` / `g_arrangeHost` 都是普通 `static`，非 `thread_local`——与 `g_bubbles` 同一条理由。`TagRegistry` 同理：位号在配置期解析，那本来就是名字已知的时刻。

### 3.12 布局引擎（R2）

**可选，默认关闭，与绝对坐标永久共存。** 一个不装 `Layout` 的 widget 走的还是 R2 之前那条路，且**不为引擎的存在付任何代价**。

```
Widget::setLayout<BoxLayout>(H)      ← 装一次，终身绑定，宿主不可更换
   ↓
setGeometry / add / takeChild / setVisible / invalidateSizeHint
   ↓  第一步一律先测 detail::g_layoutHosts（进程内拥有 Layout 的 widget 数）
   ↓  为 0 → 一次热全局 load + 一次必然预测正确的分支，然后什么都不做
   ↓
Widget::runLayoutIfAny()   M1 重入闩 / M2 下行单向红线 / M3 幂等短路 / M4 深度上限
   ↓
Layout::arrange(host, contentRect)   contentRect = layoutRect() − 布局 margins
```

**四条结构性规则**，每一条都承重：

1. **`Layout` 不得持有 `Widget*`**（`host_` 除外，一次绑定终身不变）。子项一律用**子节点索引**标识。索引错 = 摆错位置（可见、可测）；指针错 = UAF（不可见、月级后爆）。
2. **`sizeHint()` 不读 `geometry()`**（ADR-R2-09）。几何是上一趟 arrange 的**输出**，从它测量就是循环定义：窗口被一格一格拖小时，控件会一路缩下去且拖回来再也回不来。无 layout 的 widget 报**自然尺寸**——它这辈子拿到的第一个非空尺寸，锁存一次，终身不改。
3. **有 layout 的 widget，`sizeHint()` 就是 `layout()->measure()`**。容器的尺寸是关于它内容的陈述；否则嵌套根本无法工作——GroupBox 里套 GridLayout 再放进页面的 BoxLayout，会按"某次手写几何"报尺寸，整棵树由构造顺序而不是内容定尺寸。
4. **溢出只报告，从不强制**（`Widget::lastLayoutOverflow()`）。发信号 = 在布局 pass 中间跑应用代码 = M1/M2 刚禁掉的那种重入。

**装饰型容器**：`GroupBox` override `layoutRect()`，交给布局的是**边框内、标题线下**那块。于是"边框有多厚"永远只有 GroupBox 自己知道，调用方不必在每个 GroupBox 上写 `setMargins({12, 34, 12, 12})`；margins 仍然是作者自己的留白，两者叠加而不是二选一。

**执行时机**：几何驱动（`setGeometry`）→ 同步，在 `onGeometryChanged` **之前**（显式代码最后跑、代码赢）；内容驱动（`add` / `takeChild` / `setVisible` / `invalidateSizeHint`）→ 向上标脏，从最顶层脏宿主跑一次。**不引入帧边界调度器**。

**已知开销**：布局引擎自身零分配（`BoxLayout` / `GridLayout` 的 scratch 只增不减，见 `tests/widget/test_layout_alloc.cpp`），但每趟 arrange 会向每个子项要一次 `sizeHint()`，而文本控件的 `sizeHint()` 要走一次 `measureText`。拖动窗口边缘时这是每帧一次的文本整形。修法是按 `(text, fontSize, styleGeneration)` 做宽度缓存，属于**文本引擎的事**，归 R3；用例 `layout_alloc.text_is_re_measured_on_every_pass_r3` 把这个数量钉住，R3 落地时它会变红。

**已迁移**：showcase 的 `PageWidgets` / `PageInputs` / `PageOps` 三页（75 处 `setGeometry` → 0）。另外 5 页与 `AppWindow` / `ScrollArea` / `WindowHeader` / `Shell` 四个容器**保持绝对坐标**——见 §4。

---

## 4. v1 范围

**做：**
- Win32 后端（HWND + DIB section，Blend2D 零拷贝直接渲染进 DIB）
- Per-Monitor DPI Awareness v2
- 几何类型、信号槽、事件分发、Widget 树、脏矩形
- 焦点管理、Tab 遍历、平台无关键盘路由、禁用态级联
- 通用控件：`Label` `PushButton` `CheckBox` `RadioButton` `ToggleSwitch`
  `Slider` `ProgressBar` `GroupBox` `Separator` `SpinBox`
- 领域控件：`StatusLed` `Gauge` `TrendChart`
- 文本渲染（系统字体加载，含中文）

**明确不做（v1）：**
- ~~布局引擎 —— v1 用绝对坐标~~ —— **已实现，见 §3.12**（R2）。但原来的理由**只被推翻了一半**，另一半现在是永久约定：
  - **绝对坐标不是过渡期，是一等公民。** `Layout` 是**可选**的，一个不装它的 widget 走的还是原来那条路，`sizeHint()` 报自然尺寸，且不为引擎的存在付任何代价（所有挂钩第一步先测 `g_layoutHosts`）。库内 32 个控件本身一个都不用布局。
  - **组态区就该用绝对坐标。** HMI 监控画面上的仪表、趋势图、管线是按工艺流程图定位的：泵画在阀门左边 40px 是**工艺语义**，不是排版。用布局表达它只会把一张确定的图变成一组容易漂移的约束。showcase 的 HMI 页、总览页等 5 页保持绝对坐标，作为"共存真的能用"的活证据，不是待办事项。
  - **参数表单、设置页、工具栏则该用布局**：它们的内容随语言、字体、翻译长度变化，坐标一写死就必然在某个客户现场重叠。
  - 四个特殊容器（`AppWindow` / `ScrollArea` / `WindowHeader` / `Shell`）也保持手写几何：它们带的是**规则**而不是排版——`commandZoneWidth()`、`needVBar/needHBar` 刻意的非对称打破、最大化时 `borderWidth()` 归零——这些用通用布局表达都是削足适履。
- **IME 内联预编辑** —— 中文输入本身可用（见 §3.8），但组合中的拼音由系统 IME 窗口绘制，不在字段内带下划线显示
- **撤销 / 重做** —— 文本控件没有 undo 栈。`Esc` 可把 `LineEdit` 回滚到获得焦点时的值，仅此而已
- **双击选词 / Ctrl+方向键按词移动** —— 需要分词规则（中文尤其麻烦），暂用逐字符移动
- ~~**滚动与虚拟化** —— 留到报警列表那一轮一起做~~ —— **已实现，见 §3.10**：`Widget::contentOffset_` + `paintTree` 的累积裁剪矩形，`ScrollArea`（三层）与 `ListView`（拉取式模型）都在库里了。`TextArea` 的垂直滚动仍是控件内部的，不是通用容器
- **下拉家族的其余成员** —— overlay 地基已就位（§3.9），以下复用同一套机制，尚未实现：可编辑下拉 / 自动补全、级联选择 Cascader、动作菜单 DropdownMenu 与子菜单、拆分按钮 SplitButton、日期选择 DatePicker、颜色选择 ColorPicker
- **弹层溢出窗口** —— 弹层被限制在窗口内（向上翻转而非溢出屏幕）。要溢出需要独立 HWND，工控全屏场景不划算
- **垂直 `Slider`** —— 宁可没有，也不要半成品
- 无障碍（UIA）—— 领域专用库，优先级低
- ~~样式表引擎 —— 用 token 结构体，不做选择器解析~~ —— **已实现，见 §2.6**。原来的理由（组态画面调一次跑几年，级联带不来价值）对**画面**成立，对**产品**不成立：同一个二进制发给多个厂，每家要自己的配色，改 Theme 字段意味着一厂一次重新编译
- X11 / Cocoa 后端 —— 接口已预留

---

## 5. 目录结构

```
GeeyoouUI/
├─ CMakeLists.txt
├─ cmake/Dependencies.cmake      Blend2D + AsmJit（FetchContent，钉 commit）
├─ docs/architecture.md          本文档
├─ include/geeyoou/
│  ├─ core/       Types.hpp  Signal.hpp  Event.hpp
│  ├─ platform/   Platform.hpp
│  ├─ render/     Painter.hpp  Theme.hpp
│  ├─ widget/     Widget.hpp  Window.hpp
│  │              Label  PushButton  CheckBox  RadioButton  ToggleSwitch
│  │              Slider  ProgressBar  GroupBox  Separator  SpinBox
│  └─ hmi/        StatusLed.hpp  Gauge.hpp  TrendChart.hpp
├─ src/           与 include 镜像的 .cpp，外加 platform/win32/
└─ examples/showcase/   唯一示例入口（admin console 外壳 + 六个页面）
```

> **示例只有一个入口**。所有控件族都是 `showcase` 里的一个页面，共用一个 `AppState`（采集线程 + `DataHub`）。页面按需构建，隐藏页面因 `animationTickTree` 跳过不可见子树而自动停止周期性工作——不需要任何页面记得"离开时取消订阅"。

---

## 6. 后续路线

| 阶段 | 内容 |
|---|---|
| v1 | Win32 + 5 个控件，端到端跑通 |
| v1.1 | 数据源抽象（队列 + 定时 drain）、报警列表控件 |
| v1.2 | 简单布局（线性 / 网格），组态画面序列化（JSON 描述界面） |
| v2 | X11 后端；region 级脏区；GPU 后端探索 |
| v2+ | Cocoa 后端；无障碍 |
