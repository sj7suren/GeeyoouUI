#include "geeyoou/render/StyleSheet.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace geeyoou {
namespace {

bool isIdentChar(char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_';
}

std::string trimmed(std::string_view s) {
  std::size_t a = 0, b = s.size();
  while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
  while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
  return std::string(s.substr(a, b - a));
}

// Strips /* ... */ before anything else looks at the text.  Doing it as a
// pre-pass rather than inside the tokeniser keeps every later position simple,
// and a comment can then appear literally anywhere.
std::string stripComments(std::string_view in) {
  std::string out;
  out.reserve(in.size());
  for (std::size_t i = 0; i < in.size();) {
    if (i + 1 < in.size() && in[i] == '/' && in[i + 1] == '*') {
      i += 2;
      while (i + 1 < in.size() && !(in[i] == '*' && in[i + 1] == '/')) ++i;
      i = std::min(in.size(), i + 2);
      out.push_back(' ');  // a comment still separates tokens
      continue;
    }
    out.push_back(in[i++]);
  }
  return out;
}

int hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

bool parseHexColor(std::string_view s, Color& out) {
  std::vector<int> d;
  d.reserve(s.size());
  for (char c : s) {
    const int v = hexDigit(c);
    if (v < 0) return false;
    d.push_back(v);
  }
  auto b8 = [&](int i) { return std::uint8_t(d[std::size_t(i)] * 16 + d[std::size_t(i + 1)]); };
  switch (d.size()) {
    case 3:  // #RGB, each digit doubled -- the CSS shorthand
      return out = Color::rgb(std::uint8_t(d[0] * 17), std::uint8_t(d[1] * 17),
                              std::uint8_t(d[2] * 17)),
             true;
    case 6:
      return out = Color::rgb(b8(0), b8(2), b8(4)), true;
    case 8:  // #AARRGGBB, matching Color's own packing order
      return out = Color::rgba(b8(2), b8(4), b8(6), b8(0)), true;
    default:
      return false;
  }
}

// `@accent`, `@panelBorder`, ... resolved against the LIVE theme, so a sheet
// written once follows every skin change.  This is the single most useful thing
// the syntax has over plain hex.
bool themeToken(std::string_view name, Color& out) {
  const Theme& t = Theme::current();
  struct Entry { const char* n; Color Theme::*m; };
  static const Entry kMap[] = {
      {"background", &Theme::background},   {"panel", &Theme::panel},
      {"panelBorder", &Theme::panelBorder}, {"text", &Theme::text},
      {"textDim", &Theme::textDim},         {"textDisabled", &Theme::textDisabled},
      {"accent", &Theme::accent},           {"ok", &Theme::ok},
      {"warn", &Theme::warn},               {"alarm", &Theme::alarm},
      {"grid", &Theme::grid},               {"track", &Theme::track},
      {"focusRing", &Theme::focusRing},     {"field", &Theme::field},
      {"placeholder", &Theme::placeholder}, {"selection", &Theme::selection},
      {"scrollbar", &Theme::scrollbar},     {"primary", &Theme::primary},
      {"success", &Theme::success},         {"danger", &Theme::danger},
      {"onFilled", &Theme::onFilled},
  };
  for (const Entry& e : kMap) {
    if (name == e.n) {
      out = t.*(e.m);
      return true;
    }
  }
  return false;
}

bool parseColorValue(std::string_view raw, Color& out) {
  const std::string v = trimmed(raw);
  if (v.empty()) return false;
  if (v == "transparent" || v == "none") {
    out = Color::rgba(0, 0, 0, 0);
    return true;
  }
  if (v[0] == '@') return themeToken(std::string_view(v).substr(1), out);
  if (v[0] == '#') return parseHexColor(std::string_view(v).substr(1), out);

  const bool rgba = v.rfind("rgba(", 0) == 0;
  const bool rgb = v.rfind("rgb(", 0) == 0;
  if (!rgb && !rgba) return false;
  const std::size_t open = v.find('(');
  const std::size_t close = v.rfind(')');
  if (close == std::string::npos || close < open) return false;

  int comp[4] = {0, 0, 0, 255};
  int n = 0;
  std::size_t i = open + 1;
  while (i <= close && n < 4) {
    std::size_t j = i;
    while (j < close && v[j] != ',') ++j;
    const std::string part = trimmed(std::string_view(v).substr(i, j - i));
    if (part.empty()) return false;
    comp[n++] = std::atoi(part.c_str());
    i = j + 1;
  }
  if (n < 3) return false;
  auto cl = [](int x) { return std::uint8_t(std::clamp(x, 0, 255)); };
  out = Color::rgba(cl(comp[0]), cl(comp[1]), cl(comp[2]), cl(comp[3]));
  (void)rgba;
  return true;
}

bool parseNumberValue(std::string_view raw, float& out) {
  const std::string v = trimmed(raw);
  if (v.empty()) return false;
  char* end = nullptr;
  const double d = std::strtod(v.c_str(), &end);
  if (end == v.c_str()) return false;
  // Tolerate a "px" suffix so pasted CSS does not silently drop the rule.
  out = float(d);
  return true;
}

}  // namespace

StyleState styleStateFromName(std::string_view n) {
  if (n == "hover") return StyleState::Hover;
  if (n == "pressed" || n == "active") return StyleState::Pressed;
  if (n == "checked") return StyleState::Checked;
  if (n == "disabled") return StyleState::Disabled;
  if (n == "focus" || n == "focused") return StyleState::Focus;
  if (n == "read-only" || n == "readonly") return StyleState::ReadOnly;
  if (n == "invalid") return StyleState::Invalid;
  if (n == "open") return StyleState::Open;
  if (n == "selected") return StyleState::Selected;
  return StyleState::None;
}

// ----------------------------------------------------------------- parsing ---
void StyleSheet::clear() {
  rules_.clear();
  errors_.clear();
  source_.clear();
}

bool StyleSheet::parse(std::string_view qss) {
  // Copied up front because `qss` is very often a view of source_ itself --
  // re-parsing after a theme change is exactly that call -- and clearing
  // source_ below would leave the view dangling.
  const std::string incoming(qss);
  rules_.clear();
  errors_.clear();
  source_.clear();
  return append(incoming);
}

bool StyleSheet::append(std::string_view qss) {
  const std::string incoming(qss);  // same aliasing hazard as parse()
  if (!source_.empty()) source_ += "\n";
  source_ += incoming;

  const std::string text = stripComments(incoming);
  std::size_t i = 0;
  int order = int(rules_.size());
  const std::size_t errorsBefore = errors_.size();

  auto lineOf = [&text](std::size_t pos) {
    int line = 1;
    for (std::size_t k = 0; k < pos && k < text.size(); ++k) {
      if (text[k] == '\n') ++line;
    }
    return line;
  };

  while (i < text.size()) {
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) ++i;
    if (i >= text.size()) break;

    const std::size_t selStart = i;
    const std::size_t brace = text.find('{', i);
    if (brace == std::string::npos) {
      errors_.push_back("第 " + std::to_string(lineOf(selStart)) +
                        " 行：选择器后缺少 '{'");
      break;
    }
    const std::size_t close = text.find('}', brace);
    if (close == std::string::npos) {
      errors_.push_back("第 " + std::to_string(lineOf(brace)) + " 行：缺少 '}'");
      break;
    }

    Rule rule;
    rule.order = order++;

    // --- selector list ---
    const std::string selText = text.substr(selStart, brace - selStart);
    std::size_t p = 0;
    while (p <= selText.size()) {
      const std::size_t comma = selText.find(',', p);
      const std::string one =
          trimmed(std::string_view(selText).substr(
              p, (comma == std::string::npos ? selText.size() : comma) - p));
      p = (comma == std::string::npos) ? selText.size() + 1 : comma + 1;
      if (one.empty()) continue;

      Selector sel;
      std::size_t q = 0;
      while (q < one.size()) {
        while (q < one.size() && std::isspace(static_cast<unsigned char>(one[q]))) ++q;
        if (q >= one.size()) break;

        SimpleSelector ss;
        // A leading type name (or '*'), then any number of .class / #id / :state
        if (one[q] == '*') {
          ss.type = "*";
          ++q;
        } else if (isIdentChar(one[q])) {
          const std::size_t s = q;
          while (q < one.size() && isIdentChar(one[q])) ++q;
          ss.type = one.substr(s, q - s);
        }
        while (q < one.size() && (one[q] == '.' || one[q] == '#' || one[q] == ':')) {
          const char kind = one[q++];
          const std::size_t s = q;
          while (q < one.size() && isIdentChar(one[q])) ++q;
          const std::string name = one.substr(s, q - s);
          if (name.empty()) {
            errors_.push_back("第 " + std::to_string(lineOf(selStart)) +
                              " 行：'" + std::string(1, kind) + "' 后缺少名称");
            break;
          }
          if (kind == '.') {
            ss.classes.push_back(name);
          } else if (kind == '#') {
            ss.id = name;
          } else {
            const StyleState st = styleStateFromName(name);
            if (st == StyleState::None) {
              errors_.push_back("第 " + std::to_string(lineOf(selStart)) +
                                " 行：未知状态 ':" + name + "'");
            }
            ss.states |= st;
          }
        }
        if (ss.type.empty() && ss.classes.empty() && ss.id.empty() &&
            !any(ss.states)) {
          break;
        }
        sel.parts.push_back(std::move(ss));
      }
      if (sel.parts.empty()) continue;

      // CSS specificity, packed so a straight integer compare orders it:
      // ids dominate classes+states, which dominate type names.
      int ids = 0, cls = 0, types = 0;
      for (const SimpleSelector& ss : sel.parts) {
        if (!ss.id.empty()) ++ids;
        cls += int(ss.classes.size());
        for (int b = 0; b < 16; ++b) {
          if (contains(ss.states, StyleState(std::uint16_t(1u << b)))) ++cls;
        }
        if (!ss.type.empty() && ss.type != "*") ++types;
      }
      sel.specificity = ids * 10000 + cls * 100 + types;
      rule.selectors.push_back(std::move(sel));
    }

    // --- declarations ---
    const std::string body = text.substr(brace + 1, close - brace - 1);
    std::size_t d = 0;
    while (d < body.size()) {
      const std::size_t semi = body.find(';', d);
      const std::size_t end = (semi == std::string::npos) ? body.size() : semi;
      const std::string decl = trimmed(std::string_view(body).substr(d, end - d));
      d = end + 1;
      if (decl.empty()) continue;

      const std::size_t colon = decl.find(':');
      if (colon == std::string::npos) {
        errors_.push_back("第 " + std::to_string(lineOf(brace)) + " 行：'" + decl +
                          "' 缺少 ':'");
        continue;
      }
      const std::string key = trimmed(std::string_view(decl).substr(0, colon));
      const std::string val = trimmed(std::string_view(decl).substr(colon + 1));

      Color c;
      float f = 0.0f;
      auto badColor = [&] {
        errors_.push_back("第 " + std::to_string(lineOf(brace)) + " 行：'" + key +
                          "' 的颜色值无法解析：" + val);
      };
      auto badNumber = [&] {
        errors_.push_back("第 " + std::to_string(lineOf(brace)) + " 行：'" + key +
                          "' 需要数字，得到：" + val);
      };

      if (key == "color") {
        if (parseColorValue(val, c)) { rule.props.color = c; rule.props.set |= StyleProps::kColor; }
        else badColor();
      } else if (key == "background" || key == "background-color") {
        if (parseColorValue(val, c)) { rule.props.background = c; rule.props.set |= StyleProps::kBackground; }
        else badColor();
      } else if (key == "border-color") {
        if (parseColorValue(val, c)) { rule.props.borderColor = c; rule.props.set |= StyleProps::kBorderColor; }
        else badColor();
      } else if (key == "accent") {
        if (parseColorValue(val, c)) { rule.props.accent = c; rule.props.set |= StyleProps::kAccent; }
        else badColor();
      } else if (key == "icon-color") {
        if (parseColorValue(val, c)) { rule.props.iconColor = c; rule.props.set |= StyleProps::kIconColor; }
        else badColor();
      } else if (key == "border-width") {
        if (parseNumberValue(val, f)) { rule.props.borderWidth = f; rule.props.set |= StyleProps::kBorderWidth; }
        else badNumber();
      } else if (key == "border-radius") {
        if (parseNumberValue(val, f)) { rule.props.radius = f; rule.props.set |= StyleProps::kRadius; }
        else badNumber();
      } else if (key == "font-size") {
        if (parseNumberValue(val, f)) { rule.props.fontSize = f; rule.props.set |= StyleProps::kFontSize; }
        else badNumber();
      } else if (key == "padding") {
        if (parseNumberValue(val, f)) { rule.props.padding = f; rule.props.set |= StyleProps::kPadding; }
        else badNumber();
      } else {
        errors_.push_back("第 " + std::to_string(lineOf(brace)) + " 行：未知属性 '" +
                          key + "'");
      }
    }

    if (!rule.selectors.empty() && !rule.props.empty()) rules_.push_back(std::move(rule));
    i = close + 1;
  }

  bumpStyleGeneration();
  return errors_.size() == errorsBefore;
}

// ---------------------------------------------------------------- matching ---
StyleProps StyleSheet::resolve(const StyleSubject& subject, StyleState state) const {
  StyleProps out;
  if (rules_.empty()) return out;

  auto matchSimpleOn = [](const StyleSubject& n, const SimpleSelector& ss,
                          StyleState st) {
    if (!ss.type.empty() && ss.type != "*" && !n.styleMatchesType(ss.type)) return false;
    if (!ss.id.empty() && n.styleObjectName() != ss.id) return false;
    for (const std::string& c : ss.classes) {
      if (!n.styleMatchesClass(c)) return false;
    }
    // Subset test, not equality: `:hover` still matches a hovered AND focused
    // widget, which is what makes single-state rules usable.
    return contains(st, ss.states);
  };

  // Walks the descendant chain right-to-left.  `idx` is the part that must
  // match `node`; everything before it must be found among node's ancestors,
  // in order but not necessarily adjacent -- ordinary CSS descendant semantics.
  auto matchChain = [&](auto&& self, const StyleSubject& node,
                        const Selector& sel, int idx, StyleState st) -> bool {
    if (!matchSimpleOn(node, sel.parts[std::size_t(idx)], st)) return false;
    if (idx == 0) return true;
    for (const StyleSubject* a = node.styleParentSubject(); a;
         a = a->styleParentSubject()) {
      if (self(self, *a, sel, idx - 1, a->styleState())) return true;
    }
    return false;
  };

  // Winner-per-property rather than winner-per-rule: two rules can each
  // contribute, exactly as a cascade should.
  int bestSpec[32] = {};
  int bestOrder[32] = {};
  for (int b = 0; b < 32; ++b) {
    bestSpec[b] = -1;
    bestOrder[b] = -1;
  }

  for (const Rule& rule : rules_) {
    int spec = -1;
    for (const Selector& sel : rule.selectors) {
      if (sel.parts.empty()) continue;
      if (matchChain(matchChain, subject, sel, int(sel.parts.size()) - 1, state)) {
        spec = std::max(spec, sel.specificity);
      }
    }
    if (spec < 0) continue;

    for (int b = 0; b < 32; ++b) {
      const std::uint32_t bit = 1u << b;
      if ((rule.props.set & bit) == 0) continue;
      if (spec < bestSpec[b]) continue;
      if (spec == bestSpec[b] && rule.order < bestOrder[b]) continue;
      bestSpec[b] = spec;
      bestOrder[b] = rule.order;

      out.set |= bit;
      switch (StyleProps::Field(bit)) {
        case StyleProps::kColor:       out.color = rule.props.color; break;
        case StyleProps::kBackground:  out.background = rule.props.background; break;
        case StyleProps::kBorderColor: out.borderColor = rule.props.borderColor; break;
        case StyleProps::kAccent:      out.accent = rule.props.accent; break;
        case StyleProps::kIconColor:   out.iconColor = rule.props.iconColor; break;
        case StyleProps::kBorderWidth: out.borderWidth = rule.props.borderWidth; break;
        case StyleProps::kRadius:      out.radius = rule.props.radius; break;
        case StyleProps::kFontSize:    out.fontSize = rule.props.fontSize; break;
        case StyleProps::kPadding:     out.padding = rule.props.padding; break;
        default: break;  // reserved bits; `set` never carries them
      }
    }
  }
  return out;
}

// ------------------------------------------------------------------ global ---
StyleSheet& activeStyleSheet() {
  static StyleSheet s;
  return s;
}

namespace {
std::uint64_t& generation() {
  static std::uint64_t g = 1;
  return g;
}
}  // namespace

std::uint64_t styleGeneration() { return generation(); }
void bumpStyleGeneration() { ++generation(); }

}  // namespace geeyoou
