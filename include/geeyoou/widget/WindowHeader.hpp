#pragma once
//
// WindowHeader: the app-drawn title bar of a frameless window.
//
// Everything the OS caption used to provide -- the title, the icon, and the
// minimise / maximise / close commands -- is drawn here with the same Painter
// and the same Theme as every other widget, so a GeeyoouUI window looks
// identical on a Windows 7 panel PC, a Windows 11 workstation and (once the
// backend lands) an X11 kiosk.  That consistency is the point: an HMI screen
// commissioned once should not change appearance because IT rolled out a new
// desktop theme.
//
// The three window buttons are PAINTED BY THE HEADER rather than being child
// widgets.  They have to sit flush in the top-right corner with no rounding and
// no margin -- that is a Fitts's-law target the operator hits by slamming the
// mouse into the screen corner -- and a generic button would fight that with
// its own padding and radius.
//
// Anything else that belongs in the bar (an avatar menu, a language switcher, a
// notification bell) IS an ordinary child widget, added through
// addTrailingItem() and laid out right-to-left beside the window buttons.
//
#include <string>
#include <utility>
#include <vector>

#include "geeyoou/core/Signal.hpp"
#include "geeyoou/platform/Platform.hpp"
#include "geeyoou/render/Icon.hpp"
#include "geeyoou/widget/MenuButton.hpp"
#include "geeyoou/widget/Widget.hpp"

namespace geeyoou {

// Which of the three window commands the header offers.  A kiosk build
// typically ships {false, false, false} and exits through its own screen.
struct WindowButtons {
  bool minimize = true;
  bool maximize = true;
  bool close = true;
};

class WindowHeader : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(WindowHeader, Widget)

  WindowHeader();

  // --- metrics -------------------------------------------------------------
  void setHeight(float px);
  float height() const { return height_; }
  void setLeadingPadding(float px);
  float leadingPadding() const { return padLeft_; }
  void setTrailingPadding(float px);
  float trailingPadding() const { return padRight_; }
  // Height given to trailing child widgets; 0 = derive from the bar height.
  void setItemHeight(float px);
  float itemHeight() const;
  void setItemGap(float px);

  // --- colours -------------------------------------------------------------
  void setBackground(Color c);
  Color background() const { return background_; }
  void setBorderColor(Color c);
  void setBorderVisible(bool on);
  void setTitleColor(Color c);
  void setSubtitleColor(Color c);
  void setButtonColor(Color c);       // window-command glyphs
  void setButtonHoverColor(Color c);  // plate behind a hovered glyph
  void setCloseHoverColor(Color c);   // close is red on hover, by convention

  // --- brand block (leading edge) ------------------------------------------
  void setTitle(std::string utf8);
  const std::string& title() const { return title_; }
  void setSubtitle(std::string utf8);
  void setTitleFontSize(float px);
  void setSubtitleFontSize(float px);
  void setIcon(Icon icon);
  Icon icon() const { return icon_; }
  void setIconColor(Color c);
  void setIconSize(float px);
  // Rounded plate behind the icon -- turns a line glyph into a product mark.
  void setIconBadge(bool on);
  void setIconBadgeColor(Color c);

  // --- window commands (trailing edge) -------------------------------------
  void setButtons(WindowButtons b);
  const WindowButtons& buttons() const { return buttons_; }
  void setButtonWidth(float px);
  float buttonWidth() const { return buttonW_; }
  // Swaps the maximise glyph for the restore glyph.  AppWindow keeps this in
  // step with the real window state; call it yourself only when driving a
  // WindowHeader outside an AppWindow.
  void setMaximized(bool on);
  bool isMaximized() const { return maximized_; }

  // --- drag ----------------------------------------------------------------
  // When true, bare header background reports itself as the window caption.
  void setDraggable(bool on);
  bool isDraggable() const { return draggable_; }

  // --- trailing items ------------------------------------------------------
  //
  // The group is right-aligned against the window buttons, and items sit
  // left-to-right in the order they were added -- so the LAST one added ends up
  // nearest the corner, which is where an admin console puts the account menu.
  // Each item is a normal child widget: normal input, focus and repaint.
  template <class T, class... Args>
  T* addTrailingItem(float width, Args&&... args) {
    T* w = add<T>(std::forward<Args>(args)...);
    slots_.push_back({w, width, itemGap_ + pendingGap_});
    pendingGap_ = 0.0f;
    relayoutItems();
    return w;
  }
  // Extra space before the NEXT item added, for grouping.
  void addTrailingGap(float px);
  // Resizes an item already in the bar.  The usual reason is that its natural
  // width is only known once its icon and label have been set -- add it, fill
  // it in, then size it to its own preferredWidth().
  void setTrailingItemWidth(Widget* item, float width);
  void relayoutItems();

  // --- hit testing ---------------------------------------------------------
  // Caption for bare background (when draggable), Client over a window button.
  // Child widgets are excluded by the caller, which can see the whole tree.
  HitZone hitZone(Point local) const;

  Signal<> minimizeRequested;
  Signal<> maximizeRequested;
  Signal<> closeRequested;
  // Fires when a property changed the bar's height, so the host can re-lay out.
  Signal<> metricsChanged;

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;
  void onMouse(const MouseEvent& e) override;
  void onGeometryChanged() override;

 private:
  enum class Btn { None, Min, Max, Close };

  struct Slot {
    Widget* widget = nullptr;
    float width = 0.0f;
    float gap = 0.0f;
  };

  Btn buttonAt(Point local) const;
  Rect buttonRect(Btn b) const;
  float commandZoneWidth() const;
  void paintButton(Painter& p, Btn b, const Rect& r) const;

  float height_ = 44.0f;
  float padLeft_ = 14.0f;
  float padRight_ = 8.0f;
  float itemHeight_ = 0.0f;  // 0 = derived
  float itemGap_ = 6.0f;
  float pendingGap_ = 0.0f;

  bool hasBackground_ = false;
  Color background_;
  bool hasBorderColor_ = false;
  Color borderColor_;
  bool borderVisible_ = true;
  bool hasTitleColor_ = false;
  Color titleColor_;
  bool hasSubtitleColor_ = false;
  Color subtitleColor_;
  bool hasButtonColor_ = false;
  Color buttonColor_;
  bool hasButtonHover_ = false;
  Color buttonHover_;
  bool hasCloseHover_ = false;
  Color closeHover_;

  std::string title_;
  std::string subtitle_;
  float titleSize_ = 0.0f;     // 0 = theme default
  float subtitleSize_ = 0.0f;  // 0 = theme default
  Icon icon_ = Icon::None;
  bool hasIconColor_ = false;
  Color iconColor_;
  float iconSize_ = 20.0f;
  bool iconBadge_ = false;
  bool hasIconBadgeColor_ = false;
  Color iconBadgeColor_;

  WindowButtons buttons_;
  float buttonW_ = 46.0f;
  bool maximized_ = false;
  bool draggable_ = true;

  Btn hovered_ = Btn::None;
  Btn pressed_ = Btn::None;
  std::vector<Slot> slots_;
};

// ---------------------------------------------------------------------------
// A drop-down sized and styled for the header: optional icon, optional label,
// optional chevron, no border until hovered.
//
// Derives from MenuButton so the popup, the keyboard handling and the Alt+1..9
// shortcuts are inherited whole; only the painting differs.  This is the
// "language switcher" / "environment picker" of an admin console.
class HeaderMenu : public MenuButton {
 public:
  GEEYOOU_STYLE_TYPE(HeaderMenu, MenuButton)

  HeaderMenu();

  void setShowChevron(bool on);
  void setLabelColor(Color c);
  // Small counter badge on the glyph, the way a notification bell works.
  // 0 hides it; anything above 99 renders as "99+".
  void setBadgeCount(int n);
  int badgeCount() const { return badge_; }
  void setBadgeColor(Color c);

  // Width this menu wants for its current icon and label.
  float preferredWidth() const;

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;
  float arrowWidth() const override { return 0.0f; }

 private:
  bool chevron_ = true;
  bool hasLabelColor_ = false;
  Color labelColor_;
  int badge_ = 0;
  bool hasBadgeColor_ = false;
  Color badgeColor_;
};

// ---------------------------------------------------------------------------
// The account block every web admin console puts in its top-right corner: a
// round avatar, an optional name and role, and a menu behind it.
//
// The avatar is drawn from INITIALS rather than an image on purpose -- an HMI
// often has no filesystem to load a photo from, and a missing image would leave
// a hole in the chrome.
class HeaderAvatar : public MenuButton {
 public:
  GEEYOOU_STYLE_TYPE(HeaderAvatar, MenuButton)

  HeaderAvatar();

  void setInitials(std::string utf8);  // 1-2 characters; CJK reads best as 1
  void setAvatarColor(Color c);
  void setName(std::string utf8);
  void setCaption(std::string utf8);  // second line: role, plant, shift
  void setShowText(bool on);
  void setAvatarDiameter(float px);
  // Presence dot on the avatar's lower-right; alpha 0 hides it.
  void setStatusColor(Color c);

  float preferredWidth() const;

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;
  float arrowWidth() const override { return 0.0f; }

 private:
  std::string initials_;
  std::string name_;
  std::string caption_;
  bool showText_ = true;
  float diameter_ = 28.0f;
  bool hasAvatarColor_ = false;
  Color avatarColor_;
  Color status_ = Color::rgba(0, 0, 0, 0);
};

}  // namespace geeyoou
