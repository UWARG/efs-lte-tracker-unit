#pragma once

#include <stdint.h>

struct gps_data;

struct TelemetryRecord {
  char id[48];
  char row[128];
};

bool telemetryBegin();
void telemetryAppend(const gps_data &fix);
bool telemetryPeek(TelemetryRecord &record);
bool telemetryAcknowledge(const char *id);
uint32_t telemetryDropped();
uint32_t telemetryUtcNow();
