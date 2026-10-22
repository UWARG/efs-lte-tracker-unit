#include <Arduino.h>

void setup() {
  Serial.begin(115200);

  //serial 1 is gonna be used for the uart at cmds
  Serial1.begin(115200, SERIAL_8N1, 20, 21); // rx, tx pins 

  while (!Serial) {
    delay(10); // wait for the usb serial monitor
  }

  delay(5000);
  Serial1.println("AT"); //check communication
  delay(1000);
  Serial1.println("ATE0");//turn command echo off
  delay(1000);
  Serial1.println("ATI"); //request modem identification
}

void loop() {
  if (Serial.available()) {
    Serial1.write(Serial.read());
  }
  if (Serial1.available()) {
    Serial.write(Serial1.read());
  }
}
