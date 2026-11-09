#pragma once
#include "Arduino.h"
struct IPStub {String toString(){return "192.0.2.1";}};
struct WiFiStub {int RSSI(){return -50;} IPStub localIP(){return {};}};
inline WiFiStub WiFi;
