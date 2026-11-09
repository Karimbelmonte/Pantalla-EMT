#pragma once
#include <stdint.h>
void beginWifi(void (*portalScreen)());
void startApplicationServer();
void wifiTick();
bool wifiPortalActive();
void requestWifiSetup();
const char* wifiCredentialsAtBoot();
uint32_t wifiRetryInMs();
uint32_t wifiDisconnectCount();
uint16_t wifiLastDisconnectReason();
