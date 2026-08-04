#include "geeyoou/widget/ToggleSwitch.hpp"

#include <algorithm>

#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"

namespace geeyoou {

void ToggleSwitch::setText(std::string utf8) {
  text_ = std::move(utf8);
  update();
}

void ToggleSwitch::setOnColor(Color c) {
  onColor_ = c;
  update();
}

void ToggleSwitch::setChecked(bool on) {
  if (checked_ == on) return;
  checked_ = on;
  update();
  toggled.emit(checked_);
}

void ToggleSwitch::onPaint(Painter& p, const Rect&) {
  const Theme& t = Theme::current();
  const Rect r = localRect();
  const bool en = isEffectivelyEnabled();

  const float h = std::min(20.0f, r.height());
  const float w = h * 1.85f;
  const Rect track(0.0f, r.center().y - h * 0.5f, w, h);
  const float radius = h * 0.5f;

  const StyleProps& sp = style(styleState());
  Color trackColor = checked_ ? sp.accentOr(onColor_) : sp.backgroundOr(t.track);
  if (!en) trackColor = trackColor.lerp(t.background, 0.6f);
  else if (hovered_) trackColor = trackColor.lerp(t.text, 0.12f);

  p.fillRoundRect(track, radius, trackColor);
  p.strokeRoundRect(track.deflated(0.5f), radius,
                    en ? sp.borderColorOr(t.panelBorder) : t.textDisabled, 1.0f);

  const float knobR = radius - 3.0f;
  const float knobX = checked_ ? track.right() - radius : track.x() + radius;
  p.fillCircle({knobX, track.center().y}, knobR,
               en ? t.text : t.textDisabled);

  // Encloses the whole row, and stays inside localRect -- paintTree clips each
  // widget to its bounds, so a ring around the track alone would be cut at x=0.
  if (hasFocus() && en) {
    p.strokeRoundRect(r.deflated(0.5f), 3.0f, t.focusRing, 1.0f);
  }

  if (!text_.empty()) {
    p.drawText({w + 10.0f, r.center().y}, text_, t.fontBody,
               en ? t.text : t.textDisabled, HAlign::Left, VAlign::Middle);
  }
}

void ToggleSwitch::onMouse(const MouseEvent& e) {
  switch (e.action) {
    case MouseAction::Enter: hovered_ = true; update(); e.accept(); break;
    case MouseAction::Leave: hovered_ = false; update(); e.accept(); break;
    case MouseAction::Press:
      if (e.button == MouseButton::Left) e.accept();
      break;
    case MouseAction::Release:
      if (e.button == MouseButton::Left && localRect().contains(e.pos)) {
        toggle();
        e.accept();
      }
      break;
    default: break;
  }
}

void ToggleSwitch::onKey(const KeyEvent& e) {
  if (e.pressed && e.key == Key::Space) {
    toggle();
    e.accept();
  }
}

}  // namespace geeyoou
