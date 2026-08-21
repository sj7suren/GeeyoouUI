#include "geeyoou/widget/NumericKeypad.hpp"

#include <cstdlib>

#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/BoxLayout.hpp"
#include "geeyoou/widget/Dialog.hpp"
#include "geeyoou/widget/Window.hpp"

namespace geeyoou {
namespace {
constexpr float kPad = 8.0f;
constexpr float kReadoutH = 44.0f;
constexpr float kGap = 6.0f;
constexpr int kCols = 4;
constexpr int kRows = 4;
constexpr std::size_t kMaxLen = 12;

// The key grid.  Column 3 is the edit column (backspace / clear); the rest is
// the number pad with the sign and point on the bottom row.
struct Cell {
  const char* label;
  int row, col;
};
// The backspace and enter keys use Chinese words rather than the ⌫ / ⏎ glyphs:
// the bundled font has neither codepoint, so those symbols render as tofu.  ±
// (U+00B1) and the digits are fine.
const Cell kCells[] = {
    {"7", 0, 0}, {"8", 0, 1}, {"9", 0, 2}, {"退格", 0, 3},
    {"4", 1, 0}, {"5", 1, 1}, {"6", 1, 2}, {"C", 1, 3},
    {"1", 2, 0}, {"2", 2, 1}, {"3", 2, 2}, {"\xC2\xB1", 2, 3},  // ±
    {".", 3, 0}, {"0", 3, 1}, {"确定", 3, 2},                    // Enter
};
}  // namespace

NumericKeypad::NumericKeypad() { buildKeys(); }

void NumericKeypad::buildKeys() {
  for (const Cell& c : kCells) {
    auto* b = add<PushButton>();
    b->setText(c.label);
    const std::string label = c.label;
    b->clicked.connect([this, label] { press(label); });
    keys_.push_back({b, label, c.row, c.col});
  }
  layoutKeys();
}

void NumericKeypad::layoutKeys() {
  const Rect r = localRect();
  const float top = r.y() + kReadoutH + kPad;
  const float gridW = r.width() - 2.0f * kPad;
  const float gridH = r.bottom() - top - kPad;
  if (gridW <= 0.0f || gridH <= 0.0f) return;
  const float cw = (gridW - (kCols - 1) * kGap) / kCols;
  const float ch = (gridH - (kRows - 1) * kGap) / kRows;

  for (Key& k : keys_) {
    // The Enter key spans the two remaining cells on the bottom row.
    float w = cw;
    if (k.label == "确定") w = cw * 2.0f + kGap;
    const float x = r.x() + kPad + k.col * (cw + kGap);
    const float y = top + k.row * (ch + kGap);
    k.widget->setGeometry({x, y, w, ch});
  }
}

void NumericKeypad::onGeometryChanged() { layoutKeys(); }

void NumericKeypad::press(const std::string& key) {
  if (key == "C") {
    entry_.clear();
  } else if (key == "退格") {  // backspace
    if (!entry_.empty()) entry_.pop_back();
  } else if (key == "\xC2\xB1") {  // sign
    if (!allowSign_) return;
    if (!entry_.empty() && entry_[0] == '-') {
      entry_.erase(entry_.begin());
    } else {
      entry_.insert(entry_.begin(), '-');
    }
  } else if (key == ".") {
    if (!allowDecimal_) return;
    if (entry_.find('.') == std::string::npos && entry_.size() < kMaxLen) {
      entry_ += (entry_.empty() ? "0." : ".");
    }
  } else if (key == "确定") {  // Enter
    update();
    committed.emit(value());  // tail
    return;
  } else {  // a digit
    if (entry_.size() < kMaxLen) entry_ += key;
  }
  changed();
}

void NumericKeypad::changed() {
  update();
  valueChanged.emit(value());  // tail
}

void NumericKeypad::setValue(double v) {
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%g", v);
  entry_ = buf;
  changed();
}

double NumericKeypad::value() const {
  if (entry_.empty() || entry_ == "-" || entry_ == "-.") return 0.0;
  return std::strtod(entry_.c_str(), nullptr);
}

void NumericKeypad::setText(std::string utf8) {
  entry_ = std::move(utf8);
  changed();
}

void NumericKeypad::clear() {
  entry_.clear();
  changed();
}

void NumericKeypad::setAllowSign(bool on) {
  allowSign_ = on;
  update();
}

void NumericKeypad::setAllowDecimal(bool on) {
  allowDecimal_ = on;
  update();
}

void NumericKeypad::setPrompt(std::string utf8) {
  prompt_ = std::move(utf8);
  update();
}

void NumericKeypad::setUnit(std::string utf8) {
  unit_ = std::move(utf8);
  update();
}

void NumericKeypad::onPaint(Painter& p, const Rect&) {
  const Theme& t = Theme::current();
  const Rect r = localRect();

  // Readout field.
  const Rect ro{r.x() + kPad, r.y() + kPad, r.width() - 2.0f * kPad, kReadoutH};
  p.fillRoundRect(ro, t.radius, t.field);
  p.strokeRoundRect(ro, t.radius, t.panelBorder, 1.0f);

  if (!prompt_.empty()) {
    p.drawText({ro.x() + 10.0f, ro.y() + 4.0f}, prompt_, t.fontSmall, t.textDim,
               HAlign::Left, VAlign::Top);
  }
  const std::string shown =
      entry_.empty() ? std::string("0") : entry_;
  const std::string line = unit_.empty() ? shown : (shown + " " + unit_);
  p.drawText({ro.right() - 10.0f, ro.center().y}, line, 20.0f, t.text,
             HAlign::Right, VAlign::Middle);
}

// =========================================================== convenience ===

void numericInput(Window* w, std::string prompt, double initial,
                  std::function<void(double)> onAccept, std::string unit) {
  if (!w) return;
  Dialog* dlg = w->add<Dialog>();
  dlg->setTitle(prompt);
  dlg->setPanelSize({320.0f, 420.0f});

  auto* pad = dlg->body()->add<NumericKeypad>();
  pad->setUnit(std::move(unit));
  pad->setValue(initial);
  // The keypad fills the dialog body.
  dlg->body()->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical)
      ->addWidget(pad, 1);

  // Enter on the keypad is the same as pressing OK.
  pad->committed.connect([dlg](double) { dlg->close(1); });

  dlg->addButton("取消", 0, ButtonVariant::Default, false);
  dlg->addButton("确定", 1, ButtonVariant::Primary, true);

  dlg->onResult = [w, dlg, pad, onAccept](int r) {
    if (r == 1 && onAccept) onAccept(pad->value());
    w->removeChild(dlg);  // deferred by Dialog's own close timer; safe here
  };
  dlg->show(w);
}

}  // namespace geeyoou
