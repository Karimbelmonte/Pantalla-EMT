#pragma once
#include <Arduino.h>
#include "ota_policy.h"

// Manual requests are executed from loop(), after the HTTP response is sent.
bool requestOtaCheck();
bool requestOtaInstall(const String& version);
bool processManualUpdates();
bool otaBusy();
bool otaCanInstall();
String otaAvailableVersion();

// Network/flash backend, shared by manual and opt-in automatic updates.
bool findOtaCandidate(emt::OtaCandidate<String>& candidate);
bool installOtaCandidate(const emt::OtaCandidate<String>& candidate);
void restartAfterOta();
