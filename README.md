## pinout
test
pins are gpio20 for rx and gpio21 for tx for modem

## MQTT preparation and testing

`src/mqtt.cpp` uses PubSubClient 2.8 over the TCP client returned by
`lteClient()`. After LTE setup succeeds, `mqttSetup()` configures MQTT and
`mqttLoop()` maintains it. A failed connection is retried after at least
10 seconds; the connect call itself can block while the modem opens a socket.
This does not restore a lost LTE data session or retry a failed initial LTE setup.

Before testing, set `host`, `port`, and `testTopic` in `src/mqtt_config.h`
to the agreed broker settings. The host is intentionally empty, so MQTT is
disabled until configured. The default topic is a proposed test topic, not an
agreed team interface. Set username/password only if required; do not commit
credentials. The existing LTE client uses plain TCP, not TLS. A TLS-only broker
needs a separate transport change rather than just changing the port to 8883.

1. Build with PlatformIO (`pio run` or the VS Code Build action).
2. On a computer, subscribe to the configured test topic using an MQTT client.
3. With the modem powered and SIM/antenna connected, upload the firmware and
   open the serial monitor at 115200 baud.
4. Expect `lte data ready`, `mqtt configured`, `mqtt connected`, then
   `mqtt test publish sent`. The subscriber should receive `LTE tracker online`.
   This is a non-retained QoS 0 test message, sent once per MQTT connection;
   the serial log only confirms a successful local send, not broker receipt.
5. Restart the broker or interrupt its connection, then restore it. Verify the
   device reconnects and the subscriber receives another test message.

The client ID is derived from the ESP32 chip ID to avoid collisions between
trackers. Connection failures print PubSubClient's numeric `state()` value;
see its API reference below for meanings. No GPS payload or command subscription
is implemented yet.

## references
- [esp32 uart](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/serial.html)
- [tinygsm mqtt ex](https://github.com/vshymanskyy/TinyGSM/blob/v0.12.0/examples/MqttClient/MqttClient.ino)
- [sim7080g at commands](https://download.mikroe.com/documents/datasheets/SIM70x0_AT_Command.pdf)
- [mqtt basics](https://mqtt.org/)
- [PubSubClient API](https://pubsubclient.knolleary.net/api)
