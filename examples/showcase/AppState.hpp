#pragma once
//
// State shared by the pages.
//
// One acquisition thread and one DataHub serve the whole application, exactly
// as a real upper computer would: pages are views onto the same live data, not
// each their own simulation.  Alarms raised while a page is closed are buffered
// so the list is not empty the first time it is opened.
//
#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <thread>
#include <vector>

#include "geeyoou/hmi/AlarmList.hpp"
#include "geeyoou/hmi/DataHub.hpp"
#include "geeyoou/widget/Widget.hpp"

namespace showcase {

inline std::uint64_t nowMs() {
  using namespace std::chrono;
  return std::uint64_t(
      duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count());
}

// Zero-sized helper that turns Window::enableAnimations() into a periodic
// callback for one page.  Because animationTickTree() skips invisible subtrees,
// a page that is not on screen stops ticking automatically -- no page has to
// remember to unsubscribe when it is navigated away from.
class Ticker : public geeyoou::Widget {
 public:
  std::function<void()> onTick;
  int divisor = 1;  // fire once every N animation frames

 protected:
  void onAnimationTick() override {
    if (++counter_ < divisor) return;
    counter_ = 0;
    if (onTick) onTick();
  }

 private:
  int counter_ = 0;
};

struct AppState {
  geeyoou::DataHub hub{4096};

  int chFlow = 0;
  int chTemp = 0;
  int chPress = 0;

  // Alarms raised before the ops page exists land here; the page drains them
  // on construction and then takes over via `alarmSink`.
  std::vector<geeyoou::AlarmRecord> alarmBacklog;
  std::function<void(const geeyoou::AlarmRecord&)> alarmSink;

  void raise(geeyoou::AlarmRecord r) {
    if (alarmSink) alarmSink(r);
    else alarmBacklog.push_back(std::move(r));
  }

  // --- acquisition thread ---
  std::atomic<bool> running{false};
  std::thread worker;

  void startAcquisition();
  void stopAcquisition();
};

}  // namespace showcase
