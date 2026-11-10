# Firmware source

This folder mirrors the Raily Pin firmware from Raily's main repository.

- firmware version: `0.4.5`
- source commit: `dd80ed71a4989e01ef4b29e53fd49c9646202518`
- synced: 2026-11-10


Build: `arduino-cli compile -b Seeeduino:nrf52:xiaonRF52840 firmware/RailyPinsP1`
with the U8g2 library 2.37.1 installed (the keyring OLED, from 0.2.10-qa):
`ARDUINO_LIBRARY_ENABLE_UNSAFE_INSTALL=true arduino-cli lib install --git-url https://github.com/olikraus/U8g2_Arduino.git#2.37.1`.
Licence: Apache-2.0 (see `LICENSE` at the repository root).
