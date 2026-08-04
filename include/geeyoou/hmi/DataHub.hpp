#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "geeyoou/core/DataQueue.hpp"
#include "geeyoou/core/Signal.hpp"

namespace geeyoou {

// One reading from the field.
struct Sample {
  int channel = 0;
  double value = 0.0;
  std::uint64_t timestampMs = 0;
  bool good = true;  // false = comms failure / out of range / stale
};

// Owns the acquisition-to-UI boundary for a screen.
//
//   acquisition thread ──push()──> DataQueue ──drain()──> UI thread ──> widgets
//
// push() is safe from any thread.  drain() must be called from the UI thread
// only, typically from Window::enableAnimations()'s tick or from a dedicated
// platform timer.  Nothing else in the library crosses a thread boundary.
class DataHub {
 public:
  explicit DataHub(std::size_t queueCapacity = 8192) : queue_(queueCapacity) {}

  int addChannel(std::string name, std::string unit = {});
  int channelCount() const { return int(channels_.size()); }
  const std::string& channelName(int ch) const;
  const std::string& channelUnit(int ch) const;

  // --- producer side (any thread) ------------------------------------------
  void push(const Sample& s) { queue_.push(s); }
  void push(int channel, double value, std::uint64_t timestampMs, bool good = true) {
    queue_.push(Sample{channel, value, timestampMs, good});
  }

  // --- consumer side (UI thread only) --------------------------------------
  // Moves everything queued into the latest-value cache and fires sampleArrived
  // for each.  Returns how many samples were processed.
  std::size_t drain();

  double lastValue(int ch) const;
  std::uint64_t lastTimestamp(int ch) const;
  bool lastGood(int ch) const;

  // Samples discarded because the queue filled up -- surface this on screen.
  // A silently dropping pipeline is how "the trend has gaps" tickets are born.
  std::size_t droppedCount() const { return queue_.droppedCount(); }
  void resetDroppedCount() { queue_.resetDroppedCount(); }
  std::size_t pending() const { return queue_.size(); }

  // Emitted once per sample, on the UI thread, during drain().
  Signal<const Sample&> sampleArrived;
  // Emitted once per drain() that actually moved anything, after all samples.
  Signal<std::size_t> batchDrained;

 private:
  struct Channel {
    std::string name;
    std::string unit;
    double last = 0.0;
    std::uint64_t lastTs = 0;
    bool good = false;  // no reading yet counts as not-good
  };

  DataQueue<Sample> queue_;
  std::vector<Channel> channels_;
  std::vector<Sample> scratch_;  // reused across drains; never shrunk
};

}  // namespace geeyoou
