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
  // What the pointer is over, and which widget holds the mouse grab.  Read-only
  // on purpose: both are decided by input dispatch, and a setter would be a way
  // to desynchronise them from the enter/leave bookkeeping that maintains them.
  Widget* hoveredWidget() const { return hovered_; }
  Widget* mouseGrabWidget() const { return pressGrab_; }
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

  // Whether a tooltip bubble is on screen right now.  A tooltip arms when the
  // cursor comes to rest on a widget carrying one and shows after a short delay;
  // this reports the shown state, mainly for tests and for an app that wants to
  // suppress its own transient overlays while a hint is up.
  bool isTooltipVisible() const { return tooltipShown_; }
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

  // Announced by Widget's removal API once for EVERY node of a subtree that is
  // leaving the tree, while the subtree is still intact and still attached.
  // Drops whichever of the four observer pointers named `w`: a window that kept
  // focus_ or hovered_ on a removed widget would deliver the next keystroke, or
  // the next Leave, into freed memory.
  //
  // Public because Widget calls it, not because users should.
  void widgetDetached(Widget* w);

 protected:
  // Answers the window manager's "is this pixel draggable chrome?" for a
  // frameless window.  The base window has no chrome, so everything is client;
  // AppWindow overrides this to report its header.  Only ever asked about the
  // interior -- resize borders are handled inside the platform backend.
  virtual HitZone hitZoneAt(Point windowPos) {
    (void)windowPos;
    return HitZone::Client;
  }

  // The full input path -- hover tracking, mouse grab, focus-on-click, popup
  // dismissal, Tab traversal -- as the platform backend drives it.  Protected
  // rather than private so a subclass can feed SYNTHETIC input through exactly
  // the same route: an operator-training playback, a soft keyboard, and the
  // test suite all need that, and a second entry point that skipped the
  // bookkeeping would be a second, subtly different set of rules.
  void handleMouse(const MouseEvent& e);
  void handleKey(const KeyEvent& e);

 private:
  Window* asWindow() override { return this; }

  void handleSurface(const Surface& s, const Rect& dirtyPhysical);
  void handlePaint(Painter& p, const Rect& dirty);
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

  // --- tooltip ---
  // A hover hint drawn by the Window itself (not a widget, so it captures no
  // input and needs no dismiss handling).  Armed when the hovered widget -- or
  // an ancestor -- carries a tooltip, shown after a rest delay, hidden on the
  // next hover change, move-away or press.  Same held-timer rule as the
  // animation clock: the callback captures `this`, so the destructor stops it.
  void armTooltip(Point cursorWindow);
  void hideTooltip();
  std::string tooltipText_;
  Point tooltipAt_{};
  bool tooltipShown_ = false;
  TimerId tooltipTimer_ = 0;

  // Declared LAST so it is destroyed FIRST: the skin subscription captures
  // `this` and calls update(), which reaches for platformWindow_.  Anything
  // added here must be unsubscribed before the state it touches is gone.
  ConnectionScope conns_;
};

}  // namespace geeyoou
