//
// English language pack.
//
// key = the original Simplified Chinese literal; see i18n/I18n.hpp for why.
//
// Rules for anyone adding a line here:
//
//   * Format specifiers must survive unchanged, in the same ORDER: a
//     "%d / %d" whose translation says "%d" once is a crash, not a typo.
//   * Leading and trailing spaces are load-bearing.  Several call sites build a
//     sentence by concatenation -- tr("已选 ") + n + tr(" 行") -- and the
//     spaces live in the fragments.
//   * "\n" placement is part of the layout.  The multi-line blocks below are
//     measured by the widget that draws them; moving a break changes the page.
//
// Terminology follows plant-floor English rather than a literal gloss:
// 位号 is Tag (not "bit number"), 投用 is In Service, 联锁 is Interlock,
// 确认 on an alarm is Acknowledge, 量程 is Range, 组态画面 is a mimic.
//
#include "LangPack.hpp"

namespace showcase {
namespace {

const LangEntry kEntries[] = {
    // ------------------------------------------------- IconGallery.hpp ---
    {"没有匹配的图标 —— 试试 chevron / window / pump / 仪表",
     "No matching icon — try chevron / window / pump / gauge"},
    {"未分类", "Uncategorised"},
    {"%d 个", "%d"},

    // ----------------------------------------------------- PageHmi.cpp ---
    {"进料流量", "Feed Flow"},
    {"釜内温度", "Reactor Temp"},
    {"系统压力", "System Pressure"},
    {"设备状态", "Equipment Status"},
    {"进料泵 P-101", "Feed Pump P-101"},
    {"加热器 H-201", "Heater H-201"},
    {"泄压阀 V-303", "Relief Valve V-303"},
    {"超压报警", "Overpressure Alarm"},
    {"Modbus 通讯", "Modbus Link"},
    {"实时趋势", "Live Trend"},
    {"流量", "Flow"},
    {"温度", "Temp"},
    {"压力×20", "Pressure ×20"},
    {"启动", "Start"},
    {"停止", "Stop"},
    {"状态：采集中", "Status: acquiring"},
    {"状态：已停止（数据保持）", "Status: stopped (data held)"},

    // --------------------------------------------------- PageIcons.cpp ---
    {"点击下面任意图标", "Click any icon below"},
    {"内置 · Icon.hpp", "Built-in · Icon.hpp"},
    {"自定义 · IconRegistry", "Custom · IconRegistry"},
    {"    分类：", "    Category: "},
    // NOTE the shape of this entry, and of the other long ones below: the KEY
    // is written as adjacent literals exactly the way the call site writes it.
    // C++ joins them before tr() ever runs, so a call site that spans four
    // lines is ONE key of one long string -- listing those four lines as four
    // separate entries compiles, looks complete, and misses at run time.
    {"共 %d 个图标   ·   内置 %d（Icon.hpp）   ·   自定义 %d"
     "（IconRegistry 运行时注册）   ·   %d 个分类",
     "%d icons   ·   %d built-in (Icon.hpp)   ·   %d custom"
     " (registered at run time via IconRegistry)   ·   %d categories"},
    {"按名字或分类搜索：chevron / window / pump / 仪表",
     "Search by name or category: chevron / window / pump / gauge"},
    {"选中的图标 —— 一份定义，任意尺寸",
     "Selected icon — one definition, any size"},
    {"同一个 Icon 句柄画四遍。矢量图标没有 @2x 资源，也没有 DPI 变体 —— "
     "16px 的行内标记和 48px 的表头标记来自同一份 24x24 授权网格。",
     "The same Icon handle drawn four times.  A vector icon has no @2x asset "
     "and no DPI variant — the 16px inline marker and the 48px header marker "
     "come from one 24x24 authoring grid."},
    {"图标画廊 —— icons().all() 的全部内容",
     "Icon gallery — everything in icons().all()"},
    {"蓝底 + 右上角 ● 的是自定义图标（examples/showcase/PlantIcons.cpp 启动时注册），"
     "其余是内置。点任意一格看它的多尺寸预览与写法。",
     "Blue tile with a ● corner = custom icon (registered at start-up by "
     "examples/showcase/PlantIcons.cpp); the rest are built in.  Click a tile "
     "for its multi-size preview and call syntax."},
    {"已选择 ", "Selected "},
    {"（", " ("},
    {"内置", "built-in"},
    {"自定义", "custom"},
    {"）   用法：", ")   Usage: "},
    {"   或 icons().find(\"", "   or icons().find(\""},
    {"显示 %d / %d", "Showing %d / %d"},

    // -------------------------------------------------- PageInputs.cpp ---
    {"状态：就绪 · Ctrl+A/C/V 可用，可直接输入中文",
     "Status: ready · Ctrl+A/C/V work, CJK input supported"},
    {"状态：", "Status: "},
    {"文本输入", "Text Input"},
    {"普通输入框", "Plain field"},
    {"请输入配方名称", "Enter a recipe name"},
    {"带清除按钮 + 长度上限 12 字", "Clear button + 12-character limit"},
    {"最多 12 个字符（中文也按字算）",
     "12 characters max (CJK counts per character)"},
    {"搜索框", "Search box"},
    {"搜索位号 / 报警内容", "Search tags / alarm text"},
    {"密码框（无眼睛）", "Password (no reveal)"},
    {"操作员密码", "Operator password"},
    {"密码框（可切换可见）", "Password (revealable)"},
    {"工程师密码", "Engineer password"},
    {"校验失败态 / 只读态", "Invalid state / read-only state"},
    {"PLC-01 (只读)", "PLC-01 (read-only)"},
    {"多行文本框", "Multi-line text"},
    {"软换行 · 上下键跨行 · 滚轮滚动 · Ctrl+A 全选",
     "Soft wrap · up/down across lines · wheel scroll · Ctrl+A select all"},
    {"交接班记录：\n"
     "1. 08:20 进料泵 P-101 启动，流量稳定在 52 m³/h。\n"
     "2. 09:05 釜内温度到达设定值 165 °C，转入保温阶段。\n"
     "3. 10:30 泄压阀 V-303 手动排空一次，压力由 8.2 降至 5.1 MPa。\n"
     "4. 11:00 巡检未发现异常，交接完毕。",
     "Shift handover log:\n"
     "1. 08:20 Feed pump P-101 started, flow steady at 52 m³/h.\n"
     "2. 09:05 Reactor temperature reached the 165 °C set point; holding.\n"
     "3. 10:30 Relief valve V-303 vented once by hand, pressure 8.2 → 5.1 "
     "MPa.\n"
     "4. 11:00 Round found nothing abnormal; handover complete."},
    {"占位符 / 只读", "Placeholder / read-only"},
    {"在此填写备注…", "Notes go here…"},
    {"此栏由系统写入，不可编辑。",
     "Written by the system; not editable."},
    {"按钮变体", "Button Variants"},
    {"带图标 · 禁用 · loading", "With icon · disabled · loading"},
    {"保存", "Save"},
    {"删除", "Delete"},
    {"已禁用", "Disabled"},
    {"下发参数", "Download Params"},
    {"下发中…", "Downloading…"},
    {"锁定", "Latch"},
    {"图标按钮", "Icon Buttons"},
    {"配方名称 = \"", "Recipe name = \""},
    {"受限字段 = \"", "Limited field = \""},
    {"执行搜索：\"", "Search run: \""},
    {"交接班记录已修改，共 ", "Handover log edited, "},
    {" 显示行", " display lines"},
    {"已保存", "Saved"},
    {"已删除", "Deleted"},
    {"画面已锁定", "Screen latched"},
    {"画面已解锁", "Screen unlatched"},
    {"参数下发中…（点击“锁定”可解除）",
     "Downloading parameters… (click Latch to cancel)"},

    // -------------------------------------------------- PageLayout.cpp ---
    {"① BoxLayout —— 余量按 stretch 权重分",
     "① BoxLayout — slack shared by stretch weight"},
    {"四块的 preferred 都是 120。窗口变宽时，多出来的空间按 0 : 1 : 2 : 3 分 —— "
     "第一块永远不动，最后一块长得最快。",
     "All four prefer 120.  As the window widens the extra space is split "
     "0 : 1 : 2 : 3 — the first never moves, the last grows fastest."},
    {"② SizeHint —— 缩小时谁先让，放大时谁先停",
     "② SizeHint — who yields first shrinking, who stops first growing"},
    {"把窗口拖窄：可伸缩的先让到 min，固定的一步不退。再拖宽："
     "有上限的到 240 就不长了，把余量让给旁边。",
     "Drag the window narrower: the flexible ones fall back to min, the fixed "
     "one does not budge.  Wider again: the capped one stops at 240 and hands "
     "the slack to its neighbour."},
    {"固定 140", "fixed 140"},
    {"min 80 / 无上限", "min 80 / no cap"},
    {"上限 240", "cap 240"},
    {"③ GridLayout —— 跨列与列对齐",
     "③ GridLayout — column spans and alignment"},
    {"跨 3 列", "spans 3 columns"},
    {"第 1 列", "column 1"},
    {"第 2 列", "column 2"},
    {"第 3 列", "column 3"},
    {"④ GridLayout::addRow —— 参数表单",
     "④ GridLayout::addRow — parameter form"},
    {"设备位号", "Tag"},
    {"工程描述", "Description"},
    {"Modbus 地址", "Modbus address"},
    {"反应釜内温度", "Reactor internal temperature"},
    {"⑤ 对照组 —— 绝对坐标（不会响应缩放）",
     "⑤ Control group — absolute coordinates (does not respond to resize)"},
    {"下面三块用 setGeometry 摆死。窗口怎么拉，它们纹丝不动 —— "
     "这不是 bug，组态画面（P&ID / 罐区）就该是这样，位置是工艺含义。",
     "The three below are pinned with setGeometry.  Resize all you like, they "
     "do not move — which is not a bug.  On a mimic (P&ID, tank farm) position "
     "IS process meaning."},
    {"固定坐标", "fixed coords"},
    {"内容区 %d x %d px    ·    固定 %d / 可伸缩 %d / 有上限 %d    ·    %s",
     "content %d x %d px    ·    fixed %d / flexible %d / capped %d    ·    %s"},
    {"空间不足：已按 min 截断（LayoutOverflow）",
     "out of room: clamped to min (LayoutOverflow)"},
    {"空间充足", "room to spare"},

    // ----------------------------------------------------- PageOps.cpp ---
    {"实时值（来自采集线程）", "Live Values (from the acquisition thread)"},
    {"队列：0 待处理 / 0 丢弃", "Queue: 0 pending / 0 dropped"},
    {"参数表单（ScrollArea）", "Parameter Form (ScrollArea)"},
    {"参数 P-%02d", "Param P-%02d"},
    {"报警列表（ListView 拉取式模型）",
     "Alarm List (ListView, pull model)"},
    {"未确认 0 / 活动 0", "0 unacknowledged / 0 active"},
    {"显示已确认", "Show acknowledged"},
    {"确认", "Acknowledge"},
    {"全部确认", "Acknowledge All"},
    {"队列：%zu 待处理 / %zu 丢弃", "Queue: %zu pending / %zu dropped"},
    {"未确认 %d / 活动 %d", "%d unacknowledged / %d active"},

    // ------------------------------------------------ PageOverview.cpp ---
    {"控件总数", "Widgets"},
    {"代码行数", "Lines of code"},
    {"上行依赖", "Upstream deps"},
    {"后端", "Backends"},
    {"釜内温度（实时）", "Reactor temp (live)"},
    {"GeeyoouUI 是什么", "What GeeyoouUI Is"},
    {"面向工控 HMI / 上位机的跨平台 C++20 自绘控件库。\n\n"
     "· 自绘渲染（Blend2D），跨平台外观完全一致\n"
     "· 无 moc —— 信号槽用模板 + std::function，没有代码生成步骤\n"
     "· 脏矩形增量重绘 —— HMI 画面 90% 的像素是静止的\n"
     "· 热路径零分配 —— 实时数据走固定容量环形缓冲\n"
     "· 平台层是纯虚接口 —— 新后端只需给出一块像素缓冲\n\n"
     "左侧导航逐页浏览各控件族。所有页面共用同一个采集线程与 DataHub：\n"
     "「HMI 监控」和「运维控制台」看到的是同一份实时数据。",
     "A cross-platform C++20 self-drawn widget library for industrial HMI and "
     "supervisory software.\n\n"
     "· Self-drawn rendering (Blend2D) — identical on every platform\n"
     "· No moc — signals/slots are templates + std::function, no codegen "
     "step\n"
     "· Dirty-rect incremental repaint — 90% of an HMI screen is static\n"
     "· Zero allocation on the hot path — live data rides a ring buffer\n"
     "· The platform layer is a pure interface — a new backend need only "
     "hand over a pixel buffer\n\n"
     "Walk the widget families from the left rail.  Every page shares one\n"
     "acquisition thread: HMI Monitor and Ops Console show the same data."},
    {"分层与规模", "Layers and Size"},
    {"widget  控件树 + 窗口层", "widget  widget tree + window layer"},
    {"render  绘制 / 主题 / 样式 / 图标",
     "render  painting / theme / style / icons"},
    {"hmi     领域控件", "hmi     domain widgets"},
    {"platform 移植边界", "platform porting boundary"},
    {"core    无依赖基础", "core    dependency-free foundation"},
    {" 行", " lines"},
    {"明确未实现（有意推迟，理由见 docs/architecture.md §4）",
     "Explicitly Not Implemented (deferred on purpose — docs/architecture.md "
     "§4)"},
    {"布局引擎 —— v1 用绝对坐标，符合固定分辨率组态画面习惯",
     "Layout engine — v1 uses absolute coordinates, which suits fixed-"
     "resolution mimics"},
    {"IME 内联预编辑 —— 中文输入可用，但组合串由系统 IME 窗口绘制",
     "Inline IME pre-edit — CJK input works, but the composition string is "
     "drawn by the system IME window"},
    {"撤销/重做、双击选词 —— 文本控件无 undo 栈",
     "Undo/redo and double-click word select — the text widgets have no undo "
     "stack"},
    {"无障碍 UIA —— 领域专用库，优先级低",
     "UIA accessibility — a domain-specific library, low priority"},
    {"X11 / Cocoa 后端 —— 接口已预留，未实现",
     "X11 / Cocoa backends — the interface is reserved, the code is not "
     "written"},

    // -------------------------------------------------- PageScene3D.cpp ---
    {"运行", "Running"},
    {"停机", "Stopped"},
    {"故障", "Fault"},
    {"维护", "Maintenance"},
    {"未投用", "Not In Service"},
    {"正常", "Normal"},
    {"V-101 反应釜", "V-101 Reactor"},
    {"M-101 搅拌电机", "M-101 Agitator Motor"},
    {"P-101 进料泵", "P-101 Feed Pump"},
    {"XV-101 进料阀", "XV-101 Feed Valve"},
    {"E-101 冷凝器", "E-101 Condenser"},
    {"工艺管线", "Process Piping"},
    {"开度 100%", "100% open"},
    {"循环水 32 °C", "cooling water 32 °C"},
    {"部件", "Part"},
    {"状态", "Status"},
    {"三维设备视图 · 左键拖动旋转 · 滚轮缩放 · Shift+左键平移 · 单击选中部件",
     "3D equipment view · drag to orbit · wheel to zoom · Shift+drag to pan · "
     "click to select a part"},
    {"软件渲染，无 GPU 依赖 · 视口有自己的中间调底色，"
     "所以浅色皮肤下白色模型依然看得见",
     "Software rendered, no GPU required · the viewport carries its own "
     "mid-tone ground, so a white model stays visible under a light skin"},
    {"部件与状态", "Parts and Status"},
    {"未选中部件 —— 在三维视图里点一个试试",
     "No part selected — click one in the 3D view"},
    {"点到了空处", "Clicked empty space"},
    {"已选中：", "Selected: "},
    {"复位视角", "Reset View"},
    {"全部正常", "All Normal"},
    {"模拟泵故障", "Simulate Pump Fault"},
    {"阀门检修", "Valve Maintenance"},
    {"着色模式", "Colouring Mode"},
    {"按状态", "By status"},
    {"按材质", "By material"},
    {"热力图", "Heat map"},
    {"给选中部件上色（切到「按材质」查看）",
     "Colour the selected part (switch to \"By material\" to see it)"},
    {"钢灰", "Steel grey"},
    {"工程蓝", "Engineering blue"},
    {"设备绿", "Equipment green"},
    {"安全橙", "Safety orange"},
    {"先在三维视图里选中一个部件，再上色",
     "Select a part in the 3D view first, then colour it"},
    {"已上色：", "Coloured: "},
    {"地面网格", "Ground grid"},
    {"标注", "Annotations"},
    {"悬停高亮", "Hover highlight"},
    {"V-101 的颜色由釜内温度驱动：≥148 转维护色，≥158 转故障色",
     "V-101's colour is driven by reactor temperature: ≥148 goes maintenance, "
     "≥158 goes fault"},

    // -------------------------------------------------- PageSelects.cpp ---
    {"反应釜进料温度", "Reactor feed temperature"},
    {"反应釜夹套温度", "Reactor jacket temperature"},
    {"泵出口压力", "Pump discharge pressure"},
    {"回流流量", "Reflux flow"},
    {"反应釜液位", "Reactor level"},
    {"pH 值", "pH"},
    {"溶氧", "Dissolved oxygen"},
    {"搅拌转速", "Agitator speed"},
    {"振动烈度", "Vibration severity"},
    {"电机电流", "Motor current"},
    {"状态：就绪 · Enter/↓ 展开，↑↓ 移动，Alt+1..9 快捷选中，Esc 关闭",
     "Status: ready · Enter/↓ opens, ↑↓ moves, Alt+1..9 picks, Esc closes"},
    {"基础单选 / 分组", "Single Select / Groups"},
    {"扁平列表", "Flat list"},
    {"请选择运行模式", "Select an operating mode"},
    {"手动", "Manual"},
    {"半自动", "Semi-auto"},
    {"全自动", "Full auto"},
    {"带分组标题 + 禁用项", "Group headers + disabled entries"},
    {"请选择设备", "Select a unit"},
    {"反应单元", "Reaction Unit"},
    {"R-101 主反应釜", "R-101 Main Reactor"},
    {"R-102 备用反应釜", "R-102 Spare Reactor"},
    {"分离单元", "Separation Unit"},
    {"T-201 精馏塔", "T-201 Distillation Column"},
    {"T-202 精馏塔", "T-202 Distillation Column"},
    {"检修中", "under maintenance"},
    {"C-301 冷凝器", "C-301 Condenser"},
    {"长列表（120 项，只渲染可见行）",
     "Long list (120 entries, only visible rows are drawn)"},
    {"请选择配方", "Select a recipe"},
    {"配方 R-", "Recipe R-"},
    {"批次 ", "batch "},
    {"搜索匹配下拉（多字段）", "Searchable Select (multi-field)"},
    {"可搜位号 / 中文描述 / Modbus 地址 / 单位",
     "Searches tag / description / Modbus address / unit"},
    {"输入以搜索位号…", "Type to search tags…"},
    {"试试输入 “温度” / “400” / “MPa” —— 分别命中描述、地址、单位",
     "Try \"temp\" / \"400\" / \"MPa\" — they hit description, address and "
     "unit respectively"},
    {"多选下拉", "Multi Select"},
    {"超过 2 项折叠为摘要", "More than 2 collapses into a summary"},
    {"请选择报警级别", "Select alarm severities"},
    {"紧急", "Critical"},
    {"高", "High"},
    {"中", "Medium"},
    {"低", "Low"},
    {"提示", "Info"},
    {"带全选 / 清空", "With select-all / clear"},
    {"请选择趋势通道", "Select trend channels"},
    {"已选通道：（无）", "Channels: (none)"},
    {"树形下拉", "Tree Select"},
    {"仅叶子可选，显示全路径", "Leaves only, full path shown"},
    {"请选择测点", "Select a measurement point"},
    {"温度 TI-101", "Temperature TI-101"},
    {"压力 PI-201", "Pressure PI-201"},
    {"液位 LI-401", "Level LI-401"},
    {"温度 TI-102", "Temperature TI-102"},
    {"塔顶温度 TI-211", "Overhead temperature TI-211"},
    {"塔釜温度 TI-212", "Bottoms temperature TI-212"},
    {"C-301 冷凝器出口 TI-311", "C-301 condenser outlet TI-311"},
    {"公用工程", "Utilities"},
    {"循环水温度 TI-901", "Cooling water temperature TI-901"},
    {"蒸汽压力 PI-902", "Steam pressure PI-902"},
    {"已选测点：（无）", "Point: (none)"},
    {"动作菜单 / 拆分按钮 / 级联 / 日期",
     "Action Menu / Split Button / Cascader / Date"},
    {"动作菜单", "Action menu"},
    {"批次操作", "Batch actions"},
    {"开始批次", "Start batch"},
    {"暂停批次", "Hold batch"},
    {"终止批次", "Abort batch"},
    {"导出报表", "Export report"},
    {"打印", "Print"},
    {"删除记录", "Delete record"},
    {"拆分按钮", "Split button"},
    {"下发配方", "Download recipe"},
    {"下发并启动", "Download and start"},
    {"仅下发", "Download only"},
    {"下发到备用釜", "Download to spare reactor"},
    {"级联选择", "Cascader"},
    {"选择设备位置", "Select an equipment location"},
    {"一号车间", "Plant 1"},
    {"A 产线", "Line A"},
    {"R-101 反应釜", "R-101 Reactor"},
    {"B 产线", "Line B"},
    {"R-102 反应釜", "R-102 Reactor"},
    {"二号车间", "Plant 2"},
    {"C 产线", "Line C"},
    {"P-401 输送泵", "P-401 Transfer Pump"},
    {"日期选择", "Date picker"},
    {"选择日期", "Pick a date"},
    {"禁用 / 必填未选", "Disabled / required-but-empty"},
    {"必填项未选", "Required, nothing picked"},
    {"靠近底部——应向上弹", "Near the bottom — should open upward"},
    {"选项一", "Option one"},
    {"选项二", "Option two"},
    {"选项三", "Option three"},
    {"选项四", "Option four"},
    {"选项五", "Option five"},
    {"选项六", "Option six"},
    {"运行模式 = ", "Operating mode = "},
    {"设备 = ", "Unit = "},
    {"配方 = ", "Recipe = "},
    {"位号 = ", "Tag = "},
    {"无匹配：\"", "No match: \""},
    // One format string rather than prefix + number + suffix: Chinese puts a
    // counter word after the number and English puts nothing there, and the
    // concatenated form could only express that with an empty translation --
    // which is indistinguishable from a line somebody forgot to fill in.
    {"报警级别已选 %d 项", "%d alarm severities selected"},
    {"已选通道：", "Channels: "},
    {"（无）", "(none)"},
    {"、", ", "},
    {"已选测点：", "Point: "},
    {"测点 = ", "Point = "},
    {"菜单 → ", "Menu → "},
    {"拆分按钮主操作：下发配方",
     "Split button primary action: download recipe"},
    {"拆分按钮菜单 → ", "Split button menu → "},
    {"级联 → ", "Cascader → "},
    {"日期 → ", "Date → "},

    // --------------------------------------------------- PageTables.cpp ---
    {"位号", "Tag"},
    {"名称", "Name"},
    {"区域", "Area"},
    {"普通表格 · 斑马纹 / 列排序 / 行选择",
     "Plain table · zebra stripes / column sort / row selection"},
    {"点击表头排序（升 / 降 / 取消三态）· Ctrl 加选 · Shift 连选",
     "Click a header to sort (asc / desc / off) · Ctrl to add · Shift for a "
     "range"},
    {"已取消排序 —— 顺序回到模型给出的原始顺序",
     "Sort cleared — the order is back to the model's own"},
    {"排序由模型完成，视图只移动了表头的指示符",
     "The model did the sorting; the view only moved the header indicator"},
    {"样式开关", "Style Switches"},
    {"改的是表格属性，不是主题", "These are table properties, not the theme"},
    {"奇偶行变色", "Alternating rows"},
    {"网格线", "Grid lines"},
    {"显示表头", "Show header"},
    {"紧凑行高", "Compact rows"},
    {"空状态", "Empty State"},
    {"没有匹配的仪表", "No matching instrument"},
    {"放宽筛选条件，或检查所选区域",
     "Loosen the filter, or check the area you picked"},
    {"表头仍然在：空的是数据，不是这张表",
     "The header stays: what is empty is the data, not the table"},
    {"加载中", "Loading"},
    {"忙碌", "Busy"},
    {"行仍在下面：刷新不该让操作员丢失位置",
     "The rows are still underneath: a refresh must not lose the operator's "
     "place"},
    {"行内编辑 · 行内下拉 / 多选 / 数值 / 开关 / 勾选 / 进度条 / 操作",
     "Inline editing · select / multi-select / spin / switch / check / "
     "progress / actions"},
    {"类型", "Type"},
    {"压力", "Pressure"},
    {"液位", "Level"},
    {"阀门", "Valve"},
    {"标签", "Labels"},
    {"关键", "Critical"},
    {"联锁", "Interlock"},
    {"常规", "Routine"},
    {"备用", "Spare"},
    {"量程", "Range"},
    {"完成度", "Progress"},
    {"投用", "In Service"},
    {"复核", "Reviewed"},
    {"操作", "Actions"},
    {"编辑", "Edit"},
    {"单击选中一格，再单击它进入编辑 · 回车提交，Esc 还原本格原值",
     "Click a cell to select it, click again to edit · Enter commits, Esc "
     "restores that cell"},
    {"已提交：第 ", "Committed: row "},
    {" 行，第 ", ", column "},
    {" 列 → ", " → "},
    {"已切换：第 ", "Toggled: row "},
    {"开", "on"},
    {"关", "off"},
    {"已删除：", "Deleted: "},
    {"（模型删的行，视图只是重新问了一次行数）",
     " (the model dropped the row; the view merely asked for the count again)"},
    {"操作：第 ", "Action: row "},
    {" 行 → ", " → "},
    {"全选", "Select All"},
    {"清空选择", "Clear Selection"},
    {"已选 0 行", "0 rows selected"},
    {"已选 ", "Selected "},
    {"分页表格 · 分页移动的是模型的窗口，不是视图的能力",
     "Paged table · paging moves the model's window, not the view's ability"},
    {"分页与虚拟滚动不是二选一：视图永远只画看得见的行，分页只决定它拿到哪一段",
     "Paging and virtual scrolling are not alternatives: the view only ever "
     "draws visible rows; paging decides which slice it gets"},

    // ----------------------------------------------- PageTablesTree.cpp ---
    {"采集点 %d 号", "Point no. %d"},
    {"一号反应区", "Reaction Area 1"},
    {"二号反应区", "Reaction Area 2"},
    {"罐区", "Tank Farm"},
    {"码头", "Jetty"},
    {"设备 / 位号", "Equipment / Tag"},
    {"1# 反应釜", "Reactor 1#"},
    {"2# 反应釜", "Reactor 2#"},
    {"3# 罐区", "Tank Farm 3#"},
    {"4# 公用工程", "Utilities 4#"},
    {"站点", "Station"},
    {"已请求子节点，等待返回…（模拟 700ms 网络往返）",
     "Children requested, waiting… (simulated 700 ms round trip)"},
    {"读取失败 —— 分支保留重试标记，不会自己重发",
     "Fetch failed — the branch keeps a retry marker and will not resend on "
     "its own"},
    {"子节点已送达，分支自动展开",
     "Children arrived; the branch expanded itself"},
    {"固定列 · 左侧两列与右侧操作列钉住，中间横向滚动",
     "Frozen columns · two on the left and the action column on the right are "
     "pinned, the middle scrolls"},
    {"Shift + 滚轮横向滚动 · 冻结列上出现的那道边影只在真的有内容藏在下面时才画",
     "Shift + wheel scrolls sideways · the shadow on a frozen column is drawn "
     "only when something really is hidden under it"},
    {"合并行 · 区域列按连续段合并",
     "Spanned rows · the Area column merges consecutive runs"},
    {"被覆盖的格子回答的是「锚点在我上面几行」的负偏移 —— 所以合并在二十万行上也是每格 O(1)",
     "A covered cell answers with a negative offset — \"the anchor is N rows "
     "above me\" — so spanning stays O(1) per cell even at 200 000 rows"},
    {"树形表格 · 区域 → 设备类型 → 仪表",
     "Tree table · area → equipment type → instrument"},
    {"仪表", "Instrument"},
    {"层级 / 位号", "Hierarchy / Tag"},
    {"当前可见行：", "Visible rows: "},
    {" / 节点总数：", " / total nodes: "},
    {"全部展开", "Expand All"},
    {"全部折叠", "Collapse All"},
    {"异步加载子节点 · 展开时才去取，取的过程画在展开箭头上",
     "Async children · fetched only on expand, with progress drawn on the "
     "expander arrow"},
    {"点开任意一个站点：箭头变成转圈，约 700ms 后子节点到达",
     "Open any station: the arrow becomes a spinner and the children land "
     "about 700 ms later"},
    {"每第四次请求故意失败 —— 分支变成重试标记，再点一次才会重发，不会自己轮询",
     "Every fourth request fails on purpose — the branch turns into a retry "
     "marker and only resends when clicked again; it never polls"},
    {"大数据量 · 200 000 行，模型里一行都没有存",
     "Large data · 200 000 rows, not one of them stored in the model"},
    {"瞬时值", "Instant value"},
    {"回到顶部", "Back to Top"},
    {"跳到末行", "Jump to Last"},
    {"定位到第 100 000 行", "Go to Row 100 000"},
    {"拖到任意位置都不会卡：一次绘制只向模型问看得见的那二十来行",
     "Drag anywhere without a stall: one paint asks the model for only the "
     "twenty-odd rows on screen"},

    // ---------------------------------------------------- PageTheme.cpp ---
    // The editable sample sheet.  Only the /* comments */ are prose; the rules
    // between them are the thing being demonstrated and are reproduced
    // verbatim -- translating a selector would break the example it teaches.
    {"/* 选择器示例：改完点「应用样式表」 */\n"
     "\n"
     "/* 按 objectName 选中单个控件 */\n"
     "#emergencyStop {\n"
     "  accent: #FF3B30;\n"
     "  border-radius: 2;\n"
     "  border-width: 2;\n"
     "}\n"
     "\n"
     "/* 按 style class 选中一组控件 */\n"
     ".pill { border-radius: 16; }\n"
     "\n"
     "/* 按类型 + 状态；@token 引用当前主题 */\n"
     "PushButton:hover { border-color: @accent; }\n"
     "\n"
     "/* 后代选择器：只影响这个分组里的进度条 */\n"
     "#styleDemo ProgressBar { accent: @warn; }\n",

     "/* Selector examples: edit, then click Apply Style Sheet */\n"
     "\n"
     "/* Match one widget by objectName */\n"
     "#emergencyStop {\n"
     "  accent: #FF3B30;\n"
     "  border-radius: 2;\n"
     "  border-width: 2;\n"
     "}\n"
     "\n"
     "/* Match a group of widgets by style class */\n"
     ".pill { border-radius: 16; }\n"
     "\n"
     "/* By type + state; @token refers to the current theme */\n"
     "PushButton:hover { border-color: @accent; }\n"
     "\n"
     "/* Descendant selector: only the progress bars in this group */\n"
     "#styleDemo ProgressBar { accent: @warn; }\n"},
    {"当前皮肤：", "Current skin: "},
    {"皮肤（注册表）", "Skins (registry)"},
    {"已注册皮肤", "Registered skins"},
    {"已切换皮肤：", "Skin changed: "},
    {"（整棵控件树立即重绘）", " (the whole widget tree repaints at once)"},
    {"一个皮肤 = Theme（token 结构体）+ 一段样式表。\n\n"
     "库里所有控件每次绘制都从 Theme::current() 取色，"
     "所以换皮肤不需要逐控件通知，整窗重绘一次即可。",
     "A skin = a Theme (the token struct) + a style sheet.\n\n"
     "Every widget in the library reads its colours from Theme::current() on "
     "each paint, so changing a skin needs no per-widget notification — one "
     "full repaint does it."},
    {"主题色", "Accent Colour"},
    {"仪表蓝", "Instrument blue"},
    {"品牌青", "Brand cyan"},
    {"安全绿", "Safety green"},
    {"工程橙", "Engineering orange"},
    {"警示红", "Warning red"},
    {"品牌紫", "Brand purple"},
    {"一个颜色带动整套配色", "One colour drives the whole palette"},
    {"主题色 = ", "Accent = "},
    {"（accent / primary / focusRing / 选区 一起走）",
     " (accent / primary / focusRing / selection all move together)"},
    {"只有派生自品牌色的 token 会动：accent / primary / "
     "focusRing / 选区底色，以及填充按钮上的字色（按亮度自动选黑或白）。\n\n"
     "ok / warn / alarm 不动 —— 报警必须永远是报警色。",
     "Only the tokens derived from the brand colour move: accent / primary / "
     "focusRing / selection fill, plus the label colour on filled buttons "
     "(black or white, chosen by luminance).\n\n"
     "ok / warn / alarm never move — an alarm must always be alarm-coloured."},
    {"样式表（类 QSS）", "Style Sheet (QSS-like)"},
    {"应用样式表", "Apply Style Sheet"},
    {"清空", "Clear"},
    {"样式表已应用：", "Style sheet applied: "},
    {" 条规则生效", " rules in effect"},
    {"样式表有 ", "The style sheet has "},
    {" 处问题：", " problem(s): "},
    {"样式表已清空，只剩皮肤自带规则与 Theme token",
     "Style sheet cleared — only the skin's own rules and the Theme tokens "
     "remain"},
    {"样本控件（上面的规则作用在这里）",
     "Sample Widgets (the rules above act here)"},
    {"紧急停车", "Emergency Stop"},
    {".pill —— 按 style class 命中", ".pill — matched by style class"},
    {"未加类", "no class"},
    {"#styleDemo ProgressBar —— 后代选择器",
     "#styleDemo ProgressBar — descendant selector"},
    {"跟随 accent 的控件", "Widgets that follow accent"},
    {"进料泵", "Feed pump"},
    {"安全联锁", "Safety interlock"},
    {"只读字段 :read-only", "read-only field :read-only"},
    {"语法速查", "Syntax Reference"},
    // The syntax cheat sheet, one key.  Only the four headings and the two
    // parenthetical glosses are prose; the token lists are syntax.
    {"选择器\n"
     "  *   PushButton   .danger   #pump1\n"
     "  GroupBox Label（后代）  A, B（分组）\n"
     "状态\n"
     "  :hover :pressed :checked :focus\n"
     "  :disabled :read-only :invalid :open\n"
     "属性\n"
     "  color  background  border-color\n"
     "  border-width  border-radius\n"
     "  font-size  accent  icon-color  padding\n"
     "颜色\n"
     "  #RGB  #RRGGBB  #AARRGGBB\n"
     "  rgb() rgba() transparent\n"
     "  @accent @panel @text …（跟随当前主题）",

     "Selectors\n"
     "  *   PushButton   .danger   #pump1\n"
     "  GroupBox Label (descendant)  A, B (group)\n"
     "States\n"
     "  :hover :pressed :checked :focus\n"
     "  :disabled :read-only :invalid :open\n"
     "Properties\n"
     "  color  background  border-color\n"
     "  border-width  border-radius\n"
     "  font-size  accent  icon-color  padding\n"
     "Colours\n"
     "  #RGB  #RRGGBB  #AARRGGBB\n"
     "  rgb() rgba() transparent\n"
     "  @accent @panel @text … (follow the current theme)"},

    // -------------------------------------------------- PageWidgets.cpp ---
    {"按钮与开关", "Buttons and Switches"},
    {"普通按钮", "Plain button"},
    {"手动/自动", "Manual/Auto"},
    {"启用安全联锁", "Enable safety interlock"},
    {"记录历史曲线", "Record history"},
    {"远程写入（无权限）", "Remote write (no permission)"},
    {"运行模式（单选组）", "Operating Mode (radio group)"},
    {"自动", "Auto"},
    {"批次进度", "Batch progress"},
    {"料位 72%", "Level 72%"},
    {"参数设定", "Parameter Setting"},
    {"目标温度", "Target temperature"},
    {"压力上限", "Pressure limit"},
    {"重复次数", "Repeat count"},
    {"状态：就绪 · Tab / Shift+Tab 切换焦点，空格激活，方向键调值",
     "Status: ready · Tab / Shift+Tab moves focus, Space activates, arrows "
     "adjust"},
    {"SpinBox：↑↓ 步进，PgUp/PgDn ×10，可直接键入数字",
     "SpinBox: ↑↓ steps, PgUp/PgDn ×10, or type a number"},
    {"设定值滑块", "Set-point Sliders"},
    {"阀门开度", "Valve opening"},
    {"（禁用示例）", "(disabled example)"},
    {"联锁：整组禁用", "Interlock: Whole Group Disabled"},
    {"允许修改下列参数", "Allow the parameters below to be changed"},
    {"旁路阀", "Bypass valve"},
    {"普通按钮被点击", "Plain button clicked"},
    {"已切到自动", "Switched to Auto"},
    {"已切到手动", "Switched to Manual"},
    {"进料泵 启动", "Feed pump started"},
    {"进料泵 停止", "Feed pump stopped"},
    {"安全联锁 开", "Safety interlock on"},
    {"安全联锁 关", "Safety interlock off"},
    {"开始记录曲线", "History recording started"},
    {"停止记录曲线", "History recording stopped"},
    {"模式 → 停机", "Mode → Stopped"},
    {"模式 → 手动", "Mode → Manual"},
    {"模式 → 自动", "Mode → Auto"},

    // --------------------------------------------------- PageWindow.cpp ---
    {"提示：按住标题栏空白处即可拖动窗口，双击可最大化 / 还原。",
     "Tip: drag the empty part of the title bar to move the window; "
     "double-click to maximise / restore."},
    {"标题栏度量", "Title Bar Metrics"},
    {"标题栏高度", "Title bar height"},
    {"标题栏高度 = ", "Title bar height = "},
    {"窗口按钮宽度", "Window button width"},
    {"窗口按钮宽度 = ", "Window button width = "},
    {"左侧留白", "Left padding"},
    {"标题与图标", "Title and Icon"},
    {"标题文本", "Title text"},
    {"副标题", "Subtitle"},
    {"工控 HMI 控件库 · 演示工程",
     "Industrial HMI widget library · demo project"},
    {"图标", "Icon"},
    {"设置齿轮", "Settings gear"},
    {"信息", "Info"},
    {"报警", "Alarm"},
    {"全球", "Globe"},
    {"泵（自定义）", "Pump (custom)"},
    {"阀（自定义）", "Valve (custom)"},
    {"罐（自定义）", "Tank (custom)"},
    {"闪电（SVG）", "Bolt (SVG)"},
    {"无图标", "No icon"},
    {"图标底板", "Icon plate"},
    {"配色", "Colours"},
    {"面板灰（默认）", "Panel grey (default)"},
    {"深空蓝", "Deep space blue"},
    {"石墨黑", "Graphite"},
    {"工业绿", "Industrial green"},
    {"警示棕", "Caution brown"},
    {"标题栏底色", "Title bar background"},
    {"仪表蓝（默认）", "Instrument blue (default)"},
    {"警戒橙", "Alert orange"},
    {"报警红", "Alarm red"},
    {"图标 / 强调色", "Icon / accent"},
    {"标题栏下沿分隔线", "Title bar bottom rule"},
    {"窗口外框线", "Window outline"},
    {"窗口按钮", "Window Buttons"},
    {"显示「最小化」", "Show Minimise"},
    {"显示「最大化 / 还原」", "Show Maximise / Restore"},
    {"显示「关闭」", "Show Close"},
    {"标题栏空白处可拖动窗口",
     "Empty title bar area drags the window"},
    {"标题栏已可拖动", "Title bar drag enabled"},
    {"标题栏已锁定，窗口无法拖动",
     "Title bar locked; the window cannot be dragged"},
    {"窗口命令", "Window Commands"},
    {"与标题栏右上角的按钮是同一套 API（Window 提供）",
     "Same API as the corner buttons (from Window)"},
    {"最小化", "Minimise"},
    {"最大化 / 还原", "Max / Restore"},  // 130px button
    {"隐藏标题栏", "Hide Title Bar"},
    {"标题栏已隐藏（全屏画面模式）",
     "Title bar hidden (full-screen mimic mode)"},
    {"标题栏已恢复", "Title bar restored"},
    {"关闭窗口", "Close Window"},
    {"这一层做了什么", "What This Layer Does"},
    // Hard-wrapped to a fixed column by hand -- the Label it feeds is a fixed
    // width, so the line breaks are layout, not punctuation.  The English is
    // re-wrapped to roughly the same column rather than translated line by
    // line, which would have produced ragged or overflowing lines.
    {"AppWindow 是无边框窗口：Windows 不再绘制标题栏、\n"
     "边框和主题色，全部由 WindowHeader 用同一套\n"
     "Painter / Theme 画出来。\n\n"
     "但它仍是一个正常的顶层窗口——贴边分屏、最小化\n"
     "动画、Alt+Tab 缩略图、双击标题栏最大化都还在，\n"
     "因为拖动区是作为「窗口标题区」上报给系统的。",

     "AppWindow is frameless: Windows draws no\n"
     "title bar, border or accent — WindowHeader\n"
     "paints all of it with one Painter / Theme.\n\n"
     "Still an ordinary top-level window: snap\n"
     "layouts, minimise animation and Alt+Tab\n"
     "still work, because the drag area is\n"
     "reported to the system as the caption."},
    {"图标扩展（IconRegistry）—— 上面「图标」下拉里的自定义项就是这些",
     "Icon extensions (IconRegistry) — these are the custom entries in the "
     "Icon dropdown above"},
    {"前三个用 IconCanvas 代码绘制，其余用 SVG path 注册（marker 是填充式）。"
     "每个都以 32 / 20 / 14 px 三种尺寸绘制——同一份定义，无需按 DPI 出图。",
     "The first three are drawn in code with IconCanvas, the rest from SVG "
     "paths (marker is filled).  Each renders at 32 / 20 / 14 px from one "
     "definition."},
    {"已注册自定义图标 ", "Custom icons registered: "},
    // One literal, not three: the "pump" inside it is hard-coded, so there is
    // nothing concatenated in the middle to split the sentence around.
    {" 个，SVG 路径全部解析通过；名字可直接 icons().find(\"pump\") 取回",
     ", every SVG path parsed; a name goes straight into "
     "icons().find(\"pump\")"},
    {"SVG 解析有 ", "SVG parsing had "},

    // -------------------------------------------------- PlantIcons.cpp ---
    {"设备", "Equipment"},

    // ------------------------------------------------------- Shell.cpp ---
    {"控件库演示", "Widget Demo"},

    // ---------------------------------------------- ShowcaseWindow.cpp ---
    {"GeeyoouUI 控件库演示", "GeeyoouUI Widget Showcase"},
    {"PI-201 系统压力超高限", "PI-201 system pressure above high-high limit"},
    {"TI-102 釜内温度预警", "TI-102 reactor temperature warning"},
    {"Modbus 从站响应超时", "Modbus slave response timeout"},
    {"全部标记为已读", "Mark all as read"},
    {"通知菜单：", "Notification menu: "},
    // (no "语言切换：" entry: picking a language rebuilds the pages, so an
    // activity-log line about it would be wiped by the very act it reports.)
    {"张", "Z"},
    {"张工", "Zhang"},
    {"值班工程师", "Duty Engineer"},
    {"个人资料", "Profile"},
    {"偏好设置", "Preferences"},
    {"操作日志", "Audit Log"},
    {"退出登录", "Sign Out"},
    {"账户菜单：", "Account menu: "},

    // ------------------------------------------------ TableDemoData.hpp ---
    {"关键, 联锁", "Critical, Interlock"},
    {"常规, 备用", "Routine, Spare"},
    {"变送器 ", "Transmitter "},

    // -------------------------------------------------------- main.cpp ---
    {"总览", "Overview"},
    {"概览", "At a Glance"},
    {"库的构成、分层规模与未实现清单",
     "What the library is made of, how big each layer is, what is not "
     "implemented"},
    {"窗口外壳", "Window Shell"},
    {"无边框窗口、自绘标题栏与可配置属性",
     "Frameless window, self-drawn title bar, configurable properties"},
    {"主题与皮肤", "Theme and Skins"},
    {"皮肤注册表 / 主题色 / 类 QSS 选择器",
     "Skin registry / accent colour / QSS-like selectors"},
    {"图标库", "Icon Library"},
    {"全部内置与自定义图标 · 搜索 / 尺寸对比 / 用法",
     "Every built-in and custom icon · search / size comparison / usage"},
    {"演示画面", "Demo Screens"},
    {"HMI 监控", "HMI Monitor"},
    {"仪表 / 指示灯 / 实时趋势", "Gauges / status lamps / live trends"},
    {"运维控制台", "Ops Console"},
    {"报警列表 / 滚动表单 / 采集队列",
     "Alarm list / scrolling form / acquisition queue"},
    {"布局引擎", "Layout Engine"},
    {"拖窗口边缘看 stretch / min-max / 跨列的实时反应",
     "Drag the window edge to watch stretch / min-max / spans react live"},
    {"控件族", "Widget Families"},
    {"基础控件", "Basic Widgets"},
    {"按钮 / 开关 / 单选 / 滑块 / 数值设定",
     "Buttons / switches / radios / sliders / spin boxes"},
    {"输入与按钮", "Inputs and Buttons"},
    {"文本输入族与按钮变体、图标按钮",
     "The text-input family, button variants and icon buttons"},
    {"下拉选择", "Selects"},
    {"单选 / 搜索 / 多选 / 树形 / 级联 / 菜单 / 日期",
     "Single / searchable / multi / tree / cascader / menu / date"},
    {"表格", "Tables"},
    {"基础表格", "Basic Table"},
    {"普通表格 / 斑马纹 / 列排序 / 空状态 / 加载中",
     "Plain table / zebra stripes / column sort / empty state / loading"},
    {"行内控件与编辑", "Inline Editing"},
    {"行内编辑 / 下拉 / 多选 / 数值 / 开关 / 勾选 / 进度条 / 操作列",
     "Inline edit / select / multi-select / spin / switch / check / progress / "
     "action column"},
    {"分页表格", "Paged Table"},
    {"分页控件 / 每页条数 / 保持操作员所在位置",
     "Pager widget / page size / keeping the operator's place"},
    {"固定列与合并", "Frozen & Spans"},  // rail is 212px: must stay short
    {"左右冻结列 / 横向滚动 / 合并行",
     "Frozen left and right columns / horizontal scroll / spanned rows"},
    {"树形表格", "Tree Table"},
    {"层级展开 / 全展开 / 全折叠",
     "Hierarchical expand / expand all / collapse all"},
    {"异步树形表格", "Async Tree Table"},
    {"展开时才取子节点 / 转圈 / 失败重试",
     "Children fetched on expand / spinner / retry on failure"},
    {"大数据量表格", "Large Data Table"},
    {"20 万行虚拟滚动 · 模型里一行都不存",
     "200 000 rows virtualised · not one row stored in the model"},
    {"三维", "3D"},
    {"设备三维视图", "3D Equipment View"},
    {"反应釜撑块 · 旋转/缩放/平移 · 点选部件 · 状态着色",
     "Reactor skid · orbit/zoom/pan · click to select a part · status "
     "colouring"},
    {"系统压力超过高限", "System pressure above the high limit"},
    {"釜内温度进入预警区", "Reactor temperature entered the warning band"},
    {"Modbus 从站响应超时，已重试", "Modbus slave response timeout, retried"},

    // ------------------------------------------------- PageDialogs.cpp ---
    {"对话框与新控件", "Dialogs & New Widgets"},
    {"模态对话框 / 标签页 / 数字键盘 / 右键菜单 / 棒图",
     "Modal dialogs / tabs / numeric keypad / context menu / bargraph"},
    {"状态：就绪 · 试试下面的对话框、键盘、右键菜单",
     "Status: ready · try the dialogs, keypad and context menu below"},
    {"标签页 · 每页一组棒图", "Tabs · a group of bargraphs per page"},
    {"对话框 · 模态遮罩", "Dialogs · a modal scrim"},
    {"消息框", "Message box"},
    {"配方已下发到 2# 反应釜。", "Recipe downloaded to Reactor 2#."},
    {"知道了", "Got it"},
    {"消息框：已确认", "Message box: acknowledged"},
    {"确认框（危险操作）", "Confirm (dangerous)"},
    {"确认操作", "Confirm action"},
    {"确定要停止进料泵 P-101 吗？此操作会中断当前批次。",
     "Stop feed pump P-101?  This will interrupt the current batch."},
    {"确认框：操作员确认了停泵",
     "Confirm: the operator confirmed the stop"},
    {"停止", "Stop"},
    {"取消", "Cancel"},
    {"数字键盘设定目标温度（触摸屏）",
     "Set the target temperature on the keypad (touchscreen)"},
    {"已设定目标温度 = %.1f °C", "Target temperature set = %.1f °C"},
    {"对话框是全窗口遮罩 + 居中面板：它靠几何实现模态，背后点不到。\n\n"
     "触摸屏没有物理键盘，改设定值必须弹屏上数字键盘——工控现场这是刚需。",
     "A dialog is a full-window scrim plus a centred panel: it is modal by "
     "geometry, so nothing behind it can be clicked.\n\n"
     "A touchscreen has no physical keyboard, so changing a set point means "
     "popping up an on-screen keypad — on a plant floor that is essential, not "
     "a nicety."},
    {"右键上下文菜单", "Right-click context menu"},
    {"在这块区域点右键", "Right-click in this area"},
    {"确认报警", "Acknowledge"},
    {"屏蔽此点", "Shelve point"},
    {"查看历史", "View history"},
    {"删除记录", "Delete record"},
    {"右键菜单 → ", "Context menu → "},
};

}  // namespace

const LangPack& enUSPack() {
  static const LangPack kPack{
      "en-US",
      "English",
      kEntries,
      sizeof(kEntries) / sizeof(kEntries[0]),
  };
  return kPack;
}

}  // namespace showcase
