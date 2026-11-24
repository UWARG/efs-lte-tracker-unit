#pragma once

#include <Client.h>


bool mqttSetup(Client &client);
void mqttLoop();
void mqttPause();
