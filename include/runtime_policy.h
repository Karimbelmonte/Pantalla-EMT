#pragma once
#include <stdint.h>

namespace emt {
// Deadlines must be less than 2^31 ms ahead; subtraction handles millis wrap.
inline bool due(uint32_t now, uint32_t deadline) {
  return static_cast<int32_t>(now - deadline) >= 0;
}
struct RetrySchedule {
  uint32_t deadline = 0;
  uint8_t failures = 0;
  bool scheduled = false;
  bool ready(uint32_t now) const { return !scheduled || due(now, deadline); }
  uint32_t remaining(uint32_t now) const { return ready(now) ? 0 : deadline - now; }
  void reset() { scheduled = false; failures = 0; }
  void success(uint32_t now, uint32_t interval) {
    failures = 0; scheduled = true; deadline = now + interval;
  }
  void fail(uint32_t now) {
    static const uint32_t delays[] = {60000, 120000, 300000, 600000};
    const uint8_t index = failures < 4 ? failures : 3;
    if (failures < 4) ++failures;
    deadline = now + delays[index]; scheduled = true;
  }
};

// A single gate surrounds both coordinate and weather requests. Callbacks permit
// testing the real orchestration without network access or an Arduino runtime.
template<class Ready, class Fetch, class Clock>
bool weatherAttempt(RetrySchedule& retry, uint32_t now, bool enabled,
                    Ready ensureCoordinates, Fetch fetch, Clock clock) {
  if (!enabled || !retry.ready(now)) return false;
  if (!ensureCoordinates() || !fetch()) {
    retry.fail(clock());
    return false;
  }
  retry.success(clock(), 600000);
  return true;
}
}
