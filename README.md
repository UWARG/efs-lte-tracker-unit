# efs-lte-tracker-unit

basic uart setup for the esp32-c3 and sim7080g. it sends `AT`, `ATE0` (echo off), and `ATI` (modem info) once at startup, then passes commands and replies between usb serial and the modem. just uses the arduino framework, no extra libraries.

build with the platformio build button or `pio run`. the build passes, but we don't have the board yet so the hardware is untested.

current pins are gpio20 for rx and gpio21 for tx. check these against the schematic when the board is available. tx goes to modem rx and rx goes to modem tx through the 3.3 v / 1.8 v level translator, with a common ground.

once we have the board:

1. power on the modem using the board's power-on procedure; the code doesn't control its power yet.
2. upload through the esp32's native usb port and open the serial monitor at 115200 with crlf endings.
3. opening the monitor starts the sequence after a five-second wait. look for two `OK` replies, then modem info and another `OK`. the code shows raw replies; it doesn't check them or retry. no sim needed.
4. if the modem wasn't ready, send `AT` manually and look for `OK`. later, with a sim inserted while power is off, restart and use `AT+CPIN?` to check it.

we'll add the rest once basic uart works on the board.

references:

- [esp32 uart](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/serial.html)
- [platformio serial monitor](https://docs.platformio.org/en/latest/core/userguide/device/cmd_monitor.html)
- [sim7080g at commands](https://download.mikroe.com/documents/datasheets/SIM70x0_AT_Command.pdf)
