#include <Arduino.h>
#include <LittleFS.h>
#include <esp_partition.h>
#include <esp_timer.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <ctime>

#include "gps_external.hpp"
#include "telemetry.h"

namespace {
constexpr uint32_t retentionSeconds = 310;
constexpr uint32_t segmentSeconds = 10;
// keep one extra segment since the window can start partway through a file
constexpr uint32_t segmentCount = retentionSeconds / segmentSeconds + 1;
constexpr char flashLabel[] = "spiffs";
constexpr char ackPath[] = "/littlefs/gps-v1.ack";
constexpr char ackTempPath[] = "/littlefs/gps-v1.tmp";
constexpr uint32_t epoch2000 = 946684800;

struct StoredFix {
  uint32_t capturedAt;
  int32_t latitude;
  int32_t longitude;
  uint16_t pdop;
  uint16_t reserved;
};

static_assert(sizeof(StoredFix) == 16, "gps flash record size changed");

StoredFix segments[segmentCount][segmentSeconds]{};
SemaphoreHandle_t storeMutex = nullptr;
bool storeReady = false;
uint32_t clockEpoch = 0;
int64_t clockSetUs = 0;
uint32_t lastCapture = 0;
uint32_t lastPruned = 0;
uint32_t ackThrough = 0;
uint32_t savedAck = 0;
uint32_t dropped = 0;
unsigned long long deviceId = 0;

class StoreLock {
 public:
  StoreLock() : held(storeMutex && xSemaphoreTake(storeMutex, portMAX_DELAY) == pdTRUE) {}
  ~StoreLock() {
    if (held) {
      xSemaphoreGive(storeMutex);
    }
  }
  explicit operator bool() const { return held; }

 private:
  bool held;
};

bool isLeapYear(uint16_t year) {
  return year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
}

uint32_t captureTime(const gps_data_t &fix) {
  if (!fix.isValidTime || !fix.gnssFixOK || fix.isInvalid ||
      fix.year < 2000 || fix.year > 2105 || fix.month < 1 || fix.month > 12 ||
      fix.hour > 23 || fix.min > 59 || fix.second > 59) {
    return 0;
  }

  const uint8_t daysInMonth[] = {
      31, static_cast<uint8_t>(isLeapYear(fix.year) ? 29 : 28), 31, 30, 31, 30,
      31, 31, 30, 31, 30, 31,
  };
  if (fix.day < 1 || fix.day > daysInMonth[fix.month - 1]) {
    return 0;
  }

  uint32_t days = fix.day - 1;
  for (uint16_t year = 2000; year < fix.year; ++year) {
    days += isLeapYear(year) ? 366 : 365;
  }
  for (uint8_t month = 1; month < fix.month; ++month) {
    days += daysInMonth[month - 1];
  }
  return epoch2000 + days * 86400 + fix.hour * 3600 + fix.min * 60 + fix.second;
}

uint32_t utcNow() {
  if (clockEpoch == 0) {
    return 0;
  }
  return clockEpoch + (esp_timer_get_time() - clockSetUs) / 1000000;
}

uint32_t segmentSlot(uint32_t capturedAt) {
  return capturedAt / segmentSeconds % segmentCount;
}

void segmentPath(char (&path)[32], uint32_t slot) {
  snprintf(path, sizeof(path), "/littlefs/gps-v1-%02u.bin", static_cast<unsigned>(slot));
}

bool flashIsBlank() {
  const esp_partition_t *partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_SPIFFS, flashLabel);
  if (!partition) {
    return false;
  }

  uint8_t bytes[512];
  for (size_t offset = 0; offset < partition->size; offset += sizeof(bytes)) {
    size_t count = std::min<size_t>(sizeof(bytes), partition->size - offset);
    if (esp_partition_read_raw(partition, offset, bytes, count) != ESP_OK) {
      return false;
    }
    for (size_t i = 0; i < count; ++i) {
      if (bytes[i] != 0xff) {
        return false;
      }
    }
  }
  return true;
}

void recoverStore() {
  for (uint32_t slot = 0; slot < segmentCount; ++slot) {
    char path[32];
    segmentPath(path, slot);
    FILE *file = fopen(path, "rb");
    if (!file) {
      continue;
    }
    for (uint32_t i = 0; i < segmentSeconds; ++i) {
      StoredFix fix{};
      if (fread(&fix, sizeof(fix), 1, file) != 1) {
        break;
      }
      if (fix.capturedAt >= epoch2000 && segmentSlot(fix.capturedAt) == slot &&
          fix.capturedAt % segmentSeconds == i) {
        segments[slot][i] = fix;
        lastCapture = std::max(lastCapture, fix.capturedAt);
      }
    }
    fclose(file);
  }

  FILE *file = fopen(ackPath, "rb");
  if (file) {
    if (fread(&savedAck, sizeof(savedAck), 1, file) == 1) {
      ackThrough = savedAck;
    }
    fclose(file);
  }
}

bool syncAndClose(FILE *file, bool written) {
  bool synced = written && fflush(file) == 0 && fsync(fileno(file)) == 0;
  return fclose(file) == 0 && synced;
}

void saveAck() {
  FILE *file = fopen(ackTempPath, "wb");
  if (file && syncAndClose(file, fwrite(&ackThrough, sizeof(ackThrough), 1, file) == 1) &&
      rename(ackTempPath, ackPath) == 0) {
    savedAck = ackThrough;
  } else {
    Serial.println("gps ack checkpoint failed");
  }
}

void prune(uint32_t now) {
  if (now == 0 || now == lastPruned) {
    return;
  }
  lastPruned = now;

  uint32_t newest = 0;
  for (uint32_t slot = 0; slot < segmentCount; ++slot) {
    bool removed = false;
    bool hasFix = false;
    for (StoredFix &fix : segments[slot]) {
      if (fix.capturedAt == 0) {
        continue;
      }
      if (fix.capturedAt > now || now - fix.capturedAt >= retentionSeconds) {
        if (fix.capturedAt > ackThrough) {
          ++dropped;
        }
        fix = {};
        removed = true;
      } else {
        newest = std::max(newest, fix.capturedAt);
        hasFix = true;
      }
    }
    if (removed && !hasFix) {
      char path[32];
      segmentPath(path, slot);
      unlink(path);
    }
  }

  // reset a future watermark if gps corrected the time across a reboot
  if (ackThrough > now) {
    ackThrough = 0;
    saveAck();
  }
  if (lastCapture > now) {
    lastCapture = newest;
  }
}

const StoredFix *oldestPending() {
  const StoredFix *oldest = nullptr;
  for (auto &segment : segments) {
    for (const StoredFix &fix : segment) {
      if (fix.capturedAt > ackThrough &&
          (!oldest || fix.capturedAt < oldest->capturedAt)) {
        oldest = &fix;
      }
    }
  }
  return oldest;
}

void recordId(char (&id)[48], uint32_t capturedAt) {
  snprintf(id, sizeof(id), "%012llx-%lu", deviceId, static_cast<unsigned long>(capturedAt));
}

bool storeFix(const StoredFix &fix) {
  uint32_t slot = segmentSlot(fix.capturedAt);
  uint32_t index = fix.capturedAt % segmentSeconds;
  char path[32];
  segmentPath(path, slot);

  bool existingSegment = false;
  for (const StoredFix &previous : segments[slot]) {
    if (previous.capturedAt != 0) {
      existingSegment = true;
      break;
    }
  }

  FILE *file = fopen(path, existingSegment ? "r+b" : "wb");
  if (!file) {
    return false;
  }
  bool written = fseek(file, index * sizeof(fix), SEEK_SET) == 0 &&
                 fwrite(&fix, sizeof(fix), 1, file) == 1;
  if (!syncAndClose(file, written)) {
    return false;
  }
  segments[slot][index] = fix;
  return true;
}
}  // namespace

bool telemetryBegin() {
  storeMutex = xSemaphoreCreateMutex();
  if (!storeMutex) {
    return false;
  }

  deviceId = ESP.getEfuseMac();
  storeReady = LittleFS.begin();
  if (!storeReady && flashIsBlank()) {
    Serial.println("formatting blank gps flash");
    storeReady = LittleFS.format() && LittleFS.begin();
  }
  if (storeReady) {
    recoverStore();
  }
  return storeReady;
}

void telemetryAppend(const gps_data &fix) {
  uint32_t capturedAt = captureTime(fix);
  if (capturedAt == 0) {
    return;
  }

  StoreLock lock;
  if (!lock) {
    return;
  }
  if (capturedAt > utcNow()) {
    clockEpoch = capturedAt;
    clockSetUs = esp_timer_get_time();
  }
  if (!storeReady) {
    return;
  }

  uint32_t now = utcNow();
  prune(now);
  if (capturedAt <= lastCapture || now - capturedAt >= retentionSeconds ||
      !std::isfinite(fix.lat) || !std::isfinite(fix.lon) || !std::isfinite(fix.pdop) ||
      fix.lat < -90 || fix.lat > 90 || fix.lon < -180 || fix.lon > 180 ||
      fix.pdop < 0 || fix.pdop > 655.35) {
    return;
  }

  const StoredFix stored{
      capturedAt,
      static_cast<int32_t>(std::lround(fix.lat * 1e7)),
      static_cast<int32_t>(std::lround(fix.lon * 1e7)),
      static_cast<uint16_t>(std::lround(fix.pdop * 100)),
      0,
  };
  if (storeFix(stored)) {
    lastCapture = capturedAt;
  } else {
    Serial.println("gps flash write failed");
  }
}

bool telemetryPeek(TelemetryRecord &record) {
  StoreLock lock;
  if (!lock || !storeReady || clockEpoch == 0) {
    return false;
  }
  prune(utcNow());
  const StoredFix *fix = oldestPending();
  if (!fix) {
    return false;
  }

  recordId(record.id, fix->capturedAt);
  time_t epoch = fix->capturedAt;
  tm utc{};
  char timestamp[24];
  gmtime_r(&epoch, &utc);
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%dT%H:%M:%SZ", &utc);
  snprintf(record.row, sizeof(record.row), "%s,%s,%.7f,%.7f,%.2f",
           record.id, timestamp, fix->latitude / 1e7, fix->longitude / 1e7, fix->pdop / 100.0);
  return true;
}

bool telemetryAcknowledge(const char *id) {
  StoreLock lock;
  if (!lock || !storeReady || clockEpoch == 0 || !id) {
    return false;
  }
  prune(utcNow());
  const StoredFix *fix = oldestPending();
  if (!fix) {
    return false;
  }
  char expected[48];
  recordId(expected, fix->capturedAt);
  if (strcmp(id, expected) != 0) {
    return false;
  }
  ackThrough = fix->capturedAt;
  // checkpoint every segment and let the receiver dedupe any replay after reset
  if (ackThrough / segmentSeconds != savedAck / segmentSeconds) {
    saveAck();
  }
  return true;
}

uint32_t telemetryDropped() {
  StoreLock lock;
  if (!lock) {
    return 0;
  }
  if (storeReady) {
    prune(utcNow());
  }
  return dropped;
}

uint32_t telemetryUtcNow() {
  StoreLock lock;
  return lock ? utcNow() : 0;
}
