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
  // Short outages retry the saved network; a full minute opens configuration.
  assert(policy.step(now+29999,C::Saved,false,false,false)==A::None);
  now+=30000;
  assert(policy.step(now,C::Saved,false,false,false)==A::OpenPortal);
  assert(policy.step(now+1000,C::Saved,false,true,false)==A::None);
  policy.portalClosed(now+600000);
  now+=600000;
  assert(policy.step(now,C::Saved,false,false,false)==A::ConnectSaved);
  assert(policy.step(now+30000,C::Saved,false,false,false)==A::ConnectSaved);
  assert(policy.step(now+60000,C::Saved,false,false,false)==A::OpenPortal);
  // A 24-hour simulated outage alternates retry and portal windows, without reset.
  for(int i=0;i<131;++i){
    now+=660000;policy.portalClosed(now);
    assert(policy.step(now,C::Saved,false,false,false)==A::ConnectSaved);
    assert(policy.step(now+60000,C::Saved,false,false,false)==A::OpenPortal);
  }
  assert(policy.step(now,C::Saved,true,false,false)==A::None);
  now+=1000;assert(policy.step(now,C::Saved,false,false,false)==A::ConnectSaved);
  assert(policy.step(now+59999,C::Saved,true,false,false)==A::None);
  now+=60000;assert(policy.step(now,C::Saved,false,false,false)==A::ConnectSaved);
  assert(policy.step(now+59999,C::Saved,false,false,false)==A::ConnectSaved);
  assert(policy.step(now+60000,C::Saved,false,false,false)==A::OpenPortal);
  assert(policy.step(now,C::Saved,true,false,true)==A::OpenPortal);
  policy.retry.reset();assert(policy.step(now,C::Missing,false,false,false)==A::OpenPortal);
  policy.retry.reset();assert(policy.step(now,C::Unknown,false,false,false)==A::ReadCredentials);
  now+=3600000;
  assert(policy.step(now,C::Unknown,false,false,false)==A::ReadCredentials);
  emt::PortalWindow window;
  now=0xffffff00;
  window.start(now,true,false);
  assert(!window.expired(now+599999,false));
  assert(window.expired(now+600000,false));
  window.start(now,true,false);
  assert(!window.expired(now+600001,true)); // phone keeps configuration open
  assert(!window.expired(now+1200000,false));
  assert(window.expired(now+1200001,false));
  window.start(now,true,true);
  assert(!window.expired(now+179999,false));assert(window.expired(now+180000,false));
  window.start(now,false,false);assert(!window.expired(now+86400000,false));
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
  puts("wifi: saved/absent/unknown credentials, 60s fallback without BOOT, 24h simulated recovery, portal expiry/clients/wrap and RTC trace PASS");
}
