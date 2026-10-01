#pragma once

// The keyring OLED (docs/pins/keyring-oled.md): a 0.96" SSD1306 128 x 64 on
// the Expansion Board's I2C (D4 SDA, D5 SCL, 0x3C), shown portrait.
// -DRAILY_OLED=0 builds the LED-only firmware: nothing touches D4/D5 and
// U8g2 is not linked.
#ifndef RAILY_OLED
#define RAILY_OLED 1
#endif

#if RAILY_OLED
#include "screen_state.h"

// Thin U8g2 layer; every call comes from loop(), never from a BLE callback,
// and never before BLE advertising runs (the pre-flash gate: a fault before
// Bluefruit.begin() reaches neither USB nor BLE).
//
// oledProbe() runs once. It looks for the board's pull-ups before touching
// Wire (the core's Wire busy-waits, so a bus with no pull-ups could hang
// the loop task), then for an ACK at 0x3C. No pull-ups or no ACK: the
// renderer stays inert for the whole boot and the pin runs as the LED-only
// build. Returns whether a panel answered.
bool oledProbe();
bool oledPresent();

// Draws the frame if it differs from the one on the glass: only the 8 x 8
// tiles that changed go over I2C, contrast only when it moved, and a frame
// with contrast 0 puts the panel to sleep (charge pump off).
void oledShow(const ScreenFrame& frame);
#endif
