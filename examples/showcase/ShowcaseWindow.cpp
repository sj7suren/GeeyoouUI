#include "ShowcaseWindow.hpp"

#include "PlantIcons.hpp"
#include "geeyoou/render/IconRegistry.hpp"
#include "geeyoou/render/Skin.hpp"
#include "geeyoou/render/StyleSheet.hpp"
#include "geeyoou/render/Theme.hpp"
#include "i18n/I18n.hpp"

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
    : AppWindow(tr("GeeyoouUI 控件库演示"), 1320, 860) {
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
  h->setTitleFontSize(14.0f);

  // ------------------------------------------------- 通知 / 语言 / 账户 ---
  // Added left-to-right, so the account block ends up nearest the corner.
  // Only the WIRING is here; every string comes from applyHeaderTexts() below,
  // which runs again whenever the language changes.
  bell_ = h->addTrailingItem<HeaderMenu>(38.0f);
  bell_->setIcon(Icon::Bell);
  bell_->setShowChevron(false);
  bell_->setBadgeCount(3);
  bell_->triggered.connect([this](const std::string& id) {
    if (id == "ack-all") bell_->setBadgeCount(0);
    headerAction.emit(tr("通知菜单：") + id);
  });

  language_ = h->addTrailingItem<HeaderMenu>(0.0f);
  language_->setIcon(Icon::Globe);
  // The menu index IS the language index -- applyHeaderTexts() builds the item
  // list straight from the pack list, in order, so there is no id-to-language
  // lookup table to keep in step with i18n/I18n.cpp.
  language_->triggeredIndex.connect([](int i) { setLang(i); });

  h->addTrailingGap(6.0f);
  account_ = h->addTrailingItem<HeaderAvatar>(0.0f);
  account_->triggered.connect(
      [this](const std::string& id) { headerAction.emit(tr("账户菜单：") + id); });

  // ------------------------------------------------------------- 内容区 ---
  // setContent keeps the shell sized to the content area, so the shell never
  // learns that a title bar exists above it.
  shell_ = setContent<Shell>();
  shell_->sidebar()->setBrandVisible(false);

  // ---------------------------------------------------------------- 语言 ---
  applyHeaderTexts();

  // A language change re-labels the header in place and rebuilds every page.
  // The header is deliberately NOT rebuilt: it holds the menu the click came
  // from, and destroying that mid-emit is exactly the D7 violation
  // core/Signal.hpp refuses to make safe.
  conns_ += langChanged().connect([this] {
    applyHeaderTexts();
    shell_->rebuildPages();
  });

  // The showcase's cross-page subscription: pages connect their activity logs
  // to headerAction, capturing a Label that lives inside the page.  Before the
  // shell destroys those pages, drop the lot -- otherwise the next header click
  // writes into a freed Label.  Pages re-subscribe as they are rebuilt.
  conns_ += shell_->pagesAboutToRebuild.connect(
      [this] { headerAction.disconnectAll(); });

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

void ShowcaseWindow::applyHeaderTexts() {
  WindowHeader* h = header();
  setTitle(tr("GeeyoouUI 控件库演示"));
  h->setTitle("GeeyoouUI");  // the product mark is not translated
  h->setSubtitle(tr("工控 HMI 控件库 · 演示工程"));

  bell_->setItems({
      MenuItem(tr("PI-201 系统压力超高限"), "alarm-press", Icon::Warning),
      MenuItem(tr("TI-102 釜内温度预警"), "alarm-temp", Icon::Warning),
      MenuItem(tr("Modbus 从站响应超时"), "alarm-comm", Icon::Info),
      MenuItem::sep(),
      MenuItem(tr("全部标记为已读"), "ack-all", Icon::Check),
  });

  // Language names are NOT translated: a language always writes its own name in
  // itself.  Someone looking for 简体中文 in an English UI is looking for those
  // four characters, and "Simplified Chinese" is no use to them.
  std::vector<MenuItem> langs;
  const int n = langCount();
  langs.reserve(std::size_t(n));
  for (int i = 0; i < n; ++i) langs.push_back(MenuItem(langNativeName(i), langId(i)));
  language_->setItems(std::move(langs));
  // The menu's own label is the switcher's state -- exactly the affordance a
  // web admin console uses, and it needs no separate indicator.
  language_->setText(langNativeName(lang()));
  h->setTrailingItemWidth(language_, language_->preferredWidth());

  account_->setInitials(tr("张"));
  account_->setName(tr("张工"));
  account_->setCaption(tr("值班工程师"));
  account_->setItems({
      MenuItem(tr("个人资料"), "profile", Icon::User),
      MenuItem(tr("偏好设置"), "prefs", Icon::Settings),
      MenuItem(tr("操作日志"), "audit", Icon::Copy),
      MenuItem::sep(),
      MenuItem(tr("退出登录"), "logout", Icon::Logout),
  });
  h->setTrailingItemWidth(account_, account_->preferredWidth());
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
