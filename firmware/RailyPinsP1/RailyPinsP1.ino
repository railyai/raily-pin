#include <Adafruit_TinyUSB.h>
#include <bluefruit.h>
#include <nrf.h>
#include <atomic>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include "counter_select.h"
#include "press_command_gate.h"
#include "feedback_state.h"

using namespace Adafruit_LittleFS_Namespace;

static const char DEVICE_NAME[] = "Raily Pin P1";
static const char FW_VERSION[] = "0.2.3-qa";

BLEService railyService("7B1E0001-6F3A-4C2E-9A10-0D5C8F2A9E01");
BLECharacteristic deviceInfoChr("7B1E0002-6F3A-4C2E-9A10-0D5C8F2A9E01");
BLECharacteristic buttonEventChr("7B1E0003-6F3A-4C2E-9A10-0D5C8F2A9E01");
BLECharacteristic eventAckChr("7B1E0004-6F3A-4C2E-9A10-0D5C8F2A9E01");
BLECharacteristic pressCommandChr("7B1E0005-6F3A-4C2E-9A10-0D5C8F2A9E01");
BLECharacteristic deviceControlChr("7B1E0006-6F3A-4C2E-9A10-0D5C8F2A9E01");
BLEDis bledis;
BLEDfu bledfu;

char deviceId[24];
char deviceInfoJson[80];
uint32_t eventCounter = 0;
// The server enforces a monotone device_counter watermark per device, so
// the counter must survive reset/power loss — it lives in InternalFS and
// is written INSIDE emitButtonEvent before the BLE notify: the device may
// never emit a counter it has not committed to flash, or a power loss in
// that window would make the next press look like a replay.
// Legacy bare-u32 file — still WRITTEN on every persist so a downgrade to
// pre-slot firmware keeps a current counter instead of a stale floor.
static const char COUNTER_PATH[] = "/counter.bin";
// Record layout and restore selection live in counter_select.h (host-testable).
static const char* COUNTER_PATHS[2] = {"/counter_a.bin", "/counter_b.bin"};
static uint32_t counterSeq = 0;
static int8_t counterSlot = -1;  // slot holding the newest valid record this boot
static std::atomic<bool> connected(false);
static std::atomic<uint8_t> pendingFeedback(0);
static FeedbackState feedback = {};
static FeedbackLights renderedLights = {false, false, false};

struct ButtonEventPayload {
  uint32_t counter;
  uint32_t uptime_ms;
  uint8_t press_type;
} __attribute__((packed));

static void ledsOff() {
  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_BLUE, HIGH);
}

// Single builder for the device-info document: the GATT characteristic
// serves the bare form, the serial 'i' command serves it extended with
// live link state — one field list per mode, shared constants.
static int buildDeviceInfoJson(char* buf, size_t size, bool linkState) {
  if (linkState) {
    return snprintf(buf, size,
                    "{\"device_id\":\"%s\",\"fw\":\"%s\",\"ble_conn\":%d,\"ble_adv\":%d}",
                    deviceId, FW_VERSION,
                    Bluefruit.connected(), Bluefruit.Advertising.isRunning());
  }
  return snprintf(buf, size, "{\"device_id\":\"%s\",\"fw\":\"%s\"}", deviceId, FW_VERSION);
}

/// Persists the current counter to the slot that does not hold the last valid record.
static bool persistCounter() {
  // Ping-pong: overwrite the slot that is NOT the newest valid one, so a
  // torn write can never destroy the last good record.
  // Total for every counterSlot value, including -1 (no valid slot yet).
  int slot = (counterSlot == 0) ? 1 : 0;
  CounterRecord rec = {COUNTER_MAGIC, eventCounter, counterSeq + 1, 0};
  rec.checksum = counterChecksum(rec);
  File file(InternalFS);
  if (!file.open(COUNTER_PATHS[slot], FILE_O_WRITE)) return false;
  // FILE_O_WRITE is append mode in Adafruit_LittleFS — seek back to 0
  // so the file stays a single record.
  file.seek(0);
  size_t written = file.write((const uint8_t*)&rec, sizeof(rec));
  file.close();
  if (written != sizeof(rec)) return false;
  counterSeq = rec.seq;
  counterSlot = slot;
  // Best-effort dual-write of the legacy bare-u32 file AFTER the validated
  // slot commit: keeps it current so a downgrade to pre-slot firmware does
  // not resume from a stale floor, and gives restore a plausible bound for
  // torn-slot salvage. Failure here must not fail the persist — the slot
  // record is authoritative.
  File legacy(InternalFS);
  if (legacy.open(COUNTER_PATH, FILE_O_WRITE)) {
    legacy.seek(0);
    legacy.write((const uint8_t*)&eventCounter, sizeof(eventCounter));
    legacy.close();
  }
  return true;
}

/// Restores the newest valid counter record or salvages the safest fallback.
/// Storage reads stay here; the decision itself is selectCounterRecord()
/// (counter_select.h) so it can be unit-tested on the host.
/// Called once from setup() before emitMutex exists — single-context by
/// construction; all later persists run serialized under emitMutex.
static void restoreCounter() {
  static bool counterRestored = false;
  if (counterRestored) return;
  counterRestored = true;
  CounterSlotCandidate slots[2] = {};
  for (int slot = 0; slot < 2; slot++) {
    File file(InternalFS);
    if (!file.open(COUNTER_PATHS[slot], FILE_O_READ)) continue;
    CounterRecord rec = {};
    if (file.read(&rec, sizeof(rec)) == sizeof(rec)) {
      slots[slot].present = true;
      slots[slot].rec = rec;
    }
    file.close();
  }
  // The legacy bare-u32 file can coexist with a torn slot after an interrupted
  // upgrade. It has no integrity field, but remains a plausible lower bound.
  bool hasLegacy = false;
  uint32_t legacy = 0;
  File legacyFile(InternalFS);
  if (legacyFile.open(COUNTER_PATH, FILE_O_READ)) {
    hasLegacy = legacyFile.read(&legacy, sizeof(legacy)) == sizeof(legacy);
    legacyFile.close();
  }
  CounterDecision d = selectCounterRecord(slots[0], slots[1], hasLegacy, legacy);
  if (d.counter > eventCounter) eventCounter = d.counter;
  // d.seq is the newest generation seen even on the salvage path, so
  // adopting it keeps persisted seqs monotone; d.slot is -1 there, which
  // correctly aims the next persist at slot 0.
  counterSeq = d.seq;
  counterSlot = d.slot;
}

// emitButtonEvent runs from two contexts — the BLE write callback task and
// the serial 'p' handler in loop() — so the increment + flash commit must
// be serialized or two emits could tear the counter record.
static SemaphoreHandle_t emitMutex;

static void emitButtonEvent(uint8_t pressType) {
  if (emitMutex) xSemaphoreTake(emitMutex, portMAX_DELAY);
  eventCounter++;
  // Commit before the notify — a reset after this point can never rewind
  // the counter below a value the server may already have accepted. If the
  // flash write fails the press is dropped but the counter STAYS bumped:
  // a rolled-back counter could re-emit a value the flash record may
  // already hold, and the server would reject it as a replay. Gaps in the
  // counter are harmless for the > watermark check.
  if (!persistCounter()) {
    if (emitMutex) xSemaphoreGive(emitMutex);
    pendingFeedback.store(4, std::memory_order_relaxed);
    Serial.println("counter persist FAILED - press dropped");
    return;
  }
  ButtonEventPayload evt = {eventCounter, millis(), pressType};
  buttonEventChr.write(&evt, sizeof(evt));
  buttonEventChr.notify(&evt, sizeof(evt));
  if (emitMutex) xSemaphoreGive(emitMutex);
  Serial.print("press counter=");
  Serial.println(eventCounter);
}

static void onAckWrite(uint16_t, BLECharacteristic*, uint8_t* data, uint16_t len) {
  uint8_t pattern = len > 0 ? data[0] : 1;
  if (pattern == 0) pattern = 1;
  pendingFeedback.store(pattern, std::memory_order_relaxed);
}

// pressCommand accepts exactly one value: 1 = in-app press. The byte is
// untrusted radio input — anything else is dropped, and writes closer than
// the cooldown to the last emitted event are ignored (WRITE_WO_RESP flood).
static const uint8_t PRESS_TYPE_IN_APP = 1;
static PressCommandGate pressCommandGate = {};

static void onPressCommand(uint16_t, BLECharacteristic*, uint8_t* data, uint16_t len) {
  uint32_t now = millis();
  if (len == 1 && connected.load(std::memory_order_relaxed)
      && data[0] == PRESS_TYPE_IN_APP && pressCommandGate.allows(now)) {
    pressCommandGate.record(now);
    emitButtonEvent(PRESS_TYPE_IN_APP);
  }
}

// device_control accepts exactly one opcode: 0x01 reboot. 0x02 enter-DFU
// stays unimplemented — on open write permissions a DFU jump would let any
// nearby radio brick the OTA path (needs bonded writes, C12). Accepted
// commands are rate-limited to one per 60 s of uptime, which also blocks a
// reboot loop across reconnects. The reset is armed, not executed, inside
// the write callback: delaying there would eat the GATT write response,
// so loop() fires NVIC_SystemReset ~300 ms after the callback returns and
// the response is already on the air.
static const uint8_t DEVICE_CONTROL_REBOOT = 0x01;
static const uint32_t DEVICE_CONTROL_MIN_INTERVAL_MS = 60000;
static const uint32_t DEVICE_CONTROL_DRAIN_MS = 300;
// Written inside the BLE callback, read from loop() — volatile so the
// armed deadline cannot be cached in a register across contexts. A bool
// flag (not rebootAtMs != 0) marks the armed state so the once-per-49d
// wrap-to-0 case cannot silently lose a pending reboot.
static volatile bool rebootArmed = false;
static volatile uint32_t rebootAtMs = 0;

static void onDeviceControl(uint16_t, BLECharacteristic*, uint8_t* data, uint16_t len) {
  if (len != 1 || data[0] != DEVICE_CONTROL_REBOOT) return;
  uint32_t now = millis();
  if (now < DEVICE_CONTROL_MIN_INTERVAL_MS || rebootArmed) return;
  rebootAtMs = now + DEVICE_CONTROL_DRAIN_MS;
  rebootArmed = true;
  Serial.println("device_control: reboot armed");
}

static void onConnect(uint16_t) {
  connected.store(true, std::memory_order_relaxed);
}

static void onDisconnect(uint16_t, uint8_t) {
  connected.store(false, std::memory_order_relaxed);
}

static void startAdv() {
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  static const uint8_t kMfg[] = {0xFF, 0xFF, 0x52, 0x50, 0x31};
  Bluefruit.Advertising.addData(BLE_GAP_AD_TYPE_MANUFACTURER_SPECIFIC_DATA, kMfg, sizeof(kMfg));
  Bluefruit.Advertising.addUuid(railyService.uuid);
  Bluefruit.ScanResponse.addName();
  Bluefruit.Advertising.restartOnDisconnect(true);
  Bluefruit.Advertising.setInterval(32, 244);
  Bluefruit.Advertising.setFastTimeout(30);
  Bluefruit.Advertising.start(0);
}

void setup() {
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  ledsOff();
  Serial.begin(115200);
  uint32_t serialWait = millis();
  while (!Serial && millis() - serialWait < 4000) {
    delay(10);
  }

  snprintf(
      deviceId,
      sizeof(deviceId),
      "rp1-%08lx%08lx",
      (unsigned long)NRF_FICR->DEVICEID[0],
      (unsigned long)NRF_FICR->DEVICEID[1]);
  buildDeviceInfoJson(deviceInfoJson, sizeof(deviceInfoJson), false);

  InternalFS.begin();
  restoreCounter();
  emitMutex = xSemaphoreCreateMutex();

  Bluefruit.begin();
  Bluefruit.setTxPower(4);
  Bluefruit.setName(DEVICE_NAME);
  Bluefruit.Periph.setConnectCallback(onConnect);
  Bluefruit.Periph.setDisconnectCallback(onDisconnect);

  bledis.setManufacturer("Raily");
  bledis.setModel("Pin P1");
  bledis.setFirmwareRev(FW_VERSION);
  bledis.begin();

  bledfu.begin();

  railyService.begin();

  deviceInfoChr.setProperties(CHR_PROPS_READ);
  deviceInfoChr.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  deviceInfoChr.setMaxLen(sizeof(deviceInfoJson) - 1);
  deviceInfoChr.begin();
  deviceInfoChr.write(deviceInfoJson, strlen(deviceInfoJson));

  buttonEventChr.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
  buttonEventChr.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  buttonEventChr.setFixedLen(sizeof(ButtonEventPayload));
  buttonEventChr.begin();
  ButtonEventPayload zero = {0, 0, 0};
  buttonEventChr.write(&zero, sizeof(zero));

  eventAckChr.setProperties(CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP);
  eventAckChr.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  eventAckChr.setFixedLen(1);
  eventAckChr.begin();
  eventAckChr.setWriteCallback(onAckWrite);

  pressCommandChr.setProperties(CHR_PROPS_WRITE | CHR_PROPS_WRITE_WO_RESP);
  pressCommandChr.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  pressCommandChr.setFixedLen(1);
  pressCommandChr.begin();
  pressCommandChr.setWriteCallback(onPressCommand);

  // Write-with-response only — no WRITE_WO_RESP: the central must get the
  // response back before the link drops to know the command landed.
  deviceControlChr.setProperties(CHR_PROPS_WRITE);
  deviceControlChr.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  deviceControlChr.setFixedLen(1);
  deviceControlChr.begin();
  deviceControlChr.setWriteCallback(onDeviceControl);

  startAdv();
  Serial.println(deviceInfoJson);
  Serial.println("Type p + Enter to simulate a button press, i + Enter to reprint device info, r + Enter to reset");
}

void loop() {
  if (rebootArmed && (int32_t)(millis() - rebootAtMs) >= 0) {
    Serial.println("device_control: reboot");
    Serial.flush();
    delay(20);
    NVIC_SystemReset();
  }
  FeedbackLights lights = feedback.tick(
      millis(), connected.load(std::memory_order_relaxed),
      pendingFeedback.exchange(0, std::memory_order_relaxed));
  if (lights.red != renderedLights.red) digitalWrite(LED_RED, lights.red ? LOW : HIGH);
  if (lights.green != renderedLights.green) digitalWrite(LED_GREEN, lights.green ? LOW : HIGH);
  if (lights.blue != renderedLights.blue) digitalWrite(LED_BLUE, lights.blue ? LOW : HIGH);
  renderedLights = lights;
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (c == 'p' || c == 'P') emitButtonEvent(0);
    if (c == 'i' || c == 'I') {
      char line[128];
      int n = buildDeviceInfoJson(line, sizeof(line), true);
      if (n > 0 && n < (int)sizeof(line)) Serial.println(line);
    }
    if (c == 'r' || c == 'R') {
      delay(50);
      NVIC_SystemReset();
    }
  }
}
