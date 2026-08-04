#include "geeyoou/hmi/DataHub.hpp"

namespace geeyoou {
namespace {
const std::string kEmpty;
}

int DataHub::addChannel(std::string name, std::string unit) {
  Channel c;
  c.name = std::move(name);
  c.unit = std::move(unit);
  channels_.push_back(std::move(c));
  return int(channels_.size()) - 1;
}

const std::string& DataHub::channelName(int ch) const {
  if (ch < 0 || ch >= int(channels_.size())) return kEmpty;
  return channels_[std::size_t(ch)].name;
}

const std::string& DataHub::channelUnit(int ch) const {
  if (ch < 0 || ch >= int(channels_.size())) return kEmpty;
  return channels_[std::size_t(ch)].unit;
}

std::size_t DataHub::drain() {
  scratch_.clear();  // keeps its capacity, so a steady rate stops allocating
  const std::size_t n = queue_.drain(scratch_);
  if (n == 0) return 0;

  for (const Sample& s : scratch_) {
    if (s.channel >= 0 && s.channel < int(channels_.size())) {
      Channel& c = channels_[std::size_t(s.channel)];
      c.last = s.value;
      c.lastTs = s.timestampMs;
      c.good = s.good;
    }
    // Fired after the cache is updated, so a slot that reads lastValue() during
    // the callback sees this sample rather than the previous one.
    sampleArrived.emit(s);
  }
  batchDrained.emit(n);
  return n;
}

double DataHub::lastValue(int ch) const {
  if (ch < 0 || ch >= int(channels_.size())) return 0.0;
  return channels_[std::size_t(ch)].last;
}

std::uint64_t DataHub::lastTimestamp(int ch) const {
  if (ch < 0 || ch >= int(channels_.size())) return 0;
  return channels_[std::size_t(ch)].lastTs;
}

bool DataHub::lastGood(int ch) const {
  if (ch < 0 || ch >= int(channels_.size())) return false;
  return channels_[std::size_t(ch)].good;
}

}  // namespace geeyoou
