#pragma once

// Fill in the team's broker before hardware testing. An empty host disables MQTT.
// This uses the existing plain TCP LTE client, so the broker must support non-TLS MQTT.
namespace mqttConfig {
constexpr char host[] = "";
constexpr unsigned short port = 1883;
constexpr char username[] = "";
constexpr char password[] = "";
// Test topic only; confirm the team's topic before integration.
constexpr char testTopic[] = "warg/lte-tracker/test";
constexpr unsigned long reconnectIntervalMs = 10000;
} // namespace mqttConfig
