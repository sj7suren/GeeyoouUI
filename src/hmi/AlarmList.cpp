#include "geeyoou/hmi/AlarmList.hpp"

#include <algorithm>
#include <cstdio>

#include "geeyoou/render/Theme.hpp"

namespace geeyoou {
namespace {

const char* severityLabel(AlarmSeverity s) {
  switch (s) {
    case AlarmSeverity::Critical: return "紧急";
    case AlarmSeverity::High:     return "高";
    case AlarmSeverity::Medium:   return "中";
    case AlarmSeverity::Low:      return "低";
    case AlarmSeverity::Info:     return "提示";
  }
  return "";
}

const char* stateLabel(AlarmState s) {
  switch (s) {
    case AlarmState::Active:       return "未确认";
    case AlarmState::Acknowledged: return "已确认";
    case AlarmState::Cleared:      return "已恢复";
  }
  return "";
}

Color severityColor(AlarmSeverity s) {
  const Theme& t = Theme::current();
  switch (s) {
    case AlarmSeverity::Critical: return t.alarm;
    case AlarmSeverity::High:     return t.alarm.lerp(t.warn, 0.5f);
    case AlarmSeverity::Medium:   return t.warn;
    case AlarmSeverity::Low:      return t.accent;
    case AlarmSeverity::Info:     return t.textDim;
  }
  return t.textDim;
}

}  // namespace

AlarmList::AlarmList(std::size_t capacity) : ring_(capacity ? capacity : 1) {
  setColumns({
      {"时间", 88.0f, HAlign::Left},
      {"级别", 56.0f, HAlign::Center},
      {"位号", 92.0f, HAlign::Left},
      {"报警内容", -1.0f, HAlign::Left},  // takes the remaining width
      {"值", 84.0f, HAlign::Right},
      {"状态", 70.0f, HAlign::Center},
  });
  setSelectionMode(SelectionMode::Single);
  setRowHeight(26.0f);

  // The pull model wired to the ring: ListView asks, we look up in place.
  cellText = [this](int row, int col) -> std::string {
    const AlarmRecord* r = recordAt(row);
    if (!r) return {};
    switch (col) {
      case 0: return formatTime(r->timestampMs);
      case 1: return severityLabel(r->severity);
      case 2: return r->tag;
      case 3: return r->message;
      case 4: return r->value;
      case 5: return stateLabel(r->state);
      default: return {};
    }
  };
  rowAccent = [this](int row) -> Color {
    const AlarmRecord* r = recordAt(row);
    if (!r) return Color::rgba(0, 0, 0, 0);
    Color c = severityColor(r->severity);
    // Acknowledged and cleared alarms fade back so the eye lands on what still
    // needs action -- the entire job of an alarm banner.
    if (r->state == AlarmState::Acknowledged) c = c.lerp(Theme::current().panel, 0.45f);
    else if (r->state == AlarmState::Cleared) c = c.lerp(Theme::current().panel, 0.7f);
    return c;
  };
}

std::string AlarmList::formatTime(std::uint64_t ms) {
  const std::uint64_t total = ms / 1000;
  char buf[16];
  std::snprintf(buf, sizeof(buf), "%02llu:%02llu:%02llu",
                (unsigned long long)((total / 3600) % 24),
                (unsigned long long)((total / 60) % 60),
                (unsigned long long)(total % 60));
  return buf;
}

std::uint64_t AlarmList::add(AlarmRecord record) {
  record.id = nextId_++;
  if (count_ == ring_.size()) {
    ring_[head_] = std::move(record);
    head_ = (head_ + 1) % ring_.size();
    ++discarded_;
  } else {
    ring_[(head_ + count_) % ring_.size()] = std::move(record);
    ++count_;
  }
  rebuildVisible();
  const AlarmRecord* added = nullptr;
  for (std::size_t i = 0; i < count_; ++i) {
    const AlarmRecord& r = ring_[(head_ + i) % ring_.size()];
    if (r.id == nextId_ - 1) { added = &r; break; }
  }
  if (added) alarmAdded.emit(*added);
  return nextId_ - 1;
}

AlarmRecord* AlarmList::find(std::uint64_t id) {
  for (std::size_t i = 0; i < count_; ++i) {
    AlarmRecord& r = ring_[(head_ + i) % ring_.size()];
    if (r.id == id) return &r;
  }
  return nullptr;
}

void AlarmList::acknowledge(std::uint64_t id) {
  AlarmRecord* r = find(id);
  if (!r || r->state != AlarmState::Active) return;
  r->state = AlarmState::Acknowledged;
  alarmAcknowledged.emit(*r);
  rebuildVisible();
}

void AlarmList::acknowledgeAll() {
  bool any = false;
  for (std::size_t i = 0; i < count_; ++i) {
    AlarmRecord& r = ring_[(head_ + i) % ring_.size()];
    if (r.state == AlarmState::Active) {
      r.state = AlarmState::Acknowledged;
      alarmAcknowledged.emit(r);
      any = true;
    }
  }
  if (any) rebuildVisible();
}

void AlarmList::clearAlarm(std::uint64_t id) {
  AlarmRecord* r = find(id);
  if (!r) return;
  r->state = AlarmState::Cleared;
  rebuildVisible();
}

void AlarmList::removeAll() {
  head_ = count_ = 0;
  rebuildVisible();
}

void AlarmList::setMinSeverity(AlarmSeverity s) {
  minSeverity_ = s;
  rebuildVisible();
}

void AlarmList::setShowAcknowledged(bool on) {
  showAck_ = on;
  rebuildVisible();
}

void AlarmList::setShowCleared(bool on) {
  showCleared_ = on;
  rebuildVisible();
}

void AlarmList::setTextFilter(std::string utf8) {
  textFilter_ = std::move(utf8);
  rebuildVisible();
}

bool AlarmList::passesFilter(const AlarmRecord& r) const {
  // Severity is an enum ordered most-severe-first, so "at least this severe"
  // is a <= test.  Naming it minSeverity while comparing with <= is the kind
  // of thing that bites later, hence this comment.
  if (static_cast<std::uint8_t>(r.severity) > static_cast<std::uint8_t>(minSeverity_)) {
    return false;
  }
  if (!showAck_ && r.state == AlarmState::Acknowledged) return false;
  if (!showCleared_ && r.state == AlarmState::Cleared) return false;
  if (!textFilter_.empty()) {
    if (r.tag.find(textFilter_) == std::string::npos &&
        r.message.find(textFilter_) == std::string::npos) {
      return false;
    }
  }
  return true;
}

void AlarmList::rebuildVisible() {
  visible_.clear();
  activeCount_ = 0;
  unackCount_ = 0;
  // Newest first: an operator looks at the top of an alarm list.
  for (std::size_t i = count_; i-- > 0;) {
    const std::size_t idx = (head_ + i) % ring_.size();
    const AlarmRecord& r = ring_[idx];
    if (r.state != AlarmState::Cleared) ++activeCount_;
    if (r.state == AlarmState::Active) ++unackCount_;
    if (passesFilter(r)) visible_.push_back(int(idx));
  }
  setRowCount(int(visible_.size()));
  update();
}

const AlarmRecord* AlarmList::recordAt(int visibleRow) const {
  if (visibleRow < 0 || visibleRow >= int(visible_.size())) return nullptr;
  return &ring_[std::size_t(visible_[std::size_t(visibleRow)])];
}

const AlarmRecord* AlarmList::currentRecord() const {
  return recordAt(currentRow());
}

}  // namespace geeyoou
