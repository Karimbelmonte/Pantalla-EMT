#include "../src/boot_diagnostics.cpp"
#include <cassert>
#include <cstring>
#include <cstdio>
int main() {
  fakeResetReason=ESP_RST_POWERON;beginBootDiagnostics();
  assert(!bootHasPreviousTrace());
  fakeMillis=123456;recordStage(emt::Stage::Weather);
  fakeResetReason=ESP_RST_TASK_WDT;fakeMillis=1;beginBootDiagnostics();
  assert(bootHasPreviousTrace());
  assert(bootPreviousUptimeMs()==123456);
  assert(std::strcmp(bootPreviousStage(),"Consulta OpenWeather")==0);
  assert(std::strcmp(bootResetReason(),"Watchdog de tarea")==0);
  recordRestartIntent(emt::RestartIntent::Web);
  fakeResetReason=ESP_RST_SW;beginBootDiagnostics();
  assert(std::strcmp(bootPreviousIntent(),"Reinicio solicitado desde la web")==0);
  fakeResetReason=ESP_RST_BROWNOUT;beginBootDiagnostics();
  assert(!bootHasPreviousTrace()); // cannot trust retained memory after supply failure
  fakeResetReason=ESP_RST_POWERON;beginBootDiagnostics();
  assert(!bootHasPreviousTrace());
  puts("boot: actual diagnostic module, watchdog stage, requested reset and invalidation after power/brownout PASS");
}
