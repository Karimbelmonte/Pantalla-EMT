#pragma once
#include <stdint.h>
#include <stddef.h>
#include <cstring>
namespace emt {
struct BoardRow {
  const char* line = "";
  const char* destination = "";
  int arrivals[2] = {0,0};
  int count = 0;
};
template<class Service>
size_t buildRows(const Service* services, size_t count, bool combine,
                 uint32_t now, BoardRow* rows, size_t capacity) {
  size_t used=0;
  for(size_t i=0; i<count; ++i) {
    const auto& svc=services[i];
    uint32_t elapsed=static_cast<uint32_t>(now-svc.receivedAtMs)/1000;
    if(elapsed>90) elapsed=90;
    int seconds=svc.timeToStation-static_cast<int>(elapsed);
    if(seconds<0) seconds=0;
    size_t j=0;
    for(; j<used; ++j) {
      if(std::strcmp(rows[j].line,svc.via)==0 && std::strcmp(rows[j].destination,svc.destination)==0
         && rows[j].count<(combine?2:1)) break;
    }
    if(j==used) {
      if(used==capacity) continue;
      rows[used]=BoardRow{};
      rows[used].line=svc.via; rows[used].destination=svc.destination;
      ++used;
    }
    rows[j].arrivals[rows[j].count++]=seconds;
  }
  return used;
}
inline bool hasSecondPage(size_t rowCount, uint32_t secondMs) { return rowCount>3 && secondMs>0; }
inline uint32_t firstPageDuration(uint32_t total, uint32_t second, bool shown) {
  return shown ? total-second : total;
}
inline const char* stopDisplayName(const char* manual, const char* resolved) {
  return manual[0] ? manual : (resolved[0] ? resolved : "EMT Madrid");
}
}
