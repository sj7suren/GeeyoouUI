#pragma once
//
// AppWindow: the base class an application window derives from.
//
// A plain Window is a bare surface with a widget tree on it.  AppWindow adds
// the layer above that -- the chrome -- and is the class every GeeyoouUI
// application is expected to inherit:
//
//     class PlantWindow : public AppWindow {
//      public:
//       PlantWindow() : AppWindow("配料工段", 1280, 800) {
//         header()->setTitle("配料工段");
//         header()->setIcon(Icon::Settings);
//         setContent<PlantScreen>();
//       }
//     };
//
// It is FRAMELESS by default: no Windows caption, no Windows border, no
// Windows accent colour bleeding into an HMI that was colour-matched to a
// customer's plant standard.  The title bar is a WindowHeader drawn with the
// library's own Painter and Theme, so the window looks the same on every
// desktop theme and every OS version -- and, once the X11 backend lands, on
// every platform.
//
// The whole header drags the window (that is a window-manager caption, so
// Aero Snap, double-click-to-maximise and the right-click system menu all keep
// working), EXCEPT where an actual control sits: the window buttons and any
// widget added with WindowHeader::addTrailingItem().
//
#include <string>
#include <utility>

#include "geeyoou/core/Signal.hpp"
#include "geeyoou/widget/Window.hpp"
#include "geeyoou/widget/WindowHeader.hpp"

namespace geeyoou {

class AppWindow : public Window {
 public:
  GEEYOOU_STYLE_TYPE(AppWindow, Window)

  AppWindow(const std::string& title, int logicalWidth, int logicalHeight,
            const WindowOptions& options = framelessDefaults());
  ~AppWindow() override;

  // Frameless + resizable.  Pass a modified copy to the constructor for a
  // fixed-size panel, a larger minimum, or to opt back into the OS frame.
  static WindowOptions framelessDefaults();

  // The title bar.  Every visual property of the chrome is set through it.
  WindowHeader* header() { return header_; }
  // Everything below the header.  Add the application's UI here.
  Widget* content() { return content_; }

  // Creates `T` inside the content area AND keeps it sized to it, so a root
  // container does not have to subscribe to resized() itself.  Only the last
  // widget passed through here is tracked.
  template <class T, class... Args>
  T* setContent(Args&&... args) {
    T* w = content_->add<T>(std::forward<Args>(args)...);
    fill_ = w;
    relayout();
    return w;
  }

  void setHeaderVisible(bool on);
  bool isHeaderVisible() const;

  // Hairline outline around the whole window.  A frameless window has no OS
  // border, so without this it dissolves into a dark desktop background.
  // Automatically suppressed while maximised, where there is no edge to mark.
  void setBorderVisible(bool on);
  bool isBorderVisible() const { return borderVisible_; }
  void setBorderColor(Color c);

  void relayout();

  // New size of the content area, for anything laying itself out inside it.
  Signal<Size> contentResized;

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;
  void onGeometryChanged() override;
  HitZone hitZoneAt(Point windowPos) override;

 private:
  float borderWidth() const;

  WindowHeader* header_ = nullptr;
  Widget* content_ = nullptr;
  Widget* fill_ = nullptr;
  bool borderVisible_ = true;
  bool hasBorderColor_ = false;
  Color borderColor_;
  Connection skinConn_;
};

}  // namespace geeyoou
