#include <Arduino.h>
#include "gps_external.hpp"
#include "lte.h"
#include "mqtt.h"
#include "telemetry.h"

constexpr char lteApn[] = "chatrweb.apn";
constexpr unsigned long lteCheckIntervalMs = 10000;
constexpr unsigned long lteRetryStartMs = 5000;
constexpr unsigned long lteRetryMaxMs = 60000;

static gpsExternalDataQueues_t gpsQueues;
static bool lteReady = false;
static unsigned long lastLteCheckMs = 0;
static unsigned long lteRetryMs = 0;

static void logGpsFixes(void *) {
  uint32_t lastDropped = 0;
  gps_data_t fix;
  for (;;) {
    if (xQueueReceive(gpsQueues.gpsToLTEQueue, &fix, pdMS_TO_TICKS(1000)) == pdPASS) {
      telemetryAppend(fix);
    }
    const uint32_t dropped = telemetryDropped();
    if (dropped != lastDropped) {
      Serial.printf("gps fixes expired before upload %lu\n", static_cast<unsigned long>(dropped));
      lastDropped = dropped;
    }
  }
}

static void startGpsLogging() {
  // keep both queues expected by the gps task
  gpsQueues.gpsToLTEQueue = xQueueCreate(5, sizeof(gps_data_t));
  gpsQueues.gpsToSDCardQueue = xQueueCreate(5, sizeof(gps_data_t));

  TaskHandle_t gpsTask = nullptr;
  if (gpsQueues.gpsToLTEQueue && gpsQueues.gpsToSDCardQueue &&
      xTaskCreate(gpsExternalTask, "gps", 10000, &gpsQueues, 2, &gpsTask) == pdPASS &&
      xTaskCreate(logGpsFixes, "gps log", 6144, nullptr, 1, nullptr) == pdPASS) {
    xTaskNotify(gpsTask, RUN_TRACKING, eSetValueWithOverwrite);
    return;
  }

  if (gpsTask) {
    vTaskDelete(gpsTask);
  }
  if (gpsQueues.gpsToLTEQueue) {
    vQueueDelete(gpsQueues.gpsToLTEQueue);
  }
  if (gpsQueues.gpsToSDCardQueue) {
    vQueueDelete(gpsQueues.gpsToSDCardQueue);
  }
  Serial.println("could not start gps logging");
}

void setup() {
  Serial.begin(115200);

  if (!telemetryBegin()) {
    Serial.println("gps flash unavailable");
  }
  startGpsLogging();

  // give us time to open the monitor
  const unsigned long usbStart = millis();
  while (!Serial && millis() - usbStart < 10000) {
    delay(10);
  }

  mqttSetup(lteClient());
}

void loop() {
  if (lteReady) {
    lteLoop();
    mqttLoop();
    if (millis() - lastLteCheckMs >= lteCheckIntervalMs) {
      lteReady = lteIsConnected();
      if (!lteReady) {
        Serial.println("lte connection lost");
        mqttPause();
      }
      lastLteCheckMs = millis();
    }
  } else if (millis() - lastLteCheckMs >= lteRetryMs) {
    lteReady = lteSetup(lteApn);
    lastLteCheckMs = millis();
    if (lteReady || lteRetryMs == 0) {
      lteRetryMs = lteRetryStartMs;
    } else {
      lteRetryMs = min(lteRetryMs * 2, lteRetryMaxMs);
    }
  }
  delay(1);
}
