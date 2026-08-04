#include "ShowcaseWindow.hpp"

#include "PlantIcons.hpp"
#include "geeyoou/render/IconRegistry.hpp"
#include "geeyoou/render/Skin.hpp"
#include "geeyoou/render/StyleSheet.hpp"
#include "geeyoou/render/Theme.hpp"

namespace showcase {

using namespace geeyoou;

namespace {
// The application's own rules, applied under every skin.
//
// `.caption` exists so the demo pages never hard-code a dim grey: a literal
// colour captured at build time is the classic reason a light skin comes out
// unreadable, and this is the one-line cure.
const char* kBaseStyleSheet =
    ".caption { color: @textDim; }\n";
}  // namespace

ShowcaseWindow::ShowcaseWindow()
    : AppWindow("GeeyoouUI 控件库演示", 1320, 860) {
  // Before any widget asks for one: the pack has to be registered while ids are
  // still being handed out, not after something has already stored an Icon.
  registerPlantIcons();

  // ------------------------------------------------------------- 标题栏 ---
  WindowHeader* h = header();
  h->setHeight(48.0f);
  // NOTE: no colours here.  Anything DERIVED from the theme (this bar's tint is
  // panel pulled 20% towards the background) has to be recomputed when the skin
  // changes, so it lives in applyHeaderTheme() below.  Setting it once in the
  // constructor is what left the title bar dark under a light skin.
  h->setIcon(Icon::Settings);
  h->setIconBadge(true);
  h->setIconSize(18.0f);
  h->setTitle("GeeyoouUI");
  h->setSubtitle("工控 HMI 控件库 · 演示工程");
  h->setTitleFontSize(14.0f);

  // ------------------------------------------------- 通知 / 语言 / 账户 ---
  // Added left-to-right, so the account block ends up nearest the corner.
  bell_ = h->addTrailingItem<HeaderMenu>(38.0f);
  bell_->setIcon(Icon::Bell);
  bell_->setShowChevron(false);
  bell_->setBadgeCount(3);
  bell_->setItems({
      MenuItem("PI-201 系统压力超高限", "alarm-press", Icon::Warning),
      MenuItem("TI-102 釜内温度预警", "alarm-temp", Icon::Warning),
      MenuItem("Modbus 从站响应超时", "alarm-comm", Icon::Info),
      MenuItem::sep(),
      MenuItem("全部标记为已读", "ack-all", Icon::Check),
  });
  bell_->triggered.connect([this](const std::string& id) {
    if (id == "ack-all") bell_->setBadgeCount(0);
    headerAction.emit("通知菜单：" + id);
  });

  language_ = h->addTrailingItem<HeaderMenu>(0.0f);
  language_->setIcon(Icon::Globe);
  language_->setText("简体中文");
  language_->setItems({
      MenuItem("简体中文", "zh-CN"),
      MenuItem("English", "en-US"),
      MenuItem("日本語", "ja-JP"),
  });
  language_->triggeredIndex.connect([this](int i) {
    // The menu's own label is the switcher's state -- exactly the affordance a
    // web admin console uses, and it needs no separate indicator.
    const auto& items = language_->items();
    if (i < 0 || i >= int(items.size())) return;
    language_->setText(items[std::size_t(i)].text);
    header()->setTrailingItemWidth(language_, language_->preferredWidth());
    headerAction.emit("语言切换：" + items[std::size_t(i)].text);
  });
  h->setTrailingItemWidth(language_, language_->preferredWidth());

  h->addTrailingGap(6.0f);
  account_ = h->addTrailingItem<HeaderAvatar>(0.0f);
  account_->setInitials("张");
  account_->setName("张工");
  account_->setCaption("值班工程师");
  account_->setItems({
      MenuItem("个人资料", "profile", Icon::User),
      MenuItem("偏好设置", "prefs", Icon::Settings),
      MenuItem("操作日志", "audit", Icon::Copy),
      MenuItem::sep(),
      MenuItem("退出登录", "logout", Icon::Logout),
  });
  account_->triggered.connect(
      [this](const std::string& id) { headerAction.emit("账户菜单：" + id); });
  h->setTrailingItemWidth(account_, account_->preferredWidth());

  // ------------------------------------------------------------- 内容区 ---
  // setContent keeps the shell sized to the content area, so the shell never
  // learns that a title bar exists above it.
  shell_ = setContent<Shell>();
  shell_->sidebar()->setBrandVisible(false);

  // ---------------------------------------------------------------- 样式 ---
  applyHeaderTheme();
  composeStyleSheet();
  // Applying a skin installs that skin's sheet, wiping ours -- so recompose
  // afterwards.  Safe from recursion: composeStyleSheet writes the sheet
  // directly instead of going back through skins(), so it emits nothing.
  conns_ += skins().changed.connect([this] {
    applyHeaderTheme();
    composeStyleSheet();
  });
}

void ShowcaseWindow::applyHeaderTheme() {
  const Theme& t = Theme::current();
  header()->setBackground(t.panel.lerp(t.background, 0.2f));
  account_->setStatusColor(t.ok);
}

ShowcaseWindow::~ShowcaseWindow() = default;

void ShowcaseWindow::setUserStyleSheet(std::string qss) {
  userQss_ = std::move(qss);
  composeStyleSheet();
}

void ShowcaseWindow::composeStyleSheet() {
  std::string s;
  if (const Skin* sk = skins().current()) s = sk->styleSheet;
  s += "\n";
  s += kBaseStyleSheet;
  s += "\n";
  s += userQss_;
  activeStyleSheet().parse(s);
  bumpStyleGeneration();
  update();
}

}  // namespace showcase
