#include "globals.h"
#include "config_codec.h"
#include <LittleFS.h>

bool configFsReady = false;
String configError;
static const char* configPath = "/config.json";
static const char* tempPath = "/config.tmp";

bool loadConfig(AppConfig& out) {
  out = AppConfig{};
  if (!configFsReady) { configError = "LittleFS no disponible"; return false; }
  if (!LittleFS.exists(configPath)) { configError = "Sin configuración guardada"; return false; }
  File f = LittleFS.open(configPath, "r");
  if (!f || f.size() > 8192) { configError = "No se puede leer configuración"; return false; }
  JsonDocument doc;
  auto error = deserializeJson(doc, f);
  f.close();
  if (error || !emt::decodeConfig(doc, out)) {
    configError = "Configuración inválida; se usan valores iniciales";
    return false;
  }
  configError = "";
  return true;
}

struct ConfigStore {
  bool writeTemp(const String& data) {
    File f = LittleFS.open(tempPath, "w");
    if (!f) return false;
    const size_t written = f.print(data);
    f.flush();
    f.close();
    return written == data.length();
  }
  bool verifyTemp(const String& data) {
    File f = LittleFS.open(tempPath, "r");
    if (!f) return false;
    bool valid = f.size() == data.length() && f.readString() == data;
    f.close();
    return valid;
  }
  bool replace() { return LittleFS.rename(tempPath, configPath); }
  void cleanup() { LittleFS.remove(tempPath); }
};

bool saveConfig(const AppConfig& in) {
  if (!configFsReady) { configError = "LittleFS no disponible"; return false; }
  JsonDocument doc;
  emt::encodeConfig(in, doc);
  String data;
  if (doc.overflowed() || !serializeJson(doc, data)) {
    configError = "No se pudo serializar configuración"; return false;
  }
  ConfigStore store;
  if (!emt::verifiedReplace(store, data)) {
    configError = "No se pudo guardar/verificar configuración"; return false;
  }
  configError = "";
  return true;
}
