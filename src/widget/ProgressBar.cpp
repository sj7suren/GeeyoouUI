#include "geeyoou/widget/ProgressBar.hpp"

#include <algorithm>
#include <cstdio>

#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"

namespace geeyoou {
namespace {
// A bar with a label in it has to be at least a line of text tall; without one
// it is just a rule with a fill and 8px still reads.  The preferred height is
// the one every showcase form uses.
constexpr float kBarHeight = 8.0f;
constexpr float kPreferredHeight = 20.0f;
constexpr float kMinWidth = 48.0f;
constexpr float kPreferredWidth = 160.0f;
}  // namespace

void ProgressBar::setRange(double minValue, double maxValue) {
  min_ = minValue;
  max_ = maxValue;
  update();
}

void ProgressBar::setValue(double v) {
  v = std::clamp(v, min_, max_);
  if (v == value_) return;
  value_ = v;
  update();
}

void ProgressBar::setBarColor(Color c) {
  bar_ = c;
  update();
}

void ProgressBar::setTextVisible(bool on) {
  textVisible_ = on;
  update();
  invalidateSizeHint();  // the label decides the floor on the height
}

void ProgressBar::setText(std::string utf8) {
  text_ = std::move(utf8);
  update();
}

// The width is NOT measured from the label, and the height is not measured from
// the value.  This widget's text is "72%" one second and "8%" the next; a hint
// derived from it would re-flow the row roughly as often as the plant changes
// state, and the operator is reading exactly that row.  So: a fixed, honest
// minimum, and whatever the layout can spare above it.
SizeHint ProgressBar::sizeHint() const {
  const float line = textVisible_ ? fontLineHeight(Theme::current().fontSmall)
                                  : kBarHeight;
  SizeHint h;
  h.preferred = Size{kPreferredWidth, (std::max)(kPreferredHeight, line)};
  h.min = Size{kMinWidth, line};
  return h;
}

void ProgressBar::onPaint(Painter& p, const Rect&) {
  const Theme& t = Theme::current();
  const Rect r = localRect();
  const bool en = isEffectivelyEnabled();

  const StyleProps& sp = style(styleState());
  const double n = (max_ > min_) ? std::clamp((value_ - min_) / (max_ - min_), 0.0, 1.0)
                                 : 0.0;
  const float radius = std::min(r.height() * 0.5f, sp.radiusOr(t.radius));
  const Color bar = sp.accentOr(bar_);

  p.fillRoundRect(r, radius, sp.backgroundOr(t.track));
  if (n > 0.0) {
    // Clamp the fill's width to at least its own corner diameter, otherwise a
    // 1% value renders as a degenerate rounded rect with visible pinching.
    const float w = std::max(float(n) * r.width(), radius * 2.0f);
    p.fillRoundRect({r.x(), r.y(), w, r.height()}, radius,
                    en ? bar : bar.lerp(t.background, 0.6f));
  }
  p.strokeRoundRect(r.deflated(0.5f), radius, sp.borderColorOr(t.panelBorder), 1.0f);

  if (textVisible_) {
    char buf[32];
    const char* label = text_.c_str();
    if (text_.empty()) {
      std::snprintf(buf, sizeof(buf), "%.0f%%", n * 100.0);
      label = buf;
    }
    p.drawText(r.center(), label, t.fontSmall, en ? t.text : t.textDisabled,
               HAlign::Center, VAlign::Middle);
  }
}

}  // namespace geeyoou
