# Firmware source

This folder mirrors the Raily Pin firmware from Raily's main repository.

- firmware version: `0.3.0`
- source commit: `b7f6b2cf027fbd938fe31281be5d161555e3aea8`
- synced: 2026-10-08


Build: `arduino-cli compile -b Seeeduino:nrf52:xiaonRF52840 firmware/RailyPinsP1`
with the U8g2 library 2.37.1 installed (the keyring OLED, from 0.2.10-qa):
`ARDUINO_LIBRARY_ENABLE_UNSAFE_INSTALL=true arduino-cli lib install --git-url https://github.com/olikraus/U8g2_Arduino.git#2.37.1`.
Licence: Apache-2.0 (see `LICENSE` at the repository root).
