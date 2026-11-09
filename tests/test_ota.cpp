#include "ota_policy.h"
#include <string>
#include <vector>
#include <cassert>
#include <cstdio>
struct Reader {
  int left=2048; bool stalled=false, connectedFlag=true;
  int available(){return stalled?0:(left>0?64:0);}
  bool connected(){return connectedFlag && (left>0 || stalled);}
  int read(uint8_t* data,size_t n){for(size_t i=0;i<n;++i)data[i]=0x42;left-=n;return n;}
};
struct Writer { int count=0; bool fail=false; bool write(uint8_t*,size_t n){count+=n;return !fail;} };
int main() {
  for(auto v:{"0.12","v0.13.0","1.0.0","65535.0"}) {emt::Version p;assert(emt::parseVersion(v,p));}
  for(auto v:{"","1","0.13beta","1.2.3.4","1.-1","1.2.","65536.0","999999999999.1"}) {emt::Version p;assert(!emt::parseVersion(v,p));}
  assert(emt::newer("v0.13.0","0.12")); assert(emt::newer("0.10","0.9"));
  assert(!emt::newer("0.12","0.13.0") && !emt::newer("0.13","0.13.0"));
  JsonDocument d;
  d["tag_name"]="v0.14.0";d["draft"]=false;d["prerelease"]=false;
  d["assets"][0]["name"]="bootloader.bin";
  auto a=d["assets"][1].to<JsonObject>();
  a["name"]="firmware.bin";a["state"]="uploaded";a["size"]=1024;
  a["browser_download_url"]="https://github.com/Karimbelmonte/Pantalla-EMT/releases/download/v0.14.0/firmware.bin";
  a["digest"]="sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  emt::OtaCandidate<std::string> result;
  assert(emt::selectRelease(d,"0.13.0",1966080,result) && result.size==1024);
  assert(!emt::selectRelease(d,"0.14.0",1966080,result));
  assert(!emt::selectRelease(d,"0.13.0",512,result));
  a["size"]=-1;assert(!emt::selectRelease(d,"0.13.0",1966080,result));a["size"]=1024;
  a["digest"]="";assert(!emt::selectRelease(d,"0.13.0",1966080,result));
  a["digest"]="sha256:0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
  d["assets"][2].set(a);assert(!emt::selectRelease(d,"0.13.0",1966080,result));
  assert(!emt::downloadUrl("http://github.com/file"));
  assert(!emt::downloadUrl("https://release-assets.githubusercontent.com.evil/file"));
  assert(emt::downloadUrl("https://release-assets.githubusercontent.com/file"));
  for(int fault=0;fault<5;++fault) {
    Reader input;Writer writer;uint32_t now=0xfffffff0;
    if(fault==1) input.left=1000; // truncated
    if(fault==2) input.stalled=true; // idle timeout
    if(fault==3) writer.fail=true; // flash write failure
    auto clock=[&](){return now;};
    bool ok=emt::transferImage(input,writer,2048,clock,[&](){now+=(fault==4?9000:1);});
    assert(ok==(fault==0));
    if(ok) assert(writer.count==2048);
  }
  puts("ota: numeric versions, exact unique asset, size/digest/URL checks, interrupted/stalled/write-failed transfers PASS");
}
