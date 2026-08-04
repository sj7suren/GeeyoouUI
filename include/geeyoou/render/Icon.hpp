#pragma once
//
// Vector icons drawn in code.
//
// No icon font and no image assets, deliberately:
//   * an icon font would have to be shipped and installed, and a missing glyph
//     renders as a tofu box on a machine you cannot log into;
//   * bitmaps would need one asset per DPI scale;
//   * code-drawn paths cost nothing at build time, scale to any size, and take
//     the theme colour like any other primitive.
//
// Every icon is authored in a 24x24 box and scaled into whatever Rect it is
// given, so a 14px inline icon and a 48px toolbar icon come from one source.
//
// EXTENSION: the enumerators below are the built-in set, but Icon is a HANDLE,
// not a closed list.  IconRegistry hands out ids at or above FirstCustom for
// icons registered at runtime -- from a drawing callback or from SVG path data.
// Because those ids are still `Icon` values, every existing API that takes an
// Icon (PushButton::setIcon, MenuItem::icon, PopupRow::icon, ...) accepts a
// custom icon with no change at all.  See render/IconRegistry.hpp.
//
#include <cstdint>

#include "geeyoou/core/Types.hpp"

namespace geeyoou {

class Painter;

enum class Icon : std::uint16_t {
  None = 0,
  Search,
  Close,        // X -- also the "clear field" affordance
  Eye,          // password visible
  EyeOff,       // password hidden
  Check,
  Warning,      // triangle + bang
  Error,        // circle + X
  Info,
  Plus,
  Minus,
  ChevronUp,
  ChevronDown,
  ChevronLeft,
  ChevronRight,
  Refresh,
  Settings,
  Play,
  Pause,
  Stop,
  Trash,
  Save,
  Lock,
  Unlock,
  Filter,
  Download,
  Upload,
  Edit,
  Copy,
  Menu,

  // --- window chrome -------------------------------------------------------
  // Drawn by WindowHeader in place of the OS caption buttons.  They are
  // deliberately geometric rather than "styled": an operator must recognise
  // minimise/maximise/close instantly, and a clever glyph is a support call.
  WindowMinimize,
  WindowMaximize,
  WindowRestore,

  // --- admin-console header ------------------------------------------------
  User,
  Globe,    // language switcher
  Bell,     // notifications
  Logout,
  Sun,      // light theme
  Moon,     // dark theme

  // Everything at or above this is handed out by IconRegistry.  The gap leaves
  // room to add built-ins later without renumbering anyone's saved icon id.
  FirstCustom = 0x1000,
};

// Draws `icon` centred in `box`, using the largest square that fits.
// `strokeScale` multiplies the default stroke weight (1.8 units in the 24x24
// authoring grid) for callers that want a lighter or heavier look.
//
// Ids at or above Icon::FirstCustom are resolved through IconRegistry; an
// unregistered id draws nothing rather than a placeholder box, because a
// missing glyph must never be mistaken for a real one on a plant display.
void drawIcon(Painter& p, Icon icon, const Rect& box, Color color,
              float strokeScale = 1.0f);

}  // namespace geeyoou
