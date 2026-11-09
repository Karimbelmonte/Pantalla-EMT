#include "runtime_policy.h"
#include <cassert>
#include <cstdio>

int main() {
  assert(emt::due(0x10, 0xfffffff0));
  assert(!emt::due(0xfffffff0, 0x10));
  emt::RetrySchedule retry;
  uint32_t now = 0xfffffff0;
  const uint32_t delays[] = {60000,120000,300000,600000,600000};
  for (auto delay : delays) {
    retry.fail(now);
    assert(retry.remaining(now) == delay);
    assert(!retry.ready(now + delay - 1));
    now += delay;
    assert(retry.ready(now));
  }
  retry.success(now, 600000);
  assert(retry.failures == 0 && !retry.ready(now));
  retry.reset();
  assert(retry.ready(now));
  int coords = 0, fetches = 0;
  bool coordOk = false, fetchOk = false;
  auto attempt = [&](bool enabled=true) {
    return emt::weatherAttempt(retry, now, enabled,
      [&]{ ++coords; return coordOk; },
      [&]{ ++fetches; return fetchOk; }, [&]{ return now; });
  };
  assert(!attempt(false) && coords == 0);
  assert(!attempt() && coords == 1 && fetches == 0);
  for (int i=0; i<10000; ++i) assert(!attempt());
  assert(coords == 1); // missing coords / auth / DNS failure cannot hammer API
  now += 60000; coordOk = true;
  assert(!attempt() && fetches == 1); // timeout / HTTP / invalid JSON failure
  now += 119999; assert(!attempt() && fetches == 1);
  ++now; fetchOk = true;
  assert(attempt() && retry.failures == 0);
  assert(!attempt() && fetches == 2);
  now += 600000; assert(attempt());
  puts("runtime: deadlines, capped backoff, shared gate, failures and recovery PASS");
}
