#pragma once
//
// A modal dialog: a dimming scrim over the whole window, plus a centred panel
// carrying a title, a body, and a row of buttons.
//
// WHY IT IS A FULL-WINDOW OVERLAY, NOT AN ANCHORED POPUP.  Window already has
// openPopup() for dropdowns -- a small widget placed next to an anchor, with a
// click outside dismissing it.  A dialog is the opposite: it must swallow every
// click that is not on its panel, so nothing behind it can be operated while it
// is up.  Making the Dialog cover the entire window turns that into a property
// of geometry rather than a special input mode: openPopup gives first refusal
// on every hit to the topmost widget, and the topmost widget here IS the whole
// window, so the machinery that would dismiss a dropdown never fires.  Modality
// for free, on the popup path that already exists.
//
// LIFETIME -- read before touching close().  A button lives inside the dialog.
// When it is clicked, its `clicked` signal is mid-emit.  If that handler
// destroyed the dialog, it would destroy the object owning a signal that is
// still emitting -- contract D7 in core/Signal.hpp, the exact crash this
// codebase keeps designing out.  So close() does NOT tear anything down inline:
// it records the result and defers the actual closePopup()/callback to a
// zero-delay timer, which runs on a clean stack after every button emit has
// unwound.  onResult is the LAST thing the deferred step does and touches no
// member afterwards, so a caller MAY destroy the dialog from inside onResult.
//
#include <functional>
#include <string>
#include <vector>

#include "geeyoou/platform/Platform.hpp"
#include "geeyoou/widget/PushButton.hpp"
#include "geeyoou/widget/Widget.hpp"

namespace geeyoou {

class Dialog : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(Dialog, Widget)

  Dialog();
  ~Dialog() override;

  void setTitle(std::string utf8);

  // The content area between the title and the button row.  Fill it with your
  // own widgets; the dialog sizes and positions it.
  Widget* body() { return body_; }

  // Add a button to the bottom row.  `resultId` is what onResult receives when
  // it is clicked; make one button the primary so Enter can trigger it.
  void addButton(std::string label, int resultId,
                 ButtonVariant variant = ButtonVariant::Default,
                 bool isDefault = false);

  // Called once with the result of the interaction, on a clean stack: a clicked
  // button's id, or kCancelled for Esc / a click on the scrim.  Safe to destroy
  // the dialog from here.
  static constexpr int kCancelled = -1;
  std::function<void(int)> onResult;

  // Desired panel size.  The body drives most dialogs, but a fixed size keeps a
  // message box from collapsing around one short line.
  void setPanelSize(Size logical) { panel_ = logical; }

  // Shows the dialog as a modal overlay on `w` (defaults to this widget's
  // window).  The dialog must already be a child of that window.
  void show(Window* w = nullptr);

  // Requests close with `resultId`.  Deferred -- see the lifetime note above.
  void close(int resultId = kCancelled);

  // Whether the scrim / Esc cancels.  On by default; turn off for a dialog the
  // operator must answer (an interlock confirmation, say).
  void setDismissable(bool on) { dismissable_ = on; }

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;
  void onMouse(const MouseEvent& e) override;
  void onKey(const KeyEvent& e) override;
  void onGeometryChanged() override;

 private:
  Rect panelRect() const;    // the centred panel, in dialog-local coords
  Rect titleRect() const;
  Rect buttonRowRect() const;
  void layoutChildren();

  struct Btn {
    PushButton* widget = nullptr;
    int resultId = 0;
    bool isDefault = false;
  };

  std::string title_;
  Widget* body_ = nullptr;
  std::vector<Btn> buttons_;
  Size panel_{420.0f, 200.0f};
  bool dismissable_ = true;

  bool closing_ = false;
  int result_ = kCancelled;
  TimerId closeTimer_ = 0;
};

// ---------------------------------------------------------------------------
// Convenience: a fire-and-forget message box.
//
// Creates a dialog on `w`, shows it, and destroys it after the operator
// answers -- the caller keeps no pointer and manages no lifetime.  `buttons`
// are laid out left to right; the LAST one is the primary (Enter) button, and
// its index is what `onAnswer` receives (or Dialog::kCancelled on Esc/scrim).
void messageBox(Window* w, std::string title, std::string message,
                std::vector<std::string> buttons,
                std::function<void(int)> onAnswer = {});

// The two-button confirm every HMI needs: onConfirm runs only if the operator
// chose the confirming button.  `danger` paints it red (stop / delete).
void confirmBox(Window* w, std::string title, std::string message,
                std::function<void()> onConfirm,
                std::string confirmLabel = "确定",
                std::string cancelLabel = "取消", bool danger = false);

}  // namespace geeyoou
