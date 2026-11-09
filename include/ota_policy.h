#pragma once
#include <stdint.h>
#include <stddef.h>
#include <cstring>
#include <ArduinoJson.h>

namespace emt {
struct Version { uint32_t part[3] = {0,0,0}; };
inline bool parseVersion(const char* text, Version& v) {
  v=Version{};
  if(*text=='v') ++text;
  for(int i=0; i<3; ++i) {
    if(*text<'0' || *text>'9') return false;
    while(*text>='0' && *text<='9') {
      uint32_t digit=*text++-'0';
      if(v.part[i]>(65535-digit)/10) return false;
      v.part[i]=v.part[i]*10+digit;
    }
    if(!*text) return i>=1; // major.minor or major.minor.patch only
    if(*text++!='.' || i==2) return false;
  }
  return false;
}
inline bool newer(const char* remote, const char* local) {
  Version r,l;
  if(!parseVersion(remote,r) || !parseVersion(local,l)) return false;
  for(int i=0; i<3; ++i) if(r.part[i]!=l.part[i]) return r.part[i]>l.part[i];
  return false;
}
inline bool prefix(const char* text, const char* start) { return std::strncmp(text,start,std::strlen(start))==0; }
inline bool firmwareUrl(const char* url) {
  return prefix(url,"https://github.com/Karimbelmonte/Pantalla-EMT/releases/download/");
}
inline bool downloadUrl(const char* url) {
  return firmwareUrl(url) || prefix(url,"https://release-assets.githubusercontent.com/")
    || prefix(url,"https://objects.githubusercontent.com/");
}
inline bool sha256Digest(const char* value) {
  if(!prefix(value,"sha256:") || std::strlen(value)!=71) return false;
  for(const char* p=value+7; *p; ++p) if(!((*p>='0' && *p<='9') || (*p>='a' && *p<='f'))) return false;
  return true;
}
template<class Text> struct OtaCandidate { Text version, url, digest; uint32_t size=0; };
template<class Text>
bool selectRelease(const JsonDocument& d, const char* local, uint32_t partitionSize, OtaCandidate<Text>& result) {
  result=OtaCandidate<Text>{};
  const char* version=d["tag_name"] | "";
  if(!newer(version,local) || !d["draft"].is<bool>() || !d["prerelease"].is<bool>()
     || d["draft"].as<bool>() || d["prerelease"].as<bool>()) return false;
  int matches=0;
  JsonObjectConst chosen;
  for(JsonObjectConst asset:d["assets"].as<JsonArrayConst>()) {
    if(std::strcmp(asset["name"] | "", "firmware.bin")==0) { ++matches; chosen=asset; }
  }
  if(matches!=1 || std::strcmp(chosen["state"] | "", "uploaded")!=0) return false;
  if(!chosen["size"].is<uint32_t>()) return false;
  uint32_t size=chosen["size"].as<uint32_t>();
  const char* url=chosen["browser_download_url"] | "";
  const char* digest=chosen["digest"] | "";
  if(size<32 || size>partitionSize || !firmwareUrl(url) || !sha256Digest(digest)) return false;
  result.version=version; result.url=url; result.digest=digest; result.size=size;
  return true;
}

// Copy exactly the declared image, bounded in both idle and total time. Writer
// commits the boot partition only AFTER this succeeds and its digest is checked.
template<class Reader, class Writer, class Clock, class Idle>
bool transferImage(Reader& input, Writer& output, uint32_t expected, Clock clock, Idle idle) {
  uint8_t buffer[1024];
  uint32_t total=0, started=clock(), progressed=started;
  while(total<expected) {
    uint32_t now=clock();
    if(static_cast<uint32_t>(now-started)>=180000 || static_cast<uint32_t>(now-progressed)>=10000) return false;
    int available=input.available();
    if(available>0) {
      size_t wanted=expected-total;
      if(wanted>sizeof(buffer)) wanted=sizeof(buffer);
      if(wanted>static_cast<size_t>(available)) wanted=available;
      int received=input.read(buffer,wanted);
      if(received<=0 || static_cast<size_t>(received)>wanted || !output.write(buffer,received)) return false;
      total+=received; progressed=clock();
    } else if(!input.connected()) return false;
    idle();
  }
  return true;
}
}
