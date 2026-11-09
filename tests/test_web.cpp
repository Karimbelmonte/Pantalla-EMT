#include "globals.h"
#include "board_model.h"
#include "boot_diagnostics.h"
#include <cassert>
#include <cstdio>
AppConfig cfg;
WebServer server(80);
U8G2_SSD1322_NHD_256X64_F_4W_HW_SPI u8g2;
const char contentTypeText[]="text/plain", contentTypeHtml[]="text/html";
bool wifiConnected=true;
unsigned long maxLoopGapMs=0,lastDataLoadTime=0;
int brightness=80,dataLoadSuccess=0,dataLoadFailure=0;
AppState appState=ST_RUNNING;
UiScreen uiScreen=UI_BUS;
String configError,emtAccessToken,emtLastErrorMsg;
WeatherState weather;
emt::RetrySchedule weatherRetry,authRetry,stopRetry;
bool persistenceOk=true;int applied=0;
bool saveConfig(const AppConfig&){if(!persistenceOk) configError="Flash error";return persistenceOk;}
void applyConfig(const AppConfig& candidate){cfg=candidate;++applied;}
void enterDeepSleepUntilWake(){}
const char* displayStopName(){return emt::stopDisplayName(cfg.emtStopName.c_str(),"");}
bool wifiSetupRequested=false;
void requestWifiSetup(){wifiSetupRequested=true;}
const char* wifiCredentialsAtBoot(){return "SSID cargado";}
uint32_t wifiRetryInMs(){return 0;}
uint32_t wifiDisconnectCount(){return 2;}
uint16_t wifiLastDisconnectReason(){return 200;}
const char* bootResetReason(){return "Watchdog de tarea";}
const char* bootPreviousStage(){return "Consulta OpenWeather";}
const char* bootPreviousIntent(){return "Sin solicitud";}
uint32_t bootPreviousUptimeMs(){return 120000;}
bool bootHasPreviousTrace(){return true;}
void recordRestartIntent(emt::RestartIntent){}
#include "ota_control.h"
int checks=0,installs=0,restarts=0;
bool findOtaCandidate(emt::OtaCandidate<String>& candidate){
  ++checks;candidate.version="0.14.0";return true;
}
bool installOtaCandidate(const emt::OtaCandidate<String>&){++installs;return true;}
void restartAfterOta(){++restarts;}
#include "../src/ota_control.cpp"
#include "../src/web_server.cpp"
int main(){
  setupWebServer();
  cfg.emtStopName="Anterior";
  persistenceOk=false;
  server.request("/config",HTTP_POST,{{"stopName","Nuevo"},{"busSecs","90"}});
  assert(server.code==500 && cfg.emtStopName=="Anterior" && applied==0);
  persistenceOk=true;
  cfg.sleepEnabled=true;cfg.autoBrightness=true;cfg.combineArrivals=true;
  server.request("/config",HTTP_POST,{{"stopName",""},{"busSecs","90"},{"brightness","101"}});
  assert(server.code==200 && cfg.emtStopName.isEmpty() && cfg.uiBusMs==90000 && cfg.manualBrightness==101);
  assert(cfg.sleepEnabled && cfg.autoBrightness && cfg.combineArrivals); // partial update
  server.request("/config",HTTP_POST,{{"fullForm","1"},{"busSecs","5"},{"busSecs2","5"},{"emtPassword"," a & \"b\" "}});
  assert(server.code==200 && !cfg.sleepEnabled && !cfg.autoBrightness && !cfg.combineArrivals);
  assert(cfg.uiBus2Ms==0 && cfg.emtPassword==" a & \"b\" ");
  brightness=20;
  server.request("/config",HTTP_GET);
  assert(server.body.find("name=\"brightness\" value=\"101\"")!=std::string::npos);
  assert(server.body.find("a &amp; &quot;b&quot;")!=std::string::npos);
  assert(server.body.find("name=\"fullForm\"")!=std::string::npos);
  server.request("/brightness",HTTP_GET,{{"b","0"}});assert(server.code==400);
  server.request("/brightness",HTTP_GET,{{"b","33"}});assert(server.code==200 && brightness==33 && cfg.manualBrightness==101);
  server.request("/info",HTTP_GET);assert(server.code==200 && server.body.find("Sin datos")!=std::string::npos);
  assert(server.body.find("Watchdog de tarea")!=std::string::npos);
  assert(server.body.find("Consulta OpenWeather")!=std::string::npos);
  server.request("/wifi/setup",HTTP_POST); assert(server.code==200 && wifiSetupRequested);
  server.request("/update",HTTP_GET);
  assert(server.code==200 && checks==0 && installs==0);
  assert(server.body.find("Buscar actualización")!=std::string::npos);
  assert(server.routes.count({"/update/install",HTTP_GET})==0);
  server.request("/update/check",HTTP_POST);assert(server.code==403 && !otaBusy());
  server.request("/update/check",HTTP_POST,{{"token","wrong"}});assert(server.code==403);
  wifiConnected=false;
  server.request("/update/check",HTTP_POST,{{"token",otaFormToken}});assert(server.code==503);
  wifiConnected=true;
  server.request("/update/check",HTTP_POST,{{"token",otaFormToken}});
  assert(server.code==202 && checks==0 && otaBusy()); // HTTP returns before network work
  server.request("/update/check",HTTP_POST,{{"token",otaFormToken}});assert(server.code==409);
  fakeMillis+=500;processManualUpdates();assert(checks==1 && installs==0);
  server.request("/update",HTTP_GET);
  assert(server.body.find("Descargar e instalar 0.14.0")!=std::string::npos);
  server.request("/update/install",HTTP_POST,{{"token",otaFormToken},{"version","0.15.0"}});
  assert(server.code==409 && !otaBusy());
  server.request("/update/install",HTTP_POST,{{"token",otaFormToken},{"version","0.14.0"}});
  assert(server.code==202 && installs==0 && restarts==0);
  fakeMillis+=500;processManualUpdates();assert(installs==1 && restarts==1);
  puts("web: real routes, failed persistence, partial/full forms, automatic name, manual brightness and escaping PASS");
}
