#include "geeyoou/render/Skin.hpp"

#include <algorithm>

namespace geeyoou {
namespace {

// Perceived brightness, ITU-R BT.709.  Used to decide whether the label on a
// filled button should be black ink or white -- get this wrong and a "start
// pump" button becomes unreadable on a sunlit plant floor.
float luminance(Color c) {
  return 0.2126f * float(c.red()) + 0.7152f * float(c.green()) +
         0.0722f * float(c.blue());
}

Color inkFor(Color fill) {
  return luminance(fill) > 140.0f ? Color::rgb(0x0B, 0x0F, 0x16)
                                  : Color::rgb(0xFF, 0xFF, 0xFF);
}

}  // namespace

// ------------------------------------------------------------ built-in skins ---
Theme darkTheme() {
  return Theme{};  // the struct's own defaults are the dark industrial palette
}

Theme lightTheme() {
  Theme t;
  t.background = Color::rgb(0xF4, 0xF6, 0xFA);
  t.panel = Color::rgb(0xFF, 0xFF, 0xFF);
  t.panelBorder = Color::rgb(0xD3, 0xDA, 0xE6);
  t.text = Color::rgb(0x17, 0x1F, 0x2E);
  t.textDim = Color::rgb(0x5A, 0x68, 0x80);
  t.textDisabled = Color::rgb(0xA7, 0xB0, 0xC0);
  t.accent = Color::rgb(0x1B, 0x6F, 0xD6);
  t.ok = Color::rgb(0x1E, 0x9E, 0x5A);
  t.warn = Color::rgb(0xB2, 0x6A, 0x00);
  t.alarm = Color::rgb(0xD6, 0x29, 0x3E);
  t.grid = Color::rgb(0xE4, 0xE9, 0xF1);
  t.track = Color::rgb(0xE1, 0xE6, 0xEF);
  t.focusRing = Color::rgb(0x4A, 0x96, 0xE8);
  t.field = Color::rgb(0xFF, 0xFF, 0xFF);
  t.placeholder = Color::rgb(0x9A, 0xA5, 0xB8);
  t.selection = Color::rgb(0xBB, 0xD9, 0xF7);
  t.scrollbar = Color::rgb(0xB6, 0xC0, 0xD0);
  t.primary = t.accent;
  t.success = t.ok;
  t.danger = t.alarm;
  // Every semantic fill above is dark enough for white ink -- chosen that way
  // on purpose, because onFilled is a single token shared by all of them.
  t.onFilled = Color::rgb(0xFF, 0xFF, 0xFF);
  return t;
}

Theme highContrastTheme() {
  Theme t;
  t.background = Color::rgb(0x00, 0x00, 0x00);
  t.panel = Color::rgb(0x0A, 0x0A, 0x0A);
  t.panelBorder = Color::rgb(0xFF, 0xFF, 0xFF);
  t.text = Color::rgb(0xFF, 0xFF, 0xFF);
  t.textDim = Color::rgb(0xC8, 0xC8, 0xC8);
  t.textDisabled = Color::rgb(0x6E, 0x6E, 0x6E);
  t.accent = Color::rgb(0x00, 0xE0, 0xFF);
  t.ok = Color::rgb(0x00, 0xFF, 0x66);
  t.warn = Color::rgb(0xFF, 0xD4, 0x00);
  t.alarm = Color::rgb(0xFF, 0x2D, 0x2D);
  t.grid = Color::rgb(0x30, 0x30, 0x30);
  t.track = Color::rgb(0x26, 0x26, 0x26);
  t.focusRing = Color::rgb(0xFF, 0xFF, 0xFF);
  t.field = Color::rgb(0x00, 0x00, 0x00);
  t.placeholder = Color::rgb(0x8A, 0x8A, 0x8A);
  t.selection = Color::rgb(0x00, 0x5E, 0x8A);
  t.scrollbar = Color::rgb(0x90, 0x90, 0x90);
  t.primary = t.accent;
  t.success = t.ok;
  t.danger = t.alarm;
  t.onFilled = Color::rgb(0x00, 0x00, 0x00);
  // Bigger type and squarer corners: this skin exists for gloved operators
  // reading a panel in direct sunlight, not for looking modern.
  t.radius = 2.0f;
  t.fontSmall = 12.0f;
  t.fontBody = 14.0f;
  t.fontLarge = 24.0f;
  return t;
}

Theme amberTheme() {
  Theme t;
  t.background = Color::rgb(0x14, 0x0F, 0x06);
  t.panel = Color::rgb(0x1E, 0x17, 0x08);
  t.panelBorder = Color::rgb(0x4A, 0x3A, 0x14);
  t.text = Color::rgb(0xFF, 0xC2, 0x4D);
  t.textDim = Color::rgb(0xB8, 0x86, 0x2E);
  t.textDisabled = Color::rgb(0x6B, 0x52, 0x20);
  t.accent = Color::rgb(0xFF, 0xA0, 0x00);
  t.ok = Color::rgb(0xC8, 0xD2, 0x00);
  t.warn = Color::rgb(0xFF, 0xB0, 0x20);
  t.alarm = Color::rgb(0xFF, 0x5A, 0x2D);
  t.grid = Color::rgb(0x32, 0x27, 0x10);
  t.track = Color::rgb(0x35, 0x29, 0x0F);
  t.focusRing = Color::rgb(0xFF, 0xC2, 0x4D);
  t.field = Color::rgb(0x12, 0x0D, 0x05);
  t.placeholder = Color::rgb(0x7A, 0x5D, 0x22);
  t.selection = Color::rgb(0x5C, 0x43, 0x10);
  t.scrollbar = Color::rgb(0x6B, 0x52, 0x20);
  t.primary = t.accent;
  t.success = t.ok;
  t.danger = t.alarm;
  t.onFilled = Color::rgb(0x1A, 0x12, 0x04);
  return t;
}

Theme themeWithAccent(const Theme& base, Color accent) {
  Theme t = base;
  t.accent = accent;
  t.primary = accent;
  // The focus ring has to stay visible against the accent it usually sits
  // beside, so it is the accent pulled towards the foreground, not the accent.
  t.focusRing = accent.lerp(base.text, 0.35f);
  t.selection = accent.lerp(base.background, 0.55f);
  t.onFilled = inkFor(accent);
  // ok / warn / alarm are deliberately NOT touched: an alarm must read as an
  // alarm regardless of the customer's brand colour.
  return t;
}

// -------------------------------------------------------------- the registry ---
SkinRegistry& SkinRegistry::instance() {
  static SkinRegistry r;
  return r;
}

SkinRegistry::SkinRegistry() {
  skins_.push_back({"dark", "工业深色", darkTheme(), ""});
  skins_.push_back({"light", "浅色办公", lightTheme(), ""});
  skins_.push_back({"contrast", "高对比（强光）", highContrastTheme(),
                    // Shows the selector layer doing what a palette cannot: a
                    // heavier outline on every control, not a different colour.
                    "PushButton { border-width: 2; }\n"
                    "GroupBox { border-width: 2; }\n"});
  skins_.push_back({"amber", "琥珀（传统 HMI）", amberTheme(), ""});
  apply("dark");
}

void SkinRegistry::add(Skin skin) {
  const std::string name = skin.name;
  for (Skin& s : skins_) {
    if (s.name == name) {
      s = std::move(skin);
      if (current_ == name) apply(name);  // live edit of the active skin
      return;
    }
  }
  skins_.push_back(std::move(skin));
}

bool SkinRegistry::remove(const std::string& name) {
  for (auto it = skins_.begin(); it != skins_.end(); ++it) {
    if (it->name != name) continue;
    const bool wasActive = (current_ == name);
    skins_.erase(it);
    if (wasActive && !skins_.empty()) apply(skins_.front().name);
    return true;
  }
  return false;
}

const Skin* SkinRegistry::find(const std::string& name) const {
  for (const Skin& s : skins_) {
    if (s.name == name) return &s;
  }
  return nullptr;
}

std::vector<std::string> SkinRegistry::names() const {
  std::vector<std::string> out;
  out.reserve(skins_.size());
  for (const Skin& s : skins_) out.push_back(s.name);
  return out;
}

std::vector<const Skin*> SkinRegistry::all() const {
  std::vector<const Skin*> out;
  out.reserve(skins_.size());
  for (const Skin& s : skins_) out.push_back(&s);
  return out;
}

bool SkinRegistry::apply(const std::string& name) {
  const Skin* s = find(name);
  if (!s) return false;
  current_ = name;

  // Order matters: the theme goes in FIRST, because the style sheet's `@token`
  // references are resolved at parse time against the live theme.
  Theme::current() = s->theme;
  activeStyleSheet().parse(s->styleSheet);

  bumpStyleGeneration();
  changed.emit();
  return true;
}

void SkinRegistry::setAccent(Color accent) {
  Theme::current() = themeWithAccent(Theme::current(), accent);
  // Re-parse rather than keep the old rules: any `@accent` in the sheet was
  // baked into a literal when it was parsed, and must be re-resolved now.
  activeStyleSheet().parse(activeStyleSheet().source());
  bumpStyleGeneration();
  changed.emit();
}

void SkinRegistry::reloadStyleSheet(std::string_view qss) {
  activeStyleSheet().parse(qss);
  bumpStyleGeneration();
  changed.emit();
}

}  // namespace geeyoou
