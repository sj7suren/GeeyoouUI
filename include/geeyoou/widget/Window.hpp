#pragma once
//
// Window: the root of a widget tree, bound to a PlatformWindow.
//
#include <memory>
#include <string>

#include "geeyoou/core/ConnectionScope.hpp"
#include "geeyoou/core/Signal.hpp"
#include "geeyoou/core/Surface.hpp"
#include "geeyoou/platform/Platform.hpp"
#include "geeyoou/render/Canvas.hpp"
#include "geeyoou/widget/Widget.hpp"

namespace geeyoou {

class Window : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(Window, Widget)

  Window(const std::string& title, int logicalWidth, int logicalHeight,
         const WindowOptions& options = {});
  ~Window() override;

  void show();
  void setTitle(const std::string& utf8);
  void setBackground(Color c) { background_ = c; }
  Color background() const { return background_; }

  float scaleFactor() const;

  // --- window commands -----------------------------------------------------
  //
  // Public so an app-drawn title bar can drive them.  On a normal framed window
  // they still work; they just duplicate buttons the OS already provides.
  void close();
  void minimize();
  void maximize();
  void restore();
  void toggleMaximize();
  bool isMaximized() const;
  bool isFrameless() const;
  void setMinimumSize(Size logical);

  // Maximised <-> restored, including via snap gestures and a double-click on
  // whatever the app reports as its caption.
  Signal<bool> maximizedChanged;

  // --- focus ---------------------------------------------------------------
  void setFocusWidget(Widget* w);
  Widget* focusWidget() const { return focus_; }
  // Moves focus to the next (or previous) FocusPolicy::Tab widget in tree
  // order, wrapping around.  Returns false when nothing is focusable.
  bool focusNext(bool backwards);

  // Starts a single UI-thread clock that walks the tree calling
  // Widget::onAnimationTick().  Required for spinners and blinking lamps.
  // Idempotent: calling it twice does not start a second timer.
  void enableAnimations(int fps = 30);
  bool animationsEnabled() const { return animationsOn_; }

  // Forwards the focused text control's caret to the platform so the IME
  // candidate window follows it.  Called by text widgets, not by users.
  void setImeCaret(const Rect& windowRect);

  // --- overlay / popup -----------------------------------------------------
  //
  // A popup is an ordinary child of the Window that is painted LAST and
  // hit-tested FIRST.  Being parented to the Window rather than to the control
  // that opened it is the whole point: paintTree clips every widget to its own
  // bounds, so a dropdown parented inside a GroupBox would be sliced off at the
  // group's edge.
  //
  // Popups are confined to the window.  A native child HWND would let them
  // spill onto the desktop, but an HMI runs full-screen or kiosked, so the
  // extra platform surface is not worth it -- instead a popup with no room
  // below its anchor flips above it.
  //
  // `anchor` is in window coordinates; the popup keeps its own size.
  void openPopup(Widget* popup, const Rect& anchor);
  void closePopup();
  Widget* popup() const { return popup_; }
  // Fires after the popup closes, for whatever opened it.
  Signal<> popupClosed;

  Signal<> closed;
  // New client size in logical pixels.  A root-level container connects to
  // this to re-lay itself out; without it a shell would be pinned to whatever
  // size the window was constructed at.
  Signal<Size> resized;

  // Called by Widget::update(); accumulates damage and asks the platform for a
  // repaint.  Public because Widget calls it, not because users should.
  void addDirtyRect(const Rect& windowRect);

 protected:
  // Answers the window manager's "is this pixel draggable chrome?" for a
  // frameless window.  The base window has no chrome, so everything is client;
  // AppWindow overrides this to report its header.  Only ever asked about the
  // interior -- resize borders are handled inside the platform backend.
  virtual HitZone hitZoneAt(Point windowPos) {
    (void)windowPos;
    return HitZone::Client;
  }

 private:
  Window* asWindow() override { return this; }

  void handleSurface(const Surface& s, const Rect& dirtyPhysical);
  void handlePaint(Painter& p, const Rect& dirty);
  void handleMouse(const MouseEvent& e);
  void handleKey(const KeyEvent& e);
  void handleResize(const ResizeEvent& e);

  std::unique_ptr<PlatformWindow> platformWindow_;
  // The Window owns the binding between the platform's pixels and the
  // renderer.  Keeping it here (rather than per-paint) lets Canvas cache its
  // backing image across frames.
  Canvas canvas_;
  Color background_ = Color::rgb(0x12, 0x16, 0x1D);

  // Mouse grab: once a button goes down on a widget, that widget receives every
  // subsequent move and the release, even outside its bounds.  Without this a
  // button would get stuck "pressed" whenever the cursor slid off it.
  Widget* pressGrab_ = nullptr;
  Widget* hovered_ = nullptr;
  Widget* focus_ = nullptr;
  Widget* popup_ = nullptr;
  bool animationsOn_ = false;
  // The animation clock lives in the platform, not in this object, and its
  // callback captures `this`.  Held so the destructor can stop it -- otherwise
  // closing any window at all ticks into freed memory a few milliseconds later.
  TimerId animationTimer_ = 0;

  // Declared LAST so it is destroyed FIRST: the skin subscription captures
  // `this` and calls update(), which reaches for platformWindow_.  Anything
  // added here must be unsubscribed before the state it touches is gone.
  ConnectionScope conns_;
};

}  // namespace geeyoou
