#include "globals.h"
#include "ota_control.h"
#include "firmware_version.h"

String otaStatus = "Listo para buscar actualizaciones";
namespace {
enum class Action { None, Check, Install };
Action pending = Action::None;
bool running = false, candidateValid = false;
uint32_t queuedAt = 0, checkedAt = 0;
emt::OtaCandidate<String> candidate;

void check() {
  candidateValid = false;
  candidate = emt::OtaCandidate<String>{};
  otaStatus = "Buscando actualización...";
  if (findOtaCandidate(candidate)) {
    candidateValid = true;
    checkedAt = millis();
    otaStatus = "Disponible: " + candidate.version;
  }
}

void install() {
  // Recheck after queuing: connectivity or validity may have changed.
  if (!otaCanInstall()) {
    candidateValid = false;
    otaStatus = "No se puede instalar; conecta WiFi y busca de nuevo";
    return;
  }
  candidateValid = false; // Every retry requires a fresh check.
  otaStatus = "Descargando " + candidate.version;
  if (!installOtaCandidate(candidate)) {
    otaStatus = "Actualización fallida; firmware actual conservado. Busca de nuevo para reintentar";
    return;
  }
  otaStatus = "Actualización verificada; reiniciando";
  restartAfterOta();
}
}

bool otaBusy() { return running || pending != Action::None; }
bool otaCanInstall() {
  return candidateValid && wifiConnected && static_cast<uint32_t>(millis() - checkedAt) < 600000;
}
String otaAvailableVersion() { return otaCanInstall() ? candidate.version : String(""); }

bool requestOtaCheck() {
  if (otaBusy() || !wifiConnected) return false;
  pending = Action::Check;
  queuedAt = millis();
  otaStatus = "Búsqueda solicitada";
  return true;
}

bool requestOtaInstall(const String& version) {
  if (otaBusy() || !otaCanInstall() || version != candidate.version) return false;
  pending = Action::Install;
  queuedAt = millis();
  otaStatus = "Descarga e instalación solicitadas";
  return true;
}

bool processManualUpdates() {
  if (pending == Action::None) return false;
  if (static_cast<uint32_t>(millis() - queuedAt) < 500) return true;
  Action action = pending;
  pending = Action::None;
  running = true;
  if (action == Action::Check) check(); else install();
  running = false;
  return true;
}

void checkUpdates() {
  if (otaBusy()) return;
  running = true;
  check();
  if (otaCanInstall()) install();
  running = false;
}
