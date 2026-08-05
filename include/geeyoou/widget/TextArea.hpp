#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include "geeyoou/core/Signal.hpp"
#include "geeyoou/widget/Widget.hpp"

namespace geeyoou {

// Multi-line text field with soft wrapping and vertical scrolling.
//
// Soft wrap + vertical scroll only; there is no horizontal scrolling, because
// wrapping makes it unnecessary and supporting both doubles the caret-mapping
// cases for no benefit in a notes/recipe field.
class TextArea : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(TextArea, Widget)

  TextArea() { setFocusPolicy(FocusPolicy::Tab); }

  void setText(std::string utf8);
  const std::string& text() const { return text_; }
  void clear() { setText({}); }

  void setPlaceholder(std::string utf8);
  void setReadOnly(bool on);
  bool isReadOnly() const { return readOnly_; }

  void selectAll();
  bool hasSelection() const { return selAnchor_ != caret_; }
  std::string selectedText() const;

  int lineCount() const { return int(lines_.size()); }

  SizeHint sizeHint() const override;

  Signal<const std::string&> textChanged;
  Signal<const std::string&> editingFinished;

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;
  void onMouse(const MouseEvent& e) override;
  void onKey(const KeyEvent& e) override;
  void onFocusChanged(bool focused) override;
  void onGeometryChanged() override;
  void onAnimationTick() override;

 private:
  // One wrapped display row.  `xs` and `offs` are parallel: offs[i] is the byte
  // offset of the i-th codepoint boundary on this row and xs[i] its pixel x.
  // Precomputing them turns caret placement and click hit-testing into array
  // lookups instead of re-measuring text on every mouse move.
  struct VisualLine {
    std::size_t start = 0;
    std::size_t end = 0;  // exclusive, excludes the '\n' itself
    std::vector<float> xs;
    std::vector<std::size_t> offs;
  };

  Rect contentRect() const;
  float lineHeight() const;
  void rebuildLayout();
  std::size_t lineOfOffset(std::size_t byteOffset) const;
  Point caretPoint() const;                       // content-local, pre-scroll
  std::size_t offsetAtPoint(Point contentLocal) const;

  void insertText(const std::string& utf8);
  void deleteSelection();
  void moveCaret(std::size_t to, bool extend);
  void ensureCaretVisible();
  void notifyImeCaret();
  void emitChanged();
  float maxScroll() const;

  std::string text_;
  std::string placeholder_;
  std::vector<VisualLine> lines_;
  float layoutWidth_ = -1.0f;  // width the current layout was built for

  std::size_t caret_ = 0;
  std::size_t selAnchor_ = 0;
  float scrollY_ = 0.0f;
  // Remembered x for Up/Down so walking through short lines does not drag the
  // caret permanently leftwards.
  float desiredX_ = -1.0f;

  bool readOnly_ = false;
  bool hovered_ = false;
  bool dragging_ = false;
  bool caretOn_ = true;
  int blinkCounter_ = 0;
};

}  // namespace geeyoou
