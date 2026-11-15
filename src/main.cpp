#include <Arduino.h>
#include "gps_external.hpp"

#define LTE_QUEUE_SIZE 50
#define SD_CARD_QUEUE_SIZE 30

// GPS Task data
TaskHandle_t gpsExtTaskHandle = NULL;
gpsExternalDataQueues_t gpsExtDataQueues;

void setup() {
  Serial.begin(9600);
  
  // Set up output Queues for GPS data
  gpsExtDataQueues.gpsToLTEQueue = xQueueCreate(LTE_QUEUE_SIZE, sizeof(gps_data_t));
  gpsExtDataQueues.gpsToSDCardQueue = xQueueCreate(SD_CARD_QUEUE_SIZE, sizeof(gps_data_t));
  if(gpsExtDataQueues.gpsToLTEQueue == NULL) {
    Serial.println("Failed to create LTE queue!");
    return;
  }
  if(gpsExtDataQueues.gpsToSDCardQueue == NULL) {
    Serial.println("Failed to create SD card queue!");
    return;
  }

  // Set up GPS external sensor task
  if(xTaskCreate(gpsExternalTask, "gpsExternalTask", 10000, &gpsExtDataQueues, 2, &gpsExtTaskHandle) != pdPASS) {
    Serial.println("Failed to create task!");
    return;
  }

  xTaskNotify(gpsExtTaskHandle, RUN_TRACKING, eSetValueWithOverwrite);
  Serial.println("GPS task started");
}

void loop() {
  // put your main code here, to run repeatedly:
    delay(2000);
}