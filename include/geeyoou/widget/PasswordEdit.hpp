#pragma once
#include "geeyoou/widget/LineEdit.hpp"

namespace geeyoou {

// Masked field, optionally with an eye toggle that reveals the value.
//
// A thin subclass rather than a fourth copy of the text-editing machinery:
// masking is a rendering concern (LineEdit::displayString), so the caret,
// selection, clipboard and IME behaviour come along unchanged.
//
// Note that Ctrl+C / Ctrl+X are refused while masked -- see LineEdit::onKey.
// Copying a password out of a field that is showing bullets would quietly
// defeat the masking.
class PasswordEdit : public LineEdit {
 public:
  GEEYOOU_STYLE_TYPE(PasswordEdit, LineEdit)

  PasswordEdit() {
    setEchoMode(EchoMode::Password);
    setLeadingIcon(Icon::Lock);
  }

  // Shows the eye button.  Off by default: on a shared control-room screen,
  // "reveal" is a decision the screen designer should make deliberately.
  void setRevealEnabled(bool on) { setRevealButtonEnabled(on); }
};

}  // namespace geeyoou
