#pragma once
//
// Skins: named, registered, switchable at runtime.
//
// A skin is a Theme (the token struct every widget already reads) plus an
// optional StyleSheet (the selector layer on top).  Registering them by name is
// what turns "recompile per customer" into "ship a different string".
//
// The Theme half is where the leverage is: every widget in the library resolves
// its colours through Theme::current() on each paint, so swapping the theme
// re-skins ALL of them with no per-widget work.  The style sheet then handles
// the cases a global palette cannot express -- "this plant wants square buttons
// and a red emergency stop".
//
#include <string>
#include <vector>

#include "geeyoou/core/Signal.hpp"
#include "geeyoou/render/StyleSheet.hpp"
#include "geeyoou/render/Theme.hpp"

namespace geeyoou {

struct Skin {
  std::string name;   // lookup key, e.g. "dark"
  std::string title;  // shown to the operator, e.g. "工业深色"
  Theme theme;
  std::string styleSheet;  // QSS source; parsed when the skin is applied
};

class SkinRegistry {
 public:
  static SkinRegistry& instance();

  // Registers (or replaces) a skin.  Replacing the ACTIVE skin re-applies it,
  // so editing a skin in a settings screen shows up immediately.
  void add(Skin skin);
  bool remove(const std::string& name);

  const Skin* find(const std::string& name) const;
  std::vector<std::string> names() const;
  std::vector<const Skin*> all() const;

  // Installs the skin's theme and style sheet, then fires changed().
  // Returns false when no such skin is registered.
  bool apply(const std::string& name);
  const std::string& currentName() const { return current_; }
  const Skin* current() const { return find(current_); }

  // --- 主题色 --------------------------------------------------------------
  // Recolours the ACTIVE theme around a new brand colour without disturbing the
  // rest of the palette, and re-applies the active style sheet so that `@accent`
  // references follow.  This is the "one colour picker restyles the product"
  // knob a customer actually asks for.
  void setAccent(Color accent);
  Color accent() const { return Theme::current().accent; }

  // Re-parses the active skin's style sheet.  Call after editing it live.
  void reloadStyleSheet(std::string_view qss);

  // Fired after the theme or style sheet changed.  Windows repaint on it; a
  // widget that caches derived colours should listen too.
  Signal<> changed;

 private:
  SkinRegistry();

  std::vector<Skin> skins_;
  std::string current_;
};

inline SkinRegistry& skins() { return SkinRegistry::instance(); }

// --- theme construction helpers ---------------------------------------------
//
// Built-in palettes.  Registered automatically the first time skins() is
// touched; nothing has to call these unless you want a variant of one.
Theme darkTheme();
Theme lightTheme();
Theme highContrastTheme();
Theme amberTheme();

// Returns a copy of `base` recoloured around `accent`.  Only the accent-derived
// tokens move (accent / primary / focusRing, and the ink that sits on top of a
// filled accent); status colours keep their meaning, because "alarm" must stay
// red no matter what the brand colour is.
Theme themeWithAccent(const Theme& base, Color accent);

}  // namespace geeyoou
