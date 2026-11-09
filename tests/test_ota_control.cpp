#include "globals.h"
#include "ota_control.h"
#include <cassert>
#include <cstdio>
bool wifiConnected=true;
int checks=0,installs=0,restarts=0;
bool found=true,downloadOk=true;
bool findOtaCandidate(emt::OtaCandidate<String>& result) {
  ++checks;
  if (!found || !wifiConnected) { otaStatus="Consulta fallida"; return false; }
  result.version="0.14.0"; result.size=100;
  return true;
}
bool installOtaCandidate(const emt::OtaCandidate<String>& result) {
  assert(result.version=="0.14.0" && result.size==100);
  ++installs; return downloadOk;
}
void restartAfterOta(){++restarts;}
#include "../src/ota_control.cpp"

void runCheck() {
  assert(requestOtaCheck());
  fakeMillis+=500;
  assert(processManualUpdates());
  assert(!otaBusy());
}
int main() {
  assert(!processManualUpdates() && !otaCanInstall());
  assert(!requestOtaInstall("0.14.0"));
  wifiConnected=false;assert(!requestOtaCheck());wifiConnected=true;
  fakeMillis=0xFFFFFF00UL;
  assert(requestOtaCheck() && otaBusy());
  assert(!requestOtaCheck() && !requestOtaInstall("0.14.0"));
  fakeMillis=0xF3; // 499 milliseconds across rollover
  assert(processManualUpdates() && checks==0);
  fakeMillis=0xF4;
  assert(processManualUpdates() && checks==1 && otaCanInstall() && installs==0);
  assert(!requestOtaInstall("0.13.0"));
  fakeMillis+=600000;assert(!otaCanInstall() && !requestOtaInstall("0.14.0"));
  runCheck();assert(otaCanInstall());
  assert(requestOtaInstall("0.14.0"));
  wifiConnected=false;fakeMillis+=500;processManualUpdates();
  assert(installs==0 && restarts==0 && !otaCanInstall());
  wifiConnected=true;runCheck();
  downloadOk=false;
  assert(requestOtaInstall("0.14.0"));
  fakeMillis+=500;processManualUpdates();
  assert(installs==1 && restarts==0 && !otaCanInstall());
  assert(!requestOtaInstall("0.14.0"));
  runCheck();found=false;runCheck();
  assert(!otaCanInstall() && otaStatus=="Consulta fallida");
  found=true;downloadOk=true;runCheck();
  assert(requestOtaInstall("0.14.0"));
  fakeMillis+=500;processManualUpdates();
  assert(installs==2 && restarts==1);
  // The explicit automatic profile retains its check-and-install behavior.
  checkUpdates();assert(installs==3 && restarts==2);
  puts("ota_control: queued check/confirm/install, no install on check, expiry, clock wrap, connection loss and failures PASS");
}
