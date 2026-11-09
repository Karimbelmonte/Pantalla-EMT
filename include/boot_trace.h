#pragma once
#include <stdint.h>
namespace emt {
enum class Stage : uint32_t { Boot, WifiConnect, WifiPortal, Idle, Web, EmtLogin, EmtDetail, EmtArrivals, Weather, Ota, Sleep, End };
enum class RestartIntent : uint32_t { None, Web, Ota, Sleep, End };
struct BootTrace {
  uint32_t magic, boots, uptime, stage, intent, checksum;
};
inline uint32_t traceChecksum(const BootTrace& t) {
  return 0x83749215U ^ t.magic ^ (t.boots*31U) ^ (t.uptime*17U) ^ (t.stage*101U) ^ (t.intent*257U);
}
inline bool validTrace(const BootTrace& t) {
  return t.magic==0x454d5431 && t.stage<static_cast<uint32_t>(Stage::End)
    && t.intent<static_cast<uint32_t>(RestartIntent::End) && t.checksum==traceChecksum(t);
}
inline const char* stageName(Stage s) {
  switch(s) {
    case Stage::Boot:return "Arranque";
    case Stage::WifiConnect:return "Conexión/reconexión WiFi";
    case Stage::WifiPortal:return "Portal de configuración WiFi";
    case Stage::Idle:return "Bucle y pantalla";
    case Stage::Web:return "Petición web";
    case Stage::EmtLogin:return "Autenticación EMT";
    case Stage::EmtDetail:return "Detalle de parada EMT";
    case Stage::EmtArrivals:return "Llegadas EMT";
    case Stage::Weather:return "Consulta OpenWeather";
    case Stage::Ota:return "Actualización OTA";
    case Stage::Sleep:return "Descanso";
    default:return "Desconocido";
  }
}
}
