#include "config_codec.h"
#include <string>
#include <cassert>
#include <cstdio>
using Config = emt::Config<std::string>;
struct Store {
  std::string original="old", temp;
  bool writeOk=true, verifyOk=true, renameOk=true;
  bool writeTemp(const std::string& data) { temp=data; return writeOk; }
  bool verifyTemp(const std::string& data) { return verifyOk && temp==data; }
  bool replace() { if (!renameOk) return false; original=temp; temp.clear(); return true; }
  void cleanup() { temp.clear(); }
};
int main() {
  Config c; JsonDocument doc;
  assert(c.manualBrightness==80 && c.sleepEnds==6 && c.combineArrivals);
  deserializeJson(doc, R"({"emtStopName":"EMT Madrid","uiBusMs":60000})");
  assert(emt::decodeConfig(doc,c));
  assert(c.uiBusMs==60000 && c.emtStopName=="EMT Madrid" && c.uiWeatherMs==10000);
  deserializeJson(doc, R"({"uiBusMs":-1,"uiWeatherMs":999999,"uiBus2Ms":10000,"brightnessDay":999,"sleepEnds":300})");
  assert(emt::decodeConfig(doc,c));
  assert(c.uiBusMs==5000 && c.uiBus2Ms==0 && c.uiWeatherMs==120000);
  assert(c.brightnessDay==255 && c.sleepEnds==23);
  c.emtPassword="test & \" quoted"; c.manualBrightness=111;
  emt::encodeConfig(c,doc);
  std::string bytes; serializeJson(doc,bytes);
  JsonDocument read; assert(!deserializeJson(read,bytes));
  Config restored; assert(emt::decodeConfig(read,restored));
  assert(restored.emtPassword==c.emtPassword && restored.manualBrightness==111);
  deserializeJson(doc,"[]"); assert(!emt::decodeConfig(doc,c));
  assert(c.uiBusMs==45000 && c.manualBrightness==80);
  for(int fault=0; fault<4; ++fault) {
    Store store; store.writeOk=fault!=0; store.verifyOk=fault!=1; store.renameOk=fault!=2;
    Config active; Config candidate; candidate.uiBusMs=90000;
    bool ok=emt::commitConfig(candidate,[&](const Config&){return emt::verifiedReplace(store,std::string("new"));},[&](const Config& v){active=v;});
    assert(ok==(fault==3));
    assert(active.uiBusMs==(ok?90000U:45000U));
    assert(store.original==(ok?"new":"old") && store.temp.empty());
  }
  puts("config: legacy/defaults, ranges, JSON roundtrip, failed write/read/rename and commit PASS");
}
