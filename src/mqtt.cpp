#include <Arduino.h>
#include <PubSubClient.h>
#include "mqtt.h"
#include "mqtt_config.h"

namespace {
PubSubClient mqtt;
bool configured = false;
bool attemptedConnection = false;
bool testPublished = false;
unsigned long lastAttemptMs = 0;
char clientId[32];
}

void mqttSetup(Client &client) {
  mqtt.setClient(client);
  mqtt.setServer(mqttConfig::host, mqttConfig::port);
  mqtt.setKeepAlive(60);
  mqtt.setSocketTimeout(10);

  // A chip-specific ID avoids disconnecting another tracker using the same broker.
  const uint64_t chipId = ESP.getEfuseMac();
  snprintf(clientId, sizeof(clientId), "lte-%04x%08lx",
           static_cast<unsigned int>((chipId >> 32) & 0xffff),
           static_cast<unsigned long>(chipId & 0xffffffff));
  attemptedConnection = false;
  testPublished = false;
  configured = mqttConfig::host[0] != '\0' && mqttConfig::testTopic[0] != '\0';
  Serial.println(configured ? "mqtt configured" :
                 "mqtt disabled: set host and testTopic in mqtt_config.h");
}

void mqttLoop() {
  if (!configured) {
    return;
  }

  if (!mqtt.connected()) {
    const unsigned long now = millis();
    if (attemptedConnection && now - lastAttemptMs < mqttConfig::reconnectIntervalMs) {
      return;
    }
    attemptedConnection = true;
    Serial.println("connecting mqtt");
    const bool connected = mqttConfig::username[0] != '\0'
        ? mqtt.connect(clientId, mqttConfig::username, mqttConfig::password)
        : mqtt.connect(clientId);
    // Space attempts from completion: cellular socket connection can take a while.
    lastAttemptMs = millis();
    if (!connected) {
      Serial.print("mqtt connect failed, state: ");
      Serial.println(mqtt.state());
      return;
    }
    Serial.println("mqtt connected");
    testPublished = false;
  }

  if (!mqtt.loop()) {
    return;
  }
  if (!testPublished) {
    testPublished = mqtt.publish(mqttConfig::testTopic, "LTE tracker online", false);
    Serial.println(testPublished ? "mqtt test publish sent" : "mqtt test publish failed");
    if (!testPublished) {
      // Reconnect on a failed send rather than retrying on every loop iteration.
      mqtt.disconnect();
      lastAttemptMs = millis();
    }
  }
}
