//
// StyleSheet parse + resolve tests.
//
// Every case here uses a LOCAL StyleSheet and calls resolve() directly, rather
// than going through activeStyleSheet() and Widget::style().  That keeps the
// cases order-independent -- a cascade test that mutated global state would
// quietly decide what the golden-image tests render.
//
#include "geeyoou/render/StyleSheet.hpp"

#include <string>

#include "framework/Test.hpp"
#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/GroupBox.hpp"
#include "geeyoou/widget/IconButton.hpp"
#include "geeyoou/widget/Label.hpp"
#include "geeyoou/widget/PushButton.hpp"

using geeyoou::Color;
using geeyoou::GroupBox;
using geeyoou::IconButton;
using geeyoou::Label;
using geeyoou::PushButton;
using geeyoou::StyleProps;
using geeyoou::StyleSheet;
using geeyoou::StyleState;
using geeyoou::Theme;

namespace {
constexpr std::uint32_t kNone = 0xFFFFFFFFu;  // "no colour resolved"

// Resolved colour as a plain integer, so a mismatch prints as 0xff112233 rather
// than as "<?>".
std::uint32_t colorOf(const StyleProps& p) {
  return p.has(StyleProps::kColor) ? p.color.argb() : kNone;
}
}  // namespace

// -------------------------------------------------------------- specificity ---
GEEYOOU_TEST(style, id_beats_class_beats_type) {
  StyleSheet s;
  // Deliberately written LEAST specific last, so a naive "last rule wins"
  // implementation would fail every assertion below.
  CHECK(s.parse(
      "#stop      { color: #333333; }"
      ".danger    { color: #222222; }"
      "PushButton { color: #111111; }"));
  CHECK(s.errors().empty());
  CHECK_EQ(s.ruleCount(), std::size_t(3));

  PushButton b;
  CHECK_EQ(colorOf(s.resolve(b, StyleState::None)), 0xFF111111u);

  b.setStyleClasses("danger");
  CHECK_EQ(colorOf(s.resolve(b, StyleState::None)), 0xFF222222u);

  b.setObjectName("stop");
  CHECK_EQ(colorOf(s.resolve(b, StyleState::None)), 0xFF333333u);

  // A state counts towards the class tier, so `PushButton:hover` outranks
  // `PushButton` but still loses to an id.
  StyleSheet s2;
  CHECK(s2.parse("PushButton:hover { color: #444444; } PushButton { color: #111111; }"));
  PushButton plain;
  CHECK_EQ(colorOf(s2.resolve(plain, StyleState::None)), 0xFF111111u);
  CHECK_EQ(colorOf(s2.resolve(plain, StyleState::Hover)), 0xFF444444u);
}

GEEYOOU_TEST(style, equal_specificity_is_broken_by_source_order) {
  StyleSheet s;
  CHECK(s.parse("PushButton { color: #111111; } PushButton { color: #999999; }"));
  PushButton b;
  CHECK_EQ(colorOf(s.resolve(b, StyleState::None)), 0xFF999999u);  // later wins
}

GEEYOOU_TEST(style, the_cascade_is_decided_per_property) {
  StyleSheet s;
  CHECK(s.parse(
      "PushButton { color: #111111; border-radius: 4; font-size: 12; }"
      "#stop      { color: #333333; }"));

  PushButton b;
  b.setObjectName("stop");
  const StyleProps p = s.resolve(b, StyleState::None);

  // The id rule wins `color` and contributes nothing else -- the type rule's
  // other two properties survive.  A winner-per-RULE cascade would drop them.
  CHECK_EQ(colorOf(p), 0xFF333333u);
  CHECK(p.has(StyleProps::kRadius));
  CHECK_NEAR(p.radius, 4.0f, 0.0001f);
  CHECK(p.has(StyleProps::kFontSize));
  CHECK_NEAR(p.fontSize, 12.0f, 0.0001f);

  // Unset properties stay unset, which is what makes the *Or() accessors the
  // whole API a widget needs.
  CHECK(!p.has(StyleProps::kBackground));
  CHECK_EQ(p.backgroundOr(Color::rgb(1, 2, 3)).argb(), 0xFF010203u);
}

// -------------------------------------------------------------------- state ---
GEEYOOU_TEST(style, a_state_selector_matches_any_superset_of_its_states) {
  StyleSheet s;
  CHECK(s.parse(
      "PushButton:hover       { color: #AA0000; }"
      "PushButton:hover:focus { color: #BB0000; }"));

  PushButton b;
  CHECK_EQ(colorOf(s.resolve(b, StyleState::None)), kNone);
  CHECK_EQ(colorOf(s.resolve(b, StyleState::Focus)), kNone);
  CHECK_EQ(colorOf(s.resolve(b, StyleState::Hover)), 0xFFAA0000u);

  // Hover+focus satisfies BOTH rules; the two-state one is more specific.
  CHECK_EQ(colorOf(s.resolve(b, StyleState::Hover | StyleState::Focus)), 0xFFBB0000u);
  // Hover plus an unrelated state still matches the single-state rule.
  CHECK_EQ(colorOf(s.resolve(b, StyleState::Hover | StyleState::Pressed)), 0xFFAA0000u);
}

// ------------------------------------------------------------------ matching ---
GEEYOOU_TEST(style, type_selectors_are_inheritance_aware) {
  StyleSheet s;
  CHECK(s.parse(
      "Widget     { font-size: 10; }"
      "PushButton { color: #010101; }"
      "Label      { color: #020202; }"));

  IconButton ib;  // IconButton -> PushButton -> Widget
  const StyleProps p = s.resolve(ib, StyleState::None);
  CHECK_EQ(colorOf(p), 0xFF010101u);
  CHECK(p.has(StyleProps::kFontSize));

  Label label;
  CHECK_EQ(colorOf(s.resolve(label, StyleState::None)), 0xFF020202u);

  // `*` matches anything, but at the lowest tier.
  StyleSheet s2;
  CHECK(s2.parse("* { color: #050505; } Label { color: #060606; }"));
  PushButton b;
  CHECK_EQ(colorOf(s2.resolve(b, StyleState::None)), 0xFF050505u);
  CHECK_EQ(colorOf(s2.resolve(label, StyleState::None)), 0xFF060606u);
}

GEEYOOU_TEST(style, descendant_and_group_selectors) {
  StyleSheet s;
  CHECK(s.parse(
      "GroupBox PushButton { color: #0A0B0C; }"
      "Label, PushButton   { border-radius: 3; }"));

  GroupBox box;
  PushButton* inside = box.add<PushButton>();
  PushButton outside;
  REQUIRE(inside != nullptr);

  CHECK_EQ(colorOf(s.resolve(*inside, StyleState::None)), 0xFF0A0B0Cu);
  CHECK_EQ(colorOf(s.resolve(outside, StyleState::None)), kNone);

  // Descendant, not child: an intervening widget must not break the match.
  GroupBox deepBox;
  PushButton* deep = deepBox.add<geeyoou::Widget>()->add<PushButton>();
  REQUIRE(deep != nullptr);
  CHECK_EQ(colorOf(s.resolve(*deep, StyleState::None)), 0xFF0A0B0Cu);

  // Both members of the group carry the shared declaration.
  Label label;
  CHECK(s.resolve(label, StyleState::None).has(StyleProps::kRadius));
  CHECK(s.resolve(outside, StyleState::None).has(StyleProps::kRadius));
}

// -------------------------------------------------------------------- values ---
GEEYOOU_TEST(style, colour_value_forms) {
  StyleSheet s;
  CHECK(s.parse(
      "#a { color: #F00; }"
      "#b { color: #10203040; }"
      "#c { color: rgb(1, 2, 3); }"
      "#d { color: rgba(4, 5, 6, 7); }"
      "#e { color: transparent; }"));

  PushButton w;
  constexpr std::size_t kCount = 5;
  const char* const names[kCount] = {"a", "b", "c", "d", "e"};
  // #RGB doubles each digit; #AARRGGBB matches Color's own packing order; a
  // transparent value is a real value, not an absence.
  const std::uint32_t want[kCount] = {0xFFFF0000u, 0x10203040u, 0xFF010203u,
                                      0x07040506u, 0x00000000u};
  for (std::size_t i = 0; i < kCount; ++i) {
    w.setObjectName(names[i]);
    CHECK_EQ(colorOf(s.resolve(w, StyleState::None)), want[i]);
  }
}

GEEYOOU_TEST(style, theme_tokens_resolve_against_the_live_theme) {
  // Saved and restored rather than assumed: this is the one test that touches
  // process-global state, and the golden-image cases depend on the default.
  const Theme saved = Theme::current();
  PushButton b;

  // An unknown token drops that ONE declaration and reports it; the rest of the
  // rule survives.
  StyleSheet bad;
  CHECK(!bad.parse("* { accent: @accent; background: @nosuchtoken; }"));
  CHECK(!bad.errors().empty());
  CHECK(bad.resolve(b, StyleState::None).has(StyleProps::kAccent));
  CHECK(!bad.resolve(b, StyleState::None).has(StyleProps::kBackground));

  StyleSheet s;
  CHECK(s.parse("* { accent: @accent; color: @textDim; }"));
  CHECK(s.errors().empty());
  const StyleProps p = s.resolve(b, StyleState::None);
  CHECK(p.has(StyleProps::kAccent));
  CHECK_EQ(p.accent.argb(), Theme::current().accent.argb());
  CHECK_EQ(p.color.argb(), Theme::current().textDim.argb());

  // `@accent` is resolved at PARSE time, so a theme swap needs a re-parse --
  // which is exactly what the skin registry does.  Pinned here because "one
  // sheet follows every skin change" is the feature the syntax exists for, and
  // a regression would stay invisible until a customer switched skins.
  Theme::current().accent = Color::rgb(0x11, 0x22, 0x33);
  const std::uint32_t stale = s.resolve(b, StyleState::None).accent.argb();
  CHECK(s.parse(s.source()));  // source() aliases the sheet's own buffer
  const std::uint32_t fresh = s.resolve(b, StyleState::None).accent.argb();
  CHECK_EQ(fresh, 0xFF112233u);
  CHECK_NE(stale, fresh);

  Theme::current() = saved;
}

GEEYOOU_TEST(style, numeric_values_and_px_suffix) {
  StyleSheet s;
  CHECK(s.parse("* { border-width: 2.5; border-radius: 8px; padding: 0; }"));
  PushButton b;
  const StyleProps p = s.resolve(b, StyleState::None);
  CHECK_NEAR(p.borderWidth, 2.5f, 0.0001f);
  CHECK_NEAR(p.radius, 8.0f, 0.0001f);
  // A zero is a VALUE, not an absence: `{ border-radius: 0 }` has to be able to
  // square off a corner the theme rounded.
  CHECK(p.has(StyleProps::kPadding));
  CHECK_NEAR(p.padding, 0.0f, 0.0001f);
}

// ------------------------------------------------------------------- errors ---
GEEYOOU_TEST(style, a_bad_rule_never_takes_the_good_ones_down) {
  StyleSheet s;
  CHECK(s.parse("PushButton { color: #111111; }"));
  CHECK(s.errors().empty());
  CHECK_EQ(s.ruleCount(), std::size_t(1));

  // Errors are reported through the return value and errors(), never thrown: a
  // style sheet is content, and bad content must not take the display down.
  const bool ok = s.parse(
      "PushButton { color: #111111; }"
      "Label      { color: notacolour; }"
      "Label      { bogus-property: 4; }"
      "Separator  { color; }");
  CHECK(!ok);
  CHECK_GE(s.errors().size(), std::size_t(3));

  // The good rule parsed before the bad ones is still installed.
  PushButton b;
  CHECK_EQ(colorOf(s.resolve(b, StyleState::None)), 0xFF111111u);
  // ...and the rules whose every declaration failed contribute nothing rather
  // than contributing a default.
  Label label;
  CHECK_EQ(colorOf(s.resolve(label, StyleState::None)), kNone);
}

GEEYOOU_TEST(style, an_unterminated_rule_keeps_everything_before_it) {
  StyleSheet s;
  const bool ok = s.parse(
      "PushButton { color: #111111; }"
      "Label      { color: #222222; ");  // no closing brace
  CHECK(!ok);
  CHECK(!s.errors().empty());
  CHECK_EQ(s.ruleCount(), std::size_t(1));

  PushButton b;
  CHECK_EQ(colorOf(s.resolve(b, StyleState::None)), 0xFF111111u);

  // Comments are stripped everywhere, including mid-selector.
  StyleSheet s2;
  CHECK(s2.parse("/* header */ Push/**/Button { color: #123456; } /* tail"));
  CHECK_EQ(colorOf(s2.resolve(b, StyleState::None)), kNone);  // "Push Button" != PushButton
}

GEEYOOU_TEST(style, an_unknown_pseudo_class_is_reported_but_not_fatal) {
  StyleSheet s;
  const bool ok = s.parse("Label:bogus { color: #444444; }");
  CHECK(!ok);
  CHECK(!s.errors().empty());

  // CURRENT CONTRACT: the unknown state contributes StyleState::None, so the
  // rule degrades to a plain `Label` selector and applies in EVERY state.  That
  // is the more dangerous of the two possible failure modes -- the safe one
  // would be to drop the rule.  Asserted so the behaviour is a decision rather
  // than an accident; see the accompanying report.
  Label label;
  CHECK_EQ(colorOf(s.resolve(label, StyleState::None)), 0xFF444444u);
}

GEEYOOU_TEST(style, empty_sheet_and_append) {
  StyleSheet s;
  CHECK(s.empty());
  PushButton b;
  CHECK(s.resolve(b, StyleState::None).empty());

  CHECK(s.parse("PushButton { color: #111111; }"));
  CHECK(!s.empty());
  CHECK(s.append("PushButton { border-radius: 9; }"));
  CHECK_EQ(s.ruleCount(), std::size_t(2));

  const StyleProps p = s.resolve(b, StyleState::None);
  CHECK_EQ(colorOf(p), 0xFF111111u);  // append kept the earlier rule
  CHECK_NEAR(p.radius, 9.0f, 0.0001f);

  s.clear();
  CHECK(s.empty());
  CHECK(s.resolve(b, StyleState::None).empty());
}

GEEYOOU_TEST(style, state_names_map_to_bits) {
  CHECK(geeyoou::styleStateFromName("hover") == StyleState::Hover);
  CHECK(geeyoou::styleStateFromName("active") == StyleState::Pressed);
  CHECK(geeyoou::styleStateFromName("focused") == StyleState::Focus);
  CHECK(geeyoou::styleStateFromName("read-only") == StyleState::ReadOnly);
  CHECK(geeyoou::styleStateFromName("nonsense") == StyleState::None);

  CHECK(geeyoou::contains(StyleState::Hover | StyleState::Focus, StyleState::Hover));
  CHECK(!geeyoou::contains(StyleState::Hover, StyleState::Hover | StyleState::Focus));
  CHECK(geeyoou::contains(StyleState::None, StyleState::None));
}
