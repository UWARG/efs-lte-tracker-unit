#pragma once

#include <Client.h>

// Configure MQTT over the supplied LTE TCP client; call after LTE setup succeeds.
void mqttSetup(Client &client);
// Maintain MQTT and publish a test message once per connection. Call frequently.
void mqttLoop();
