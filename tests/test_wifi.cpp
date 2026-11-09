#include "wifi_recovery_policy.h"
#include "boot_trace.h"
#include <cassert>
#include <cstdio>
int main() {
  using C=emt::WifiCredentials; using A=emt::WifiAction;
  emt::WifiRecovery policy;
  uint32_t now=0xfffffff0;
  assert(policy.step(now,C::Saved,false,false,false)==A::ConnectSaved);
  for(int i=1;i<30000;++i) assert(policy.step(now+i,C::Saved,false,false,false)==A::None);
  now+=30000;
  assert(policy.step(now,C::Saved,false,false,false)==A::ConnectSaved);
  // Router absent for a full day must never erase credentials or open a portal.
  for(int i=0;i<2880;++i){now+=30000;assert(policy.step(now,C::Saved,false,false,false)==A::ConnectSaved);}
  assert(policy.step(now,C::Saved,true,false,false)==A::None);
  now+=1000;assert(policy.step(now,C::Saved,false,false,false)==A::ConnectSaved);
  assert(policy.step(now,C::Saved,false,false,true)==A::OpenPortal);
  assert(policy.step(now,C::Saved,false,true,false)==A::None);
  policy.retry.reset();assert(policy.step(now,C::Missing,false,false,false)==A::OpenPortal);
  policy.retry.reset();assert(policy.step(now,C::Unknown,false,false,false)==A::ReadCredentials);
  emt::HoldButton button;
  now=0xfffffff0;
  assert(!button.update(now,true));assert(!button.update(now+2999,true));
  assert(button.update(now+3000,true));assert(!button.update(now+5000,true));
  assert(!button.update(now+5100,false));assert(!button.update(now+5200,true));
  assert(button.update(now+8200,true));
  emt::BootTrace trace{0x454d5431,1,123456,static_cast<uint32_t>(emt::Stage::Weather),0,0};
  trace.checksum=emt::traceChecksum(trace);assert(emt::validTrace(trace));
  trace.uptime++;assert(!emt::validTrace(trace));
  trace.checksum=emt::traceChecksum(trace);trace.stage=999;
  trace.checksum=emt::traceChecksum(trace);assert(!emt::validTrace(trace));
  puts("wifi: saved/absent/unknown credentials, 24h simulated outage, recovery, manual portal, button and RTC trace PASS");
}
