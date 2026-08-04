#include "geeyoou/hmi/TrendChart.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

#include "geeyoou/render/Painter.hpp"
#include "geeyoou/render/Theme.hpp"

namespace geeyoou {

int TrendChart::addChannel(std::string name, Color color, std::size_t capacity) {
  Channel ch;
  ch.name = std::move(name);
  ch.color = color;
  ch.samples.assign(capacity ? capacity : 1, 0.0f);
  channels_.push_back(std::move(ch));

  // Grow the scratch buffer once, here, so onPaint never allocates.
  scratch_.reserve(std::max(scratch_.capacity(), capacity));
  return int(channels_.size()) - 1;
}

void TrendChart::push(int channel, float value) {
  if (channel < 0 || std::size_t(channel) >= channels_.size()) return;
  Channel& ch = channels_[std::size_t(channel)];
  ch.samples[ch.head] = value;
  ch.head = (ch.head + 1) % ch.samples.size();
  if (ch.count < ch.samples.size()) ++ch.count;
}

void TrendChart::pushAll(const float* values, std::size_t count) {
  const std::size_t n = std::min(count, channels_.size());
  for (std::size_t i = 0; i < n; ++i) push(int(i), values[i]);
  // One repaint for the whole batch, not one per channel.
  update(plotArea());
}

void TrendChart::setYRange(float minY, float maxY) {
  minY_ = minY;
  maxY_ = maxY;
  autoScale_ = false;
  update();
}

void TrendChart::setGridDivisions(int x, int y) {
  gridX_ = std::max(1, x);
  gridY_ = std::max(1, y);
  update();
}

std::size_t TrendChart::sampleCount(int channel) const {
  if (channel < 0 || std::size_t(channel) >= channels_.size()) return 0;
  return channels_[std::size_t(channel)].count;
}

void TrendChart::onGeometryChanged() { update(); }

Rect TrendChart::plotArea() const {
  // Left margin holds the Y axis labels, top holds the title, bottom the legend.
  const float left = 46.0f;
  const float right = 12.0f;
  const float top = title_.empty() ? 12.0f : 30.0f;
  const float bottom = 22.0f;
  const Rect r = localRect();
  const float w = r.width() - left - right;
  const float h = r.height() - top - bottom;
  if (w <= 0.0f || h <= 0.0f) return {};
  return {left, top, w, h};
}

void TrendChart::resolveYRange(float& lo, float& hi) const {
  if (!autoScale_) {
    lo = minY_;
    hi = maxY_;
  } else {
    lo = std::numeric_limits<float>::max();
    hi = std::numeric_limits<float>::lowest();
    for (const Channel& ch : channels_) {
      for (std::size_t i = 0; i < ch.count; ++i) {
        const float v = ch.samples[(ch.head + ch.samples.size() - ch.count + i) %
                                   ch.samples.size()];
        lo = std::min(lo, v);
        hi = std::max(hi, v);
      }
    }
    if (lo > hi) { lo = 0.0f; hi = 1.0f; }
    const float pad = std::max(1e-3f, (hi - lo) * 0.1f);
    lo -= pad;
    hi += pad;
  }
  if (hi - lo < 1e-6f) hi = lo + 1.0f;
}

void TrendChart::onPaint(Painter& p, const Rect&) {
  const Theme& t = Theme::current();
  const Rect r = localRect();

  p.fillRoundRect(r, t.radius, t.panel);
  p.strokeRoundRect(r.deflated(0.5f), t.radius, t.panelBorder, 1.0f);

  if (!title_.empty()) {
    p.drawText({r.x() + 12.0f, r.y() + 9.0f}, title_, t.fontBody, t.text,
               HAlign::Left, VAlign::Top);
  }

  const Rect plot = plotArea();
  if (plot.isEmpty()) return;

  float lo = 0.0f, hi = 1.0f;
  resolveYRange(lo, hi);

  p.fillRect(plot, t.background.withAlpha(140));

  // --- grid + Y labels -----------------------------------------------------
  char buf[48];
  for (int i = 0; i <= gridY_; ++i) {
    const float y = plot.y() + plot.height() * float(i) / float(gridY_);
    p.strokeLine({plot.left(), y}, {plot.right(), y}, t.grid, 1.0f);
    const float v = hi - (hi - lo) * float(i) / float(gridY_);
    std::snprintf(buf, sizeof(buf), "%.0f", v);
    p.drawText({plot.left() - 6.0f, y}, buf, t.fontSmall, t.textDim, HAlign::Right,
               VAlign::Middle);
  }
  for (int i = 0; i <= gridX_; ++i) {
    const float x = plot.x() + plot.width() * float(i) / float(gridX_);
    p.strokeLine({x, plot.top()}, {x, plot.bottom()}, t.grid, 1.0f);
  }
  p.strokeRect(plot, t.panelBorder, 1.0f);

  // --- traces --------------------------------------------------------------
  p.save();
  p.clip(plot);
  const float span = hi - lo;
  for (const Channel& ch : channels_) {
    if (ch.count < 2) continue;

    // X is laid out over the channel's FULL capacity, not over count, so a
    // partially-filled buffer draws from the left and grows rightwards instead
    // of stretching -- which is what an operator expects from a chart recorder.
    const std::size_t cap = ch.samples.size();
    const float dx = plot.width() / float(cap - 1);

    scratch_.clear();
    const std::size_t first = (ch.head + cap - ch.count) % cap;
    for (std::size_t i = 0; i < ch.count; ++i) {
      const float v = ch.samples[(first + i) % cap];
      const float nx = plot.x() + dx * float(i);
      const float ny = plot.bottom() - (v - lo) / span * plot.height();
      scratch_.push_back({nx, ny});
    }
    p.strokePolyline(scratch_.data(), scratch_.size(), ch.color, 1.6f);
  }
  p.restore();

  // --- legend --------------------------------------------------------------
  float lx = plot.right();
  for (auto it = channels_.rbegin(); it != channels_.rend(); ++it) {
    const Size ts = p.measureText(it->name, t.fontSmall);
    lx -= ts.width + 10.0f;
    p.drawText({lx, r.bottom() - 16.0f}, it->name, t.fontSmall, t.textDim,
               HAlign::Left, VAlign::Top);
    lx -= 8.0f;
    p.fillRect({lx, r.bottom() - 12.0f, 6.0f, 6.0f}, it->color);
    lx -= 10.0f;
  }
}

}  // namespace geeyoou
