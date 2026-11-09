#include "globals.h"
#include "firmware_version.h"
#include "ota_policy.h"
#include "secure_client.h"
#include "boot_diagnostics.h"
#include "ota_control.h"
#include <HTTPClient.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <esp_task_wdt.h>
#include <mbedtls/sha256.h>

static bool downloadFirmware(const emt::OtaCandidate<String>& candidate) {
  WiFiClientSecure client;
  HTTPClient http;
  String url=candidate.url;
  int code=0;
  for(int redirects=0; redirects<=3; ++redirects) {
    esp_task_wdt_reset();
    if(!emt::downloadUrl(url.c_str()) || !prepareSecureClient(client) || !http.begin(client,url)) return false;
    http.setConnectTimeout(4000); http.setTimeout(4000); http.useHTTP10(true);
    http.addHeader("User-Agent", "Pantalla-EMT/" FIRMWARE_VERSION);
    code=http.GET();
    if(code==200) break;
    if(code!=301 && code!=302 && code!=303 && code!=307 && code!=308) { http.end(); return false; }
    url=http.getLocation(); http.end();
  }
  if(code!=200 || http.getSize()!=static_cast<int>(candidate.size)) { http.end(); return false; }
  if(!Update.begin(candidate.size,U_FLASH)) { http.end(); return false; }
  struct Writer {
    mbedtls_sha256_context hash;
    Writer() { mbedtls_sha256_init(&hash); }
    ~Writer() { mbedtls_sha256_free(&hash); }
    bool write(uint8_t* data, size_t size) {
      return mbedtls_sha256_update(&hash,data,size)==0 && Update.write(data,size)==size;
    }
  } writer;
  bool ok=mbedtls_sha256_starts(&writer.hash,0)==0;
  auto& stream=http.getStream();
  if(ok) ok=emt::transferImage(stream,writer,candidate.size,
    [](){return static_cast<uint32_t>(millis());}, [](){esp_task_wdt_reset(); delay(1);});
  uint8_t digest[32]; char hex[65];
  if(ok) ok=mbedtls_sha256_finish(&writer.hash,digest)==0;
  if(ok) {
    for(int i=0;i<32;++i) snprintf(hex+2*i,3,"%02x",digest[i]);
    ok=candidate.digest.substring(7)==hex;
  }
  http.end();
  if(!ok) { Update.abort(); return false; }
  esp_task_wdt_reset();
  // Update.end checks image completeness/format and selects the boot partition.
  if(!Update.end(false)) { Update.abort(); return false; }
  return true;
}

bool findOtaCandidate(emt::OtaCandidate<String>& candidate) {
  candidate = emt::OtaCandidate<String>{};
  recordStage(emt::Stage::Ota);
  if(!wifiConnected) { otaStatus="Sin WiFi"; return false; }
  WiFiClientSecure client;
  if(!prepareSecureClient(client)) { otaStatus="TLS pendiente de hora válida o certificados"; return false; }
  const esp_partition_t* partition=esp_ota_get_next_update_partition(nullptr);
  if(!partition) { otaStatus="Sin partición OTA disponible"; return false; }
  HTTPClient http;
  if(!http.begin(client,"https://api.github.com/repos/Karimbelmonte/Pantalla-EMT/releases/latest")) {
    otaStatus="No se pudo iniciar consulta OTA"; return false;
  }
  http.setConnectTimeout(4000); http.setTimeout(4000); http.useHTTP10(true);
  http.addHeader("User-Agent","Pantalla-EMT/" FIRMWARE_VERSION);
  http.addHeader("Accept","application/vnd.github+json");
  int code=http.GET();
  bool valid=false;
  if(code==200) {
    // Ignore release prose/uploader fields to keep the metadata allocation small.
    JsonDocument filter,doc;
    filter["tag_name"]=true; filter["draft"]=true; filter["prerelease"]=true;
    for(const char* key:{"name","state","size","browser_download_url","digest"}) filter["assets"][0][key]=true;
    if(!deserializeJson(doc,http.getStream(),DeserializationOption::Filter(filter)))
      valid=emt::selectRelease(doc,FIRMWARE_VERSION,partition->size,candidate);
  }
  http.end(); esp_task_wdt_reset();
  if(!valid) otaStatus=code==200 ? "Sin versión superior con binario válido" : "Consulta OTA fallida";
  return valid;
}

bool installOtaCandidate(const emt::OtaCandidate<String>& candidate) {
  recordStage(emt::Stage::Ota);
  drawLoadingScreen("Actualizando firmware...");
  if(!downloadFirmware(candidate)) { drawEmtBoard(); return false; }
  return true;
}

void restartAfterOta() {
  recordRestartIntent(emt::RestartIntent::Ota);
  Serial.println(otaStatus); delay(100); ESP.restart();
}
