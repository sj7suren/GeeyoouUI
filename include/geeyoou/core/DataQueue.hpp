#pragma once
//
// The ONE sanctioned bridge between an acquisition thread and the UI thread.
//
// docs/architecture.md section 3.10 states the rule this file exists to serve:
// widgets are not thread-safe and never will be.  A Modbus/CAN/OPC worker must
// therefore hand data over, not touch a Widget.
//
#include <cstddef>
#include <mutex>
#include <utility>
#include <vector>

namespace geeyoou {

// Bounded multi-producer / single-consumer queue.
//
// BOUNDED is the whole point.  An unbounded queue in an upper computer that
// runs for months is a slow memory leak waiting for the day the UI stalls:
// the producer never blocks, so the backlog grows without limit. Here the
// oldest samples are dropped instead and the loss is COUNTED, so a screen can
// show "采集积压，已丢弃 N 帧" rather than dying quietly.
//
// A plain mutex rather than a lock-free ring: contention is one short critical
// section per sample at kHz rates at worst, and a correct, obvious
// implementation beats a clever one nobody can audit.
template <class T>
class DataQueue {
 public:
  explicit DataQueue(std::size_t capacity = 4096)
      : buffer_(capacity ? capacity : 1) {}

  // Callable from any thread.
  void push(T value) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (count_ == buffer_.size()) {
      buffer_[head_] = std::move(value);
      head_ = (head_ + 1) % buffer_.size();
      ++dropped_;  // overwrote the oldest unread item
      return;
    }
    buffer_[(head_ + count_) % buffer_.size()] = std::move(value);
    ++count_;
  }

  // UI thread only.  Moves everything currently queued into `out`, appending.
  // Returns how many items were taken.
  //
  // Drains into a caller-owned vector rather than invoking a callback under the
  // lock: a slot that repaints, or that pushes back into another queue, must
  // not do so while a producer is blocked behind us.
  std::size_t drain(std::vector<T>& out) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::size_t n = count_;
    out.reserve(out.size() + n);
    for (std::size_t i = 0; i < n; ++i) {
      out.push_back(std::move(buffer_[(head_ + i) % buffer_.size()]));
    }
    head_ = (head_ + n) % buffer_.size();
    count_ = 0;
    return n;
  }

  std::size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return count_;
  }

  std::size_t capacity() const { return buffer_.size(); }

  // Total items discarded because the queue was full since the last reset.
  std::size_t droppedCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return dropped_;
  }

  void resetDroppedCount() {
    std::lock_guard<std::mutex> lock(mutex_);
    dropped_ = 0;
  }

 private:
  mutable std::mutex mutex_;
  std::vector<T> buffer_;
  std::size_t head_ = 0;
  std::size_t count_ = 0;
  std::size_t dropped_ = 0;
};

}  // namespace geeyoou
