#include <Arduino.h>
#include "lte.h"
#include "mqtt.h"

constexpr char lteApn[] = "chatrweb.apn";
static bool lteReady = false;

void setup() {
  Serial.begin(115200);

  // give us time to open the monitor
  const unsigned long usbStart = millis();
  while (!Serial && millis() - usbStart < 10000) {
    delay(10);
  }

  lteReady = lteSetup(lteApn);
  if (lteReady) {
    mqttSetup(lteClient());
  }
}

void loop() {
  if (lteReady) {
    lteLoop();
    mqttLoop();
  }
}
