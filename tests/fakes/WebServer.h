#pragma once
#include "Arduino.h"
#include <map>
#include <functional>
enum HTTPMethod {HTTP_GET,HTTP_POST};
class WebServer {
public:
  explicit WebServer(int){}
  std::map<std::pair<std::string,HTTPMethod>,std::function<void()>> routes;
  std::map<std::string,String> arguments;
  int code=0; String body;
  void on(const char* path,HTTPMethod method,std::function<void()> fn){routes[{path,method}]=fn;}
  bool hasArg(const char* key){return arguments.count(key)!=0;}
  String arg(const char* key){return arguments[key];}
  void send(int status,const char*,const String& content){code=status;body=content;}
  void sendHeader(const char*,const char*){}
  void request(const char* path,HTTPMethod method,std::map<std::string,String> args={}){
    arguments=args;code=0;body="";routes.at({path,method})();
  }
};
