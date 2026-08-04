#pragma once
#include <cstddef>
#include <string>
#include <vector>

#include "geeyoou/widget/Widget.hpp"

namespace geeyoou {

// Scrolling multi-channel trend plot.
//
// Each channel is a FIXED-CAPACITY ring buffer.  Nothing grows, nothing is
// reallocated after setup, and the plot point scratch buffer is reused across
// frames -- an upper computer runs for months and a per-frame allocation is a
// slow leak plus a frame-time spike.  See docs/architecture.md section 1.
class TrendChart : public Widget {
 public:
  GEEYOOU_STYLE_TYPE(TrendChart, Widget)

  // `capacity` is the number of samples retained per channel (the visible
  // history).  Adding a channel after data has been pushed is allowed; the new
  // channel simply starts empty.
  int addChannel(std::string name, Color color, std::size_t capacity = 600);

  // Appends one sample to `channel`.  O(1), no allocation.
  void push(int channel, float value);

  // Appends one sample to every channel at once, keeping them index-aligned.
  void pushAll(const float* values, std::size_t count);

  void setYRange(float minY, float maxY);
  void setAutoScale(bool on) { autoScale_ = on; }
  void setGridDivisions(int x, int y);
  void setTitle(std::string utf8) { title_ = std::move(utf8); update(); }

  std::size_t sampleCount(int channel) const;

 protected:
  void onPaint(Painter& p, const Rect& dirtyLocal) override;
  void onGeometryChanged() override;

 private:
  struct Channel {
    std::string name;
    Color color;
    std::vector<float> samples;  // ring buffer, fixed size == capacity
    std::size_t head = 0;        // index of the next write
    std::size_t count = 0;       // valid samples, saturates at capacity
  };

  Rect plotArea() const;
  void resolveYRange(float& lo, float& hi) const;

  std::vector<Channel> channels_;
  std::vector<Point> scratch_;  // reused every frame, never shrunk
  float minY_ = 0.0f;
  float maxY_ = 100.0f;
  bool autoScale_ = false;
  int gridX_ = 6;
  int gridY_ = 4;
  std::string title_;
};

}  // namespace geeyoou
