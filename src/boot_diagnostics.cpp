#include "boot_diagnostics.h"
#include <Arduino.h>
#include <esp_system.h>
#include <esp_attr.h>

// RTC SRAM avoids writes to flash on every request. A power loss can invalidate it.
RTC_NOINIT_ATTR static emt::BootTrace trace;
static emt::BootTrace previous{};
static bool previousValid=false;
static esp_reset_reason_t reason=ESP_RST_UNKNOWN;
void recordStage(emt::Stage stage) {
  trace.stage=static_cast<uint32_t>(stage);
  trace.uptime=millis();
  trace.checksum=emt::traceChecksum(trace);
}
void recordRestartIntent(emt::RestartIntent intent) {
  trace.intent=static_cast<uint32_t>(intent);
  trace.uptime=millis();
  trace.checksum=emt::traceChecksum(trace);
}
const char* bootResetReason() {
  switch(reason) {
    case ESP_RST_POWERON:return "Encendido / reinicio de alimentación";
    case ESP_RST_EXT:return "Reset externo";
    case ESP_RST_SW:return "Reinicio por software";
    case ESP_RST_PANIC:return "Excepción / panic";
    case ESP_RST_INT_WDT:return "Watchdog de interrupciones";
    case ESP_RST_TASK_WDT:return "Watchdog de tarea";
    case ESP_RST_WDT:return "Otro watchdog";
    case ESP_RST_DEEPSLEEP:return "Despertar de deep sleep";
    case ESP_RST_BROWNOUT:return "Brownout: tensión insuficiente detectada";
    case ESP_RST_SDIO:return "Reset SDIO";
    default:return "Motivo desconocido";
  }
}
bool bootHasPreviousTrace() { return previousValid; }
uint32_t bootPreviousUptimeMs() { return previousValid ? previous.uptime : 0; }
const char* bootPreviousStage() {
  return previousValid ? emt::stageName(static_cast<emt::Stage>(previous.stage)) : "Sin traza RTC válida";
}
const char* bootPreviousIntent() {
  if(!previousValid) return "No disponible";
  switch(static_cast<emt::RestartIntent>(previous.intent)) {
    case emt::RestartIntent::Web:return "Reinicio solicitado desde la web";
    case emt::RestartIntent::Ota:return "Instalación OTA completada";
    case emt::RestartIntent::Sleep:return "Descanso profundo programado";
    default:return "Sin reinicio solicitado por la aplicación";
  }
}
void beginBootDiagnostics() {
  reason=esp_reset_reason();
  previousValid=reason!=ESP_RST_POWERON && reason!=ESP_RST_BROWNOUT && emt::validTrace(trace);
  if(previousValid) previous=trace;
  trace={0x454d5431U, previousValid ? previous.boots+1 : 1,0,
         static_cast<uint32_t>(emt::Stage::Boot),static_cast<uint32_t>(emt::RestartIntent::None),0};
  recordStage(emt::Stage::Boot);
  Serial.printf("BOOT reason=%d (%s); previous=%s; uptime_ms=%lu; intent=%s\n",
    static_cast<int>(reason),bootResetReason(),bootPreviousStage(),
    static_cast<unsigned long>(bootPreviousUptimeMs()),bootPreviousIntent());
}
