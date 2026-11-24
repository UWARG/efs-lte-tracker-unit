#pragma once

#include <Client.h>

// return true once the modem has a working cellular data session
bool lteSetup(const char *apn);

// check whether the carrier and data session are still up
bool lteIsConnected();

// let tinygsm handle any pending modem replies
void lteLoop();

// mqtt will use this client for its connection
Client &lteClient();
