#pragma once

// open the modem uart and run the startup checks
void lteSetup();

// keep manual commands and modem replies moving through usb
void lteLoop();

