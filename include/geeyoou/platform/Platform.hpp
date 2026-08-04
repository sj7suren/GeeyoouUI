#pragma once
//
// THE PORTING BOUNDARY.
//
// Everything above this file is platform-agnostic.  A new backend has to supply
// exactly what is declared here: a window, a writable pixel buffer, and a
// stream of input events.  No layer above platform/ may include <windows.h>,
// X11 headers, or Cocoa headers.
//
// v1 ships Win32 only.  See docs/architecture.md section 2.
//
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "geeyoou/core/Event.hpp"
#include "geeyoou/core/Surface.hpp"
#include "geeyoou/core/Types.hpp"

namespace geeyoou {

// What the pointer is over, as far as the WINDOW MANAGER is concerned.
//
// A frameless window has no caption of its own, so the window manager has to
// ask the application: "is this pixel draggable chrome, or is it content?".
// Answering Caption is what buys drag-to-move, double-click-to-maximise, Aero
// Snap and the right-click system menu for free -- reimplementing those on top
// of raw mouse events gets all four subtly wrong.
enum class HitZone : std::uint8_t {
  Client,   // ordinary widget content; the app gets the mouse events
  Caption,  // the window manager takes over: drag, snap, system menu
};

// Creation-time properties that cannot be changed afterwards without
// recreating the native window.
struct WindowOptions {
  // No OS title bar and no OS border: the application draws its own chrome.
  // The window is still a normal top-level window -- it keeps snap layouts,
  // the minimise animation and the alt-tab thumbnail.
  bool frameless = false;
  bool resizable = true;
  Size minSize{360.0f, 240.0f};
};

class PlatformWindow {
 public:
  virtual ~PlatformWindow() = default;

  virtual void show() = 0;
  virtual void close() = 0;
  virtual void setTitle(const std::string& utf8) = 0;

  // --- window commands, for an app-drawn caption ---------------------------
  virtual void minimize() = 0;
  virtual void maximize() = 0;
  virtual void restore() = 0;
  virtual bool isMaximized() const = 0;
  virtual bool isFrameless() const = 0;
  virtual void setMinimumSize(Size logical) = 0;

  // Client area in LOGICAL pixels.
  virtual Size clientSize() const = 0;

  // Device pixel ratio, e.g. 1.5 at 144 DPI.
  virtual float scaleFactor() const = 0;

  // Mark a logical-pixel region as needing repaint.  Backends are free to
  // coalesce; they must never repaint less than what was requested.
  virtual void invalidate(const Rect& logicalRect) = 0;

  virtual void* nativeHandle() = 0;

  // Tells the platform where the text caret currently is, in LOGICAL pixels
  // relative to the client area.  On Windows this positions the IME candidate
  // window; on X11 it feeds the input-method context.  A focused text control
  // must call this whenever its caret moves -- otherwise the Chinese candidate
  // list appears in a corner of the screen instead of under the cursor, which
  // is the single most common "the input box feels broken" complaint.
  virtual void setImeCaret(const Rect& logicalCaretRect) = 0;

  // --- callbacks, installed by Window -------------------------------------
  //
  // onPaint hands up a raw pixel buffer and the damaged rectangle in PHYSICAL
  // pixels.  It deliberately says nothing about how those pixels get filled:
  // the backend never sees a Painter, never links Blend2D, and would not have
  // to change if the renderer were swapped for a GPU one.
  std::function<void(const Surface&, const Rect& dirtyPhysical)> onPaint;
  std::function<void(const MouseEvent&)> onMouse;
  std::function<void(const KeyEvent&)> onKey;
  std::function<void(const ResizeEvent&)> onResize;
  std::function<void()> onClose;

  // Asked on every pointer move over a FRAMELESS window, in logical client
  // coordinates.  Resize borders are the backend's business -- it knows the
  // grip thickness for the current DPI -- so this is only ever asked about the
  // interior.  Unset means "all client", i.e. a window that cannot be dragged.
  std::function<HitZone(Point clientLogical)> onHitTest;

  // Maximised <-> restored, so an app-drawn caption can swap its glyph.  Also
  // fires for the snap gestures, which never go through maximize().
  std::function<void()> onWindowStateChanged;
};

// Identifies a running timer.  Zero is never handed out and never refers to
// anything, so a default-initialised TimerId is safe to pass to stopTimer().
// Ids are not reused, which is what makes stopping a timer twice -- or stopping
// one that has already been torn down -- a no-op instead of a way to kill
// somebody else's clock.
//
// At namespace scope rather than nested in Platform: widgets store one as a
// member and should not have to name the backend interface to do so.
using TimerId = std::uint64_t;

class Platform {
 public:
  virtual ~Platform() = default;

  virtual std::unique_ptr<PlatformWindow> createWindow(
      const std::string& title, int logicalWidth, int logicalHeight,
      const WindowOptions& options = {}) = 0;

  virtual int runEventLoop() = 0;
  virtual void quit(int exitCode = 0) = 0;

  // Fires `fn` on the UI thread every `intervalMs`.  This is the only timing
  // primitive the widget layer needs: HMI screens are driven either by data
  // arrival or by a fixed refresh tick.
  //
  // The returned id MUST be kept by anything whose callback captures a pointer
  // it does not own for the rest of the process.  A timer outlives the object
  // that started it unless somebody stops it, and the callback is the last
  // thing running against a destroyed window.  [[nodiscard]] because "I meant
  // this one to run forever" is worth one explicit (void) at the call site.
  [[nodiscard]] virtual TimerId startTimer(int intervalMs,
                                           std::function<void()> fn) = 0;

  // Cancels a timer.  Silently does nothing for id 0, for an id that already
  // stopped, and for one that never existed.  Legal from inside the timer's own
  // callback.
  virtual void stopTimer(TimerId id) = 0;

  // Clipboard, as UTF-8.  Returns an empty string when the clipboard holds no
  // text (or cannot be opened -- another process may hold it locked, which is
  // a normal transient condition and not worth an error channel).
  virtual std::string clipboardText() = 0;
  virtual void setClipboardText(const std::string& utf8) = 0;
};

// The backend compiled into this build.
Platform& platform();

}  // namespace geeyoou
