/*
 * Pantalla EMT (EMT + Weather) + Watchdogs robustos
 *
 * Flujo:
 *  1) WiFiManager SOLO para credenciales WiFi.
 *  2) WebServer para configurar:
 *     - StopId, StopName, Brillo
 *     - EMT Email/Password (MobilityLabs Basic)
 *     - OpenWeather API key
 *     - Duración pantalla BUS (s) y WEATHER (s)
 *
 * Persistencia en LittleFS: /config.json
 *
 * Watchdogs:
 *  - Task WDT (ESP32) para colgados reales del loopTask.
 *  - Soft WDT (lógico) para estados "zombie" prolongados.
 */

#include <Arduino.h>
#include <functional>          // <-- IMPORTANTE para std::function
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <HTTPClient.h>
#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <WiFiManager.h>
#include <time.h>

#include <SPI.h>
#include <U8g2lib.h>

#include <stationData.h>

// Watchdog (ESP32 Task WDT)
#include "esp_task_wdt.h"

// ---------------------- Versión ----------------------
#define VERSION_MAJOR 0
#define VERSION_MINOR 9

// ---------------------- Watchdogs ----------------------
// Task WDT: si el loopTask se bloquea (no "alimenta" el WDT) -> reset/panic
static const int WDT_TIMEOUT_S = 15;
static unsigned long nextWdtFeedMs = 0;
static const unsigned long WDT_FEED_PERIOD_MS = 250;

// Soft watchdog: si no hay "progreso" durante X -> reinicia (pero con latido mínimo para evitar falsos)
static unsigned long lastHealthyTickMs = 0;
static const unsigned long SOFT_WDT_MS = 5UL * 60UL * 1000UL; // 5 min
static unsigned long lastLoopBeatMs = 0;                      // latido mínimo del loop
static const unsigned long LOOP_BEAT_MS = 1000;              // 1s

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
static const char contentTypeText[] PROGMEM = "text/plain";
static const char contentTypeHtml[] PROGMEM = "text/html";

// ---------------------- Tiempo / NTP ----------------------
static const char ntpServer[] PROGMEM = "europe.pool.ntp.org";
static struct tm timeinfo;
char displayedTime[16] = "";

// Timezone España (CET/CEST)
static const char tzSpain[] PROGMEM = "CET-1CEST,M3.5.0/2,M10.5.0/3";

// ---------------------- Config persistente ----------------------
static const char CFG_PATH[] = "/config.json";

struct AppConfig {
  // MobilityLabs BASIC
  String emtEmail;
  String emtPassword;

  // EMT
  String emtStopId;
  String emtStopName;

  // OpenWeather
  String weatherApiKey;

  // Duraciones UI
  uint32_t uiBusMs;
  uint32_t uiWeatherMs;
};

AppConfig cfg;

// ---------------------- Estado del sistema ----------------------
enum AppState : uint8_t {
  ST_BOOT = 0,
  ST_NEED_STOP,
  ST_RUNNING
};
AppState appState = ST_BOOT;

bool wifiConnected = false;
bool firstLoad = true;

bool sleepEnabled = false;
bool isSleeping = false;
uint8_t sleepStarts = 0;
uint8_t sleepEnds = 6;

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
static String emtAccessToken = "";
static unsigned long emtTokenExpiresAtMs = 0;
static String emtLastErrorMsg = "";

// ---------------------- Weather state ----------------------
struct WeatherState {
  bool hasCoords = false;
  double lat = 0.0;
  double lon = 0.0;

  bool hasData = false;
  float tempC = NAN;
  float feelsC = NAN;
  float minC = NAN;
  float maxC = NAN;

  char desc[48] = "";
  char icon[8] = "";

  unsigned long nextFetchMs = 0;
  unsigned long lastFetchMs = 0;

  String lastError = "";
};

static WeatherState weather;

#define WEATHER_UPDATE_INTERVAL_MS (10UL * 60UL * 1000UL) // 10 min

// ---------------------- UI alternancia ----------------------
enum UiScreen : uint8_t { UI_BUS = 0, UI_WEATHER = 1 };
static UiScreen uiScreen = UI_BUS;

static unsigned long uiNextSwitchMs = 0;
static uint32_t UI_BUS_MS = 45000UL;
static uint32_t UI_WEATHER_MS = 10000UL;

static inline void uiScheduleNext() {
  uiNextSwitchMs = millis() + ((uiScreen == UI_BUS) ? UI_BUS_MS : UI_WEATHER_MS);
}
static inline void uiToggleScreen() {
  uiScreen = (uiScreen == UI_BUS) ? UI_WEATHER : UI_BUS;
  uiScheduleNext();
}

// ---------------------- Helpers: “salud” ----------------------
static inline void markHealthy() {
  lastHealthyTickMs = millis();
}

// ---------------------- Helpers: FS config ----------------------
static bool loadConfig(AppConfig& out) {
  if (!LittleFS.exists(CFG_PATH)) return false;

  File f = LittleFS.open(CFG_PATH, "r");
  if (!f) return false;

  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) return false;

  out.emtEmail      = doc["emtEmail"]      | "";
  out.emtPassword   = doc["emtPassword"]   | "";
  out.emtStopId     = doc["emtStopId"]     | "";
  out.emtStopName   = doc["emtStopName"]   | "EMT Madrid";
  out.weatherApiKey = doc["weatherApiKey"] | "";

  out.uiBusMs       = doc["uiBusMs"]       | 45000UL;
  out.uiWeatherMs   = doc["uiWeatherMs"]   | 10000UL;

  return true;
}

static bool saveConfig(const AppConfig& in) {
  StaticJsonDocument<1024> doc;

  doc["emtEmail"]      = in.emtEmail;
  doc["emtPassword"]   = in.emtPassword;
  doc["emtStopId"]     = in.emtStopId;
  doc["emtStopName"]   = in.emtStopName;
  doc["weatherApiKey"] = in.weatherApiKey;
  doc["uiBusMs"]       = in.uiBusMs;
  doc["uiWeatherMs"]   = in.uiWeatherMs;

  File f = LittleFS.open(CFG_PATH, "w");
  if (!f) return false;

  if (serializeJsonPretty(doc, f) == 0) {
    f.close();
    return false;
  }
  f.close();
  return true;
}

static inline bool haveCreds(const AppConfig& c) {
  return !c.emtEmail.isEmpty() && !c.emtPassword.isEmpty();
}
static inline bool haveStop(const AppConfig& c) {
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
  markHealthy();
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
  markHealthy();
}

static void drawNoTimeScreen() {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x13_tf);
  centreText("Sin hora NTP", 0);
  u8g2.setFont(u8g2_font_6x10_tf);
  centreText("Revisa WiFi / NTP", 18);
  u8g2.sendBuffer();
  markHealthy();
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
  markHealthy();
}

static void drawLoadingScreen(const char* msg) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x13_tf);
  centreText("Pantalla EMT", 0);
  u8g2.setFont(u8g2_font_6x10_tf);
  centreText("Cargando...", 18);
  if (msg && msg[0] != '\0') centreText(msg, 32);
  drawStatusBarIcons();
  u8g2.sendBuffer();
  markHealthy();
}

// ---------------------- Reloj ----------------------
static void drawClock(bool updateRegion) {
  if (millis() < nextClockUpdate) return;
  nextClockUpdate = millis() + CLOCKUPDATEINTERVAL;

  if (!getLocalTime(&timeinfo)) return;

  char sysTime[16];
  snprintf(sysTime, sizeof(sysTime), "%02d:%02d:%02d",
           timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);

  if (strcmp(displayedTime, sysTime) == 0) return;
  strcpy(displayedTime, sysTime);

  u8g2.setFont(u8g2_font_6x10_tf);
  int w = u8g2.getStrWidth(sysTime);
  blankArea(SCREEN_WIDTH - w - 2, 0, w + 2, 10);
  u8g2.drawStr(SCREEN_WIDTH - w, 0, sysTime);

  if (updateRegion) u8g2.updateDisplayArea(0, 0, 32, 2);

  markHealthy();
}

// ---------------------- Sleep “sencillo” ----------------------
static bool isSnoozing() {
  if (!sleepEnabled) return false;
  if (!getLocalTime(&timeinfo)) return false;
  uint8_t h = timeinfo.tm_hour;

  if (sleepStarts > sleepEnds) return (h >= sleepStarts) || (h < sleepEnds);
  return (h >= sleepStarts) && (h < sleepEnds);
}

static void drawSleepingScreen() {
  u8g2.setContrast(DIMMED_BRIGHTNESS);
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_6x13_tf);
  if (getLocalTime(&timeinfo)) {
    char t[8];
    snprintf(t, sizeof(t), "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
    centreText(t, 24);
  } else {
    centreText("Sleep", 24);
  }
  u8g2.sendBuffer();
  firstLoad = true;
  markHealthy();
}

// ---------------------- HTTP helpers ----------------------
static bool httpGetJson(const String& url,
                        const std::function<void(HTTPClient&)>& addHeaders,
                        String& outBody, int& outHttpCode) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    outHttpCode = -1;
    outBody = "";
    return false;
  }

  // Timeouts anti-bloqueo
  http.setConnectTimeout(4000);
  http.setTimeout(8000);

  if (addHeaders) addHeaders(http);

  outHttpCode = http.GET();
  outBody = (outHttpCode > 0) ? http.getString() : "";
  http.end();

  return (outHttpCode > 0);
}

static bool httpPostJson(const String& url, const String& payload,
                         const std::function<void(HTTPClient&)>& addHeaders,
                         String& outBody, int& outHttpCode) {
  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  if (!http.begin(client, url)) {
    outHttpCode = -1;
    outBody = "";
    return false;
  }

  http.setConnectTimeout(4000);
  http.setTimeout(8000);

  http.addHeader("Content-Type", "application/json");
  http.addHeader("Accept", "application/json");
  if (addHeaders) addHeaders(http);

  outHttpCode = http.POST(payload);
  outBody = (outHttpCode > 0) ? http.getString() : "";
  http.end();

  return (outHttpCode > 0);
}

// ---------------------- EMT Auth BASIC ----------------------
static bool emtTokenValid() {
  if (emtAccessToken.isEmpty()) return false;
  if (emtTokenExpiresAtMs == 0) return false;
  long remaining = (long)emtTokenExpiresAtMs - (long)millis();
  return remaining > 30000; // margen 30s
}

static bool emtLoginBasic(const String& email, const String& password) {
  // Feed WDT antes de operación potencialmente pesada
  esp_task_wdt_reset();

  emtLastErrorMsg = "";
  const String url = "https://openapi.emtmadrid.es/v1/mobilitylabs/user/login/";

  String body;
  int httpCode = 0;

  bool ok = httpGetJson(url,
    [&](HTTPClient& http) {
      http.addHeader("email", email);
      http.addHeader("password", password);
      http.addHeader("Accept", "application/json");
    },
    body, httpCode
  );

  if (!ok || httpCode < 200 || httpCode >= 300) {
    emtLastErrorMsg = "Login HTTP " + String(httpCode);
    return false;
  }

  StaticJsonDocument<3072> doc;
  auto err = deserializeJson(doc, body);
  if (err) {
    emtLastErrorMsg = "Login JSON parse";
    return false;
  }

  const char* code = doc["code"] | "";
  const char* desc = doc["description"] | "";
  String scode = String(code);

  if (!(scode == "00" || scode == "01")) {
    emtLastErrorMsg = String("Login fail ") + scode + " " + desc;
    return false;
  }

  const char* token = doc["data"][0]["accessToken"] | "";
  long tokenSecExpiration = doc["data"][0]["tokenSecExpiration"] | 0;

  if (!token || token[0] == '\0') {
    emtLastErrorMsg = String("Login no token ") + scode + " " + desc;
    return false;
  }

  emtAccessToken = String(token);

  // margen: renovamos antes (restamos 60s)
  if (tokenSecExpiration <= 120) tokenSecExpiration = 120;
  unsigned long ttlMs = (unsigned long)(tokenSecExpiration - 60) * 1000UL;
  emtTokenExpiresAtMs = millis() + ttlMs;

  markHealthy();
  return true;
}

// ---------------------- Board reset / messages ----------------------
static void clearBoard() {
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
static bool emtFetchStopCoords(const String& stopId, double& outLat, double& outLon) {
  esp_task_wdt_reset();

  if (!emtTokenValid()) {
    if (!emtLoginBasic(cfg.emtEmail, cfg.emtPassword)) return false;
  }

  String url = "https://openapi.emtmadrid.es/v1/transport/busemtmad/stops/" + stopId + "/detail/";

  String body;
  int httpCode = 0;

  bool ok = httpGetJson(url,
    [&](HTTPClient& http) {
      http.addHeader("accessToken", emtAccessToken);
      http.addHeader("Accept", "application/json");
    },
    body, httpCode
  );

  if (!ok || httpCode < 200 || httpCode >= 300) return false;

  StaticJsonDocument<4096> doc;
  if (deserializeJson(doc, body)) return false;

  const char* code = doc["code"] | "";
  if (String(code) != "00") return false;

  // GeoJSON: coordinates = [lon, lat]
  JsonArray coords = doc["data"][0]["stops"][0]["geometry"]["coordinates"].as<JsonArray>();
  if (coords.isNull() || coords.size() < 2) return false;

  outLon = coords[0].as<double>();
  outLat = coords[1].as<double>();

  markHealthy();
  return true;
}

// ---------------------- EMT Arrivals (v1) ----------------------
static bool emtFetchArrivalsV1(const String& stopId) {
  esp_task_wdt_reset();
  emtLastErrorMsg = "";

  if (!emtTokenValid()) {
    if (!emtLoginBasic(cfg.emtEmail, cfg.emtPassword)) return false;
  }

  const String url = "https://openapi.emtmadrid.es/v1/transport/busemtmad/stops/" + stopId + "/arrives/";

  String payload = "{";
  payload += "\"cultureInfo\":\"ES\",";
  payload += "\"stopId\":\"" + stopId + "\",";
  payload += "\"Text_StopRequired_YN\":\"Y\",";
  payload += "\"Text_EstimationsRequired_YN\":\"Y\"";
  payload += "}";

  String body;
  int httpCode = 0;

  bool ok = httpPostJson(url, payload,
    [&](HTTPClient& http) {
      http.addHeader("accessToken", emtAccessToken);
    },
    body, httpCode
  );

  if (!ok) {
    emtLastErrorMsg = "Arrivals HTTP fail";
    return false;
  }

  // Si 401/403 -> relog 1 vez
  if (httpCode == 401 || httpCode == 403) {
    emtAccessToken = "";
    emtTokenExpiresAtMs = 0;

    if (!emtLoginBasic(cfg.emtEmail, cfg.emtPassword)) {
      emtLastErrorMsg = "Token invalid";
      return false;
    }

    ok = httpPostJson(url, payload,
      [&](HTTPClient& http) {
        http.addHeader("accessToken", emtAccessToken);
      },
      body, httpCode
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

  StaticJsonDocument<8192> doc;
  auto err = deserializeJson(doc, body);
  if (err) {
    emtLastErrorMsg = "Arrivals JSON parse";
    return false;
  }

  const char* code = doc["code"] | "";
  const char* desc = doc["description"] | "";
  String scode = String(code);

  if (scode == "00" || scode == "01") {
    clearBoard();
    strncpy(station.location, cfg.emtStopName.c_str(), MAXLOCATIONSIZE - 1);
    station.location[MAXLOCATIONSIZE - 1] = '\0';

    JsonArray arr = doc["data"][0]["Arrive"].as<JsonArray>();
    if (arr.isNull() || arr.size() == 0) {
      setMessage("Sin estimaciones");
      markHealthy();
      return true;
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
      // deja respirar al RTOS en bucles de parseo
      yield();
    }

    station.numServices = n;
    markHealthy();
    return true;
  }

  emtLastErrorMsg = "Arrive code " + scode + " " + String(desc);
  setMessage("Error EMT " + scode);
  return false;
}

// ---------------------- Lógica EMT board ----------------------
static bool getEmtBoard() {
  if (!haveCreds(cfg) || !haveStop(cfg)) return false;

  bool ok = emtFetchArrivalsV1(cfg.emtStopId);
  nextDataUpdate = millis() + DATAUPDATEINTERVAL;

  if (ok) {
    lastDataLoadTime = millis();
    dataLoadSuccess++;
    lastUpdateResult = UPD_SUCCESS;
    markHealthy();
    return true;
  } else {
    dataLoadFailure++;
    lastUpdateResult = UPD_DATA_ERROR;
    return false;
  }
}

// ---------------------- Weather fetch (OpenWeather current) ----------------------
static bool weatherFetchCurrent(double lat, double lon) {
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

  String body;
  int httpCode = 0;

  bool ok = httpGetJson(url,
    [&](HTTPClient& http) {
      http.addHeader("Accept", "application/json");
    },
    body, httpCode
  );

  if (!ok || httpCode < 200 || httpCode >= 300) {
    weather.lastError = "OW HTTP " + String(httpCode);
    return false;
  }

  StaticJsonDocument<4096> doc;
  if (deserializeJson(doc, body)) {
    weather.lastError = "OW JSON parse";
    return false;
  }

  weather.tempC  = doc["main"]["temp"] | NAN;
  weather.feelsC = doc["main"]["feels_like"] | NAN;
  weather.minC   = doc["main"]["temp_min"] | NAN;
  weather.maxC   = doc["main"]["temp_max"] | NAN;

  const char* d = doc["weather"][0]["description"] | "";
  const char* i = doc["weather"][0]["icon"] | "";
  strncpy(weather.desc, d, sizeof(weather.desc) - 1);
  weather.desc[sizeof(weather.desc) - 1] = '\0';
  strncpy(weather.icon, i, sizeof(weather.icon) - 1);
  weather.icon[sizeof(weather.icon) - 1] = '\0';

  weather.hasData = true;
  weather.lastFetchMs = millis();
  weather.nextFetchMs = millis() + WEATHER_UPDATE_INTERVAL_MS;

  markHealthy();
  return true;
}

static void weatherResetOnStopChange() {
  weather.hasCoords = false;
  weather.hasData = false;
  weather.lastError = "";
  weather.nextFetchMs = 0;
  weather.lastFetchMs = 0;
  weather.lat = 0;
  weather.lon = 0;
  weather.desc[0] = '\0';
  weather.icon[0] = '\0';
}

static void weatherTick() {
  if (!wifiConnected) return;
  if (cfg.weatherApiKey.isEmpty()) return;
  if (!haveStop(cfg)) return;

  // 1) coords (desde EMT)
  if (!weather.hasCoords) {
    if (!haveCreds(cfg)) {
      weather.lastError = "No EMT creds (coords)";
      weather.nextFetchMs = millis() + 60000UL;
      return;
    }
    double lat = 0.0, lon = 0.0;
    if (!emtFetchStopCoords(cfg.emtStopId, lat, lon)) {
      weather.lastError = "No coords from EMT";
      weather.nextFetchMs = millis() + 60000UL;
      return;
    }
    weather.lat = lat;
    weather.lon = lon;
    weather.hasCoords = true;
    weather.hasData = false;
    weather.nextFetchMs = 0;
  }

  // 2) weather fetch
  if (millis() < weather.nextFetchMs) return;
  (void)weatherFetchCurrent(weather.lat, weather.lon);
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
static void drawEmtLine(int idx, int y) {
  u8g2.setFont(u8g2_font_6x10_tf);
  blankArea(0, y, SCREEN_WIDTH, 12);

  if (idx >= station.numServices) {
    if (messages.numMessages > 0 && idx == 0) centreText(messages.messages[0], y);
    return;
  }

  rdService& svc = station.service[idx];

  char left[96];
  const char* line = svc.via;
  const char* dest = svc.destination;
  if (line && line[0] != '\0') snprintf(left, sizeof(left), "%d  %s %s", idx + 1, line, dest);
  else snprintf(left, sizeof(left), "%d  %s", idx + 1, dest);

  int elapsed = 0;
  if (svc.receivedAtMs != 0) {
    elapsed = (int)((millis() - svc.receivedAtMs) / 1000UL);
    if (elapsed > 90) elapsed = 90;
  }

  int secs = svc.timeToStation - elapsed;
  if (secs < 0) secs = 0;

  char right[16];
  if (secs <= 60) {
    strncpy(right, "Due", sizeof(right));
    right[sizeof(right)-1] = '\0';
  } else {
    int mins = (secs + 30) / 60;
    snprintf(right, sizeof(right), "%d min", mins);
  }

  int wRight = u8g2.getStrWidth(right);
  int maxLeftWidth = SCREEN_WIDTH - wRight - 6;

  if (u8g2.getStrWidth(left) <= maxLeftWidth) u8g2.drawStr(0, y, left);
  else drawTruncatedText(left, 0, y, maxLeftWidth);

  u8g2.drawStr(SCREEN_WIDTH - wRight, y, right);
}

static void drawEmtBoard() {
  u8g2.clearBuffer();
  u8g2.setContrast(brightness);

  u8g2.setFont(u8g2_font_6x13_tf);
  centreText(cfg.emtStopName.c_str(), 0);

  drawClock(false);

  u8g2.setFont(u8g2_font_6x10_tf);

  if (!haveStop(cfg)) {
    centreText("Falta StopId", 28);
    centreText("Configura en web (/)", 40);
    drawStatusBarIcons();
    u8g2.sendBuffer();
    markHealthy();
    return;
  }

  if (!haveCreds(cfg)) {
    centreText("Faltan credenciales EMT", 28);
    centreText("Configura en web (/)", 40);
    drawStatusBarIcons();
    u8g2.sendBuffer();
    markHealthy();
    return;
  }

  drawEmtLine(0, 18);
  drawEmtLine(1, 32);
  drawEmtLine(2, 46);

  drawStatusBarIcons();
  u8g2.sendBuffer();
  markHealthy();
}

// ---------------------- Dibujo Weather ----------------------
static void drawWeatherScreen() {
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
    markHealthy();
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
    markHealthy();
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
  markHealthy();
}

// ---------------------- Web helpers ----------------------
static void sendResponse(int code, const String& msg) {
  server.send(code, contentTypeText, msg);
}

static String htmlHeader(const char* title) {
  String s;
  s += "<!doctype html><html><head><meta charset='utf-8'>";
  s += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  s += "<title>";
  s += title;
  s += "</title></head><body style='font-family:Arial;max-width:720px;margin:20px;'>";
  return s;
}

static String htmlFooter() { return "</body></html>"; }

static void updateMyMdns(const char* hostname) {
  if (MDNS.begin(hostname)) {
    MDNS.addService("http", "tcp", 80);
  }
}

// ---------------------- Endpoints ----------------------
static void handleInfo() {
  unsigned long uptime = millis();
  char sysUptime[64];
  int days = uptime / 86400000UL;
  int hours = (uptime % 86400000UL) / 3600000UL;
  int minutes = ((uptime % 86400000UL) % 3600000UL) / 60000UL;
  snprintf(sysUptime, sizeof(sysUptime), "%d days, %d hrs, %d min", days, hours, minutes);

  String msg;
  msg += "Pantalla EMT\n";
  msg += "Firmware: v" + String(VERSION_MAJOR) + "." + String(VERSION_MINOR) + "\n";
  msg += "Uptime: " + String(sysUptime) + "\n";
  msg += "WiFi: " + String(WiFi.SSID()) + "\n";
  msg += "IP: " + WiFi.localIP().toString() + "\n";
  msg += "RSSI: " + String(WiFi.RSSI()) + " dB\n";
  msg += "State: " + String((int)appState) + "\n";
  msg += "Screen: " + String((uiScreen == UI_BUS) ? "BUS" : "WEATHER") + "\n";
  msg += "StopName: " + cfg.emtStopName + "\n";
  msg += "StopId: " + cfg.emtStopId + "\n";
  msg += "Brightness: " + String(brightness) + "\n";
  msg += "EMT email set: " + String(cfg.emtEmail.isEmpty() ? "NO" : "YES") + "\n";
  msg += "EMT pass set: " + String(cfg.emtPassword.isEmpty() ? "NO" : "YES") + "\n";
  msg += "Token valid: " + String(emtTokenValid() ? "YES" : "NO") + "\n";
  msg += "OpenWeather key set: " + String(cfg.weatherApiKey.isEmpty() ? "NO" : "YES") + "\n";
  msg += "UI BUS ms: " + String(cfg.uiBusMs) + "\n";
  msg += "UI WEATHER ms: " + String(cfg.uiWeatherMs) + "\n";
  msg += "Weather coords: " + String(weather.hasCoords ? "YES" : "NO") + "\n";
  if (weather.hasCoords) msg += "Lat: " + String(weather.lat, 6) + " Lon: " + String(weather.lon, 6) + "\n";
  msg += "Weather data: " + String(weather.hasData ? "YES" : "NO") + "\n";
  if (weather.hasData) {
    msg += "Desc: " + String(weather.desc) + "\n";
    msg += "TempC: " + String(weather.tempC) + "\n";
    msg += "FeelsC: " + String(weather.feelsC) + "\n";
    msg += "MinC: " + String(weather.minC) + " MaxC: " + String(weather.maxC) + "\n";
    msg += "Icon: " + String(weather.icon) + "\n";
  }
  if (!weather.lastError.isEmpty()) msg += "Weather err: " + weather.lastError + "\n";
  msg += "Success: " + String(dataLoadSuccess) + "\n";
  msg += "Failures: " + String(dataLoadFailure) + "\n";
  msg += "Services: " + String(station.numServices) + "\n";
  if (messages.numMessages) msg += "Message: " + String(messages.messages[0]) + "\n";
  if (!emtLastErrorMsg.isEmpty()) msg += "EMT Error: " + emtLastErrorMsg + "\n";

  msg += "WDT timeout (s): " + String(WDT_TIMEOUT_S) + "\n";
  msg += "SoftWDT (ms): " + String(SOFT_WDT_MS) + "\n";
  msg += "Last healthy (ms ago): " + String((lastHealthyTickMs == 0) ? -1 : (long)(millis() - lastHealthyTickMs)) + "\n";

  sendResponse(200, msg);
}

static void handleReboot() {
  sendResponse(200, "Reiniciando...");
  delay(400);
  ESP.restart();
}

static void handleBrightness() {
  if (!server.hasArg("b")) {
    sendResponse(400, "Falta parametro b");
    return;
  }
  int level = server.arg("b").toInt();
  if (level < 1 || level > 255) {
    sendResponse(400, "b debe estar entre 1 y 255");
    return;
  }
  brightness = level;
  u8g2.setContrast(brightness);
  sendResponse(200, "OK");
}

// -------- Config principal --------
static void handleConfigGet() {
  String s = htmlHeader("Pantalla EMT");
  s += "<h2>Pantalla EMT</h2>";

  s += "<p><b>IP:</b> " + WiFi.localIP().toString() + "<br>";
  s += "<b>mDNS:</b> pantalla-emt.local</p>";

  s += "<form method='POST' action='/config'>";

  s += "<hr><h3>Parada</h3>";
  s += "<label>StopId</label><br>";
  s += "<input name='stopId' value='" + cfg.emtStopId + "' style='width:100%;padding:8px'><br><br>";
  s += "<label>Nombre en pantalla (opcional)</label><br>";
  s += "<input name='stopName' value='" + cfg.emtStopName + "' style='width:100%;padding:8px'><br><br>";

  s += "<hr><h3>Credenciales EMT (MobilityLabs Basic)</h3>";
  s += "<label>EMT Email</label><br>";
  s += "<input name='emtEmail' value='" + cfg.emtEmail + "' style='width:100%;padding:8px'><br><br>";
  s += "<label>EMT Password</label><br>";
  s += "<input name='emtPassword' type='password' value='" + cfg.emtPassword + "' style='width:100%;padding:8px'><br><br>";

  s += "<hr><h3>OpenWeather</h3>";
  s += "<label>OpenWeather API key</label><br>";
  s += "<input name='weatherKey' value='" + cfg.weatherApiKey + "' style='width:100%;padding:8px'><br><br>";

  s += "<hr><h3>Pantalla</h3>";
  s += "<label>Brillo (1-255)</label><br>";
  s += "<input name='brightness' value='" + String(brightness) + "' style='width:100%;padding:8px'><br><br>";

  s += "<hr><h3>Duracion pantallas</h3>";
  s += "<label>Buses (segundos)</label><br>";
  s += "<input name='busSecs' value='" + String(cfg.uiBusMs / 1000UL) + "' style='width:100%;padding:8px'><br><br>";
  s += "<label>Tiempo (segundos)</label><br>";
  s += "<input name='weatherSecs' value='" + String(cfg.uiWeatherMs / 1000UL) + "' style='width:100%;padding:8px'><br><br>";

  s += "<button type='submit' style='padding:10px 14px;'>Guardar cambios</button>";
  s += "</form>";

  s += "<hr>";
  s += "<p><a href='/info'>/info</a> | <a href='/reboot'>Reiniciar</a></p>";
  s += htmlFooter();

  server.send(200, contentTypeHtml, s);
}

static void handleConfigPost() {
  bool changed = false;
  bool needRefreshNow = false;
  bool stopChanged = false;
  bool uiChanged = false;

  if (server.hasArg("stopId")) {
    String newStop = server.arg("stopId");
    newStop.trim();
    if (newStop != cfg.emtStopId) {
      cfg.emtStopId = newStop;
      clearBoard();
      nextDataUpdate = 0;
      changed = true;
      needRefreshNow = true;
      stopChanged = true;
    }
  }

  if (server.hasArg("stopName")) {
    String newName = server.arg("stopName");
    newName.trim();
    if (newName.isEmpty()) newName = "EMT Madrid";
    if (newName != cfg.emtStopName) {
      cfg.emtStopName = newName;
      changed = true;
    }
  }

  if (server.hasArg("emtEmail")) {
    String v = server.arg("emtEmail");
    v.trim();
    if (v != cfg.emtEmail) {
      cfg.emtEmail = v;
      changed = true;
      needRefreshNow = true;
      emtAccessToken = "";
      emtTokenExpiresAtMs = 0;
    }
  }

  if (server.hasArg("emtPassword")) {
    String v = server.arg("emtPassword");
    v.trim();
    if (v != cfg.emtPassword) {
      cfg.emtPassword = v;
      changed = true;
      needRefreshNow = true;
      emtAccessToken = "";
      emtTokenExpiresAtMs = 0;
    }
  }

  if (server.hasArg("weatherKey")) {
    String v = server.arg("weatherKey");
    v.trim();
    if (v != cfg.weatherApiKey) {
      cfg.weatherApiKey = v;
      changed = true;
      weather.nextFetchMs = 0;
      weather.hasData = false;
      needRefreshNow = true;
    }
  }

  if (server.hasArg("brightness")) {
    int b = server.arg("brightness").toInt();
    if (b < 1) b = 1;
    if (b > 255) b = 255;
    if (b != brightness) {
      brightness = b;
      u8g2.setContrast(brightness);
      changed = true;
    }
  }

  if (server.hasArg("busSecs")) {
    int v = server.arg("busSecs").toInt();
    if (v < 5) v = 5;
    if (v > 300) v = 300;
    uint32_t ms = (uint32_t)v * 1000UL;
    if (ms != cfg.uiBusMs) {
      cfg.uiBusMs = ms;
      UI_BUS_MS = ms;
      uiChanged = true;
      changed = true;
    }
  }

  if (server.hasArg("weatherSecs")) {
    int v = server.arg("weatherSecs").toInt();
    if (v < 3) v = 3;
    if (v > 120) v = 120;
    uint32_t ms = (uint32_t)v * 1000UL;
    if (ms != cfg.uiWeatherMs) {
      cfg.uiWeatherMs = ms;
      UI_WEATHER_MS = ms;
      uiChanged = true;
      changed = true;
    }
  }

  if (stopChanged) weatherResetOnStopChange();
  if (changed) saveConfig(cfg);

  appState = haveStop(cfg) ? ST_RUNNING : ST_NEED_STOP;

  if (uiChanged) uiScheduleNext();

  if (needRefreshNow) {
    drawLoadingScreen("Actualizando...");
    if (wifiConnected && appState == ST_RUNNING && haveStop(cfg) && haveCreds(cfg)) {
      (void)getEmtBoard();
    }
    weatherTick();
  }

  if (uiScreen == UI_BUS) drawEmtBoard();
  else drawWeatherScreen();

  String s = htmlHeader("OK");
  s += "<h2>Guardado</h2>";
  s += "<p>Cambios aplicados.</p>";
  s += "<p><a href='/'>Volver</a></p>";
  s += htmlFooter();
  server.send(200, contentTypeHtml, s);

  markHealthy();
}

static void handleRoot() { handleConfigGet(); }

// ---------------------- WiFiManager callback ----------------------
static void wmConfigModeCallback(WiFiManager* wm) {
  (void)wm;
  drawSetupScreen();
}

// ---------------------- Setup / Loop ----------------------
void setup() {
  // OLED init
  u8g2.begin();
  u8g2.setContrast(brightness);
  u8g2.setDrawColor(1);
  u8g2.setFontMode(1);
  u8g2.setFontRefHeightAll();
  u8g2.setFontPosTop();

  drawStartupScreen("Inicializando...");

  if (!LittleFS.begin(true)) {
    drawStartupScreen("LittleFS ERROR");
    delay(1500);
  }

  (void)loadConfig(cfg);
  if (cfg.emtStopName.isEmpty()) cfg.emtStopName = "EMT Madrid";
  if (cfg.uiBusMs == 0) cfg.uiBusMs = 45000UL;
  if (cfg.uiWeatherMs == 0) cfg.uiWeatherMs = 10000UL;

  UI_BUS_MS = cfg.uiBusMs;
  UI_WEATHER_MS = cfg.uiWeatherMs;

  clearBoard();
  weatherResetOnStopChange();

  // WiFi
  drawStartupScreen("Conectando WiFi...");
  WiFi.mode(WIFI_MODE_NULL);
  WiFi.setSleep(WIFI_PS_NONE);
  WiFi.mode(WIFI_STA);

  WiFiManager wm;
  wm.setAPCallback(wmConfigModeCallback);
  wm.setWiFiAutoReconnect(true);
  wm.setConnectTimeout(10);
  wm.setConnectRetries(2);

  bool ok = wm.autoConnect("Pantalla EMT");
  if (!ok) ESP.restart();

  while (WiFi.status() != WL_CONNECTED) delay(150);
  wifiConnected = true;

  updateMyMdns("pantalla-emt");

  // NTP + TZ España
  configTime(0, 0, ntpServer);
  setenv("TZ", tzSpain, 1);
  tzset();

  int tries = 0;
  while (!getLocalTime(&timeinfo) && tries++ < 12) delay(400);
  if (tries >= 12) drawNoTimeScreen();

  // Routes
  server.on("/", HTTP_GET, handleRoot);
  server.on("/info", HTTP_GET, handleInfo);
  server.on("/reboot", HTTP_GET, handleReboot);
  server.on("/brightness", HTTP_GET, handleBrightness);
  server.on("/config", HTTP_GET, handleConfigGet);
  server.on("/config", HTTP_POST, handleConfigPost);
  server.begin();

  // Init Task WDT (vigila loopTask)
  esp_task_wdt_config_t twdt_config = {
    .timeout_ms = WDT_TIMEOUT_S * 1000,
    .idle_core_mask = 0,
    .trigger_panic = true
  };
  esp_task_wdt_init(&twdt_config);
  esp_task_wdt_add(NULL);

  uiScreen = UI_BUS;
  uiScheduleNext();

  if (!haveStop(cfg)) {
    appState = ST_NEED_STOP;
    drawNeedStopScreen();
  } else {
    appState = ST_RUNNING;
    drawStartupScreen("Listo");
    delay(300);

    nextDataUpdate = 0;
    nextClockUpdate = 0;

    weather.nextFetchMs = 0;
    weatherTick();

    drawEmtBoard();
  }

  markHealthy();
  lastLoopBeatMs = millis();
  nextWdtFeedMs = millis();
}

void loop() {
  // --- Task WDT feed (si loop está vivo) ---
  if ((long)(millis() - nextWdtFeedMs) >= 0) {
    esp_task_wdt_reset();
    nextWdtFeedMs = millis() + WDT_FEED_PERIOD_MS;
  }

  // --- Latido mínimo: evita falsos positivos del soft WDT ---
  if (millis() - lastLoopBeatMs >= LOOP_BEAT_MS) {
    // Esto indica que el loop sigue corriendo (aunque NTP falle, etc.)
    lastHealthyTickMs = millis();
    lastLoopBeatMs = millis();
  }

  server.handleClient();

  if (appState == ST_NEED_STOP) {
    if (millis() > timer) {
      drawNeedStopScreen();
      timer = millis() + 4000;
    }
    delay(1);
    return;
  }

  isSleeping = isSnoozing();
  if (isSleeping) {
    if (millis() > timer) {
      drawSleepingScreen();
      timer = millis() + 8000;
    }
    delay(1);
    return;
  } else {
    u8g2.setContrast(brightness);
  }

  // Estado WiFi
  if (WiFi.status() != WL_CONNECTED && wifiConnected) {
    wifiConnected = false;
    firstLoad = true;
  } else if (WiFi.status() == WL_CONNECTED && !wifiConnected) {
    wifiConnected = true;
    firstLoad = true;
    markHealthy();
  }

  // Weather tick
  weatherTick();

  // Alternancia de pantallas
  if (millis() > uiNextSwitchMs) {
    uiToggleScreen();
    if (uiScreen == UI_BUS) drawEmtBoard();
    else drawWeatherScreen();
  }

  // EMT update
  if (wifiConnected && appState == ST_RUNNING && haveStop(cfg) && haveCreds(cfg) && millis() > nextDataUpdate) {
    (void)getEmtBoard();
    if (uiScreen == UI_BUS) drawEmtBoard();
  }

  // Si faltan credenciales, repinta de vez en cuando
  if (wifiConnected && appState == ST_RUNNING && (!haveCreds(cfg)) && millis() > nextDataUpdate) {
    nextDataUpdate = millis() + DATAUPDATEINTERVAL;
    if (uiScreen == UI_BUS) drawEmtBoard();
  }

  // reloj
  drawClock(true);

  long delayMs = 25 - (long)(millis() - refreshTimer);
  if (delayMs > 0) delay((uint32_t)delayMs);
  refreshTimer = millis();

  // Soft watchdog (último)
  if (lastHealthyTickMs != 0 && (millis() - lastHealthyTickMs) > SOFT_WDT_MS) {
    ESP.restart();
  }
}
