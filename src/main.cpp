/*
 * Pantalla EMT (EMT + Weather) + Watchdogs robustos + Deep Sleep configurable
 *
 * Flujo:
 *  1) WiFiManager SOLO para credenciales WiFi.
 *  2) WebServer para configurar:
 *     - StopId, StopName, Brillo
 *     - EMT Email/Password (MobilityLabs Basic)
 *     - OpenWeather API key
 *     - Duración pantalla BUS (s) y WEATHER (s)
 *     - Sleep: enable, startHour, endHour, modo (soft / deep)
 *
 * Persistencia en LittleFS: /config.json
 *
 * Watchdogs:
 *  - Task WDT (ESP32) para colgados reales del loopTask.
 *  - Antigüedad de datos y errores independientes de la salud del bucle.
 *
 * Deep Sleep:
 *  - En ventana de sleep, calcula segundos hasta la hora de fin (endHour:00)
 *    usando la hora local (NTP+TZ) y programa esp_sleep_enable_timer_wakeup().
 *  - Si no hay hora válida (NTP), NO entra en deep sleep (fallback a sleep suave).
 */

#include <Arduino.h>
#include <functional>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <time.h>

#include <SPI.h>
#include <U8g2lib.h>

#include "globals.h"
#include "board_model.h"
#include "stop_detail.h"
#include "web_server.h"
#include "firmware_version.h"
#include "ota_control.h"
#include "secure_client.h"
#include "wifi_connection.h"
#include "boot_diagnostics.h"

// Watchdog (ESP32 Task WDT)
#include "esp_task_wdt.h"
// Deep sleep
#include "esp_sleep.h"

// ---------------------- Watchdogs ----------------------
extern const int WDT_TIMEOUT_S = 15;
unsigned long nextWdtFeedMs = 0;
extern const unsigned long WDT_FEED_PERIOD_MS = 250;

unsigned long lastLoopBeatMs = 0;
unsigned long maxLoopGapMs = 0;

// ---------------------- Pantalla / OLED ----------------------
#define SCREEN_WIDTH 256
#define SCREEN_HEIGHT 64
#define DIMMED_BRIGHTNESS 1

U8G2_SSD1322_NHD_256X64_F_4W_HW_SPI u8g2(
  U8G2_R0,
  /* cs=*/ 26,
  /* dc=*/ 5,
  /* reset=*/ U8X8_PIN_NONE
);

// ---------------------- WebServer ----------------------
WebServer server(80);
const char contentTypeText[] PROGMEM = "text/plain";
const char contentTypeHtml[] PROGMEM = "text/html";

// ---------------------- Tiempo / NTP ----------------------
static const char ntpServer[] PROGMEM = "europe.pool.ntp.org";
static struct tm timeinfo;
char displayedTime[16] = "";
static const char tzSpain[] PROGMEM = "CET-1CEST,M3.5.0/2,M10.5.0/3";

// ---------------------- Config persistente ----------------------


AppConfig cfg;
String resolvedStopName;
bool stopDetailsLoaded = false;
emt::RetrySchedule stopRetry;
const char* displayStopName() {
  return emt::stopDisplayName(cfg.emtStopName.c_str(), resolvedStopName.c_str());
}
static int countEmtGroups();
static bool busSecondThisCycle = false;

// ---------------------- Estado del sistema ----------------------
AppState appState = ST_BOOT;

bool wifiConnected = false;

int brightness = 80;

unsigned long nextClockUpdate = 0;
unsigned long nextDataUpdate  = 0;
unsigned long timer = 0;
unsigned long refreshTimer = 0;

#define DATAUPDATEINTERVAL 30000
#define CLOCKUPDATEINTERVAL 500

// ---------------------- Datos “board” ----------------------
rdStation station;
stnMessages messages;

int lastUpdateResult = UPD_NO_RESPONSE;
unsigned long lastDataLoadTime = 0;
int dataLoadSuccess = 0;
int dataLoadFailure = 0;

// ---------------------- EMT token state ----------------------
String emtAccessToken = "";
unsigned long emtTokenExpiresAtMs = 0;
String emtLastErrorMsg = "";

// ---------------------- Weather state ----------------------
WeatherState weather;
emt::RetrySchedule weatherRetry;
emt::RetrySchedule authRetry;

void resetNetworkRetries() {
  weatherRetry.reset();
  stopRetry.reset();
  authRetry.reset();
  nextDataUpdate = millis();
}

#define WEATHER_UPDATE_INTERVAL_MS (10UL * 60UL * 1000UL)

// ---------------------- UI alternancia ----------------------
UiScreen uiScreen = UI_BUS;

unsigned long uiNextSwitchMs = 0;
uint32_t UI_BUS_MS = 45000UL;
uint32_t UI_WEATHER_MS = 10000UL;
uint32_t UI_BUS2_MS = 0UL;

void uiScheduleNext() {
  uint32_t dur;
  if (uiScreen == UI_BUS) {
    // Tiempo de bus = total - bus2 (para que BUS+BUS2 == uiBusMs)
    busSecondThisCycle = emt::hasSecondPage(countEmtGroups(), UI_BUS2_MS);
    dur = emt::firstPageDuration(UI_BUS_MS, UI_BUS2_MS, busSecondThisCycle);
  } else if (uiScreen == UI_BUS2) {
    dur = UI_BUS2_MS;
  } else { // UI_WEATHER
    dur = UI_WEATHER_MS;
  }
  uiNextSwitchMs = millis() + dur;
}

// ---------------------- Helpers: FS config ----------------------
void applyConfig(const AppConfig& candidate) {
  bool stopChanged = cfg.emtStopId != candidate.emtStopId;
  bool nameChanged = cfg.emtStopName != candidate.emtStopName;
  bool authChanged = cfg.emtEmail != candidate.emtEmail || cfg.emtPassword != candidate.emtPassword;
  bool weatherChanged = cfg.weatherApiKey != candidate.weatherApiKey;
  cfg = candidate;
  UI_BUS_MS = cfg.uiBusMs; UI_WEATHER_MS = cfg.uiWeatherMs; UI_BUS2_MS = cfg.uiBus2Ms;
  brightness = cfg.manualBrightness;
  u8g2.setContrast(brightness);
  if (authChanged) { emtAccessToken = ""; emtTokenExpiresAtMs = 0; }
  if (stopChanged) { clearBoard(); weatherResetOnStopChange(); resolvedStopName = ""; stopDetailsLoaded = false; }
  if (nameChanged || authChanged || weatherChanged) stopDetailsLoaded = false;
  if (weatherChanged) weatherResetOnStopChange();
  if (stopChanged || nameChanged || authChanged || weatherChanged) resetNetworkRetries();
  appState = haveStop(cfg) ? ST_RUNNING : ST_NEED_STOP;
  uiScreen = UI_BUS;
  uiScheduleNext();
  if (appState == ST_RUNNING) drawEmtBoard();
}

bool haveCreds(const AppConfig& c) {
  return !c.emtEmail.isEmpty() && !c.emtPassword.isEmpty();
}
bool haveStop(const AppConfig& c) {
  return !c.emtStopId.isEmpty();
}

// ---------------------- Utilidades de dibujo ----------------------
static inline void blankArea(int x, int y, int w, int h) {
  u8g2.setDrawColor(0);
  u8g2.drawBox(x, y, w, h);
  u8g2.setDrawColor(1);
}

static void drawTruncatedText(const char* message, int x, int y, int maxWidth) {
  char buff[96];
  strncpy(buff, message, sizeof(buff));
  buff[sizeof(buff)-1] = '\0';

  int i = (int)strlen(buff);
  while (u8g2.getStrWidth(buff) > maxWidth && i > 0) {
    buff[--i] = '\0';
  }
  if (i > 0 && i < (int)sizeof(buff) - 4) {
    strncat(buff, "...", sizeof(buff) - strlen(buff) - 1);
  }
  u8g2.drawStr(x, y, buff);
}

static void centreText(const char* message, int y) {
  int w = u8g2.getStrWidth(message);
  int x = (w <= SCREEN_WIDTH) ? (SCREEN_WIDTH - w) / 2 : 0;
  if (w <= SCREEN_WIDTH) u8g2.drawStr(x, y, message);
  else drawTruncatedText(message, 0, y, SCREEN_WIDTH);
}

static void drawStatusBarIcons() {
  u8g2.setFont(u8g2_font_6x10_tf);
  u8g2.drawStr(0, 63, wifiConnected ? "WiFi" : "NO");
}

// ---------------------- Pantallas básicas ----------------------
static void drawStartupScreen(const char* msg) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x13_tf);
  centreText("Pantalla EMT", 0);
  u8g2.setFont(u8g2_font_6x10_tf);
  centreText(msg, 18);
  u8g2.sendBuffer();

}

static void drawSetupScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x13_tf);
  centreText("Configurar WiFi", 0);
  u8g2.setFont(u8g2_font_6x10_tf);
  centreText("Conecta a la red:", 16);
  centreText("\"Pantalla EMT\"", 28);
  centreText("y abre 192.168.4.1", 40);
  u8g2.sendBuffer();

}

static void drawNoTimeScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x13_tf);
  centreText("Sin hora NTP", 0);
  u8g2.setFont(u8g2_font_6x10_tf);
  centreText("Revisa WiFi / NTP", 18);
  u8g2.sendBuffer();

}

static void drawNeedStopScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x13_tf);
  centreText("Configurar en web", 0);
  u8g2.setFont(u8g2_font_6x10_tf);
  centreText("Abre:", 18);
  centreText("pantalla-emt.local", 30);
  String ip = WiFi.localIP().toString();
  centreText(ip.c_str(), 42);
  drawStatusBarIcons();
  u8g2.sendBuffer();

}

void drawLoadingScreen(const char* msg) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x13_tf);
  centreText("Pantalla EMT", 0);
  u8g2.setFont(u8g2_font_6x10_tf);
  centreText("Cargando...", 18);
  if (msg && msg[0] != '\0') centreText(msg, 32);
  drawStatusBarIcons();
  u8g2.sendBuffer();

}

// ---------------------- Reloj ----------------------
static void drawClock(bool updateRegion) {
  if (updateRegion && !emt::due(millis(), nextClockUpdate)) return;
  nextClockUpdate = millis() + CLOCKUPDATEINTERVAL;

  if (!getLocalTime(&timeinfo, 0)) return;

  char sysTime[16];
  snprintf(sysTime, sizeof(sysTime), "%02d:%02d:%02d",
           timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

  if (updateRegion && strcmp(displayedTime, sysTime) == 0) return;
  strcpy(displayedTime, sysTime);

  u8g2.setFont(u8g2_font_6x10_tf);
  int w = u8g2.getStrWidth(sysTime);
  blankArea(SCREEN_WIDTH - w - 2, 0, w + 2, 10);
  u8g2.drawStr(SCREEN_WIDTH - w, 0, sysTime);

  if (updateRegion) u8g2.updateDisplayArea(0, 0, 32, 2);

}

// ---------------------- Sleep helpers ----------------------
static bool isInSleepWindow(uint8_t h, uint8_t startH, uint8_t endH) {
  if (startH == endH) return true; // 24h sleep (edge)
  if (startH > endH) {
    // cruza medianoche (ej 23->06)
    return (h >= startH) || (h < endH);
  }
  // mismo día (ej 01->06)
  return (h >= startH) && (h < endH);
}

static bool isSnoozing() {
  if (!cfg.sleepEnabled) return false;
  if (!getLocalTime(&timeinfo, 0)) return false;
  uint8_t h = (uint8_t)timeinfo.tm_hour;
  return isInSleepWindow(h, cfg.sleepStarts, cfg.sleepEnds);
}

static void drawSleepingScreenSoft() {
  u8g2.setContrast(DIMMED_BRIGHTNESS);
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x13_tf);

  if (getLocalTime(&timeinfo, 0)) {
    char t[8];
    snprintf(t, sizeof(t), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    centreText(t, 24);
  } else {
    centreText("Sleep", 24);
  }

  u8g2.setFont(u8g2_font_6x10_tf);
  centreText("Modo sleep", 44);

  u8g2.sendBuffer();

}

static bool computeSecondsUntilWake(uint32_t& outSeconds) {
  // Necesitamos hora válida
  if (!getLocalTime(&timeinfo, 0)) return false;

  // "now" local
  struct tm nowTm = timeinfo;
  time_t nowT = mktime(&nowTm);
  if (nowT == (time_t)-1) return false;

  // wake = hoy a cfg.sleepEnds:00:05 (5s margen)
  struct tm wakeTm = nowTm;
  wakeTm.tm_hour = cfg.sleepEnds;
  wakeTm.tm_min = 0;
  wakeTm.tm_sec = 5;
  wakeTm.tm_isdst = -1;

  time_t wakeT = mktime(&wakeTm);
  if (wakeT == (time_t)-1) return false;

  // Si ya hemos pasado esa hora, mover a mañana
  if (difftime(wakeT, nowT) <= 0) {
    wakeTm.tm_mday += 1;
    wakeTm.tm_isdst = -1;
    wakeT = mktime(&wakeTm);
    if (wakeT == (time_t)-1) return false;
  }

  double diff = difftime(wakeT, nowT);
  if (diff < 10) diff = 10; // mínimo
  if (diff > (double)(7UL * 24UL * 3600UL)) diff = (double)(7UL * 24UL * 3600UL); // cap 7 días

  outSeconds = (uint32_t)diff;
  return true;
}

void enterDeepSleepUntilWake() {
  uint32_t secs = 0;
  if (!computeSecondsUntilWake(secs)) {
    // No tenemos hora -> NO deep sleep (fallback soft)
    drawNoTimeScreen();
    return;
  }

  recordStage(emt::Stage::Sleep);
  recordRestartIntent(emt::RestartIntent::Sleep);
  // Apagar la pantalla OLED del todo (modo ahorro de energía)
  // En lugar de borrarla y dejarla encendida como antes.
  u8g2.setPowerSave(1);

  // Para ahorrar, apagamos WiFi y servidor
  server.stop();
  WiFi.disconnect(false);
  WiFi.mode(WIFI_MODE_NULL);

  // Desactiva WDT para no tener comportamientos raros en el paso a sleep
  esp_task_wdt_delete(NULL);
  esp_task_wdt_deinit();

  // Programar wakeup por timer RTC
  esp_sleep_enable_timer_wakeup((uint64_t)secs * 1000000ULL);

  // (Opcional) Guarda motivo para debug
  // esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);

  // Go
  esp_deep_sleep_start();
}

// ---------------------- HTTP helpers ----------------------
// Shared client; each request validates TLS and closes its connection.
WiFiClientSecure globalSecureClient;

static bool httpGetJsonStream(const String& url,
                              const std::function<void(HTTPClient&)>& addHeaders,
                              std::function<void(Stream&)> parseStream,
                              int& outHttpCode) {
  esp_task_wdt_reset();
  if (!prepareSecureClient(globalSecureClient)) { outHttpCode = -1000; return false; }

  HTTPClient http;
  // Use the global client instead of creating a new one every time
  if (!http.begin(globalSecureClient, url)) {
    outHttpCode = -1;
    return false;
  }

  http.setConnectTimeout(4000);
  http.setTimeout(4000);
  http.useHTTP10(true);

  if (addHeaders) addHeaders(http);

  outHttpCode = http.GET();
  if (outHttpCode >= 200 && outHttpCode < 300) {
    // Pass the stream directly to the JSON parser (saves heavy RAM usage)
    parseStream(http.getStream());
  }

  http.end();
  esp_task_wdt_reset();
  return (outHttpCode > 0);
}

static bool httpPostJsonStream(const String& url, const String& payload,
                               const std::function<void(HTTPClient&)>& addHeaders,
                               std::function<void(Stream&)> parseStream,
                               int& outHttpCode) {
  esp_task_wdt_reset();
  if (!prepareSecureClient(globalSecureClient)) { outHttpCode = -1000; return false; }

  HTTPClient http;
  if (!http.begin(globalSecureClient, url)) {
    outHttpCode = -1;
    return false;
  }

  http.setConnectTimeout(4000);
  http.setTimeout(4000);
  http.useHTTP10(true);

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  if (addHeaders) addHeaders(http);

  outHttpCode = http.POST(payload);
  if (outHttpCode >= 200 && outHttpCode < 300) {
    parseStream(http.getStream());
  }

  http.end();
  esp_task_wdt_reset();
  return (outHttpCode > 0);
}

// ---------------------- EMT Auth BASIC ----------------------
static bool emtTokenValid() {
  if (emtAccessToken.isEmpty()) return false;
  if (emtTokenExpiresAtMs == 0) return false;
  int32_t remaining = static_cast<int32_t>(static_cast<uint32_t>(emtTokenExpiresAtMs - millis()));
  return remaining > 30000;
}

static bool emtLoginBasicRequest(const String& email, const String& password) {
  recordStage(emt::Stage::EmtLogin);
  esp_task_wdt_reset();

  emtLastErrorMsg = "";
  const String url = "https://openapi.emtmadrid.es/v1/mobilitylabs/user/login/";

  int httpCode = 0;
  bool jsonError = false;
  String scode, sdesc, stoken;
  long tokenSecExpiration = 0;

  bool ok = httpGetJsonStream(url,
    [&](HTTPClient& http) {
      http.addHeader("email", email);
      http.addHeader("password", password);
      http.addHeader("Accept", "application/json");
    },
    [&](Stream& stream) {
      JsonDocument doc;
      auto err = deserializeJson(doc, stream);
      if (err) {
        jsonError = true;
        return;
      }

      scode = String(doc["code"] | "");
      sdesc = String(doc["description"] | "");
      stoken = String(doc["data"][0]["accessToken"] | "");
      tokenSecExpiration = doc["data"][0]["tokenSecExpiration"] | 0;
    },
    httpCode
  );

  if (!ok || httpCode < 200 || httpCode >= 300) {
    emtLastErrorMsg = "Login HTTP " + String(httpCode);
    return false;
  }

  if (jsonError) {
    emtLastErrorMsg = "Login JSON parse error";
    return false;
  }

  if (!(scode == "00" || scode == "01")) {
    emtLastErrorMsg = "Login fail " + scode + " " + sdesc;
    return false;
  }

  if (stoken.isEmpty()) {
    emtLastErrorMsg = "Login no token " + scode + " " + sdesc;
    return false;
  }

  emtAccessToken = stoken;

  tokenSecExpiration = emt::bounded<long>(tokenSecExpiration, 120, 86400);
  unsigned long ttlMs = (unsigned long)(tokenSecExpiration - 60) * 1000UL;
  emtTokenExpiresAtMs = millis() + ttlMs;

  return true;
}

static bool emtLoginBasic(const String& email, const String& password) {
  if (!authRetry.ready(millis())) return false;
  if (!emtLoginBasicRequest(email, password)) {
    authRetry.fail(millis());
    return false;
  }
  authRetry.reset();
  return true;
}

// ---------------------- Board reset / messages ----------------------
void clearBoard() {
  station.numServices = 0;
  station.platformAvailable = false;
  station.boardChanged = true;
  station.calling[0] = '\0';
  station.origin[0] = '\0';
  station.serviceMessage[0] = '\0';
  station.location[0] = '\0';

  messages.numMessages = 0;
  for (int i = 0; i < MAXBOARDMESSAGES; i++) messages.messages[i][0] = '\0';

  for (int i = 0; i < MAXBOARDSERVICES; i++) {
    station.service[i].sTime[0] = '\0';
    station.service[i].destination[0] = '\0';
    station.service[i].via[0] = '\0';
    station.service[i].etd[0] = '\0';
    station.service[i].platform[0] = '\0';
    station.service[i].isCancelled = false;
    station.service[i].isDelayed = false;
    station.service[i].trainLength = 0;
    station.service[i].classesAvailable = 0;
    station.service[i].opco[0] = '\0';
    station.service[i].serviceType = BUS;
    station.service[i].timeToStation = 0;
    station.service[i].receivedAtMs = 0;
  }
}

static void setMessage(const String& m) {
  station.numServices = 0;
  messages.numMessages = 0;

  if (!m.isEmpty()) {
    strncpy(messages.messages[0], m.c_str(), MAXMESSAGESIZE - 1);
    messages.messages[0][MAXMESSAGESIZE - 1] = '\0';
    messages.numMessages = 1;
  }
}

// ---------------------- EMT: Stop detail coords ----------------------
static bool emtFetchStopDetail() {
  if (!emtTokenValid() && !emtLoginBasic(cfg.emtEmail, cfg.emtPassword)) return false;
  recordStage(emt::Stage::EmtDetail);
  String url = "https://openapi.emtmadrid.es/v1/transport/busemtmad/stops/" + cfg.emtStopId + "/detail/";
  int httpCode = 0;
  bool parsed = false;
  emt::StopDetail detail;
  String name;
  bool ok = httpGetJsonStream(url, [&](HTTPClient& http) {
    http.addHeader("accessToken", emtAccessToken);
    http.addHeader("Accept", "application/json");
  }, [&](Stream& stream) {
    JsonDocument doc;
    if (!deserializeJson(doc, stream)) {
      parsed = emt::parseStopDetail(doc, detail);
      if (parsed) name = detail.name;
      if (String(doc["code"] | "") == "80") emtAccessToken = "";
    }
  }, httpCode);
  if (httpCode == 401 || httpCode == 403) emtAccessToken = "";
  if (!ok || httpCode != 200 || !parsed) return false;
  resolvedStopName = name;
  stopDetailsLoaded = emt::stopDetailComplete(cfg.emtStopName.isEmpty(), !cfg.weatherApiKey.isEmpty(), name.c_str(), detail.hasCoords);
  weather.hasCoords = detail.hasCoords;
  weather.lon = detail.longitude; weather.lat = detail.latitude;
  return stopDetailsLoaded;
}

static void stopDetailsTick() {
  if (!wifiConnected || !haveCreds(cfg) || !haveStop(cfg) || stopDetailsLoaded) return;
  if (!cfg.emtStopName.isEmpty() && cfg.weatherApiKey.isEmpty()) return;
  if (!stopRetry.ready(millis())) return;
  if (emtFetchStopDetail()) stopRetry.reset();
  else { stopRetry.fail(millis()); weather.lastError = "No se pudo obtener detalle de parada EMT"; }
}

// ---------------------- EMT Arrivals (v1) ----------------------
static bool emtFetchArrivalsV1(const String& stopId) {
  esp_task_wdt_reset();
  if (!emtTokenValid()) {
    if (!emtLoginBasic(cfg.emtEmail, cfg.emtPassword)) return false;
  }
  emtLastErrorMsg = "";

  const String url = "https://openapi.emtmadrid.es/v1/transport/busemtmad/stops/" + stopId + "/arrives/";

  String payload = "{";
  payload += "\"cultureInfo\":\"ES\",";
  payload += "\"stopId\":\"" + stopId + "\",";
  payload += "\"Text_StopRequired_YN\":\"Y\",";
  payload += "\"Text_EstimationsRequired_YN\":\"Y\"";
  payload += "}";

  int httpCode = 0;
  bool jsonError = false;
  String scode, sdesc;
  int servicesFound = -1;

  auto parseArrivals = [&](Stream& stream) {
    jsonError = false;
    scode = "";
    servicesFound = -1;
    JsonDocument doc;
    auto err = deserializeJson(doc, stream);
    if (err) {
      jsonError = true;
      return;
    }

    scode = String(doc["code"] | "");
    sdesc = String(doc["description"] | "");

    if (scode == "00" || scode == "01") {
      clearBoard();
      strncpy(station.location, displayStopName(), MAXLOCATIONSIZE - 1);
      station.location[MAXLOCATIONSIZE - 1] = '\0';

      JsonArray arr = doc["data"][0]["Arrive"].as<JsonArray>();
      if (arr.isNull() || arr.size() == 0) {
        servicesFound = 0;
        return;
      }

      int n = 0;
      for (JsonObject a : arr) {
        if (n >= MAXBOARDSERVICES) break;

        rdService& s = station.service[n];

        const char* line = a["line"] | "";
        const char* destination = a["destination"] | "";
        int estimateArrive = a["estimateArrive"] | 0;

        strncpy(s.destination, destination, MAXLOCATIONSIZE - 1);
        s.destination[MAXLOCATIONSIZE - 1] = '\0';

        strncpy(s.via, line, MAXLOCATIONSIZE - 1);
        s.via[MAXLOCATIONSIZE - 1] = '\0';

        s.timeToStation = estimateArrive;
        s.receivedAtMs = millis();
        s.serviceType = BUS;

        int deviation = a["deviation"] | 0;
        s.isDelayed = (deviation > 0);

        n++;
        yield();
      }

      servicesFound = n;
    }
  };

  recordStage(emt::Stage::EmtArrivals);
  bool ok = httpPostJsonStream(url, payload,
    [&](HTTPClient& http) {
      http.addHeader("accessToken", emtAccessToken);
    },
    parseArrivals,
    httpCode
  );

  if (!ok) {
    emtLastErrorMsg = "Arrivals HTTP fail";
    return false;
  }

  // Si código es 80, o error HTTP de auth, intentamos login y reintento
  if (httpCode == 401 || httpCode == 403 || scode == "80") {
    emtAccessToken = "";
    emtTokenExpiresAtMs = 0;

    if (!emtLoginBasic(cfg.emtEmail, cfg.emtPassword)) {
      emtLastErrorMsg = "Auth retry fail (" + scode + ")";
      return false;
    }

    recordStage(emt::Stage::EmtArrivals);
    ok = httpPostJsonStream(url, payload,
      [&](HTTPClient& http) {
        http.addHeader("accessToken", emtAccessToken);
      },
      parseArrivals,
      httpCode
    );

    if (!ok || (httpCode < 200 || httpCode >= 300)) {
      emtLastErrorMsg = "Arrivals retry HTTP " + String(httpCode);
      return false;
    }
  }

  if (httpCode < 200 || httpCode >= 300) {
    emtLastErrorMsg = "Arrivals HTTP " + String(httpCode);
    return false;
  }

  if (jsonError) {
    emtLastErrorMsg = "Arrivals JSON parse";
    return false;
  }

  if (scode == "00" || scode == "01") {
    if (servicesFound <= 0) {
      setMessage("Sin estimaciones");
    } else {
      station.numServices = servicesFound;
    }

    return true;
  }

  emtLastErrorMsg = "Arrive code " + scode + " " + sdesc;
  setMessage("Error EMT " + scode);
  return false;
}

bool getEmtBoard() {
  if (!haveCreds(cfg) || !haveStop(cfg)) return false;

  bool hadSecondPage = emt::hasSecondPage(countEmtGroups(), UI_BUS2_MS);
  bool ok = emtFetchArrivalsV1(cfg.emtStopId);
  if (hadSecondPage != emt::hasSecondPage(countEmtGroups(), UI_BUS2_MS)) { uiScreen = UI_BUS; uiScheduleNext(); }
  nextDataUpdate = millis() + DATAUPDATEINTERVAL;

  if (ok) {
    lastDataLoadTime = millis();
    dataLoadSuccess++;
    lastUpdateResult = UPD_SUCCESS;

    return true;
  } else {
    dataLoadFailure++;
    lastUpdateResult = UPD_DATA_ERROR;
    return false;
  }
}

// ---------------------- Weather fetch (OpenWeather current) ----------------------
static bool weatherFetchCurrent(double lat, double lon) {
  recordStage(emt::Stage::Weather);
  esp_task_wdt_reset();
  weather.lastError = "";

  if (cfg.weatherApiKey.isEmpty()) {
    weather.lastError = "No Weather API key";
    return false;
  }

  String url = "https://api.openweathermap.org/data/2.5/weather?lat=" + String(lat, 6) +
               "&lon=" + String(lon, 6) +
               "&appid=" + cfg.weatherApiKey +
               "&units=metric&lang=es";

  WeatherState candidate = weather;
  int httpCode = 0;
  bool jsonError = false;

  bool ok = httpGetJsonStream(url,
    [&](HTTPClient& http) {
      http.addHeader("Accept", "application/json");
    },
    [&](Stream& stream) {
      JsonDocument doc;
      if (deserializeJson(doc, stream)) {
        jsonError = true;
        return;
      }

      if (!doc["main"]["temp"].is<double>()) { jsonError = true; return; }
      candidate.tempC  = doc["main"]["temp"] | NAN;
      candidate.feelsC = doc["main"]["feels_like"] | NAN;
      candidate.minC   = doc["main"]["temp_min"] | NAN;
      candidate.maxC   = doc["main"]["temp_max"] | NAN;

      const char* d = doc["weather"][0]["description"] | "";
      const char* i = doc["weather"][0]["icon"] | "";
      strncpy(candidate.desc, d, sizeof(candidate.desc) - 1);
      candidate.desc[sizeof(candidate.desc) - 1] = '\0';
      strncpy(candidate.icon, i, sizeof(candidate.icon) - 1);
      candidate.icon[sizeof(candidate.icon) - 1] = '\0';

      candidate.sunrise = doc["sys"]["sunrise"] | 0;
      candidate.sunset  = doc["sys"]["sunset"]  | 0;
    },
    httpCode
  );

  if (!ok || httpCode < 200 || httpCode >= 300) {
    weather.lastError = "OW HTTP " + String(httpCode);
    return false;
  }

  if (jsonError) {
    weather.lastError = "OW JSON parse";
    return false;
  }

  weather = candidate;
  weather.hasData = true;
  weather.lastFetchMs = millis();

  return true;
}

void weatherResetOnStopChange() {
  weatherRetry.reset();
  weather.hasCoords = false;
  weather.hasData = false;
  weather.lastError = "";
  weather.lastFetchMs = 0;
  weather.lat = 0;
  weather.lon = 0;
  weather.desc[0] = '\0';
  weather.icon[0] = '\0';
  weather.sunrise = weather.sunset = 0;
}

void weatherTick() {
  stopDetailsTick(); // Name resolution also works without OpenWeather.
  bool enabled = wifiConnected && !cfg.weatherApiKey.isEmpty() && haveStop(cfg);
  emt::weatherAttempt(weatherRetry, millis(), enabled, []() {
    if (!weather.hasCoords) { weather.lastError = "Sin coordenadas de parada EMT"; return false; }
    return true;
  }, []() { return weatherFetchCurrent(weather.lat, weather.lon); },
  []() { return static_cast<uint32_t>(millis()); });
}

// ---------------------- Iconos Weather (bitmaps 16x16) ----------------------
static const unsigned char icon_sun_16[] PROGMEM = {
  0x00,0x00,0x10,0x08,0x10,0x08,0x00,0x00,
  0x80,0x01,0x2c,0x1a,0x2c,0x1a,0x80,0x01,
  0x80,0x01,0x2c,0x1a,0x2c,0x1a,0x80,0x01,
  0x00,0x00,0x10,0x08,0x10,0x08,0x00,0x00
};
static const unsigned char icon_cloud_16[] PROGMEM = {
  0x00,0x00,0x00,0x00,0x00,0x00,0x80,0x01,
  0xc0,0x03,0xe0,0x07,0xf0,0x0f,0xf8,0x1f,
  0xfc,0x3f,0xfe,0x7f,0xfe,0x7f,0xfc,0x3f,
  0xf8,0x1f,0xf0,0x0f,0x00,0x00,0x00,0x00
};
static const unsigned char icon_partly_16[] PROGMEM = {
  0x00,0x00,0x10,0x08,0x10,0x08,0x00,0x00,
  0x80,0x01,0x2c,0x1a,0x2c,0x1a,0x80,0x01,
  0x80,0x01,0x2c,0x1a,0x2c,0x1a,0x80,0x01,
  0x00,0x00,0x00,0x00,0xe0,0x07,0xf0,0x0f
};
static const unsigned char icon_rain_16[] PROGMEM = {
  0x00,0x00,0x80,0x01,0xc0,0x03,0xe0,0x07,
  0xf0,0x0f,0xf8,0x1f,0xfc,0x3f,0xfe,0x7f,
  0xfe,0x7f,0xfc,0x3f,0x98,0x19,0x24,0x24,
  0x48,0x12,0x90,0x09,0x00,0x00,0x00,0x00
};
static const unsigned char icon_storm_16[] PROGMEM = {
  0x00,0x00,0x80,0x01,0xc0,0x03,0xe0,0x07,
  0xf0,0x0f,0xf8,0x1f,0xfc,0x3f,0xfe,0x7f,
  0x3e,0x7c,0x0c,0x30,0x18,0x18,0x30,0x0c,
  0x60,0x06,0x30,0x0c,0x00,0x00,0x00,0x00
};
static const unsigned char icon_snow_16[] PROGMEM = {
  0x00,0x00,0x10,0x08,0x92,0x49,0x54,0x2a,
  0x38,0x1c,0x7c,0x3e,0x38,0x1c,0x54,0x2a,
  0x92,0x49,0x10,0x08,0x00,0x00,0x10,0x08,
  0x92,0x49,0x54,0x2a,0x00,0x00,0x00,0x00
};
static const unsigned char icon_fog_16[] PROGMEM = {
  0x00,0x00,0xfe,0x7f,0x00,0x00,0xfc,0x3f,
  0x00,0x00,0xf8,0x1f,0x00,0x00,0xfc,0x3f,
  0x00,0x00,0xfe,0x7f,0x00,0x00,0xfc,0x3f,
  0x00,0x00,0xf8,0x1f,0x00,0x00,0x00,0x00
};

static const unsigned char* weatherIconBitmap16(const char* owIcon) {
  if (!owIcon || owIcon[0] == '\0') return icon_cloud_16;
  char a = owIcon[0];
  char b = owIcon[1];
  if (a == '0' && b == '1') return icon_sun_16;
  if (a == '0' && b == '2') return icon_partly_16;
  if (a == '0' && (b == '3' || b == '4')) return icon_cloud_16;
  if (a == '0' && (b == '9' || b == '0')) return icon_rain_16;
  if (a == '1' && b == '1') return icon_storm_16;
  if (a == '1' && b == '3') return icon_snow_16;
  if (a == '5' && b == '0') return icon_fog_16;
  return icon_cloud_16;
}

// ---------------------- Dibujo EMT ----------------------
static void drawEmtLine(const char* line, const char* dest, const int* arrivals, int count, int y) {
  u8g2.setFont(u8g2_font_6x10_tf);
  blankArea(0, y, SCREEN_WIDTH, 12);

  char left[96];
  if (line && line[0] != '\0') snprintf(left, sizeof(left), "%s %s", line, dest);
  else snprintf(left, sizeof(left), "%s", dest);

  String right = "";
  for (int i = 0; i < count; i++) {
    if (i > 0) right += " "; // Removed the comma
    if (arrivals[i] <= 60) right += "Due";
    else right += String((arrivals[i] + 30) / 60) + " min";
  }

  int wRight = u8g2.getStrWidth(right.c_str());
  int maxLeftWidth = SCREEN_WIDTH - wRight - 6;

  if (u8g2.getStrWidth(left) <= maxLeftWidth) u8g2.drawStr(0, y, left);
  else drawTruncatedText(left, 0, y, maxLeftWidth);

  u8g2.drawStr(SCREEN_WIDTH - wRight, y, right.c_str());
}

static int countEmtGroups() {
  emt::BoardRow rows[MAXBOARDSERVICES];
  return emt::buildRows(station.service, station.numServices, cfg.combineArrivals,
                        millis(), rows, MAXBOARDSERVICES);
}

static void drawEmtPage(int page) {
  u8g2.clearBuffer();
  u8g2.setContrast(brightness);
  u8g2.setFont(u8g2_font_6x13_tf);
  centreText(displayStopName(), 0);
  drawClock(false);
  u8g2.setFont(u8g2_font_6x10_tf);
  if (!haveStop(cfg) || !haveCreds(cfg)) {
    centreText(!haveStop(cfg) ? "Falta StopId" : "Faltan credenciales EMT", 28);
    centreText("Configura en web (/)", 40);
  } else if (station.numServices == 0) {
    centreText(!wifiConnected ? "WiFi: reintentando conexion" : (messages.numMessages > 0 ? messages.messages[0] : "Sin estimaciones"), 18);
    if (!wifiConnected) centreText("BOOT 3s: configurar WiFi", 36);
  } else {
    emt::BoardRow rows[MAXBOARDSERVICES];
    int count = emt::buildRows(station.service, station.numServices, cfg.combineArrivals,
                              millis(), rows, MAXBOARDSERVICES);
    for (int index = page*3; index < count && index < (page+1)*3; ++index) {
      const auto& row = rows[index];
      drawEmtLine(row.line, row.destination, row.arrivals, row.count, 18+(index%3)*14);
    }
  }
  drawStatusBarIcons(); u8g2.sendBuffer();
}
void drawEmtBoard() { drawEmtPage(0); }
void drawEmtBoard2() { drawEmtPage(1); }

// ---------------------- Dibujo Weather ----------------------
void drawWeatherScreen() {
  u8g2.clearBuffer();
  u8g2.setContrast(brightness);

  u8g2.setFont(u8g2_font_6x13_tf);
  centreText("Tiempo", 0);
  drawClock(false);

  u8g2.setFont(u8g2_font_6x10_tf);

  if (cfg.weatherApiKey.isEmpty()) {
    centreText("Falta OpenWeather key", 22);
    centreText("Configura en web (/)", 34);
    drawStatusBarIcons();
    u8g2.sendBuffer();

    return;
  }

  if (!weather.hasData) {
    if (!weather.lastError.isEmpty()) {
      centreText("Weather error:", 22);
      drawTruncatedText(weather.lastError.c_str(), 0, 34, SCREEN_WIDTH);
    } else {
      centreText("Cargando tiempo...", 28);
    }
    drawStatusBarIcons();
    u8g2.sendBuffer();

    return;
  }

  const unsigned char* bmp = weatherIconBitmap16(weather.icon);
  u8g2.drawXBMP(0, 14, 16, 16, bmp);

  char descLine[72];
  strncpy(descLine, weather.desc, sizeof(descLine) - 1);
  descLine[sizeof(descLine) - 1] = '\0';
  drawTruncatedText(descLine, 20, 18, SCREEN_WIDTH - 22);

  u8g2.setFont(u8g2_font_6x13_tf);
  int t  = isnan(weather.tempC)  ? 0 : (int)lround(weather.tempC);
  int tf = isnan(weather.feelsC) ? 0 : (int)lround(weather.feelsC);

  blankArea(0, 30, SCREEN_WIDTH, 14);
  u8g2.drawStr(0, 30, "Ahora:");
  {
    char bufT[12];
    snprintf(bufT, sizeof(bufT), " %d\xB0", t);
    u8g2.drawStr(38, 30, bufT);

    u8g2.drawStr(86, 30, "Sens:");
    char bufF[12];
    snprintf(bufF, sizeof(bufF), " %d\xB0", tf);
    u8g2.drawStr(120, 30, bufF);
  }

  u8g2.setFont(u8g2_font_6x10_tf);
  int tmin = isnan(weather.minC) ? 0 : (int)lround(weather.minC);
  int tmax = isnan(weather.maxC) ? 0 : (int)lround(weather.maxC);

  char line3[64];
  snprintf(line3, sizeof(line3), "Min: %d\xB0        Max: %d\xB0", tmin, tmax);
  blankArea(0, 46, SCREEN_WIDTH, 12);
  u8g2.drawStr(0, 46, line3);

  drawStatusBarIcons();
  u8g2.sendBuffer();

}

static void updateMyMdns(const char* hostname) {
  if (MDNS.begin(hostname)) MDNS.addService("http", "tcp", 80);
}

// ---------------------- Setup / Loop ----------------------
void setup() {
  Serial.begin(115200);
  beginBootDiagnostics();
  // OLED init
  u8g2.begin();
  u8g2.setContrast(brightness);
  u8g2.setDrawColor(1);
  u8g2.setFontMode(1);
  u8g2.setFontRefHeightAll();
  u8g2.setFontPosTop();

  drawStartupScreen("Inicializando...");

  configFsReady = LittleFS.begin(false);
  if (!configFsReady) {
    drawStartupScreen("LittleFS ERROR");
    delay(1500);
  }

  (void)loadConfig(cfg);
  UI_BUS_MS = cfg.uiBusMs;
  UI_WEATHER_MS = cfg.uiWeatherMs;
  UI_BUS2_MS = cfg.uiBus2Ms;
  brightness = cfg.manualBrightness;
  u8g2.setContrast(brightness);

  clearBoard();
  weatherResetOnStopChange();

  drawStartupScreen("Conectando WiFi guardado...");
  beginWifi(drawSetupScreen);
  if (wifiConnected) updateMyMdns("pantalla-emt");

  // NTP + TZ España
  configTime(0, 0, ntpServer);
  setenv("TZ", tzSpain, 1);
  tzset();

  int tries = 0;
  while (wifiConnected && !getLocalTime(&timeinfo, 0) && tries++ < 12) delay(400);
  if (tries >= 12 && !wifiPortalActive()) drawNoTimeScreen();

  // Routes
  setupWebServer();
  startApplicationServer();

  // Init Task WDT (vigila loopTask)
  esp_task_wdt_config_t twdt_config = {
    .timeout_ms = WDT_TIMEOUT_S * 1000,
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  esp_err_t watchdogResult = esp_task_wdt_init(&twdt_config);
  if (watchdogResult == ESP_ERR_INVALID_STATE) watchdogResult = esp_task_wdt_reconfigure(&twdt_config);
  if (watchdogResult != ESP_OK) Serial.printf("Task WDT setup error: %d\n", watchdogResult);
  if (esp_task_wdt_status(NULL) != ESP_OK) {
    watchdogResult = esp_task_wdt_add(NULL);
    if (watchdogResult != ESP_OK) Serial.printf("Task WDT registration error: %d\n", watchdogResult);
  }

  uiScreen = UI_BUS;
  uiScheduleNext();

  if (!haveStop(cfg)) {
    appState = ST_NEED_STOP;
    if (!wifiPortalActive()) drawNeedStopScreen();
  } else {
    appState = ST_RUNNING;
    if (!wifiPortalActive()) drawStartupScreen(wifiConnected ? "Listo" : "WiFi: reintentando sin borrar");
    delay(300);

    nextDataUpdate = millis();
    nextClockUpdate = millis();

    if (!wifiPortalActive()) { weatherTick(); drawEmtBoard(); }
  }

  lastLoopBeatMs = millis();
  nextWdtFeedMs = millis();
}

void loop() {
  // --- Task WDT feed ---
  if ((long)(millis() - nextWdtFeedMs) >= 0) {
    esp_task_wdt_reset();
    nextWdtFeedMs = millis() + WDT_FEED_PERIOD_MS;
  }

  unsigned long gap = millis() - lastLoopBeatMs;
  if (gap > maxLoopGapMs) maxLoopGapMs = gap;
  lastLoopBeatMs = millis();

  bool wasConnected = wifiConnected;
  wifiTick();
  if (!wasConnected && wifiConnected) {
    updateMyMdns("pantalla-emt");
    configTime(0, 0, ntpServer);
    setenv("TZ", tzSpain, 1);
    tzset();
    resetNetworkRetries();
  }
  if (wifiPortalActive()) { delay(5); return; }
  recordStage(emt::Stage::Web);
  server.handleClient();
  recordStage(emt::Stage::Idle);

  static bool otaChecked = false;
  // Manual updates also work before configuring a stop or during soft sleep.
  // Defer other work/deep sleep until the queued response has been sent.
  if (processManualUpdates()) {
    otaChecked = true;
    delay(1);
    return;
  }

  if (appState == ST_NEED_STOP) {
    if (emt::due(millis(), timer)) {
      drawNeedStopScreen();
      timer = millis() + 4000;
    }
    delay(1);
    return;
  }

  // -------- Sleep logic (soft/deep) --------
  bool snoozing = isSnoozing();
  if (snoozing) {
    recordStage(emt::Stage::Sleep);
    if (cfg.sleepDeep) {
      // Intenta deep sleep solo si tienes hora válida
      // Nota: si NTP no está OK, computeSecondsUntilWake falla y hacemos fallback soft.
      enterDeepSleepUntilWake();
      // Si por algún motivo NO se duerme (fallback), seguimos soft:
      if (emt::due(millis(), timer)) {
        drawSleepingScreenSoft();
        timer = millis() + 8000;
      }
      delay(1);
      return;
    } else {
      if (emt::due(millis(), timer)) {
        drawSleepingScreenSoft();
        timer = millis() + 8000;
      }
      delay(1);
      return;
    }
  } else {
    u8g2.setContrast(brightness);
  }

  // Check once on the first usable connection, including an offline boot.
  if (ENABLE_AUTO_OTA && !otaChecked && wifiConnected && haveStop(cfg) && time(nullptr) >= 1704067200) {
    otaChecked = true;
    checkUpdates();
  }
  // Weather tick
  weatherTick();

  // Alternancia pantallas
  if (emt::due(millis(), uiNextSwitchMs)) {
    // Determinar siguiente estado
    UiScreen next;
    if (uiScreen == UI_BUS) {
      // Pasar a BUS2 solo si está activo Y hay más de 3 grupos disponibles
      if (busSecondThisCycle) {
        next = UI_BUS2;
      } else {
        next = UI_WEATHER;
      }
    } else if (uiScreen == UI_BUS2) {
      next = UI_WEATHER;
    } else { // UI_WEATHER
      next = UI_BUS;
    }
    uiScreen = next;
    uiScheduleNext();

    if (uiScreen == UI_BUS)       drawEmtBoard();
    else if (uiScreen == UI_BUS2) drawEmtBoard2();
    else                          drawWeatherScreen();
  }

  // EMT update
  if (wifiConnected && appState == ST_RUNNING && haveStop(cfg) && haveCreds(cfg) && emt::due(millis(), nextDataUpdate)) {
    (void)getEmtBoard();
    if (uiScreen == UI_BUS) drawEmtBoard();
    else if (uiScreen == UI_BUS2) drawEmtBoard2();
  }

  // Si faltan credenciales, repinta de vez en cuando
  if (wifiConnected && appState == ST_RUNNING && (!haveCreds(cfg)) && emt::due(millis(), nextDataUpdate)) {
    nextDataUpdate = millis() + DATAUPDATEINTERVAL;
    if (uiScreen == UI_BUS) drawEmtBoard();
    else if (uiScreen == UI_BUS2) drawEmtBoard2();
  }

  // reloj
  drawClock(true);

  // Auto Brightness logic
  if (cfg.autoBrightness && weather.sunrise > 0 && weather.sunset > 0) {
    time_t now = time(nullptr);
    int target;
    if (now >= (time_t)weather.sunrise && now < (time_t)weather.sunset) {
      target = cfg.brightnessDay;
    } else {
      target = cfg.brightnessNight;
    }
    if (brightness != target) {
      brightness = target;
      u8g2.setContrast(brightness);
    }
  }

  long delayMs = 25 - (long)(millis() - refreshTimer);
  if (delayMs > 0) delay((uint32_t)delayMs);
  refreshTimer = millis();

  recordStage(emt::Stage::Idle);
  // Task WDT detects a blocked task; stale remote data must not reboot the ESP32.
}
