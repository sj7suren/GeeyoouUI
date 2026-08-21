#include "geeyoou/widget/Dialog.hpp"

#include <algorithm>
#include <memory>

#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"
#include "geeyoou/widget/BoxLayout.hpp"
#include "geeyoou/widget/Label.hpp"
#include "geeyoou/widget/Window.hpp"

namespace geeyoou {
namespace {
constexpr float kTitleH = 48.0f;
constexpr float kRowH = 60.0f;    // button row height
constexpr float kPad = 20.0f;
constexpr float kBtnH = 36.0f;
constexpr float kBtnGap = 10.0f;
constexpr float kBtnMinW = 88.0f;

// Run `fn` once, on the next event-loop turn.  startTimer is PERIODIC, so the
// callback stops itself before running -- a plain startTimer(0, fn) would fire
// fn forever.  The shared id lets the callback know which timer to stop.
void deferOnce(std::function<void()> fn) {
  auto id = std::make_shared<TimerId>(0);
  *id = platform().startTimer(0, [id, fn = std::move(fn)] {
    platform().stopTimer(*id);
    fn();
  });
}
}  // namespace

Dialog::Dialog() {
  // Tab focus so Esc/Enter reach onKey even before anything inside is focused.
  setFocusPolicy(FocusPolicy::Tab);
  body_ = add<Widget>();
}

Dialog::~Dialog() {
  // The deferred close timer captures `this`; it must not outlive the dialog.
  if (closeTimer_) platform().stopTimer(closeTimer_);
}

void Dialog::setTitle(std::string utf8) {
  title_ = std::move(utf8);
  update();
}

void Dialog::addButton(std::string label, int resultId, ButtonVariant variant,
                       bool isDefault) {
  auto* b = add<PushButton>();
  b->setText(std::move(label));
  b->setVariant(variant);
  b->clicked.connect([this, resultId] { close(resultId); });
  buttons_.push_back({b, resultId, isDefault});
  layoutChildren();
  update();
}

Rect Dialog::panelRect() const {
  const Rect r = localRect();
  const float w = std::min(panel_.width, r.width() - 2.0f * kPad);
  const float h = std::min(panel_.height, r.height() - 2.0f * kPad);
  return {(r.width() - w) * 0.5f, (r.height() - h) * 0.5f, w, h};
}

Rect Dialog::titleRect() const {
  const Rect p = panelRect();
  return {p.x(), p.y(), p.width(), kTitleH};
}

Rect Dialog::buttonRowRect() const {
  const Rect p = panelRect();
  return {p.x(), p.bottom() - kRowH, p.width(), kRowH};
}

void Dialog::layoutChildren() {
  const Rect p = panelRect();
  if (p.width() <= 0.0f) return;

  // Body: everything between the title and the button row.
  const float bodyTop = p.y() + kTitleH;
  const float bodyBottom = p.bottom() - (buttons_.empty() ? kPad : kRowH);
  body_->setGeometry({p.x() + kPad, bodyTop + kPad * 0.5f,
                      std::max(0.0f, p.width() - 2.0f * kPad),
                      std::max(0.0f, bodyBottom - bodyTop - kPad * 0.5f)});

  // Buttons: right-aligned in the button row, laid out right to left so the
  // primary (last added) sits nearest the corner.
  const Rect row = buttonRowRect();
  float x = row.right() - kPad;
  const float y = row.y() + (row.height() - kBtnH) * 0.5f;
  for (auto it = buttons_.rbegin(); it != buttons_.rend(); ++it) {
    const Size ts = measureText(it->widget->text(), Theme::current().fontBody);
    const float w = std::max(kBtnMinW, ts.width + 32.0f);
    x -= w;
    it->widget->setGeometry({x, y, w, kBtnH});
    x -= kBtnGap;
  }
}

void Dialog::onGeometryChanged() { layoutChildren(); }

void Dialog::show(Window* w) {
  if (!w) w = window();
  if (!w) return;
  const Rect wr = w->localRect();
  setGeometry(wr);            // full-window: modality by geometry (see header)
  w->openPopup(this, wr);
  layoutChildren();
  setFocus();
}

void Dialog::close(int resultId) {
  if (closing_) return;       // first answer wins; ignore later clicks
  closing_ = true;
  result_ = resultId;

  // Defer out of the button's `clicked` emit (D7).  A zero-delay timer fires on
  // the next loop turn, on a clean stack.  startTimer is periodic, so the
  // callback stops it FIRST to make it one-shot -- self-stop is supported
  // (core/Signal.hpp neighbours; platform timer test covers it).
  closeTimer_ = platform().startTimer(0, [this] {
    const TimerId t = closeTimer_;
    closeTimer_ = 0;
    platform().stopTimer(t);
    if (Window* w = window()) w->closePopup();
    // LAST: the caller may destroy this dialog from inside onResult, so nothing
    // below may touch a member.
    if (onResult) onResult(result_);
  });
}

void Dialog::onPaint(Painter& p, const Rect&) {
  const Theme& t = Theme::current();

  // Scrim over the whole window.
  p.fillRect(localRect(), Color::rgb(0, 0, 0).withAlpha(140));

  // Panel.
  const Rect panel = panelRect();
  p.fillRoundRect(panel, t.radius, t.panel);
  p.strokeRoundRect(panel, t.radius, t.panelBorder, 1.0f);

  // Title + a rule under it.
  if (!title_.empty()) {
    p.drawText({panel.x() + kPad, panel.y() + kTitleH * 0.5f}, title_, 15.0f,
               t.text, HAlign::Left, VAlign::Middle);
  }
  p.strokeLine({panel.x(), panel.y() + kTitleH},
               {panel.right(), panel.y() + kTitleH}, t.panelBorder, 1.0f);
}

void Dialog::onMouse(const MouseEvent& e) {
  if (e.action != MouseAction::Press) {
    // Swallow moves/releases inside the overlay so nothing behind reacts.
    e.accept();
    return;
  }
  // A press outside the panel is a scrim click.
  if (!panelRect().contains(e.pos)) {
    if (dismissable_) close(kCancelled);
    e.accept();  // never falls through to the window behind
    return;
  }
  e.accept();  // press landed on the panel background; children handle their own
}

void Dialog::onKey(const KeyEvent& e) {
  if (!e.pressed) return;
  if (e.key == Key::Escape) {
    if (dismissable_) close(kCancelled);
    e.accept();
    return;
  }
  if (e.key == Key::Enter) {
    for (const Btn& b : buttons_) {
      if (b.isDefault) {
        close(b.resultId);
        e.accept();
        return;
      }
    }
  }
}

// =========================================================== convenience ===

void messageBox(Window* w, std::string title, std::string message,
                std::vector<std::string> buttons,
                std::function<void(int)> onAnswer) {
  if (!w) return;
  Dialog* dlg = w->add<Dialog>();
  dlg->setTitle(std::move(title));

  // The message fills the body via a layout, so it reflows if the body resizes.
  auto* col = dlg->body()->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
  auto* msg = dlg->body()->add<Label>();
  msg->setText(std::move(message));
  msg->setWordWrap(true);
  msg->setPixelSize(13.0f);
  msg->setAlign(HAlign::Left, VAlign::Top);
  col->addWidget(msg, 1);

  const int n = int(buttons.size());
  for (int i = 0; i < n; ++i) {
    const bool primary = (i == n - 1);
    dlg->addButton(buttons[std::size_t(i)], i,
                   primary ? ButtonVariant::Primary : ButtonVariant::Default,
                   primary);
  }

  dlg->onResult = [w, dlg, onAnswer](int r) {
    if (onAnswer) onAnswer(r);
    // Destroy on a fresh stack: we are inside the dialog's own deferred close
    // callback right now, so schedule the removeChild rather than doing it here.
    deferOnce([w, dlg] { w->removeChild(dlg); });
  };
  dlg->show(w);
}

void confirmBox(Window* w, std::string title, std::string message,
                std::function<void()> onConfirm, std::string confirmLabel,
                std::string cancelLabel, bool danger) {
  if (!w) return;
  Dialog* dlg = w->add<Dialog>();
  dlg->setTitle(std::move(title));

  auto* col = dlg->body()->setLayout<BoxLayout>(BoxLayout::Orientation::Vertical);
  auto* msg = dlg->body()->add<Label>();
  msg->setText(std::move(message));
  msg->setWordWrap(true);
  msg->setPixelSize(13.0f);
  msg->setAlign(HAlign::Left, VAlign::Top);
  col->addWidget(msg, 1);

  dlg->addButton(std::move(cancelLabel), 0, ButtonVariant::Default, false);
  dlg->addButton(std::move(confirmLabel), 1,
                 danger ? ButtonVariant::Danger : ButtonVariant::Primary, true);

  dlg->onResult = [w, dlg, onConfirm](int r) {
    if (r == 1 && onConfirm) onConfirm();
    deferOnce([w, dlg] { w->removeChild(dlg); });
  };
  dlg->show(w);
}

}  // namespace geeyoou
