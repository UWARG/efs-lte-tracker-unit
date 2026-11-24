#pragma once

#include <stdint.h>

// copy this to mqtt_config.h and fill in the broker settings
constexpr char mqttHost[] = "";
constexpr uint16_t mqttPort = 8883;
constexpr char mqttClientId[] = "tracker-01";
constexpr char mqttUsername[] = "";
constexpr char mqttPassword[] = "";

// paste the pem root ca for the broker here
constexpr char mqttCa[] = R"pem()pem";
