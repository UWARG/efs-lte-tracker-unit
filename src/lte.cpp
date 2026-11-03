#include <Arduino.h>
#include "lte.h"


#define TINY_GSM_MODEM_SIM7080
#define TINY_GSM_DEBUG Serial
#include <TinyGsmClient.h>


static TinyGsm modem(Serial1);
static TinyGsmClient client(modem);

// first registration can take a while 
constexpr unsigned long networkTimeoutMs = 180000;

bool lteSetup(const char *apn) {
  Serial1.begin(115200, SERIAL_8N1, 20, 21); // esp rx, tx
  Serial.println("\nstarting lte uart check");
  Serial.println("the modem needs to be powered on already");

  // tinygsm sends at and retries for us while the modem finishes booting
  if (!modem.testAT(10000)) {
    Serial.println("uart check failed");
    return false;
  }
  Serial.println("uart check passed");

  // init handles echo and error reporting and sim checks without connecting lte data
  if (!modem.init()) {
    Serial.println("tinygsm init failed");
    return false;
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
    return false;
  } else {
    Serial.println("sim not ready");
    return false;
  }

  // show the signal before waiting so we have something useful if registration fails
  Serial.print("signal quality ");
  Serial.println(modem.getSignalQuality());

  // registration checks the carrier connection without opening a data session
  Serial.println("waiting for network registration");
  if (!modem.waitForNetwork(networkTimeoutMs)) {
    Serial.println("network registration timed out");
    return false;
  }
  Serial.println("registered on network");


  // registration alone is not enough to send data
  Serial.println("opening cellular data");
  if (!modem.gprsConnect(apn) || !modem.isGprsConnected()) {
    Serial.println("cellular data check failed");
    return false;
  }

  Serial.print("ip address ");
  Serial.println(modem.localIP());
  Serial.println("lte data ready");
  return true;
}

void lteLoop() {
  // let tinygsm read the uart so mqtt replies do not get sent to the usb monitor
  modem.maintain();
}

Client &lteClient() {
  return client;
}
