#include "board_model.h"
#include "stop_detail.h"
#include <cassert>
#include <cstdio>
#include <string>
struct Service { const char* via="27"; const char* destination="Destino"; int timeToStation=120; uint32_t receivedAtMs=0; };
int main() {
  Service services[9]; emt::BoardRow rows[9];
  for(size_t n: {0,3,4,6,9}) {
    auto count=emt::buildRows(services,n,false,1000,rows,9);
    assert(count==n);
    bool second=emt::hasSecondPage(count,15000);
    assert(second==(n>3));
    assert(emt::firstPageDuration(45000,15000,second)+(second?15000:0)==45000);
    assert(!emt::hasSecondPage(count,0));
  }
  auto count=emt::buildRows(services,9,true,0,rows,9);
  assert(count==5 && rows[0].count==2 && rows[4].count==1);
  services[1].destination="Otro";
  count=emt::buildRows(services,3,true,0,rows,9);
  assert(count==2 && rows[0].count==2 && rows[1].count==1);
  services[0].receivedAtMs=0xffffff00;
  emt::buildRows(services,1,false,0x1000,rows,9);
  assert(rows[0].arrivals[0]==116);
  emt::buildRows(services,1,false,180000,rows,9);
  assert(rows[0].arrivals[0]==30);
  assert(std::string(emt::stopDisplayName("Manual","Oficial"))=="Manual");
  assert(std::string(emt::stopDisplayName("","Oficial"))=="Oficial");
  assert(std::string(emt::stopDisplayName("",""))=="EMT Madrid");
  assert(!emt::stopDetailComplete(true, true, "Oficial", false));
  assert(!emt::stopDetailComplete(true, false, "", true));
  assert(emt::stopDetailComplete(true, false, "Oficial", false));
  assert(emt::stopDetailComplete(false, true, "", true));
  JsonDocument d; emt::StopDetail detail;
  deserializeJson(d,R"({"code":"00","data":[{"stops":[{"name":"Cibeles","geometry":{"coordinates":[-3.69,40.42]}}]}]})");
  assert(emt::parseStopDetail(d,detail) && detail.hasCoords && std::string(detail.name)=="Cibeles");
  d["data"][0]["stops"][0].remove("name");
  assert(emt::parseStopDetail(d,detail) && detail.name[0]=='\0');
  d["data"][0]["stops"][0]["geometry"]["coordinates"][1]=999;
  assert(emt::parseStopDetail(d,detail) && !detail.hasCoords);
  d["code"]="80"; assert(!emt::parseStopDetail(d,detail));
  deserializeJson(d,R"({"code":"00","data":[]})"); assert(!emt::parseStopDetail(d,detail));
  puts("board: grouping, page timing, wrap, manual/auto fallback and EMT detail parser PASS");
}
