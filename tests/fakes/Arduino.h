#pragma once
#include <string>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <cmath>
#include <type_traits>
using byte=uint8_t;
#define PROGMEM
class String : public std::string {
public:
  using std::string::string;
  String(const std::string& s):std::string(s){}
  template<class T, typename std::enable_if<std::is_integral<T>::value,int>::type = 0>
  String(T n):std::string(std::to_string(n)){}
  bool isEmpty() const {return empty();}
  long toInt() const {return std::strtol(c_str(),nullptr,10);}
  void trim(){auto first=find_first_not_of(" \t\r\n");auto last=find_last_not_of(" \t\r\n");*this=first==npos?"":substr(first,last-first+1);}
  void replace(const char* from,const char* to){size_t pos=0;while((pos=find(from,pos))!=npos){std::string::replace(pos,std::strlen(from),to);pos+=std::strlen(to);}}
};
inline unsigned long fakeMillis=1000;
inline unsigned long millis(){return fakeMillis;}
inline void delay(unsigned long){}
struct ESPStub { void restart(){} }; inline ESPStub ESP;

struct SerialStub { template<class... Args> void printf(const char*, Args...){} };
inline SerialStub Serial;
