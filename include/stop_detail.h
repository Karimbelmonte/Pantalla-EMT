#pragma once
#include <ArduinoJson.h>
#include <cmath>
namespace emt {
struct StopDetail {
  const char* name = "";
  double latitude=0, longitude=0;
  bool hasCoords=false;
};
inline bool stopDetailComplete(bool automaticName, bool weatherEnabled, const char* name, bool hasCoords) {
  return (!automaticName || name[0] != '\0') && (!weatherEnabled || hasCoords);
}
// EMT official Stop Detail: data[0].stops[0].name and geometry.coordinates.
// Returned name borrows the document; copy it before destroying that document.
inline bool parseStopDetail(const JsonDocument& doc, StopDetail& out) {
  out=StopDetail{};
  if(std::strcmp(doc["code"] | "", "00") != 0) return false;
  JsonObjectConst stop=doc["data"][0]["stops"][0].as<JsonObjectConst>();
  if(stop.isNull()) return false;
  out.name=stop["name"] | "";
  JsonArrayConst coords=stop["geometry"]["coordinates"].as<JsonArrayConst>();
  if(coords.size()>=2 && coords[0].is<double>() && coords[1].is<double>()) {
    out.longitude=coords[0].as<double>(); out.latitude=coords[1].as<double>();
    out.hasCoords=std::isfinite(out.longitude) && std::isfinite(out.latitude)
      && out.longitude>=-180 && out.longitude<=180 && out.latitude>=-90 && out.latitude<=90;
  }
  return true;
}
}
