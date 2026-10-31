#include <Arduino.h>
#include "lte.h"

void setup() {
  Serial.begin(115200);

  // give us time to open the monitor 
  const unsigned long usbStart = millis();
  while (!Serial && millis() - usbStart < 10000) {
    delay(10);
  }

  lteSetup();
}

void loop() {
  lteLoop();
}
