#include <Arduino.h>
#include <ESP_SSLClient.h>
#include <MQTTClient.h>
#include <cstring>
#include "mqtt.h"
#include "telemetry.h"

#if __has_include("mqtt_config.h")
#include "mqtt_config.h"
#else
#include "mqtt_config.example.h"
#endif

namespace {
constexpr char telemetryTopic[] = "tracker/telemetry";
constexpr char ackTopic[] = "tracker/telemetry/ack";
constexpr unsigned long connectRetryMs = 2000;
constexpr unsigned long maxConnectRetryMs = 30000;
constexpr unsigned long ackRetryMs = 5000;
constexpr unsigned long maxAckRetryMs = 20000;
constexpr unsigned long queuePollMs = 100;

ESP_SSLClient tls;
MQTTClient mqtt(512);
Client *transport = nullptr;
unsigned long lastConnectAttempt = 0;
unsigned long connectWaitMs = 0;
unsigned long lastQueueCheck = 0;
unsigned long lastPublish = 0;
unsigned long ackWaitMs = 0;
TelemetryRecord pending{};
bool ackReceived = false;

void receiveAck(MQTTClient *, char topic[], char payload[], int length) {
  const size_t idLength = strlen(pending.id);
  if (idLength != 0 && strcmp(topic, ackTopic) == 0 &&
      static_cast<int>(idLength) == length && memcmp(payload, pending.id, idLength) == 0) {
    ackReceived = true;
  }
}

void recordAck() {
  if (!ackReceived) {
    return;
  }
  ackReceived = false;
  if (telemetryAcknowledge(pending.id)) {
    pending.id[0] = '\0';
    ackWaitMs = 0;
  }
}

void retryConnection() {
  lastConnectAttempt = millis();
  connectWaitMs = connectWaitMs ? min(connectWaitMs * 2, maxConnectRetryMs) : connectRetryMs;
}

bool connectBroker() {
  const uint32_t utcNow = telemetryUtcNow();
  if (utcNow == 0 || millis() - lastConnectAttempt < connectWaitMs) {
    return false;
  }

  tls.setX509Time(utcNow);
  if (!tls.connect(mqttHost, mqttPort) || !tls.isSecure()) {
    Serial.println("mqtt tls connection failed");
    // a failed handshake can leave the underlying tcp socket open
    transport->stop();
    retryConnection();
    return false;
  }

  // tls is already connected so mqtt can start its session on that socket
  if (!mqtt.connect(mqttClientId, mqttUsername, mqttPassword, true) ||
      !mqtt.subscribe(ackTopic, 1)) {
    Serial.printf("mqtt connection or ack subscription failed %d %d\n",
                  static_cast<int>(mqtt.lastError()), static_cast<int>(mqtt.returnCode()));
    tls.stop();
    retryConnection();
    return false;
  }

  Serial.println("mqtt connected");
  connectWaitMs = 0;
  ackWaitMs = 0;
  return true;
}
} // namespace

bool mqttSetup(Client &client) {
  if (!*mqttHost || !mqttPort || !*mqttClientId || !*mqttUsername || !*mqttPassword || !*mqttCa) {
    Serial.println("mqtt settings missing");
    return false;
  }
  // esp sslclient stores the hostname in a 64 byte buffer
  if (strlen(mqttHost) >= 64) {
    Serial.println("mqtt hostname too long");
    return false;
  }

  tls.setClient(&client);
  tls.setCACert(mqttCa);
  tls.setSSLVersion(BR_TLS12, BR_TLS12);
  // allow full tls records from the broker during the handshake
  tls.setBufferSizes(16384, 512);
  tls.setHandshakeTimeout(20);
  tls.setTimeout(10);
  mqtt.begin(tls);
  mqtt.setOptions(30, true, 5000);
  mqtt.onMessageAdvanced(receiveAck);
  transport = &client;
  return true;
}

void mqttPause() {
  if (transport) {
    if (tls.isSecure()) {
      tls.stop();
    } else if (transport->connected()) {
      transport->stop();
    }
    connectWaitMs = 0;
  }
}

void mqttLoop() {
  if (!transport || (!mqtt.connected() && !connectBroker())) {
    return;
  }

  const bool connected = mqtt.loop();
  recordAck();
  if (!connected) {
    retryConnection();
    return;
  }

  const unsigned long now = millis();
  if (now - lastQueueCheck < queuePollMs || now - lastPublish < ackWaitMs) {
    return;
  }
  lastQueueCheck = now;

  TelemetryRecord next;
  if (!telemetryPeek(next)) {
    pending.id[0] = '\0';
    ackWaitMs = 0;
    return;
  }

  // check the queue again before retrying in case the record has expired
  if (strcmp(pending.id, next.id) != 0) {
    pending = next;
    ackWaitMs = 0;
  }

  const bool published = mqtt.publish(telemetryTopic, pending.row, false, 1);
  lastPublish = millis();
  ackWaitMs = ackWaitMs ? min(ackWaitMs * 2, maxAckRetryMs) : ackRetryMs;
  recordAck();
  if (!published) {
    retryConnection();
  }
}
