#pragma once
//
// Input events.  Coordinates are LOGICAL pixels; the platform layer has already
// divided out the device pixel ratio before these are constructed.
//
#include <cstdint>

#include "geeyoou/core/Types.hpp"

namespace geeyoou {

enum class MouseButton : std::uint8_t { None, Left, Right, Middle };

enum class MouseAction : std::uint8_t { Press, Release, Move, Enter, Leave, Wheel };

struct MouseEvent {
  MouseAction action = MouseAction::Move;
  MouseButton button = MouseButton::None;
  Point pos;             // relative to the widget receiving the event
  Point windowPos;       // relative to the window's client area
  float wheelDelta = 0;  // notches, positive = away from the user

  // Modifier state at the time of the event.  Needed for shift-click range
  // selection in text fields and ctrl-click multi-select in future list views.
  bool shift = false;
  bool ctrl = false;
  bool alt = false;

  // Set by a handler that consumed the event; stops it from bubbling further.
  mutable bool accepted = false;

  void accept() const { accepted = true; }
};

// Platform-independent key identity.
//
// Widgets MUST switch on this, never on KeyEvent::nativeCode.  A control that
// hardcodes VK_SPACE compiles fine today and silently does nothing the moment
// the X11 backend lands -- the whole point of the platform boundary is that the
// layers above it never learn what a virtual-key code is.
enum class Key : std::uint16_t {
  Unknown = 0,
  Tab, Enter, Escape, Space, Backspace, Delete,
  Left, Right, Up, Down, Home, End, PageUp, PageDown,
  Minus, Period,
  Digit0, Digit1, Digit2, Digit3, Digit4,
  Digit5, Digit6, Digit7, Digit8, Digit9,
  // Letters exist for SHORTCUTS only (Ctrl+A/C/V/X/Z).  Text entry never goes
  // through them -- typed characters arrive via KeyEvent::character, which is
  // what makes Chinese IME input work without the widget layer knowing.
  KeyA, KeyB, KeyC, KeyD, KeyE, KeyF, KeyG, KeyH, KeyI, KeyJ, KeyK, KeyL, KeyM,
  KeyN, KeyO, KeyP, KeyQ, KeyR, KeyS, KeyT, KeyU, KeyV, KeyW, KeyX, KeyY, KeyZ,
};

// True for Digit0..Digit9; returns the numeric value via `out`.
inline bool keyToDigit(Key k, int& out) {
  const auto v = static_cast<std::uint16_t>(k);
  const auto lo = static_cast<std::uint16_t>(Key::Digit0);
  const auto hi = static_cast<std::uint16_t>(Key::Digit9);
  if (v < lo || v > hi) return false;
  out = int(v - lo);
  return true;
}

struct KeyEvent {
  Key key = Key::Unknown;
  std::uint32_t nativeCode = 0;  // raw platform code, for escape hatches only
  std::uint32_t character = 0;   // UTF-32, 0 when the key produced no text
  bool pressed = false;
  bool shift = false;
  bool ctrl = false;
  bool alt = false;

  mutable bool accepted = false;
  void accept() const { accepted = true; }
};

struct ResizeEvent {
  Size size;           // new client size, logical pixels
  float scale = 1.0f;  // device pixel ratio
};

}  // namespace geeyoou
