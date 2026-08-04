#pragma once
//
// A QSS-like style sheet: selectors, a cascade, and a property bag.
//
// docs/architecture.md section 4 originally listed this as an explicit
// non-goal ("token struct, no selector parsing").  The reasoning was that an
// HMI screen is commissioned once and then runs for years, so the flexibility
// of a cascade buys nothing.  That holds for the SCREEN.  It stopped holding
// for the PRODUCT: the same binary now ships to plants that each want their own
// house colours, and patching Theme fields in C++ per customer means a rebuild
// per customer.  A style sheet is data, and data can be shipped in a file.
//
// What this is NOT: a CSS engine.  There is no layout, no inheritance of
// arbitrary properties, no animation, no units, no media queries.  It resolves
// a fixed set of PAINT properties for a widget in a given state, and that is
// all.  Everything a stylesheet cannot express is still a Theme token.
//
// Syntax (a strict subset of Qt's QSS):
//
//     /* comments are C-style */
//     * { font-size: 13; }                     -- universal
//     PushButton { border-radius: 8; }         -- by type (matches subclasses)
//     PushButton:hover { background: @accent; } -- by state
//     .danger { accent: #FF4D5E; }             -- by style class
//     #emergencyStop { border-width: 2; }      -- by object name
//     GroupBox PushButton { font-size: 12; }   -- descendant
//     Label, Separator { color: @textDim; }    -- selector groups
//
// `@name` resolves against the ACTIVE THEME, which is what lets one style sheet
// follow a skin change instead of hard-coding hex that only suits one palette.
//
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "geeyoou/core/Types.hpp"
#include "geeyoou/render/Theme.hpp"

namespace geeyoou {

// Interaction states a selector can test with `:pseudo`.
//
// A rule matches when its state mask is a SUBSET of the widget's current state,
// so `PushButton:hover` still applies to a button that is both hovered and
// focused, while `PushButton:hover:focus` needs both.
enum class StyleState : std::uint16_t {
  None     = 0,
  Hover    = 1u << 0,
  Pressed  = 1u << 1,
  Checked  = 1u << 2,
  Disabled = 1u << 3,
  Focus    = 1u << 4,
  ReadOnly = 1u << 5,
  Invalid  = 1u << 6,
  Open     = 1u << 7,  // a drop-down is showing its popup
  Selected = 1u << 8,
};

constexpr StyleState operator|(StyleState a, StyleState b) {
  return StyleState(std::uint16_t(a) | std::uint16_t(b));
}
constexpr StyleState operator&(StyleState a, StyleState b) {
  return StyleState(std::uint16_t(a) & std::uint16_t(b));
}
constexpr StyleState& operator|=(StyleState& a, StyleState b) { return a = a | b; }
constexpr bool any(StyleState s) { return std::uint16_t(s) != 0; }
// True when every bit of `needle` is present in `haystack`.
constexpr bool contains(StyleState haystack, StyleState needle) {
  return (std::uint16_t(haystack) & std::uint16_t(needle)) == std::uint16_t(needle);
}
StyleState styleStateFromName(std::string_view name);  // None when unknown

// ---------------------------------------------------------------------------
// What the cascade needs to know about a node.
//
// Declared here, in render/, and implemented by Widget in widget/ -- so the
// dependency still points DOWN.  render/ must never learn what a Widget is;
// docs/architecture.md section 2.
class StyleSubject {
 public:
  virtual ~StyleSubject() = default;

  // True for the node's own type and every base type it derives from, so a
  // `PushButton` rule also styles IconButton and MenuButton -- the same
  // behaviour QSS has, and the reason it is a method rather than a string.
  virtual bool styleMatchesType(std::string_view type) const = 0;
  virtual bool styleMatchesClass(std::string_view cls) const = 0;
  virtual const std::string& styleObjectName() const = 0;
  virtual const StyleSubject* styleParentSubject() const = 0;
  // Used when this node appears as an ANCESTOR in a descendant selector.  For
  // the node being resolved, the caller passes the state explicitly, because
  // only the widget itself knows it is currently hovered or pressed.
  virtual StyleState styleState() const = 0;
};

// ---------------------------------------------------------------------------
// Resolved paint properties.  Every field is optional: unset means "the widget
// keeps whatever it would have drawn", which is what makes a partial rule like
// `{ border-radius: 0 }` legal and non-destructive.
struct StyleProps {
  enum Field : std::uint32_t {
    kColor       = 1u << 0,
    kBackground  = 1u << 1,
    kBorderColor = 1u << 2,
    kBorderWidth = 1u << 3,
    kRadius      = 1u << 4,
    kFontSize    = 1u << 5,
    kAccent      = 1u << 6,
    kIconColor   = 1u << 7,
    kPadding     = 1u << 8,
  };

  std::uint32_t set = 0;
  Color color;
  Color background;
  Color borderColor;
  Color accent;
  Color iconColor;
  float borderWidth = 0.0f;
  float radius = 0.0f;
  float fontSize = 0.0f;
  float padding = 0.0f;

  bool has(Field f) const { return (set & std::uint32_t(f)) != 0; }
  bool empty() const { return set == 0; }

  // The call shape every widget uses: "the sheet's value, or what I was going
  // to draw anyway".  Keeping it an expression means no `if` at the call site.
  Color colorOr(Color fallback) const { return has(kColor) ? color : fallback; }
  Color backgroundOr(Color f) const { return has(kBackground) ? background : f; }
  Color borderColorOr(Color f) const { return has(kBorderColor) ? borderColor : f; }
  Color accentOr(Color f) const { return has(kAccent) ? accent : f; }
  Color iconColorOr(Color f) const { return has(kIconColor) ? iconColor : f; }
  float borderWidthOr(float f) const { return has(kBorderWidth) ? borderWidth : f; }
  float radiusOr(float f) const { return has(kRadius) ? radius : f; }
  float fontSizeOr(float f) const { return has(kFontSize) ? fontSize : f; }
  float paddingOr(float f) const { return has(kPadding) ? padding : f; }
};

// ---------------------------------------------------------------------------
class StyleSheet {
 public:
  StyleSheet() = default;

  // Replaces the sheet's contents.  Returns false when the text had errors;
  // whatever parsed cleanly is still installed, because a typo in rule 9 should
  // not blank the eight rules above it on a running plant display.
  bool parse(std::string_view qss);
  // Appends without discarding what is already there.
  bool append(std::string_view qss);

  void clear();
  bool empty() const { return rules_.empty(); }
  std::size_t ruleCount() const { return rules_.size(); }

  // Diagnostics from the last parse, one line per problem.  Never thrown:
  // a style sheet is content, and bad content must not take the UI down.
  const std::vector<std::string>& errors() const { return errors_; }

  // The whole point.  `state` is the subject's own interaction state.
  StyleProps resolve(const StyleSubject& subject, StyleState state) const;

  const std::string& source() const { return source_; }

 private:
  struct SimpleSelector {
    std::string type;  // empty or "*" matches anything
    std::vector<std::string> classes;
    std::string id;
    StyleState states = StyleState::None;
  };
  struct Selector {
    // Descendant chain; the LAST element is the subject being matched.
    std::vector<SimpleSelector> parts;
    int specificity = 0;
  };
  struct Rule {
    std::vector<Selector> selectors;
    StyleProps props;
    int order = 0;
  };

  std::vector<Rule> rules_;
  std::vector<std::string> errors_;
  std::string source_;
};

// The sheet every widget consults.  Owned by the skin registry; assigning to it
// directly is legal but bumps no generation counter, so prefer skins().
StyleSheet& activeStyleSheet();

// Bumped whenever the active theme or style sheet changes.  Widgets cache their
// resolved properties against it instead of re-running the cascade per paint.
std::uint64_t styleGeneration();
void bumpStyleGeneration();

}  // namespace geeyoou
