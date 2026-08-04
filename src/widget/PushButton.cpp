#include "geeyoou/widget/PushButton.hpp"

#include <algorithm>

#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"

namespace geeyoou {
namespace {
constexpr float kIconGap = 7.0f;
constexpr float kSpinDegreesPerTick = 12.0f;  // ~one turn per second at 30 fps
}  // namespace

void PushButton::setText(std::string utf8) {
  if (text_ == utf8) return;
  text_ = std::move(utf8);
  update();
}

void PushButton::setVariant(ButtonVariant v) {
  variant_ = v;
  update();
}

void PushButton::setAccent(Color c) {
  accent_ = c;
  accentSet_ = true;
  update();
}

void PushButton::setIcon(Icon icon) {
  icon_ = icon;
  update();
}

void PushButton::setCheckable(bool on) {
  checkable_ = on;
  update();
}

void PushButton::setChecked(bool on) {
  if (!checkable_ || checked_ == on) return;
  checked_ = on;
  update();
  toggled.emit(checked_);
}

void PushButton::setLoading(bool on) {
  if (loading_ == on) return;
  loading_ = on;
  // A button that becomes busy must not stay visually pressed or hovered --
  // it no longer responds, and showing it as active would be a lie.
  pressed_ = false;
  spinPhase_ = 0.0f;
  update();
}

void PushButton::setLoadingText(std::string utf8) {
  loadingText_ = std::move(utf8);
  update();
}

void PushButton::activate() {
  if (checkable_) {
    checked_ = !checked_;
    update();
    toggled.emit(checked_);
  }
  clicked.emit();
}

// ---------------------------------------------------------------- palette ---
StyleState PushButton::styleState() const {
  StyleState s = Widget::styleState();
  if (hovered_) s |= StyleState::Hover;
  if (pressed_) s |= StyleState::Pressed;
  if (checked_) s |= StyleState::Checked;
  return s;
}

PushButton::Palette PushButton::palette() const {
  const Theme& t = Theme::current();
  // The style sheet is consulted with the button's REAL state, so
  // `PushButton:hover { accent: ... }` reaches the same code path the theme
  // does instead of being a second, parallel colour system.
  const StyleProps& sp = style(styleState());

  Color base = t.primary;
  bool filled = true;
  switch (variant_) {
    case ButtonVariant::Default: base = t.panelBorder; filled = false; break;
    case ButtonVariant::Primary: base = t.primary; break;
    case ButtonVariant::Success: base = t.success; break;
    case ButtonVariant::Warning: base = t.warn;    break;
    case ButtonVariant::Danger:  base = t.danger;  break;
    case ButtonVariant::Ghost:   base = t.primary; filled = false; break;
  }
  if (accentSet_) base = accent_;
  // `accent` from the sheet overrides the variant but keeps its FILLED-ness --
  // that is what makes `.danger { accent: #C00 }` recolour a button without
  // also turning an outlined one into a solid one.
  base = sp.accentOr(base);

  Palette pal;
  if (filled) {
    pal.fill = base;
    pal.border = base;
    // Dark ink on a saturated fill: the theme's normal light text would sit at
    // roughly 1.5:1 against amber or green and be unreadable on a plant floor.
    pal.label = t.onFilled;
  } else if (variant_ == ButtonVariant::Ghost) {
    pal.fill = Color::rgba(0, 0, 0, 0);
    pal.border = Color::rgba(0, 0, 0, 0);
    pal.label = t.text;
  } else {
    pal.fill = t.panel;
    pal.border = t.panelBorder;
    pal.label = t.text;
  }

  if (!isEffectivelyEnabled()) {
    pal.fill = filled ? base.lerp(t.background, 0.68f)
                      : t.panel.lerp(t.background, 0.5f);
    pal.border = t.textDisabled;
    pal.label = t.textDisabled;
    return pal;
  }

  if (loading_) {
    pal.fill = pal.fill.lerp(t.background, 0.28f);
    pal.label = pal.label.lerp(t.background, 0.2f);
    return pal;
  }

  if (pressed_) {
    pal.fill = filled ? base.lerp(t.background, 0.3f) : base.lerp(t.panel, 0.6f);
    pal.border = base;
    if (!filled) pal.label = t.text;
  } else if (checked_) {
    pal.fill = filled ? base : base.lerp(t.panel, 0.55f);
    pal.border = base;
    if (!filled) pal.label = t.text;
  } else if (hovered_) {
    pal.fill = filled ? base.lerp(t.text, 0.13f) : pal.fill.lerp(base, 0.22f);
    if (variant_ == ButtonVariant::Ghost) pal.fill = base.withAlpha(38);
  }

  // Explicit sheet colours win outright: they are applied LAST so they are not
  // re-tinted by the state blending above.  A rule that names a colour for a
  // state has already accounted for that state.
  pal.fill = sp.backgroundOr(pal.fill);
  pal.border = sp.borderColorOr(pal.border);
  pal.label = sp.colorOr(pal.label);
  return pal;
}

// ------------------------------------------------------------------ paint ---
void PushButton::onPaint(Painter& p, const Rect&) {
  const Theme& t = Theme::current();
  const Rect r = localRect();
  const Palette pal = palette();
  const StyleProps& sp = style(styleState());

  const float radius = sp.radiusOr(t.radius);
  const float bw = sp.borderWidthOr(1.0f);
  const float fontSize = sp.fontSizeOr(t.fontBody);

  if (pal.fill.alpha() > 0) p.fillRoundRect(r, radius, pal.fill);
  if (pal.border.alpha() > 0 && bw > 0.0f) {
    p.strokeRoundRect(r.deflated(bw * 0.5f), radius, pal.border, bw);
  }
  if (hasFocus() && interactive()) {
    p.strokeRoundRect(r.deflated(bw + 1.5f), std::max(1.0f, radius - 2.0f),
                      t.focusRing, 1.0f);
  }

  const std::string& label =
      (loading_ && !loadingText_.empty()) ? loadingText_ : text_;
  const float glyph = std::min(18.0f, r.height() - 12.0f);
  const bool showGlyph = loading_ || icon_ != Icon::None;

  const float textW = label.empty() ? 0.0f
                                    : measureText(label, fontSize).width;
  const float totalW = textW + (showGlyph ? glyph + (label.empty() ? 0.0f : kIconGap)
                                          : 0.0f);
  float x = r.center().x - totalW * 0.5f;

  if (showGlyph) {
    const Rect box(x, r.center().y - glyph * 0.5f, glyph, glyph);
    if (loading_) {
      // A 90-degree arc sweeping around the circle: the gap is what makes the
      // rotation legible, which a full ring would not be.
      const float rad = glyph * 0.42f;
      p.strokeCircle(box.center(), rad, pal.label.withAlpha(70), 2.0f);
      p.strokeArc(box.center(), rad, spinPhase_, 90.0f, pal.label, 2.0f, true);
    } else {
      drawIcon(p, icon_, box, sp.iconColorOr(pal.label));
    }
    x += glyph + kIconGap;
  }

  if (!label.empty()) {
    p.drawText({x, r.center().y}, label, fontSize, pal.label, HAlign::Left,
               VAlign::Middle);
  }
}

void PushButton::onAnimationTick() {
  if (!loading_ || !isVisible()) return;
  spinPhase_ += kSpinDegreesPerTick;
  if (spinPhase_ >= 360.0f) spinPhase_ -= 360.0f;
  update();
}

// ------------------------------------------------------------------ input ---
void PushButton::onMouse(const MouseEvent& e) {
  if (!interactive()) {
    // Still swallow the click: a busy button must not let the press fall
    // through to whatever container is underneath it.
    if (loading_) e.accept();
    return;
  }
  switch (e.action) {
    case MouseAction::Enter: hovered_ = true; update(); e.accept(); break;
    case MouseAction::Leave:
      hovered_ = false;
      pressed_ = false;
      update();
      e.accept();
      break;
    case MouseAction::Press:
      if (e.button == MouseButton::Left) { pressed_ = true; update(); e.accept(); }
      break;
    case MouseAction::Release:
      if (e.button == MouseButton::Left) {
        const bool was = pressed_;
        pressed_ = false;
        update();
        // Only fire if the release landed back inside -- the standard "drag off
        // to cancel" affordance every desktop button has.
        if (was && localRect().contains(e.pos)) activate();
        e.accept();
      }
      break;
    default: break;
  }
}

void PushButton::onKey(const KeyEvent& e) {
  if (!e.pressed || !interactive()) return;
  if (e.key == Key::Space || e.key == Key::Enter) {
    activate();
    e.accept();
  }
}

}  // namespace geeyoou
