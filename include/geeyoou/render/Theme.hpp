#pragma once
//
// A token struct, not a stylesheet engine.
//
// Industrial HMI screens are built once by a commissioning engineer and then
// run unchanged for years; the flexibility of a CSS-like selector engine buys
// nothing here and costs a parser plus a cascade resolver.
// See docs/architecture.md section 4.
//
// Lives in render/ rather than hmi/ because BOTH widget/ and hmi/ need it, and
// widget/ sits below hmi/ -- putting the theme in hmi/ would force every generic
// control to depend upwards on the domain layer.
//
#include "geeyoou/core/Types.hpp"

namespace geeyoou {

struct Theme {
  Color background = Color::rgb(0x12, 0x16, 0x1D);
  Color panel = Color::rgb(0x1B, 0x21, 0x2E);
  Color panelBorder = Color::rgb(0x2C, 0x37, 0x4C);
  Color text = Color::rgb(0xE6, 0xEB, 0xF4);
  Color textDim = Color::rgb(0x86, 0x94, 0xAD);
  Color textDisabled = Color::rgb(0x4C, 0x57, 0x6B);
  Color accent = Color::rgb(0x2F, 0xA8, 0xFF);
  Color ok = Color::rgb(0x3E, 0xD1, 0x7A);
  Color warn = Color::rgb(0xFF, 0xB0, 0x20);
  Color alarm = Color::rgb(0xFF, 0x4D, 0x5E);
  Color grid = Color::rgb(0x27, 0x30, 0x42);
  Color track = Color::rgb(0x2A, 0x33, 0x46);
  Color focusRing = Color::rgb(0x5C, 0xC0, 0xFF);
  Color field = Color::rgb(0x11, 0x16, 0x20);
  Color placeholder = Color::rgb(0x5A, 0x66, 0x7C);
  Color selection = Color::rgb(0x1E, 0x4E, 0x7A);  // text selection background
  Color scrollbar = Color::rgb(0x3A, 0x46, 0x5E);

  // Semantic button/status palette.  Named by MEANING, not by hue, so a plant
  // that wants amber "success" only has to change it here.
  Color primary = Color::rgb(0x2F, 0xA8, 0xFF);
  Color success = Color::rgb(0x3E, 0xD1, 0x7A);
  Color danger = Color::rgb(0xFF, 0x4D, 0x5E);

  float radius = 6.0f;
  float fontSmall = 11.0f;
  float fontBody = 13.0f;
  float fontLarge = 22.0f;

  // Label colour that stays readable on top of a filled semantic button.
  Color onFilled = Color::rgb(0x0B, 0x0F, 0x16);

  static Theme& current() {
    static Theme t;
    return t;
  }
};

}  // namespace geeyoou
