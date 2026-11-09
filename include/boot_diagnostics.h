#pragma once
#include "boot_trace.h"
void beginBootDiagnostics();
void recordStage(emt::Stage stage);
void recordRestartIntent(emt::RestartIntent intent);
const char* bootResetReason();
const char* bootPreviousStage();
const char* bootPreviousIntent();
uint32_t bootPreviousUptimeMs();
bool bootHasPreviousTrace();
