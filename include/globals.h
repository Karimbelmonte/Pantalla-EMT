#ifndef GLOBALS_H
#define GLOBALS_H

#include <Arduino.h>
#include <WebServer.h>
#include <U8g2lib.h>
#include <stationData.h>

// ---------------------- Pantalla / OLED ----------------------
#define SCREEN_WIDTH 256
#define SCREEN_HEIGHT 64
#define DIMMED_BRIGHTNESS 1

extern U8G2_SSD1322_NHD_256X64_F_4W_HW_SPI u8g2;

// ---------------------- WebServer ----------------------
extern WebServer server;
extern const char contentTypeText[];
extern const char contentTypeHtml[];

// ---------------------- Tiempo / NTP ----------------------
extern char displayedTime[16];

// ---------------------- Config persistente ----------------------
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

  // Sleep
  bool sleepEnabled;
  bool sleepDeep;        // true = deep sleep, false = sleep suave (pantalla dim, sigue vivo)
  uint8_t sleepStarts;   // 0..23
  uint8_t sleepEnds;     // 0..23
};

extern AppConfig cfg;

// ---------------------- Estado del sistema ----------------------
enum AppState : uint8_t {
  ST_BOOT = 0,
  ST_NEED_STOP,
  ST_RUNNING
};
extern AppState appState;

extern bool wifiConnected;
extern bool firstLoad;

extern int brightness;

extern unsigned long nextClockUpdate;
extern unsigned long nextDataUpdate;
extern unsigned long timer;
extern unsigned long refreshTimer;

#define DATAUPDATEINTERVAL 30000
#define CLOCKUPDATEINTERVAL 500

// ---------------------- Datos “board” ----------------------
extern rdStation station;
extern stnMessages messages;

extern int lastUpdateResult;
extern unsigned long lastDataLoadTime;
extern int dataLoadSuccess;
extern int dataLoadFailure;

// ---------------------- EMT token state ----------------------
extern String emtAccessToken;
extern unsigned long emtTokenExpiresAtMs;
extern String emtLastErrorMsg;

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
extern WeatherState weather;
#define WEATHER_UPDATE_INTERVAL_MS (10UL * 60UL * 1000UL)

// ---------------------- UI alternancia ----------------------
enum UiScreen : uint8_t { UI_BUS = 0, UI_WEATHER = 1 };
extern UiScreen uiScreen;

extern unsigned long uiNextSwitchMs;
extern uint32_t UI_BUS_MS;
extern uint32_t UI_WEATHER_MS;

// ---------------------- Declaraciones de funciones ----------------------
void uiScheduleNext();
void uiToggleScreen();
void markHealthy();

bool loadConfig(AppConfig& out);
bool saveConfig(const AppConfig& in);

bool haveCreds(const AppConfig& c);
bool haveStop(const AppConfig& c);

void clearBoard();
bool getEmtBoard();

void weatherResetOnStopChange();
void weatherTick();

void drawEmtBoard();
void drawWeatherScreen();
void drawLoadingScreen(const char* msg);
void enterDeepSleepUntilWake();

#endif // GLOBALS_H
