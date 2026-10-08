#include "oled_render.h"

#if RAILY_OLED
#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <new>
#include "oled_compose.h"

// Which end of the glass is up on the keyring is a bench finding (spec §1,
// slice S0): R1 or R3, the same mappings as U8G2_R1 / U8G2_R3.
#ifndef RAILY_OLED_ROTATION
#define RAILY_OLED_ROTATION OLED_ROTATION_R1
#endif

static const uint8_t OLED_I2C_ADDRESS = 0x3C;
static const uint32_t OLED_I2C_HZ = 400000;  // the nRF52840 TWIM ceiling (spec §3)

// Built in place on the first probe, not as a global: no display code runs
// before setup() or before BLE is up. U8G2_R0 because the composer rotates.
alignas(U8G2_SSD1306_128X64_NONAME_F_HW_I2C) static uint8_t displayStorage[sizeof(U8G2_SSD1306_128X64_NONAME_F_HW_I2C)];
static U8G2_SSD1306_128X64_NONAME_F_HW_I2C* display = nullptr;
static bool probed = false;
static bool present = false;
static bool panelOn = false;
static bool imageValid = false;
static ScreenFrame shown = {};
static uint8_t shownContrast = 0;
// Loop task only; static so composing costs the 4 KB loop stack nothing.
static uint8_t canvas[OLED_CANVAS_BYTES];
static OledScratch scratch;

static void releaseBusPins() {
  nrf_gpio_cfg_default(g_ADigitalPinMap[PIN_WIRE_SDA]);
  nrf_gpio_cfg_default(g_ADigitalPinMap[PIN_WIRE_SCL]);
}

// The Expansion Board pulls SDA and SCL up; a bare XIAO leaves them
// floating. Discharge both lines through the internal pull-downs, let go,
// and read: external pull-ups bring them high within microseconds, a
// floating line stays low. (Reading with the ~13 kΩ pull-downs still on
// would form a divider with a 10 kΩ pull-up that can read low.)
static bool busPulledUp() {
  pinMode(PIN_WIRE_SDA, INPUT_PULLDOWN);
  pinMode(PIN_WIRE_SCL, INPUT_PULLDOWN);
  delayMicroseconds(100);
  pinMode(PIN_WIRE_SDA, INPUT);
  pinMode(PIN_WIRE_SCL, INPUT);
  delayMicroseconds(50);
  bool high = digitalRead(PIN_WIRE_SDA) == HIGH && digitalRead(PIN_WIRE_SCL) == HIGH;
  releaseBusPins();
  return high;
}

// Keyring Air (-DRAILY_IDLE_SLEEP=1, docs/pins/air-power-risks.md P2): the
// panel's charge pump runs from the cell (VBAT), so a sleeping panel turns
// it off too (8D 10 after AE) and back on before the display (8D 14, then
// AF, as the SSD1306 datasheet orders it). A P1 build leaves the pump as
// U8g2's init set it.
#if RAILY_IDLE_SLEEP
static const uint8_t SSD1306_CHARGE_PUMP = 0x8D;
static const uint8_t SSD1306_PUMP_OFF = 0x10;
static const uint8_t SSD1306_PUMP_ON = 0x14;
#endif

// The panel's sleep and wake, in the datasheet's order. Always inlined, so a
// P1 build compiles to the bare setPowerSave() calls it had before.
static inline __attribute__((always_inline)) void panelSleep() {
  display->setPowerSave(1);
#if RAILY_IDLE_SLEEP
  display->sendF("ca", SSD1306_CHARGE_PUMP, SSD1306_PUMP_OFF);
#endif
}

static inline __attribute__((always_inline)) void panelWake() {
#if RAILY_IDLE_SLEEP
  display->sendF("ca", SSD1306_CHARGE_PUMP, SSD1306_PUMP_ON);
#endif
  display->setPowerSave(0);
}

bool oledProbe() {
  if (probed) return present;
  probed = true;
  if (!busPulledUp()) return false;
  Wire.begin();
  Wire.setClock(OLED_I2C_HZ);
  Wire.beginTransmission(OLED_I2C_ADDRESS);
  if (Wire.endTransmission() != 0) {
    Wire.end();
    releaseBusPins();
    return false;
  }
  display = new (displayStorage) U8G2_SSD1306_128X64_NONAME_F_HW_I2C(U8G2_R0, U8X8_PIN_NONE);
  display->setBusClock(OLED_I2C_HZ);
  display->initDisplay();  // ends with the display off (0xAE)
  display->clearBuffer();
  display->sendBuffer();   // blank the panel RAM while it is still dark
  panelSleep();
  present = true;
  return true;
}

bool oledPresent() {
  return present;
}

void oledShow(const ScreenFrame& frame) {
  if (!present) return;
  if (frame.contrast == 0 || frame.scene == OLED_SCENE_OFF) {
    if (panelOn) {
      panelSleep();
      panelOn = false;
    }
    return;
  }
  if (!imageValid || !frame.sameImage(shown)) {
    oledCompose(frame, canvas, scratch);
    uint8_t* buffer = display->getBufferPtr();  // mirrors the panel RAM
    for (uint8_t ty = 0; ty < OLED_PANEL_PAGES; ty++) {
      int first = -1;
      int last = -1;
      for (uint8_t tx = 0; tx < OLED_PANEL_W / 8; tx++) {
        uint8_t tile[8];
        oledTile(canvas, RAILY_OLED_ROTATION, tx, ty, tile);
        uint8_t* dst = buffer + ty * OLED_PANEL_W + tx * 8;
        if (memcmp(dst, tile, sizeof(tile)) == 0) continue;
        memcpy(dst, tile, sizeof(tile));
        if (first < 0) first = tx;
        last = tx;
      }
      if (first >= 0) display->updateDisplayArea(first, ty, last - first + 1, 1);
    }
    shown = frame;
    imageValid = true;
  }
  if (frame.contrast != shownContrast) {
    display->setContrast(frame.contrast);
    shownContrast = frame.contrast;
  }
  if (!panelOn) {  // after the pixels: a wake never flashes the last scene
    panelWake();
    panelOn = true;
  }
}
#endif
