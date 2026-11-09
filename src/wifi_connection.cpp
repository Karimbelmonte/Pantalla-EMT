#include "globals.h"
#include "wifi_connection.h"
#include "wifi_recovery_policy.h"
#include "boot_diagnostics.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_wifi.h>
#include <esp_task_wdt.h>
#include <atomic>

static WiFiManager manager;
static emt::WifiRecovery recovery;
static emt::HoldButton setupButton;
static emt::WifiCredentials credentials=emt::WifiCredentials::Unknown;
static emt::WifiCredentials credentialsOnBoot=emt::WifiCredentials::Unknown;
static std::atomic<uint32_t> disconnections{0};
static std::atomic<uint16_t> lastDisconnect{0};
static bool manualRequested=false, applicationServerReady=false;
static bool timedPortal=false;
static uint32_t portalDeadline=0;
static void (*showPortal)()=nullptr;

static emt::WifiCredentials readCredentials() {
  wifi_config_t saved{};
  if (esp_wifi_get_config(WIFI_IF_STA,&saved)!=ESP_OK) return emt::WifiCredentials::Unknown;
  bool present=saved.sta.ssid[0]!=0;
  // Do not keep a second password copy or print it in diagnostics.
  memset(&saved,0,sizeof(saved));
  return present ? emt::WifiCredentials::Saved : emt::WifiCredentials::Missing;
}
bool wifiPortalActive() { return manager.getConfigPortalActive(); }
const char* wifiCredentialsAtBoot() {
  switch(credentialsOnBoot) {
    case emt::WifiCredentials::Saved: return "SSID cargado; no se ha borrado por un fallo de conexión";
    case emt::WifiCredentials::Missing: return "No había SSID configurado";
    default: return "No se pudo leer la configuración del controlador";
  }
}
uint32_t wifiRetryInMs() { return recovery.retry.remaining(millis()); }
uint32_t wifiDisconnectCount() { return disconnections.load(); }
uint16_t wifiLastDisconnectReason() { return lastDisconnect.load(); }
void requestWifiSetup() { manualRequested=true; }

static void openPortal(bool manual) {
  recordStage(emt::Stage::WifiPortal);
  if(applicationServerReady) server.stop(); // never bind two HTTP servers to port 80
  WiFi.persistent(true);
  manager.startConfigPortal("Pantalla EMT");
  // Arduino's persistent() flag only affects driver initialization. Select the
  // active driver's storage explicitly after possible STA/AP reinitialization.
  esp_err_t storageResult=esp_wifi_set_storage(WIFI_STORAGE_FLASH);
  if(storageResult!=ESP_OK) Serial.printf("WiFi storage selection failed: %d\n",storageResult);
  timedPortal=manual && credentials!=emt::WifiCredentials::Missing;
  portalDeadline=millis()+180000;
  if(!wifiPortalActive() && applicationServerReady) server.begin();
}
void beginWifi(void (*portalScreen)()) {
  showPortal=portalScreen;
  recordStage(emt::Stage::WifiConnect);
  pinMode(0,INPUT_PULLUP); // ESP32 BOOT: hold after firmware has started, not at reset
  manager.setDebugOutput(false); // WM developer output can include passwords
  manager.setConfigPortalBlocking(false);
  manager.setConfigPortalTimeout(0); // application owns rollover-safe portal deadline
  manager.setConnectTimeout(5);
  manager.setSaveConnectTimeout(5);
  manager.setConnectRetries(1);
  manager.setRestorePersistent(true);
  manager.setShowInfoErase(false);
  manager.setShowInfoUpdate(false);
  manager.setShowPassword(false);
  manager.setWiFiAutoReconnect(true);
  manager.setAPCallback([](WiFiManager*) { if(showPortal) showPortal(); });
  WiFi.onEvent([](arduino_event_id_t event, arduino_event_info_t info) {
    if(event==ARDUINO_EVENT_WIFI_STA_DISCONNECTED) {
      lastDisconnect.store(info.wifi_sta_disconnected.reason);
      disconnections.fetch_add(1);
    }
  });
  WiFi.persistent(true);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(true);
  credentials=credentialsOnBoot=readCredentials();
  Serial.printf("WiFi startup configuration: %s\n",wifiCredentialsAtBoot());
  // Keep future STA/AP initialization persistent; reconnect() does not erase configuration.
  auto action=recovery.step(millis(),credentials,false,false,false);
  if(action==emt::WifiAction::OpenPortal) openPortal(false);
  else if(action==emt::WifiAction::ConnectSaved) WiFi.begin();
  wifiConnected=WiFi.status()==WL_CONNECTED;
}
void startApplicationServer() {
  applicationServerReady=true;
  if(!wifiPortalActive()) server.begin();
}
void wifiTick() {
  recordStage(emt::Stage::WifiConnect);
  bool manual=manualRequested || setupButton.update(millis(),digitalRead(0)==LOW);
  manualRequested=false;
  if(wifiPortalActive()) {
    recordStage(emt::Stage::WifiPortal);
    esp_task_wdt_reset();
    manager.process(); // save-connect is bounded to one 5-second attempt
    esp_task_wdt_reset();
    if(timedPortal && emt::due(millis(),portalDeadline) && wifiPortalActive()) manager.stopConfigPortal();
    if(!wifiPortalActive()) {
      credentials=readCredentials();
      recovery.retry.reset();
      if(applicationServerReady) server.begin();
    }
  } else {
    wifiConnected=WiFi.status()==WL_CONNECTED;
    auto action=recovery.step(millis(),credentials,wifiConnected,false,manual);
    if(action==emt::WifiAction::OpenPortal) openPortal(true);
    else if(action==emt::WifiAction::ConnectSaved) {
      // Reconnect with the driver's existing configuration; never erase it.
      WiFi.reconnect();
    } else if(action==emt::WifiAction::ReadCredentials) {
      WiFi.mode(WIFI_STA);
      credentials=readCredentials();
    }
  }
  wifiConnected=WiFi.status()==WL_CONNECTED;
}
