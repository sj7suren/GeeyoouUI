#pragma once
#include <string>

#include "geeyoou/core/Signal.hpp"
#include "geeyoou/render/Icon.hpp"
#include "geeyoou/widget/Widget.hpp"

namespace geeyoou {

// Semantic button roles.  Named by MEANING, not by colour: a plant that maps
// "success" to amber changes one Theme field, not every call site.
enum class ButtonVariant {
  Default,  // neutral, outlined -- the safe majority
  Primary,  // filled accent, one per screen region
  Success,  // confirm, start, apply
  Warning,  // proceed-with-care
  Danger,   // stop, delete, emergency
  Ghost,    // no fill, no border; toolbar and inline actions
};

class PushButton : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(PushButton, Widget)

  PushButton() { setFocusPolicy(FocusPolicy::Tab); }

  void setText(std::string utf8);
  const std::string& text() const { return text_; }

  void setVariant(ButtonVariant v);
  ButtonVariant variant() const { return variant_; }

  // Overrides the variant's colour without changing its filled/outlined shape.
  void setAccent(Color c);

  void setIcon(Icon icon);
  Icon icon() const { return icon_; }

  // A checkable button latches instead of springing back -- the standard shape
  // of a "手动/自动" or "泵启停" control on an HMI screen.
  void setCheckable(bool on);
  bool isCheckable() const { return checkable_; }
  void setChecked(bool on);
  bool isChecked() const { return checked_; }

  // Shows a spinner in place of the icon and refuses input.  Requires
  // Window::enableAnimations() -- without a clock the spinner is drawn but
  // frozen, which is a visible bug rather than a silent one.
  void setLoading(bool on);
  bool isLoading() const { return loading_; }
  void setLoadingText(std::string utf8);

  Signal<> clicked;
  Signal<bool> toggled;  // checkable buttons only

  // Adds :hover / :pressed / :checked on top of what Widget can see.
  StyleState styleState() const override;

  SizeHint sizeHint() const override;

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;
  void onMouse(const MouseEvent& e) override;
  void onKey(const KeyEvent& e) override;
  void onAnimationTick() override;

  // Resolved fill / border / label for the current variant and state.
  struct Palette {
    Color fill;
    Color border;
    Color label;
  };
  Palette palette() const;
  bool interactive() const { return isEffectivelyEnabled() && !loading_; }
  // Spinner rotation, in degrees. Exposed so subclasses draw the SAME spinner
  // advanced by the SAME tick, instead of each keeping its own phase.
  float spinPhase() const { return spinPhase_; }

 private:
  void activate();

  std::string text_;
  std::string loadingText_;
  ButtonVariant variant_ = ButtonVariant::Default;
  Icon icon_ = Icon::None;
  bool accentSet_ = false;
  Color accent_;

  bool checkable_ = false;
  bool checked_ = false;
  bool loading_ = false;
  bool hovered_ = false;
  bool pressed_ = false;
  float spinPhase_ = 0.0f;
};

}  // namespace geeyoou
