#pragma once
#include <stdint.h>
namespace emt {
template<class Text> struct Config {
  Text emtEmail, emtPassword, emtStopId, emtStopName, weatherApiKey;
  uint32_t uiBusMs = 45000, uiWeatherMs = 10000, uiBus2Ms = 0;
  bool sleepEnabled = false, sleepDeep = false;
  int sleepStarts = 0, sleepEnds = 6;
  bool combineArrivals = true, autoBrightness = false;
  int manualBrightness = 80, brightnessDay = 128, brightnessNight = 20;
};
template<class T> inline T bounded(T n, T lo, T hi) {
  return n < lo ? lo : (n > hi ? hi : n);
}
template<class Text> void normalize(Config<Text>& c) {
  c.uiBusMs = bounded<uint32_t>(c.uiBusMs, 5000, 300000);
  c.uiWeatherMs = bounded<uint32_t>(c.uiWeatherMs, 3000, 120000);
  c.uiBus2Ms = bounded<uint32_t>(c.uiBus2Ms, 0, c.uiBusMs - 5000);
  c.sleepStarts = bounded(c.sleepStarts, 0, 23);
  c.sleepEnds = bounded(c.sleepEnds, 0, 23);
  c.manualBrightness = bounded(c.manualBrightness, 1, 255);
  c.brightnessDay = bounded(c.brightnessDay, 1, 255);
  c.brightnessNight = bounded(c.brightnessNight, 1, 255);
}
// Apply only after persistence succeeds. Also used by the web transaction.
template<class C, class Save, class Apply>
bool commitConfig(const C& candidate, Save save, Apply apply) {
  if (!save(candidate)) return false;
  apply(candidate);
  return true;
}
// Store must leave the destination intact if replace() fails.
template<class Store, class Text> bool verifiedReplace(Store& store, const Text& data) {
  if (!store.writeTemp(data) || !store.verifyTemp(data)) {
    store.cleanup(); return false;
  }
  if (!store.replace()) { store.cleanup(); return false; }
  return true;
}
}
