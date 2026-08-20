<p align="center">
  <a href="README.md">English</a>
  &nbsp;·&nbsp;
  <b>简体中文</b>
</p>

<h1 align="center">GeeyoouUI</h1>

<p align="center">
  <b>面向工控 HMI / 上位机的 MIT 协议 C++20 自绘控件库。</b><br>
  无 moc、无代码生成、无 LGPL 合规负担、无 Qt。
</p>

<p align="center">
  <img alt="License: MIT" src="https://img.shields.io/badge/license-MIT-blue.svg">
  <img alt="C++20" src="https://img.shields.io/badge/C%2B%2B-20-00599C.svg">
  <img alt="Platform" src="https://img.shields.io/badge/platform-Windows%20(Win32)-lightgrey.svg">
  <img alt="Dependencies" src="https://img.shields.io/badge/deps-Blend2D%20%2B%20AsmJit%20(%E8%87%AA%E5%8A%A8%E6%8B%89%E5%8F%96)-green.svg">
  <img alt="Status" src="https://img.shields.io/badge/status-v1%20%C2%B7%20%E5%8F%AF%E7%94%A8-orange.svg">
</p>

<p align="center">
  <i>⭐ 如果你也希望 C++ 界有一个宽松协议的 Qt Widgets 替代品，点个 star 是最省事的表态方式。</i>
</p>

<!-- TODO: 在这里放一张 build\bin\showcase.exe 的截图或 GIF —— 这是整个 README 里
     转化率最高的一块。建议路径：docs/images/showcase.png -->
<!-- <p align="center"><img src="docs/images/showcase.png" width="860" alt="GeeyoouUI showcase"></p> -->

---

## 为什么会有这个库

用 C++ 做工控 HMI 或机台上位机，默认答案是 Qt。Qt 很好——代价是三选一：

- 按 **GPL/LGPL** 发布，并为客户机柜里的程序承担重新链接义务；
- 买 **商业授权**，按人头、按年，一直付；
- 或者跟 `windeployqt`、插件目录、以及比你自己程序还大的一堆 DLL 缠斗。

GeeyoouUI 走的是另一条路：屏幕上每一个像素都是自己画的，所以**没有东西需要合规，也没有东西需要随程序分发**。

| | GeeyoouUI | Qt Widgets |
|---|---|---|
| **协议** | ✅ MIT —— 可静态链接进闭源产品，无任何义务 | GPL v3 / LGPL v3 / 付费商业授权 |
| **依赖协议** | ✅ Blend2D + AsmJit 均为 Zlib，全栈宽松 | Qt 及其第三方依赖集 |
| **代码生成** | ✅ 无。信号槽 = 模板 + `std::function` | 需要 `moc` 预编译步骤 |
| **构建准备** | ✅ 只装 Visual Studio；依赖由 CMake 自动拉取并钉死 commit | 装 Qt SDK，还要对上编译器/ABI |
| **部署** | ✅ 就一个 `.exe`，无插件目录、无 `windeployqt` | Qt DLL + 平台插件 + imageformats… |
| **外观** | ✅ 全自绘 —— 每台机器像素一致，不跟随 Windows 主题 | 跟随平台风格 |
| **重绘模型** | ✅ 脏矩形增量（HMI 画面约 90% 像素是静止的） | 整控件重绘 |
| **热路径分配** | ✅ 零分配 —— 实时数据走固定容量环形缓冲 | 通用取向 |
| **换肤** | ✅ 皮肤注册表 + 一个主题色带动整套配色 + 类 QSS 选择器，运行时热切换 | QSS，逐控件 |
| **平台** | ❌ **目前仅 Windows。** 平台层是 12 个纯虚函数，X11/Cocoa 未实现 | Windows、macOS、Linux、移动、嵌入式 |
| **布局引擎** | ❌ **只有绝对坐标**（v1 的有意取舍，见下） | 完整布局系统 |
| **无障碍** | ❌ 未实现（无 UIA） | 成熟 |
| **生态** | ❌ 30+ 控件，一个库 | 庞大 |

**这张表请如实读。** 如果你现在就要 Linux、要布局引擎、要读屏支持，那就用 Qt——GeeyoouUI 只会浪费你的时间。如果你交付的是 Windows 操作站、画面本来就是按固定坐标组态的、而挡在你面前的正是那纸授权，那这个库就是为你写的。

## 快速开始

只需要**装了 C++ 工作负载的 Visual Studio**。CMake / Ninja / MSVC 全部取自 VS 自带，**不需要任何东西在 `PATH` 上**：

```
build.bat
```

首次构建通过 CMake `FetchContent` 拉取 Blend2D 和 AsmJit（已钉死 commit，可复现），约 2 分钟。VS 若装在别处，改 `build.bat` 里的 `VSROOT`。

然后跑演示程序：

```
build\bin\showcase.exe
```

## 一个最小程序

```cpp
#include "geeyoou/hmi/Gauge.hpp"
#include "geeyoou/platform/Platform.hpp"
#include "geeyoou/widget/AppWindow.hpp"

using namespace geeyoou;

int main() {
  AppWindow win("温度监控", 400, 340);   // 无边框窗口 + 自绘标题栏
  win.header()->setIcon(Icon::Info);

  auto* g = win.content()->add<Gauge>();
  g->setGeometry({20, 20, 360, 260});
  g->setRange(0, 200);
  g->setBands(150, 180);                 // 预警 / 报警阈值
  g->setTitle("釜内温度");
  g->setUnit("°C");
  g->setValue(87.5);

  g->valueChanged.connect([](double v) { /* 无需宏、无需 moc */ });

  win.show();
  return platform().runEventLoop();
}
```

整个程序就这些。没有 `.pro` 文件，没有 `Q_OBJECT`，你和编译器之间没有任何中间步骤。

## 特性概览

- **自绘渲染** — Blend2D（CPU JIT 光栅化），跨平台外观 100% 一致
- **自绘窗口** — 无边框窗口 + 自绘标题栏，不跟随 Windows 主题；但贴边分屏 / 双击最大化 / Alt+Tab 全部保留
- **皮肤可切换** — 注册表式皮肤 + 一个主题色带动整套配色 + 类 QSS 选择器，运行时热切换，不用重新编译
- **无 moc** — 信号槽用模板 + `std::function`，没有任何代码生成步骤
- **脏矩形增量重绘** — HMI 画面 90% 的像素是静止的，不做全帧重绘
- **热路径零分配** — 实时数据走固定容量环形缓冲，适配长期无人值守运行
- **平台层为纯虚接口** — v1 实现 Win32；X11 / Cocoa 只需补 12 个方法

## 演示程序

**唯一入口**：`build\bin\showcase.exe`（`examples/showcase`）。

一个 admin console 式的外壳：左侧导航选页，内容区打开对应控件族。整个窗口是 `ShowcaseWindow : AppWindow`——没有 Windows 标题栏，左上角是图标 + 标题 + 副标题，右上角是通知 / 语言切换 / 账户下拉，再往右是自绘的最小化、最大化、关闭。

| 页面 | 内容 | 顺带验证的东西 |
|---|---|---|
| 概览 | 库的构成、分层规模、未实现清单 | 多行 `Label`、实时统计卡片 |
| 窗口外壳 | 标题栏高度 / 配色 / 图标 / 按钮显隐实时改 | 无边框窗口、`HitZone` 拖动区、窗口命令 |
| 主题与皮肤 | 换皮肤 / 换主题色 / 现场编辑样式表 | 皮肤注册表、`@token`、选择器与级联 |
| HMI 监控 | 仪表 / 指示灯 / 实时趋势 | 整条渲染链路 + 中文文本 |
| 运维控制台 | 报警列表 / 滚动表单 / 采集队列 | 拉取式 `ListView`、嵌套 `ScrollArea`、跨线程队列 |
| 基础控件 | 按钮 / 开关 / 单选 / 滑块 / 数值设定 | 焦点遍历、整组禁用联锁 |
| 输入与按钮 | 文本输入族、按钮变体、图标按钮 | IME 手工测试台（切中文输入法，候选窗应跟随光标） |
| 下拉选择 | 单选/搜索/多选/树形/级联/菜单/日期 | 弹层冲出 `GroupBox` **与** `ScrollArea` 双重限制 |

所有页面**共用同一个采集线程与 `DataHub`**——「HMI 监控」和「运维控制台」看到的是同一份实时数据，这本身就是"采集线程绝不触碰 Widget"这条规则的演示。

页面按需构建：没打开过的页面除了一条导航项之外不占任何资源；隐藏的页面因为 `animationTickTree` 跳过不可见子树，也自动停止周期性工作。

## 窗口层

应用窗口继承 `AppWindow`（`widget/AppWindow.hpp`），它默认是**无边框窗口**：Windows 不再画标题栏、边框和主题色，标题栏由 `WindowHeader` 用同一套 `Painter` / `Theme` 画出来。

```cpp
class PlantWindow : public AppWindow {
 public:
  PlantWindow() : AppWindow("配料工段", 1280, 800) {
    header()->setHeight(48);
    header()->setIcon(Icon::Settings);
    header()->setTitle("配料工段");
    header()->setSubtitle("2# 反应釜 · 在线");

    auto* lang = header()->addTrailingItem<HeaderMenu>(0);
    lang->setIcon(Icon::Globe);
    lang->setText("简体中文");
    lang->setItems({{"简体中文", "zh-CN"}, {"English", "en-US"}});
    header()->setTrailingItemWidth(lang, lang->preferredWidth());

    auto* me = header()->addTrailingItem<HeaderAvatar>(0);
    me->setInitials("张");
    me->setName("张工");
    me->setCaption("值班工程师");
    me->setItems({{"个人资料", "profile"}, MenuItem::sep(), {"退出登录", "logout"}});
    header()->setTrailingItemWidth(me, me->preferredWidth());

    setContent<PlantScreen>();   // 自动跟随内容区尺寸
  }
};
```

**标题栏整条都能拖动窗口**——除了三个窗口按钮和用 `addTrailingItem()` 放进去的控件（那些要接自己的点击）。拖动区是作为「窗口标题区」上报给系统的，所以贴边分屏、双击最大化、右键系统菜单、边框拖拽改大小全部照常工作，一行拖动逻辑都不用写。

| `WindowHeader` 属性 | 说明 |
|---|---|
| `setHeight` / `setLeadingPadding` / `setTrailingPadding` | 标题栏度量 |
| `setBackground` / `setBorderColor` / `setBorderVisible` | 底色与下沿分隔线 |
| `setTitle` / `setSubtitle` / `setTitleColor` / `setTitleFontSize` | 标题与副标题 |
| `setIcon` / `setIconColor` / `setIconSize` / `setIconBadge` | 左侧图标，可加圆角底板 |
| `setButtons` / `setButtonWidth` / `setButtonColor` / `setCloseHoverColor` | 最小化 / 最大化 / 关闭三按钮 |
| `setDraggable` | 空白处是否可拖动窗口 |
| `addTrailingItem<T>()` / `setTrailingItemWidth()` / `addTrailingGap()` | 右侧插任意控件，整组右对齐 |

| 窗口层控件 | 说明 |
|---|---|
| `AppWindow` | 窗口基类：无边框 + `header()` + `content()` + `setContent<T>()` |
| `WindowHeader` | 自绘标题栏；窗口按钮由它自己绘制，贴死右上角（费茨定律） |
| `HeaderMenu` | 标题栏下拉：图标 + 文字 + 箭头，可带数字角标（通知铃铛） |
| `HeaderAvatar` | 账户块：圆形头像（首字母）+ 姓名 + 角色 + 在线点 + 下拉菜单 |

`Window` 同时开放了 `minimize()` / `maximize()` / `restore()` / `toggleMaximize()` / `isMaximized()` / `close()`，自绘按钮和业务代码用的是同一套 API。

需要 OS 原生边框时，把 `WindowOptions{.frameless = false}` 传给 `AppWindow` 的构造函数即可。

## 主题 / 皮肤 / 样式表

三层能力，按需往上取，**能用下层解决就不要用上层**。

### 1. 主题（`Theme`）—— token 结构体

21 个颜色 + 圆角 + 字号。库里所有取色都是**每次绘制现取 `Theme::current()`**，所以换主题不需要逐控件通知：

```cpp
Theme::current() = lightTheme();   // 32 个控件全部跟着变
```

### 2. 皮肤（`Skin`）—— 注册表 + 热切换

一个皮肤 = `Theme` + 一段样式表，按名字注册，运行时切换：

```cpp
#include "geeyoou/render/Skin.hpp"

skins().apply("light");            // 内置：dark / light / contrast / amber
skins().setAccent(Color::rgb(0x12, 0xC2, 0xC2));   // 主题色：一个颜色带动整套

// 注册自家皮肤
Skin s;
s.name  = "acme";
s.title = "ACME 厂标";
s.theme = themeWithAccent(darkTheme(), Color::rgb(0xE8, 0x6C, 0x00));
s.styleSheet = "PushButton { border-radius: 2; }";
skins().add(s);
skins().apply("acme");
```

`setAccent()` 只动**派生自品牌色**的 token（accent / primary / focusRing / 选区底色，以及填充按钮上的字色——按亮度自动选黑或白）。`ok` / `warn` / `alarm` **不动**：报警必须永远是报警色。

`Window` 会自己订阅 `skins().changed` 并整窗重绘，业务代码不用管。

### 3. 样式表（`StyleSheet`）—— 类 QSS 选择器

token 表达不了的部分才用它：「这个急停键要方角单独变红」。

```qss
/* 注释是 C 风格 */
* { font-size: 13; }                        /* 任意控件           */
PushButton { border-radius: 8; }            /* 按类型，含子类     */
PushButton:hover { border-color: @accent; } /* 按状态             */
.danger { accent: #FF4D5E; }                /* 按 style class     */
#emergencyStop { border-width: 2; }         /* 按 objectName      */
GroupBox PushButton { font-size: 12; }      /* 后代               */
Label, Separator { color: @textDim; }       /* 分组               */
```

```cpp
btn->setObjectName("emergencyStop");
btn->addStyleClass("danger");
skins().reloadStyleSheet(qssText);     // 解析失败不抛异常，见下
```

| | |
|---|---|
| 状态 | `:hover` `:pressed` `:checked` `:focus` `:disabled` `:read-only` `:invalid` `:open` `:selected` |
| 属性 | `color` `background` `border-color` `border-width` `border-radius` `font-size` `accent` `icon-color` `padding` |
| 颜色 | `#RGB` `#RRGGBB` `#AARRGGBB` `rgb()` `rgba()` `transparent`，以及 **`@accent` `@panel` `@text` …** |

**`@token` 引用当前主题**，在解析时求值：一份样式表在四套皮肤下都成立，不用为每套皮肤写一份十六进制。

几条要点：

- **特异度按 CSS 规则**（id > class+状态 > 类型），同级按书写顺序，**逐属性决胜**——两条规则可以各自贡献一部分
- **状态是子集判定**：`:hover` 对「既 hover 又 focus」的控件仍然成立
- **类型选择器认子类**：`PushButton { }` 同时作用于 `IconButton` / `MenuButton` / `HeaderMenu`（无 RTTI，靠 `GEEYOOU_STYLE_TYPE` 宏织入的虚函数链）
- **优先级：代码 > 样式表 > 主题**。和 Qt 相反——为某个实例显式 `setColor()` 的颜色，比为某一类控件写的选择器更具体
- **解析错误不抛异常**：样式表是内容不是代码，一处写错只丢那一条规则，其余照常生效，错误进 `StyleSheet::errors()` 供界面显示

**已接入选择器的控件**：`PushButton` 族、`Label`、`GroupBox`、`LineEdit` 族、`CheckBox`、`RadioButton`、`ToggleSwitch`、`Slider`、`ProgressBar`、`Separator`、`WindowHeader`。其余控件只跟随 `Theme`——这是有意的取舍，理由见 `docs/architecture.md` §2.6。

## 控件清单

| 通用（`widget/`） | 说明 |
|---|---|
| `Label` | 文本，支持水平/垂直对齐；不显式 `setColor()` 时跟随主题与样式表 |
| `PushButton` | 普通 / 可锁定（`setCheckable`） |
| `CheckBox` | 布尔参数 |
| `RadioButton` | 按 父节点 + group id 自动互斥 |
| `ToggleSwitch` | 设备启停开关（无动画，状态即时明确） |
| `Slider` | 拖拽 / 点击跳转 / 方向键 / PgUp·PgDn / Home·End，可带刻度 |
| `ProgressBar` | 只读填充指示，可自定义文本 |
| `GroupBox` | 带标题容器，禁用它即禁用整棵子树 |
| `Separator` | 水平 / 垂直分隔线 |
| `SpinBox` | 数值设定：↑↓ 步进、PgUp/PgDn ×10、直接键入数字、Enter 提交 / Esc 取消 |
| `LineEdit` | 单行输入：光标、选区、鼠标拖选、剪贴板、水平滚动、占位符、长度上限（按字计）、清除按钮、前置图标、校验失败态、只读态 |
| `PasswordEdit` | 密码框，可选眼睛切换明文；掩码状态下拒绝 Ctrl+C/X |
| `SearchBox` | 搜索图标 + 清除按钮；`textChanged` 实时过滤 / `searchRequested` 回车提交 |
| `TextArea` | 多行：软换行、跨行光标与选区、滚轮与滚动条、只读态 |
| `IconButton` | 纯图标按钮，可设为圆形 |
| `ComboBox` | 单选下拉，支持分组标题、禁用项、Alt+1..9 快捷选中 |
| `SearchableSelect` | 搜索匹配下拉：跨 `text`/`secondary`/`extraFields` **多字段搜索**、匹配高亮、无结果回调 |
| `MultiSelect` | 多选下拉：复选行、全选/清空、闭合态摘要（超阈值折叠为「N 项已选」） |
| `TreeSelect` | 树形下拉：展开折叠、可限定仅叶子可选、显示全路径 |
| `PopupList` | 通用候选列表（下拉家族共用；只渲染可见行，上万项无压力） |
| `Cascader` | 级联选择：多列联动（车间→产线→设备），显示全路径 |
| `MenuButton` / `SplitButton` | 动作菜单（图标/快捷键提示/分隔线/禁用项）；拆分按钮 |
| `DatePicker` / `CalendarView` | 日期选择：周一起始、周末着色、范围限制、月份翻页 |
| `ScrollArea` | 滚动容器：滚轮、拖拽滑块、点轨道翻页、`ensureVisible` |
| `ListView` | 虚拟化多列表格，**拉取式模型**（回调取单元格，百万行不占内存） |
| `TableView` | 数据网格：**每个单元格都是画出来的**（序号 / 勾选 / 开关 / 进度条 / 标签 / 操作链接 / 树形展开），行内编辑复用四个常驻编辑器；固定列、合并行列、列排序三态、奇偶行、loading 与空状态 |
| `TableModel` / `TreeTableModel` | 表格的拉取式数据源（**非控件，不进控件树**）；树模型自带展开状态与**异步子节点**三态（Loading / Ready / Failed），只发 `childrenRequested` 信号，绝不自己去取 |
| `TablePager` | 分页条：总数 / 每页条数 / 页码（带省略号）；**不持有表格指针**，只发页码信号 |
| `scene3d/View3D` | **三维视图**：软件渲染，无 GPU 依赖。左键旋转 / 滚轮缩放 / Shift+左键平移；单击拾取部件（拾取复用当帧已排序的面表，**点到的一定是看见的那个**）。**视口有自己的中间调渐变底**，所以浅色皮肤下白色模型照样看得见；三种着色模式（状态 / 材质 / 数值热力图）|
| `scene3d/Scene3D` | 场景、部件表与**标注**（**非控件，不进控件树**）。`setPartState(id, 状态)` 给部件着色，`setPartMaterial()` 上色，`setPartValue()` 喂热力图，`addAnnotation()` 挂带引线的标签（锚点跟着部件走）；状态只存语义，颜色绘制时按主题解析 |
| `scene3d/MeshBuilder` | 箱体/圆柱/圆锥/球/封头/管道/法兰七种**凸闭合体**图元。`Mesh` 是**非拥有的数据窗口**——外部模型加载器写在库外，把结果喂进来即可 |

| 数据层 | 说明 |
|---|---|
| `core/DataQueue<T>` | 有界线程安全队列；满时丢最旧并**计数**，不做无界增长 |
| `hmi/DataHub` | 采集线程 `push()` → UI 线程 `drain()`；通道管理、最新值缓存、丢弃统计 |
| `hmi/AlarmList` | 报警列表：等级配色、确认/恢复状态、固定容量环形缓冲、过滤与统计 |

| 领域（`hmi/`） | 说明 |
|---|---|
| `StatusLed` | 指示灯，Off/Ok/Warn/Alarm，报警可闪烁 |
| `Gauge` | 弧形仪表 + 数字读数 + 预警/报警色带 |
| `TrendChart` | 多通道实时曲线，固定容量环形缓冲 |

**按钮变体**：`PushButton::setVariant()` 支持 `Default` / `Primary` / `Success` / `Warning` / `Danger` / `Ghost`，另有 `setIcon()`、`setLoading()`（转圈，需先调 `Window::enableAnimations()`）、`setCheckable()`（锁定态）。

## 图标：内置 + 收集 + 扩展

`render/Icon.hpp` 提供 **39 个内置矢量图标**，全部代码绘制：无资源文件、无图标字体、可主题着色、任意缩放（一份定义同时服务 14px 内联和 48px 标题栏）。

但内置集永远覆盖不了**领域**——配料车间要的是泵、阀、反应釜，不是通用 UI 图标。所以 `Icon` 不是一个封闭枚举，而是一个**句柄**：`IconRegistry` 从 `Icon::FirstCustom` 开始发放 id，注册进来的图标仍然是 `Icon` 值，因此**现有 19 个吃 `Icon` 的 API 全部原样可用**。

```cpp
#include "geeyoou/render/IconRegistry.hpp"

// 方式一：代码绘制。用和内置图标同一套 24x24 作画网格。
icons().add("pump", [](Painter&, const IconCanvas& g) {
  g.circle(12, 11, 6.5f);
  g.poly({{9.5f, 7.5f}, {16.5f, 11}, {9.5f, 14.5f}, {9.5f, 7.5f}});
  g.line(5.5f, 20, 18.5f, 20);
}, "设备");

// 方式二：SVG path 数据，直接抄图标集的 d 属性。
icons().addSvgPath("thermometer",
                   "M14 14.8V4a2 2 0 0 0-4 0v10.8a4 4 0 1 0 4 0z",
                   PathStyle::Stroke, 24.0f, "仪表");

// 用起来和内置图标没有任何区别
header()->setIcon(icons().find("pump"));
btn->setIcon(icons().find("thermometer"));
```

**为什么 SVG 这条路划算**：Lucide / Feather / Tabler / Material 都是 **24×24 网格 + 2 单位描边**——正好就是本库的作画网格，所以它们的 `d` 属性能直接用。支持的命令：`M m L l H h V v C c S s Q q T t A a Z z`，覆盖主流图标集实际会用到的全部指令。

**性能上反而更快**：SVG 路径在**注册时解析一次**存起来，每次绘制只是变换+描边；而手写图标每次绘制都要重新构造几何。同一套缓存以后也可以回头用到内置图标上。

| API | 说明 |
|---|---|
| `icons().add(name, drawer, category)` | 代码绘制注册；重名则**替换画法但保留 id**（已经拿着句柄的控件不会变空白） |
| `icons().addSvgPath(name, d, style, viewBox, category)` | SVG 路径注册；`PathStyle::Stroke` / `Fill` 必须选对，选错会画成毛线或色块 |
| `icons().find(name)` | 按名字取，内置图标也能取（`find("warning")`）；未知返回 `Icon::None` |
| `icons().name(id)` / `all()` / `categories()` | 反查与枚举——图标选择器 / 图标库页面靠这个 |
| `icons().errors()` | SVG 解析问题；**解析失败不抛异常**，图标是内容不是代码 |

内置图标名按 kebab-case 命名（`chevron-down` / `window-minimize` / `eye-off`），和主流图标集一致，从别处抄来的名字通常直接能解析。

**未注册的 id 画空白**，不画占位方框——工控画面上，一个长得像真符号的替身比空白更危险。

示例见 `examples/showcase/PlantIcons.cpp`（9 个自定义图标，两种录入方式），效果在「窗口外壳」页底部。

## 文本输入与键盘

**中文输入**：输入框支持 IME，候选窗跟随光标；组合中的拼音由系统 IME 窗口绘制（未做内联预编辑，详见 `docs/architecture.md` §3.8）。

键盘约定：`Tab`/`Shift+Tab` 切换焦点（顺序 = 构造顺序），`空格` 激活，方向键调值；文本控件支持 `Ctrl+A/C/X/V`、`Home`/`End`、`Shift+方向键` 扩选、`Esc` 回滚。

## 代码结构

```
include/geeyoou/
  core/       Types.hpp   几何与颜色（全部逻辑像素）
              Signal.hpp  无 moc 信号槽
              Event.hpp   输入事件
  platform/   Platform.hpp   ← 移植边界，纯虚（含 HitZone / WindowOptions）
  render/     Painter.hpp    Blend2D 门面（全库唯一接触 BL* 的地方）
              Theme.hpp      token 结构体
              Skin.hpp       皮肤注册表 + 主题色派生
              StyleSheet.hpp 类 QSS 选择器与级联
              Icon.hpp       39 个内置矢量图标
              IconRegistry.hpp  图标注册表 + IconCanvas 作画网格
              VectorPath.hpp    轮廓容器 + SVG path 解析
  widget/     AppWindow / WindowHeader   窗口层：无边框窗口与自绘 chrome
              Widget / Window / Label / PushButton
  hmi/        Theme / StatusLed / Gauge / TrendChart
src/          与 include 镜像，外加 platform/win32/
docs/         architecture.md — 设计决策与取舍，动手前先读
```

## 现状与路线图

**v1 已跑通**：Win32 后端、Per-Monitor DPI v2、脏矩形、控件树、无边框窗口层、中文文本渲染。

**尚未实现**——有意推迟，理由见 `docs/architecture.md` 第 4 节：

| | 为什么推迟 |
|---|---|
| 布局引擎 | v1 用绝对坐标，本来就符合组态画面的作图习惯 |
| 无障碍（UIA） | 需要控件树先稳定下来 |
| IME 内联预编辑 | 系统候选窗已经跟随光标，内联属于打磨项 |
| X11 / Cocoa 后端 | 平台层就 12 个纯虚函数——移植范围已划清，只是还没开工 |

后续计划见 `docs/roadmap.md`。

## 参与贡献

欢迎 issue 和 PR，尤其是这几类：

- **X11 或 Cocoa 后端** —— 实现 `platform/Platform.hpp` 的 12 个方法，整套控件即可跟上
- **领域图标包** —— 泵、阀、断路器、输送带；有 `addSvgPath` 在，成本很低
- **皮肤** —— 一个皮肤 = `Theme` + 一段样式表，一个文件就能发布
- **来自真实车间的 bug 报告** —— 这类最有价值

动手前先读 `docs/architecture.md`：里面记录了哪些取舍是有意为之，能省下你"修好"一个设计的力气。

## 许可证

[MIT](LICENSE) —— 可用于商业闭源产品，可静态链接，无任何义务。依赖（Blend2D、AsmJit）为 Zlib 协议，整条技术栈保持宽松。

---

<p align="center">
  如果 GeeyoouUI 帮你省下一份 Qt 授权、或者一个下午的 <code>windeployqt</code>，<br>
  <b>⭐ 点个 star</b> —— 这是我们继续做下去的唯一信号。
</p>
