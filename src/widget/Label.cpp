#include "geeyoou/widget/Label.hpp"

#include <algorithm>
#include <cmath>

#include "geeyoou/core/Utf8.hpp"
#include "geeyoou/render/Theme.hpp"

namespace geeyoou {

void Label::setText(std::string utf8) {
  if (text_ == utf8) return;  // guard: avoids a repaint per redundant set
  text_ = std::move(utf8);
  wrappedFor_ = -1.0f;
  update();
}

void Label::setPixelSize(float px) {
  pixelSize_ = px;
  sizeSet_ = true;
  wrappedFor_ = -1.0f;
  update();
}

void Label::setColor(Color c) {
  color_ = c;
  colorSet_ = true;
  update();
}

float Label::pixelSize() const {
  if (sizeSet_) return pixelSize_;
  return style(styleState()).fontSizeOr(Theme::current().fontBody);
}

Color Label::color() const {
  if (colorSet_) return color_;
  return style(styleState()).colorOr(Theme::current().text);
}

void Label::setAlign(HAlign h, VAlign v) {
  hAlign_ = h;
  vAlign_ = v;
  update();
}

void Label::setWordWrap(bool on) {
  if (wrap_ == on) return;
  wrap_ = on;
  wrappedFor_ = -1.0f;
  update();
}

void Label::onGeometryChanged() {
  if (wrap_) wrappedFor_ = -1.0f;  // width changed => wrapping changed
}

float Label::lineSpacing() const {
  return std::round(fontLineHeight(pixelSize()) * 1.35f);
}

void Label::rebuildLines(float width) const {
  // The wrap depends on the font size, which a skin change can move -- so the
  // cache is keyed on the style generation as well as on the width.
  const std::uint64_t gen = styleGeneration();
  if (wrappedFor_ == width && wrappedGen_ == gen) return;
  wrappedFor_ = width;
  wrappedGen_ = gen;
  const float ps = pixelSize();
  lines_.clear();
  if (text_.empty()) return;

  // Split on hard newlines first, then soft-wrap each piece.
  std::size_t start = 0;
  while (start <= text_.size()) {
    std::size_t nl = text_.find('\n', start);
    const bool last = (nl == std::string::npos);
    if (last) nl = text_.size();

    if (!wrap_ || width <= 0.0f) {
      lines_.push_back(text_.substr(start, nl - start));
    } else if (start == nl) {
      // An EMPTY hard-line (a blank line in the source text).  The wrap loop
      // below would produce nothing for it and the paragraph break would
      // silently vanish, so it is emitted explicitly.
      lines_.emplace_back();
    } else {
      std::size_t lineStart = start;
      while (lineStart < nl) {
        std::size_t i = lineStart;
        std::size_t lastSpace = std::string::npos;
        std::size_t breakAt = nl;
        while (i < nl) {
          const std::size_t next = utf8::nextBoundary(text_, i);
          const float w =
              measureText(std::string_view(text_).substr(lineStart, next - lineStart),
                          ps)
                  .width;
          if (w > width && i > lineStart) {
            // Prefer a space so Latin words stay whole; CJK has no spaces and
            // simply breaks per character, which is correct for it anyway.
            breakAt = (lastSpace != std::string::npos && lastSpace > lineStart)
                          ? lastSpace
                          : i;
            break;
          }
          if (text_[i] == ' ') lastSpace = next;
          i = next;
        }
        if (i >= nl) breakAt = nl;
        lines_.push_back(text_.substr(lineStart, breakAt - lineStart));
        lineStart = breakAt;
        // Swallow the space we broke on so the next line does not start with it.
        while (lineStart < nl && text_[lineStart] == ' ') ++lineStart;
      }
      if (lineStart >= nl && lines_.empty()) lines_.emplace_back();
    }

    if (last) break;
    start = nl + 1;
    if (start == text_.size()) {
      lines_.emplace_back();  // trailing newline yields a final empty line
      break;
    }
  }
}

float Label::heightForWidth(float width) const {
  rebuildLines(width);
  if (lines_.empty()) return 0.0f;
  return float(lines_.size()) * lineSpacing();
}

void Label::onPaint(Painter& p, const Rect&) {
  if (text_.empty()) return;
  const Rect r = localRect();
  const float ps = pixelSize();
  const Color fg = color();

  const float x = (hAlign_ == HAlign::Center)  ? r.center().x
                  : (hAlign_ == HAlign::Right) ? r.right()
                                               : r.x();

  // --- fast path: the single-line case that most labels are ---
  if (!wrap_ && text_.find('\n') == std::string::npos) {
    const float y = (vAlign_ == VAlign::Middle)   ? r.center().y
                    : (vAlign_ == VAlign::Bottom) ? r.bottom()
                                                  : r.y();
    p.drawText({x, y}, text_, ps, fg, hAlign_, vAlign_);
    return;
  }

  rebuildLines(r.width());
  if (lines_.empty()) return;

  const float lh = lineSpacing();
  const float blockH = float(lines_.size()) * lh;
  float y = r.y();
  if (vAlign_ == VAlign::Middle) y = r.center().y - blockH * 0.5f;
  else if (vAlign_ == VAlign::Bottom) y = r.bottom() - blockH;

  for (const std::string& line : lines_) {
    if (!line.empty()) {
      // Each line is drawn Top-anchored inside its own slot, so vertical
      // alignment applies to the BLOCK rather than to every line.
      p.drawText({x, y}, line, ps, fg, hAlign_, VAlign::Top);
    }
    y += lh;
  }
}

}  // namespace geeyoou
