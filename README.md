## pinout
test
pins are gpio20 for rx and gpio21 for tx for modem

## gps over lte

- gps uses uart0 with the pins from the external gps branch and the modem uses uart1
- littlefs keeps the last 5 min 10 s of valid fixes even with lte down
- set up a private hivemq broker and give the tracker and web app separate logins
- copy `include/mqtt_config.example.h` to `include/mqtt_config.h` and add the broker hostname, tracker login and root ca
- gps time is used to check the tls certificate before connecting

## mqtt topics

the tracker publishes one csv row without a header to `tracker/telemetry`

```csv
record_id,timestamp_utc,latitude_deg,longitude_deg,pdop
example-01,2026-11-24T18:42:10Z,43.4722850,-80.5448580,1.24
```

the web app backend saves each row once by `record_id` and publishes the id as plain text to `tracker/telemetry/ack` after saving. it also acks repeats. both topics use qos 1 with retain off. the backend writes the header once when exporting `gps.csv`

for a bench check, run with lte down for over 5 min 10 s and reboot the esp before reconnecting. check that only recent fixes are sent, each row shows up once in the csv and the tracker reports expired unsent fixes

## references
- [esp32 uart](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/serial.html)
- [tinygsm mqtt ex](https://github.com/vshymanskyy/TinyGSM/blob/v0.12.0/examples/MqttClient/MqttClient.ino)
- [sim7080g at commands](https://download.mikroe.com/documents/datasheets/SIM70x0_AT_Command.pdf)
- [mqtt basics](https://mqtt.org/)
