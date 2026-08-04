#pragma once
#include <cstddef>
#include <string>

#include "geeyoou/core/Signal.hpp"
#include "geeyoou/render/Icon.hpp"
#include "geeyoou/widget/Widget.hpp"

namespace geeyoou {

enum class EchoMode {
  Normal,
  Password,  // renders bullets; the buffer itself is never masked
};

// Single-line text field.
//
// Chinese input works through the system IME: typed characters arrive as
// KeyEvent::character (already decoded from UTF-16, surrogate pairs merged by
// the platform layer), and the widget reports its caret position upwards so the
// candidate window follows the cursor.  What is NOT implemented is inline
// pre-edit rendering -- the composition string is drawn by Windows' own IME
// window rather than underlined inside the field.  See docs/architecture.md.
class LineEdit : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(LineEdit, Widget)

  LineEdit() { setFocusPolicy(FocusPolicy::Tab); }

  void setText(std::string utf8);
  const std::string& text() const { return text_; }
  void clear() { setText({}); }

  void setPlaceholder(std::string utf8);
  void setEchoMode(EchoMode m);
  EchoMode echoMode() const { return echo_; }

  void setReadOnly(bool on);
  bool isReadOnly() const { return readOnly_; }

  // 0 = unlimited.  Counted in CODEPOINTS, not bytes: a 20-character limit
  // must mean 20 Chinese characters, not 6.
  void setMaxLength(std::size_t codepoints);

  void setLeadingIcon(Icon icon);
  void setClearButtonEnabled(bool on);
  // Eye toggle; only has an effect in EchoMode::Password.
  void setRevealButtonEnabled(bool on);

  // Draws a danger-coloured border, for failed validation.
  void setInvalid(bool on);
  bool isInvalid() const { return invalid_; }

  void selectAll();
  bool hasSelection() const { return selAnchor_ != caret_; }
  std::string selectedText() const;

  Signal<const std::string&> textChanged;
  Signal<const std::string&> editingFinished;  // Enter, or focus lost
  Signal<> returnPressed;

  // Adds :hover / :read-only / :invalid on top of what Widget can see.
  StyleState styleState() const override;

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;
  void onMouse(const MouseEvent& e) override;
  void onKey(const KeyEvent& e) override;
  void onFocusChanged(bool focused) override;
  void onAnimationTick() override;

  // Geometry of the editable strip, after the icon and trailing buttons.
  Rect contentRect() const;

 private:
  enum class Button { None, Clear, Reveal };

  std::string displayString() const;
  std::size_t displayIndex(std::size_t textIndex) const;
  float xForIndex(std::size_t textIndex) const;
  std::size_t indexAtX(float x) const;

  float trailingWidth() const;
  Rect buttonRect(Button b) const;
  Button buttonAt(Point p) const;

  void insertText(const std::string& utf8);
  void deleteSelection();
  void moveCaret(std::size_t to, bool extendSelection);
  void ensureCaretVisible();
  void notifyImeCaret();
  void emitChanged();

  std::string text_;
  std::string placeholder_;
  std::string textOnFocus_;  // for Escape-to-revert

  std::size_t caret_ = 0;      // byte offset, always on a codepoint boundary
  std::size_t selAnchor_ = 0;  // byte offset; == caret_ means no selection
  float scroll_ = 0.0f;        // horizontal scroll, pixels

  EchoMode echo_ = EchoMode::Normal;
  bool revealed_ = false;
  bool readOnly_ = false;
  bool invalid_ = false;
  std::size_t maxLength_ = 0;

  Icon leadingIcon_ = Icon::None;
  bool clearButton_ = false;
  bool revealButton_ = false;

  bool hovered_ = false;
  bool dragging_ = false;
  Button hoverButton_ = Button::None;

  bool caretOn_ = true;
  int blinkCounter_ = 0;
};

}  // namespace geeyoou
