#include <Arduino.h>
#include "lte.h"

// these need to be set before including tinygsm so it picks the right modem
#define TINY_GSM_MODEM_SIM7080
#define TINY_GSM_DEBUG Serial
#include <TinyGsmClient.h>

// keep the modem here so main only needs our lte functions
static TinyGsm modem(Serial1);

// first registration can take a while so keep this easy to adjust
constexpr unsigned long networkTimeoutMs = 180000;

void lteSetup() {
  Serial1.begin(115200, SERIAL_8N1, 20, 21); // esp rx, tx
  Serial.println("\nstarting lte uart check");
  Serial.println("the modem needs to be powered on already");

  // tinygsm sends at and retries for us while the modem finishes booting
  if (!modem.testAT(10000)) {
    Serial.println("uart check failed");
    return;
  }
  Serial.println("uart check passed");

  // init handles echo and error reporting and sim checks without connecting lte data
  if (!modem.init()) {
    Serial.println("tinygsm init failed");
    return;
  }

  Serial.println("tinygsm setup passed");
  Serial.print("modem: ");
  Serial.println(modem.getModemInfo());

  // init can accept a locked sim so check readiness separately
  const SimStatus sim = modem.getSimStatus();
  if (sim == SIM_READY) {
    Serial.println("sim ready");
  } else if (sim == SIM_LOCKED) {
    Serial.println("sim locked");
    return;
  } else {
    Serial.println("sim not ready");
    return;
  }

  // show the signal before waiting so we have something useful if registration fails
  Serial.print("signal quality ");
  Serial.println(modem.getSignalQuality());

  // registration checks the carrier connection without opening a data session
  Serial.println("waiting for network registration");
  if (!modem.waitForNetwork(networkTimeoutMs)) {
    Serial.println("network registration timed out");
    return;
  }
  Serial.println("registered on network");
  Serial.println("AT commands below");
}

void lteLoop() {
  // manual commands for now and tinygsm only uses the uart during setup
  if (Serial.available()) {
    Serial1.write(Serial.read());
  }
  if (Serial1.available()) {
    Serial.write(Serial1.read());
  }
}
