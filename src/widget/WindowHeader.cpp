#include "geeyoou/widget/WindowHeader.hpp"

#include <algorithm>

#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"

namespace geeyoou {
namespace {
constexpr float kIconTextGap = 10.0f;
constexpr float kChevronW = 16.0f;
}  // namespace

WindowHeader::WindowHeader() {
  // The bar itself is never a focus stop.  Tab must land on the things IN it
  // (the avatar menu, the language switcher), not on the strip they sit on.
  setFocusPolicy(FocusPolicy::None);
}

// ---------------------------------------------------------------- metrics ---
void WindowHeader::setHeight(float px) {
  const float h = std::max(24.0f, px);
  if (height_ == h) return;
  height_ = h;
  relayoutItems();
  update();
  metricsChanged.emit();
}

void WindowHeader::setLeadingPadding(float px) {
  padLeft_ = std::max(0.0f, px);
  update();
}

void WindowHeader::setTrailingPadding(float px) {
  padRight_ = std::max(0.0f, px);
  relayoutItems();
}

void WindowHeader::setItemHeight(float px) {
  itemHeight_ = std::max(0.0f, px);
  relayoutItems();
}

float WindowHeader::itemHeight() const {
  if (itemHeight_ > 0.0f) return itemHeight_;
  // Leave a visible gutter above and below so a hovered item reads as a chip
  // floating in the bar rather than as a second bar.
  return std::clamp(height_ - 14.0f, 22.0f, 40.0f);
}

void WindowHeader::setItemGap(float px) {
  itemGap_ = std::max(0.0f, px);
  relayoutItems();
}

// ---------------------------------------------------------------- colours ---
void WindowHeader::setBackground(Color c) {
  background_ = c;
  hasBackground_ = true;
  update();
}

void WindowHeader::setBorderColor(Color c) {
  borderColor_ = c;
  hasBorderColor_ = true;
  update();
}

void WindowHeader::setBorderVisible(bool on) {
  borderVisible_ = on;
  update();
}

void WindowHeader::setTitleColor(Color c) {
  titleColor_ = c;
  hasTitleColor_ = true;
  update();
}

void WindowHeader::setSubtitleColor(Color c) {
  subtitleColor_ = c;
  hasSubtitleColor_ = true;
  update();
}

void WindowHeader::setButtonColor(Color c) {
  buttonColor_ = c;
  hasButtonColor_ = true;
  update();
}

void WindowHeader::setButtonHoverColor(Color c) {
  buttonHover_ = c;
  hasButtonHover_ = true;
  update();
}

void WindowHeader::setCloseHoverColor(Color c) {
  closeHover_ = c;
  hasCloseHover_ = true;
  update();
}

// ------------------------------------------------------------ brand block ---
void WindowHeader::setTitle(std::string utf8) {
  if (title_ == utf8) return;
  title_ = std::move(utf8);
  update();
}

void WindowHeader::setSubtitle(std::string utf8) {
  if (subtitle_ == utf8) return;
  subtitle_ = std::move(utf8);
  update();
}

void WindowHeader::setTitleFontSize(float px) {
  titleSize_ = std::max(0.0f, px);
  update();
}

void WindowHeader::setSubtitleFontSize(float px) {
  subtitleSize_ = std::max(0.0f, px);
  update();
}

void WindowHeader::setIcon(Icon icon) {
  icon_ = icon;
  update();
}

void WindowHeader::setIconColor(Color c) {
  iconColor_ = c;
  hasIconColor_ = true;
  update();
}

void WindowHeader::setIconSize(float px) {
  iconSize_ = std::max(8.0f, px);
  update();
}

void WindowHeader::setIconBadge(bool on) {
  iconBadge_ = on;
  update();
}

void WindowHeader::setIconBadgeColor(Color c) {
  iconBadgeColor_ = c;
  hasIconBadgeColor_ = true;
  update();
}

// -------------------------------------------------------- window commands ---
void WindowHeader::setButtons(WindowButtons b) {
  buttons_ = b;
  hovered_ = Btn::None;
  pressed_ = Btn::None;
  relayoutItems();
  update();
}

void WindowHeader::setButtonWidth(float px) {
  buttonW_ = std::max(20.0f, px);
  relayoutItems();
  update();
}

void WindowHeader::setMaximized(bool on) {
  if (maximized_ == on) return;
  maximized_ = on;
  update();
}

void WindowHeader::setDraggable(bool on) { draggable_ = on; }

// -------------------------------------------------------- trailing layout ---
void WindowHeader::addTrailingGap(float px) {
  pendingGap_ += std::max(0.0f, px);
}

void WindowHeader::setTrailingItemWidth(Widget* item, float width) {
  for (Slot& s : slots_) {
    if (s.widget == item) {
      s.width = std::max(0.0f, width);
      relayoutItems();
      return;
    }
  }
}

void WindowHeader::onGeometryChanged() { relayoutItems(); }

void WindowHeader::relayoutItems() {
  const float h = itemHeight();
  const float y = (height_ - h) * 0.5f;
  // Measure the group, then place it flush against the inboard edge of the
  // window buttons.  Right-aligning the whole group -- rather than each item --
  // is what keeps the account menu pinned to the corner as the window resizes,
  // without any item needing to know the window width.
  const float right = localRect().width() - commandZoneWidth() - padRight_;

  float total = 0.0f;
  bool first = true;
  for (const Slot& s : slots_) {
    if (!s.widget) continue;
    if (!first) total += s.gap;
    total += s.width;
    first = false;
  }

  float x = right - total;
  first = true;
  for (const Slot& s : slots_) {
    if (!s.widget) continue;
    if (!first) x += s.gap;
    s.widget->setGeometry({x, y, s.width, h});
    x += s.width;
    first = false;
  }
  update();
}

float WindowHeader::commandZoneWidth() const {
  int n = 0;
  if (buttons_.minimize) ++n;
  if (buttons_.maximize) ++n;
  if (buttons_.close) ++n;
  return float(n) * buttonW_;
}

Rect WindowHeader::buttonRect(Btn b) const {
  // Right to left: close is outermost, because that is where every desktop
  // window manager on this platform puts it and muscle memory is not worth
  // fighting over a style preference.
  float right = localRect().width();
  const auto slot = [&](bool present) {
    Rect r(right - buttonW_, 0.0f, buttonW_, height_);
    if (present) right -= buttonW_;
    return r;
  };
  if (buttons_.close) {
    const Rect r = slot(true);
    if (b == Btn::Close) return r;
  }
  if (buttons_.maximize) {
    const Rect r = slot(true);
    if (b == Btn::Max) return r;
  }
  if (buttons_.minimize) {
    const Rect r = slot(true);
    if (b == Btn::Min) return r;
  }
  return {};
}

WindowHeader::Btn WindowHeader::buttonAt(Point local) const {
  if (buttons_.close && buttonRect(Btn::Close).contains(local)) return Btn::Close;
  if (buttons_.maximize && buttonRect(Btn::Max).contains(local)) return Btn::Max;
  if (buttons_.minimize && buttonRect(Btn::Min).contains(local)) return Btn::Min;
  return Btn::None;
}

HitZone WindowHeader::hitZone(Point local) const {
  if (!draggable_) return HitZone::Client;
  if (buttonAt(local) != Btn::None) return HitZone::Client;
  return HitZone::Caption;
}

// ------------------------------------------------------------------ paint ---
void WindowHeader::paintButton(Painter& p, Btn b, const Rect& r) const {
  const Theme& t = Theme::current();
  const bool isClose = (b == Btn::Close);
  const bool hot = (hovered_ == b);
  const bool down = (pressed_ == b);

  Color glyph = hasButtonColor_ ? buttonColor_ : t.textDim;
  if (hot || down) {
    Color plate = isClose ? (hasCloseHover_ ? closeHover_ : t.danger)
                          : (hasButtonHover_ ? buttonHover_
                                             : t.panelBorder.withAlpha(160));
    if (down) plate = plate.lerp(t.background, 0.25f);
    // Square, edge to edge, no radius: the corner button must remain hittable
    // by throwing the mouse at the screen corner, and a rounded plate visually
    // pulls the target away from it.
    p.fillRect(r, plate);
    glyph = isClose ? t.onFilled.lerp(Color::rgb(255, 255, 255), 0.85f) : t.text;
  }

  Icon ic = Icon::WindowMinimize;
  if (b == Btn::Max) ic = maximized_ ? Icon::WindowRestore : Icon::WindowMaximize;
  if (b == Btn::Close) ic = Icon::Close;

  // A fixed 10px glyph rather than a fraction of the button: the caption glyphs
  // must stay the same weight whatever the bar height is set to.
  const float g = 10.0f;
  drawIcon(p, ic, {r.center().x - g * 0.5f, r.center().y - g * 0.5f, g, g}, glyph,
           0.75f);
}

void WindowHeader::onPaint(Painter& p, const Rect&) {
  const Theme& t = Theme::current();
  const Rect r = localRect();
  // Setters on this class still win over the sheet -- the same rule the rest of
  // the library follows: code that named a colour for THIS instance beats a
  // selector that named one for a category.
  const StyleProps& sp = style(styleState());

  p.fillRect(r, hasBackground_ ? background_ : sp.backgroundOr(t.panel));
  if (borderVisible_) {
    p.strokeLine({0.0f, r.height() - 0.5f}, {r.width(), r.height() - 0.5f},
                 hasBorderColor_ ? borderColor_ : sp.borderColorOr(t.panelBorder),
                 sp.borderWidthOr(1.0f));
  }

  // --- brand block ---
  float x = padLeft_;
  if (icon_ != Icon::None) {
    const Color ic = hasIconColor_ ? iconColor_ : sp.iconColorOr(sp.accentOr(t.accent));
    if (iconBadge_) {
      const float pad = 6.0f;
      const float side = iconSize_ + pad * 2.0f;
      const Rect badge(x, r.center().y - side * 0.5f, side, side);
      p.fillRoundRect(badge, t.radius, hasIconBadgeColor_ ? iconBadgeColor_
                                                          : ic.withAlpha(46));
      drawIcon(p, icon_, badge.deflated(pad), ic);
      x += side + kIconTextGap;
    } else {
      drawIcon(p, icon_,
               {x, r.center().y - iconSize_ * 0.5f, iconSize_, iconSize_}, ic);
      x += iconSize_ + kIconTextGap;
    }
  }

  const float ts =
      titleSize_ > 0.0f ? titleSize_ : sp.fontSizeOr(t.fontBody + 1.0f);
  const float ss = subtitleSize_ > 0.0f ? subtitleSize_ : t.fontSmall;
  const Color tc = hasTitleColor_ ? titleColor_ : sp.colorOr(t.text);
  const Color sc = hasSubtitleColor_ ? subtitleColor_ : t.textDim;

  if (!title_.empty() && subtitle_.empty()) {
    p.drawText({x, r.center().y}, title_, ts, tc, HAlign::Left, VAlign::Middle);
  } else if (!title_.empty()) {
    // Two lines stacked around the centre line, the way an admin console labels
    // the product and the environment it is pointed at.
    p.drawText({x, r.center().y - ss * 0.62f}, title_, ts, tc, HAlign::Left,
               VAlign::Middle);
    p.drawText({x, r.center().y + ts * 0.62f}, subtitle_, ss, sc, HAlign::Left,
               VAlign::Middle);
  }

  // --- window commands ---
  if (buttons_.minimize) paintButton(p, Btn::Min, buttonRect(Btn::Min));
  if (buttons_.maximize) paintButton(p, Btn::Max, buttonRect(Btn::Max));
  if (buttons_.close) paintButton(p, Btn::Close, buttonRect(Btn::Close));
}

// ------------------------------------------------------------------ input ---
void WindowHeader::onMouse(const MouseEvent& e) {
  switch (e.action) {
    case MouseAction::Leave:
      if (hovered_ != Btn::None || pressed_ != Btn::None) {
        hovered_ = Btn::None;
        pressed_ = Btn::None;
        update();
      }
      e.accept();
      break;

    case MouseAction::Enter:
    case MouseAction::Move: {
      const Btn b = buttonAt(e.pos);
      if (b != hovered_) {
        hovered_ = b;
        update();
      }
      e.accept();
      break;
    }

    case MouseAction::Press:
      if (e.button == MouseButton::Left) {
        pressed_ = buttonAt(e.pos);
        if (pressed_ != Btn::None) update();
        // Accepted either way: a press on bare header background must not fall
        // through to whatever is behind it.  (In a frameless window it normally
        // never gets here at all -- the OS claims it as the caption.)
        e.accept();
      }
      break;

    case MouseAction::Release:
      if (e.button == MouseButton::Left) {
        const Btn was = pressed_;
        pressed_ = Btn::None;
        update();
        // Same "drag off to cancel" rule as PushButton: closing a plant's HMI
        // by accident because the mouse slipped is not acceptable.
        if (was != Btn::None && buttonAt(e.pos) == was) {
          switch (was) {
            case Btn::Min: minimizeRequested.emit(); break;
            case Btn::Max: maximizeRequested.emit(); break;
            case Btn::Close: closeRequested.emit(); break;
            case Btn::None: break;
          }
        }
        e.accept();
      }
      break;

    default:
      break;
  }
}

// ============================================================= HeaderMenu ===
HeaderMenu::HeaderMenu() {
  setVariant(ButtonVariant::Ghost);
  setMenuWidth(180.0f);
}

void HeaderMenu::setShowChevron(bool on) {
  chevron_ = on;
  update();
}

void HeaderMenu::setLabelColor(Color c) {
  labelColor_ = c;
  hasLabelColor_ = true;
  update();
}

void HeaderMenu::setBadgeCount(int n) {
  badge_ = std::max(0, n);
  update();
}

void HeaderMenu::setBadgeColor(Color c) {
  badgeColor_ = c;
  hasBadgeColor_ = true;
  update();
}

float HeaderMenu::preferredWidth() const {
  const Theme& t = Theme::current();
  float w = 20.0f;  // symmetric padding
  if (icon() != Icon::None) w += 18.0f;
  if (!text().empty()) {
    if (icon() != Icon::None) w += 7.0f;
    w += measureText(text(), t.fontBody).width;
  }
  if (chevron_) w += kChevronW;
  return w;
}

void HeaderMenu::onPaint(Painter& p, const Rect&) {
  const Theme& t = Theme::current();
  const Rect r = localRect();

  // palette() already folds in hover / pressed / disabled for the Ghost
  // variant, so the plate comes for free rather than duplicating that logic.
  Color plate = palette().fill;
  if (isMenuOpen()) plate = t.panelBorder.withAlpha(180);
  if (plate.alpha() > 0) p.fillRoundRect(r, t.radius, plate);
  if (hasFocus() && isEffectivelyEnabled()) {
    p.strokeRoundRect(r.deflated(1.5f), t.radius, t.focusRing.withAlpha(170), 1.0f);
  }

  Color fg = hasLabelColor_ ? labelColor_ : t.text;
  if (!isEffectivelyEnabled()) fg = t.textDisabled;

  const float chev = chevron_ ? kChevronW : 0.0f;
  float x = 10.0f;
  Rect glyphBox;
  if (icon() != Icon::None) {
    glyphBox = {x, r.center().y - 9.0f, 18.0f, 18.0f};
    drawIcon(p, icon(), glyphBox, fg);
    x += 18.0f + 7.0f;
  }
  if (!text().empty()) {
    p.drawText({x, r.center().y}, text(), t.fontBody, fg, HAlign::Left,
               VAlign::Middle);
  }
  if (chevron_) {
    drawIcon(p, isMenuOpen() ? Icon::ChevronUp : Icon::ChevronDown,
             {r.right() - chev - 4.0f, r.center().y - 7.0f, 14.0f, 14.0f},
             fg.withAlpha(190), 0.9f);
  }

  if (badge_ > 0 && !glyphBox.isEmpty()) {
    const std::string label = badge_ > 99 ? "99+" : std::to_string(badge_);
    const float tw = measureText(label, 9.0f).width;
    const float bw = std::max(14.0f, tw + 8.0f);
    // Anchored to the glyph, not to the button: a bell with a label beside it
    // must still carry its count on the bell.
    const Rect badge(glyphBox.right() - bw * 0.62f, glyphBox.y() - 3.0f, bw, 14.0f);
    p.fillRoundRect(badge, 7.0f, hasBadgeColor_ ? badgeColor_ : t.danger);
    p.drawText(badge.center(), label, 9.0f, t.onFilled, HAlign::Center,
               VAlign::Middle);
  }
}

// =========================================================== HeaderAvatar ===
HeaderAvatar::HeaderAvatar() {
  setVariant(ButtonVariant::Ghost);
  setMenuWidth(200.0f);
}

void HeaderAvatar::setInitials(std::string utf8) {
  initials_ = std::move(utf8);
  update();
}

void HeaderAvatar::setAvatarColor(Color c) {
  avatarColor_ = c;
  hasAvatarColor_ = true;
  update();
}

void HeaderAvatar::setName(std::string utf8) {
  name_ = std::move(utf8);
  update();
}

void HeaderAvatar::setCaption(std::string utf8) {
  caption_ = std::move(utf8);
  update();
}

void HeaderAvatar::setShowText(bool on) {
  showText_ = on;
  update();
}

void HeaderAvatar::setAvatarDiameter(float px) {
  diameter_ = std::max(16.0f, px);
  update();
}

void HeaderAvatar::setStatusColor(Color c) {
  status_ = c;
  update();
}

float HeaderAvatar::preferredWidth() const {
  const Theme& t = Theme::current();
  float w = 8.0f + diameter_ + 8.0f;
  if (showText_ && (!name_.empty() || !caption_.empty())) {
    const float a = name_.empty() ? 0.0f : measureText(name_, t.fontBody).width;
    const float b = caption_.empty() ? 0.0f
                                     : measureText(caption_, t.fontSmall).width;
    w += 8.0f + std::max(a, b) + kChevronW;
  }
  return w;
}

void HeaderAvatar::onPaint(Painter& p, const Rect&) {
  const Theme& t = Theme::current();
  const Rect r = localRect();

  Color plate = palette().fill;
  if (isMenuOpen()) plate = t.panelBorder.withAlpha(180);
  if (plate.alpha() > 0) p.fillRoundRect(r, t.radius, plate);
  if (hasFocus() && isEffectivelyEnabled()) {
    p.strokeRoundRect(r.deflated(1.5f), t.radius, t.focusRing.withAlpha(170), 1.0f);
  }

  const float d = std::min(diameter_, r.height() - 4.0f);
  const Point c{r.x() + 8.0f + d * 0.5f, r.center().y};
  const Color av = hasAvatarColor_ ? avatarColor_ : t.accent;
  p.fillCircle(c, d * 0.5f, av);
  if (!initials_.empty()) {
    p.drawText(c, initials_, d * 0.44f, t.onFilled, HAlign::Center, VAlign::Middle);
  } else {
    drawIcon(p, Icon::User, {c.x - d * 0.3f, c.y - d * 0.3f, d * 0.6f, d * 0.6f},
             t.onFilled);
  }
  if (status_.alpha() > 0) {
    const float sr = std::max(3.0f, d * 0.17f);
    const Point sp{c.x + d * 0.36f, c.y + d * 0.36f};
    // Ringed in the bar's own colour so the dot reads as sitting ON the avatar
    // rather than as a stray pixel next to it.
    p.fillCircle(sp, sr + 1.6f, t.panel);
    p.fillCircle(sp, sr, status_);
  }

  if (!showText_) return;

  const float x = c.x + d * 0.5f + 8.0f;
  const Color fg = isEffectivelyEnabled() ? t.text : t.textDisabled;
  if (!name_.empty() && caption_.empty()) {
    p.drawText({x, r.center().y}, name_, t.fontBody, fg, HAlign::Left,
               VAlign::Middle);
  } else if (!name_.empty()) {
    p.drawText({x, r.center().y - t.fontSmall * 0.62f}, name_, t.fontBody, fg,
               HAlign::Left, VAlign::Middle);
    p.drawText({x, r.center().y + t.fontBody * 0.62f}, caption_, t.fontSmall,
               t.textDim, HAlign::Left, VAlign::Middle);
  }
  drawIcon(p, isMenuOpen() ? Icon::ChevronUp : Icon::ChevronDown,
           {r.right() - kChevronW - 2.0f, r.center().y - 7.0f, 14.0f, 14.0f},
           t.textDim, 0.9f);
}

}  // namespace geeyoou
