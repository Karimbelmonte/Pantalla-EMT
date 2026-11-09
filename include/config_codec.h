#pragma once
#include "config_model.h"
#include <ArduinoJson.h>
namespace emt {
template<class Text> void encodeConfig(const Config<Text>& c, JsonDocument& d) {
  d.clear();
  d["emtEmail"] = c.emtEmail.c_str();
  d["emtPassword"] = c.emtPassword.c_str();
  d["emtStopId"] = c.emtStopId.c_str();
  d["emtStopName"] = c.emtStopName.c_str();
  d["weatherApiKey"] = c.weatherApiKey.c_str();
  d["uiBusMs"] = c.uiBusMs; d["uiWeatherMs"] = c.uiWeatherMs; d["uiBus2Ms"] = c.uiBus2Ms;
  d["sleepEnabled"] = c.sleepEnabled; d["sleepDeep"] = c.sleepDeep;
  d["sleepStarts"] = c.sleepStarts; d["sleepEnds"] = c.sleepEnds;
  d["combineArrivals"] = c.combineArrivals; d["autoBrightness"] = c.autoBrightness;
  d["manualBrightness"] = c.manualBrightness;
  d["brightnessDay"] = c.brightnessDay; d["brightnessNight"] = c.brightnessNight;
}
inline uint32_t readDuration(JsonVariantConst v, uint32_t fallback, uint32_t lo, uint32_t hi) {
  if (!v.is<int64_t>()) return fallback;
  return static_cast<uint32_t>(bounded<int64_t>(v.as<int64_t>(), lo, hi));
}
template<class Text> bool decodeConfig(const JsonDocument& d, Config<Text>& c) {
  c = Config<Text>{};
  if (!d.is<JsonObjectConst>()) return false;
  c.emtEmail = d["emtEmail"] | ""; c.emtPassword = d["emtPassword"] | "";
  c.emtStopId = d["emtStopId"] | ""; c.emtStopName = d["emtStopName"] | "";
  c.weatherApiKey = d["weatherApiKey"] | "";
  c.uiBusMs = readDuration(d["uiBusMs"], c.uiBusMs, 5000, 300000);
  c.uiWeatherMs = readDuration(d["uiWeatherMs"], c.uiWeatherMs, 3000, 120000);
  c.uiBus2Ms = readDuration(d["uiBus2Ms"], c.uiBus2Ms, 0, 300000);
  c.sleepEnabled = d["sleepEnabled"] | c.sleepEnabled;
  c.sleepDeep = d["sleepDeep"] | c.sleepDeep;
  c.sleepStarts = d["sleepStarts"] | c.sleepStarts; c.sleepEnds = d["sleepEnds"] | c.sleepEnds;
  c.combineArrivals = d["combineArrivals"] | c.combineArrivals;
  c.autoBrightness = d["autoBrightness"] | c.autoBrightness;
  c.manualBrightness = d["manualBrightness"] | c.manualBrightness;
  c.brightnessDay = d["brightnessDay"] | c.brightnessDay;
  c.brightnessNight = d["brightnessNight"] | c.brightnessNight;
  normalize(c);
  return true;
}
}
