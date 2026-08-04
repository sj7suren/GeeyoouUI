#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "geeyoou/core/Signal.hpp"
#include "geeyoou/widget/ListView.hpp"

namespace geeyoou {

enum class AlarmSeverity : std::uint8_t { Critical, High, Medium, Low, Info };
enum class AlarmState : std::uint8_t { Active, Acknowledged, Cleared };

struct AlarmRecord {
  std::uint64_t id = 0;
  std::uint64_t timestampMs = 0;
  AlarmSeverity severity = AlarmSeverity::Medium;
  AlarmState state = AlarmState::Active;
  std::string tag;      // 位号
  std::string message;  // 报警内容
  std::string value;    // 触发时的值，已格式化
};

// Alarm banner / history list.
//
// Storage is a FIXED-CAPACITY ring: an upper computer runs for months and an
// unbounded alarm log is the same slow leak as an unbounded sample queue.
// When it wraps, the oldest record is discarded and counted.
//
// Rendering rides on ListView's pull model, so the records are read in place
// and never copied into the widget.
class AlarmList : public ListView {
 public:
  GEEYOOU_STYLE_TYPE(AlarmList, ListView)

  explicit AlarmList(std::size_t capacity = 5000);

  // Appends and returns the assigned id.  Callable from the UI thread only --
  // an acquisition thread must route through DataHub / its own DataQueue.
  std::uint64_t add(AlarmRecord record);
  void acknowledge(std::uint64_t id);
  void acknowledgeAll();
  void clearAlarm(std::uint64_t id);
  void removeAll();

  // --- filtering -----------------------------------------------------------
  void setMinSeverity(AlarmSeverity s);
  void setShowAcknowledged(bool on);
  void setShowCleared(bool on);
  void setTextFilter(std::string utf8);  // matches tag or message

  int activeCount() const { return activeCount_; }
  int unacknowledgedCount() const { return unackCount_; }
  int visibleCount() const { return int(visible_.size()); }
  std::size_t discardedCount() const { return discarded_; }

  const AlarmRecord* recordAt(int visibleRow) const;
  const AlarmRecord* currentRecord() const;

  // Formats a timestamp as HH:MM:SS, treating `ms` as UTC -- see the timezone
  // note in core/Date.hpp.  Feed it localised milliseconds if the operator
  // should see wall-clock time.
  static std::string formatTime(std::uint64_t ms);

  Signal<const AlarmRecord&> alarmAdded;
  Signal<const AlarmRecord&> alarmAcknowledged;

 private:
  void rebuildVisible();
  bool passesFilter(const AlarmRecord& r) const;
  AlarmRecord* find(std::uint64_t id);

  std::vector<AlarmRecord> ring_;
  std::size_t head_ = 0;   // index of the oldest record
  std::size_t count_ = 0;
  std::size_t discarded_ = 0;
  std::uint64_t nextId_ = 1;

  std::vector<int> visible_;  // ring indices that pass the filter, newest first
  AlarmSeverity minSeverity_ = AlarmSeverity::Info;
  bool showAck_ = true;
  bool showCleared_ = false;
  std::string textFilter_;

  int activeCount_ = 0;
  int unackCount_ = 0;
};

}  // namespace geeyoou
