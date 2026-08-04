#pragma once
//
// The showcase's application window.
//
// This is the pattern every GeeyoouUI application is meant to follow: derive
// from AppWindow, configure the header in the constructor, and drop the real UI
// into the content area.  Nothing here reaches for a native handle, a Win32
// style bit or a DPI factor -- the whole title bar is ordinary widget code.
//
#include <string>
#include <vector>

#include "Shell.hpp"
#include "geeyoou/core/ConnectionScope.hpp"
#include "geeyoou/widget/AppWindow.hpp"
#include "geeyoou/widget/WindowHeader.hpp"

namespace showcase {

class ShowcaseWindow : public geeyoou::AppWindow {
 public:
  ShowcaseWindow();
  ~ShowcaseWindow() override;

  Shell* shell() { return shell_; }

  // Header widgets, exposed so the "窗口外壳" page can drive them live.
  geeyoou::HeaderMenu* languageMenu() { return language_; }
  geeyoou::HeaderMenu* bellMenu() { return bell_; }
  geeyoou::HeaderAvatar* accountMenu() { return account_; }

  // Anything picked from the header, for the demo page's activity log.
  geeyoou::Signal<const std::string&> headerAction;

  // --- style sheet ---------------------------------------------------------
  //
  // The live sheet is composed of three parts, in cascade order:
  //   1. the active SKIN's own rules
  //   2. this application's base rules (see kBaseStyleSheet in the .cpp)
  //   3. whatever the user typed on the 主题与皮肤 page
  //
  // Composing rather than replacing is what keeps `.caption` working after a
  // skin change -- applying a skin installs ITS sheet and would otherwise drop
  // the app's own rules on the floor.
  void setUserStyleSheet(std::string qss);
  const std::string& userStyleSheet() const { return userQss_; }

 private:
  void composeStyleSheet();
  // Re-derives the chrome colours that are computed FROM the theme rather than
  // read straight out of it.  Must run again on every skin change.
  void applyHeaderTheme();

  Shell* shell_ = nullptr;
  geeyoou::HeaderMenu* bell_ = nullptr;
  geeyoou::HeaderMenu* language_ = nullptr;
  geeyoou::HeaderAvatar* account_ = nullptr;
  std::string userQss_;

  // Declared last, destroyed first -- see core/ConnectionScope.hpp.
  geeyoou::ConnectionScope conns_;
};

}  // namespace showcase
