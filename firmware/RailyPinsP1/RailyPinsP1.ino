#include <Adafruit_TinyUSB.h>
#include <bluefruit.h>
#include <nrf.h>
#include <atomic>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
#include <Wire.h>
#include "counter_select.h"
#include "press_command_gate.h"
#include "feedback_state.h"
#include "server_pass.h"
#include "secret_record.h"
#include "secret_seal.h"
#include "seal_format.h"
#include "seal_keys.h"
#include "bind_window.h"
#include "device_info.h"
#include "bootloader_kind.h"
#include "oled_render.h"
#include "screen_state.h"
#include "mascot_record.h"
#include "rhythm.h"
#include "da7280.h"
#include "bench_serial.h"
#include "screen_bench.h"
#include "bond_table.h"
#include "bond_cccd_file.h"
#include "press_hold.h"
#include "idle_sleep.h"
#include "ship_mode.h"
#include "device_name.h"
#include "wire_pullups.h"
#include "battery_curve.h"
#include "vdd_log.h"
#include "voice_board.h"
#include "voice_bench.h"
#include "voice_capture.h"
#include "voice_gesture.h"
#include "voice_ring.h"

using namespace Adafruit_LittleFS_Namespace;

static const char DEVICE_NAME[] = RAILY_DEVICE_NAME;  // the model: device_name.h
static const char FW_VERSION[] = "0.4.3";

BLEService railyService("7B1E0001-6F3A-4C2E-9A10-0D5C8F2A9E01");
BLECharacteristic deviceInfoChr("7B1E0002-6F3A-4C2E-9A10-0D5C8F2A9E01");
BLECharacteristic buttonEventChr("7B1E0003-6F3A-4C2E-9A10-0D5C8F2A9E01");
BLECharacteristic eventAckChr("7B1E0004-6F3A-4C2E-9A10-0D5C8F2A9E01");
BLECharacteristic pressCommandChr("7B1E0005-6F3A-4C2E-9A10-0D5C8F2A9E01");
BLECharacteristic deviceControlChr("7B1E0006-6F3A-4C2E-9A10-0D5C8F2A9E01");
// Server pass (docs/pins/security-bonding-secure-dfu.md §3): restart and
// DFU entry need a pass the Raily server computes with this pin's secret.
BLECharacteristic passChallengeChr("7B1E000C-6F3A-4C2E-9A10-0D5C8F2A9E01");
BLECharacteristic passChr("7B1E000D-6F3A-4C2E-9A10-0D5C8F2A9E01");
BLECharacteristic deviceSecretChr("7B1E000E-6F3A-4C2E-9A10-0D5C8F2A9E01");
// The keyring screen's state from the phone (docs/pins/keyring-oled.md §10).
BLECharacteristic screenStateChr("7B1E0014-6F3A-4C2E-9A10-0D5C8F2A9E01");
// BLE bonding stage 1 (docs/pins/ble-bonding.md): an encrypted read that
// makes iOS pair, and tells the app whether its link is admitted.
BLECharacteristic linkSecurityChr("7B1E0010-6F3A-4C2E-9A10-0D5C8F2A9E01");
// Battery telemetry (docs/pins/battery-telemetry-design.md): the power state
// (bit 0 USB present, bit 1 charging, bit 2 low, bit 3 critical) in the
// Raily service, open read +
// notify. The percent (standard Battery Service 0x180F / 0x2A19) only with
// -DRAILY_BAT_ON_XIAO=1, a board whose cell is on the XIAO's own BAT pads:
// on the Expansion Board kit the cell never reaches a XIAO pin, so P0.31
// means nothing there, no percent is served and the charging bit is 0.
#ifndef RAILY_BAT_ON_XIAO
#define RAILY_BAT_ON_XIAO 0
#endif
// The VDD discharge bench (serial v/V/L, a log in InternalFS): bench builds
// only (build_firmware.sh --bench). Its writes share the 28 KB volume with
// the counter, the secret and the bonds, so a release never carries it.
#ifndef RAILY_VDD_BENCH
#define RAILY_VDD_BENCH 0
#endif
BLECharacteristic powerStateChr("7B1E0007-6F3A-4C2E-9A10-0D5C8F2A9E01");
// Voice from the pin (docs/pins/voice-from-pin.md, slice V2): hold the D1
// button 2 s or more and talk while held; the pin streams IMA-ADPCM frames
// on voice_stream (NOTIFY) and takes a phone STOP on voice_control (WRITE).
// Both exist only on a board whose Sense IMU answers the boot probe (the
// IMU and the PDM mic come together on the Sense module), only on an
// encrypted link at the ATT level, and the pin streams only to a trusted
// link (owned, encrypted, bonded, admitted) that subscribed. -DRAILY_VOICE=0
// drops the feature; -DRAILY_VOICE_BENCH=1 (build_firmware.sh --voice-bench)
// adds the serial v/V/m/G/d bench (voice_bench.h), never in a release.
#ifndef RAILY_VOICE
#define RAILY_VOICE 1
#endif
#ifndef RAILY_VOICE_BENCH
#define RAILY_VOICE_BENCH 0
#endif
#if RAILY_VOICE_BENCH && !RAILY_VOICE
#error "RAILY_VOICE_BENCH needs RAILY_VOICE"
#endif
#if RAILY_VOICE_BENCH && RAILY_VDD_BENCH
#error "the voice bench and the VDD bench both own serial v/V: build one or the other"
#endif
#if RAILY_VOICE
BLECharacteristic voiceStreamChr("7B1E000A-6F3A-4C2E-9A10-0D5C8F2A9E01");
BLECharacteristic voiceControlChr("7B1E000B-6F3A-4C2E-9A10-0D5C8F2A9E01");
#endif
#if RAILY_BAT_ON_XIAO
BLEBas blebas;
#endif
BLEDis bledis;
// The Nordic legacy DFU service (BLEDfu) is gone on purpose: its control
// point let any nearby radio jump the pin into the bootloader. DFU entry
// is now a control pass (enterDfuArmed below).

char deviceId[24];
// The bootloader this pin runs (bootloader_kind.h), read once in setup()
// before the SoftDevice starts. Only OTAFIX takes a BLE update.
static BootloaderKind bootloaderKind = BOOTLOADER_UNKNOWN;
char deviceInfoJson[DEVICE_INFO_MAX];
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
// Authorize replies the SoftDevice refused; serial `i` prints it. A refused
// reply leaves the phone's ATT request unanswered until its 30 s timeout.
static volatile uint32_t authorizeReplyFailures = 0;
static FeedbackState feedback = {};
static FeedbackLights renderedLights = {false, false, false};
// A press for the screen (screen_state.h). emitButtonEvent also runs in the
// BLE task (in-app press), so it only raises these flags; loop() draws.
// pendingScreenPress: the press reached the phone (counter saved, notified).
// pendingScreenPressFailed: it was dropped (counter not saved): the screen
// shows «didn't work» at once, never a search the phone cannot answer.
static std::atomic<bool> pendingScreenPress(false);
static std::atomic<bool> pendingScreenPressFailed(false);
// The pin was dropped (screenOnFall): loop() plays the fall on the screen.
static std::atomic<bool> pendingScreenFall(false);
// event_ack 0x11 «found you»: its own flag, so a later ack byte in the same
// loop pass (the LED and motor mailboxes keep only the latest) cannot lose it.
static std::atomic<bool> pendingScreenFoundYou(false);
// The raw event_ack byte for the motor (rhythm.h): only bytes with a
// pattern, so a silent ack never cancels an important one.
static std::atomic<uint8_t> pendingRhythmAck(0);
// Notifications in the SoftDevice's queue (3 deep with BANDWIDTH_MAX), all
// characteristics together: every sent notify adds one, the BLE task's
// BLE_GATTS_EVT_HVN_TX_COMPLETE takes them back. Voice keeps one slot free
// for a press and never calls notify() without room, so it never waits.
#if RAILY_VOICE
static std::atomic<uint8_t> hvnInflight(0);
static inline bool noteNotify(bool sent) {
  if (sent) hvnInflight.fetch_add(1, std::memory_order_relaxed);
  return sent;
}
#else
static inline bool noteNotify(bool sent) { return sent; }
#endif

// Keyring Air sleep (idle_sleep.h, docs/pins/air-power-risks.md): loop()
// blocks on its task notification when it has nothing to do, so every flag
// another task raises for loop() also wakes it. Nothing in a P1 build.
#if RAILY_IDLE_SLEEP
static TaskHandle_t loopTask = NULL;  // set first thing in setup(), which runs on the loop task
static void wakeLoop() {
  if (loopTask) xTaskNotifyGive(loopTask);
}
static void wakeLoopFromISR() {
  if (!loopTask) return;
  BaseType_t woken = pdFALSE;
  vTaskNotifyGiveFromISR(loopTask, &woken);
  portYIELD_FROM_ISR(woken);
}
#define RAILY_WAKE_LOOP() wakeLoop()
#else
#define RAILY_WAKE_LOOP() ((void)0)
#endif

// Keyring Air ship mode (ship_mode.h, docs/pins/air-ship-mode.md): an
// unowned Air outside its bind window keeps the radio off and wakes on a
// 3 s hold. build_firmware.sh --air only; the P1 and DIY images never have
// it.
#if RAILY_SHIP_MODE && !(RAILY_BUTTON && RAILY_IDLE_SLEEP)
#error "RAILY_SHIP_MODE needs RAILY_BUTTON and RAILY_IDLE_SLEEP (build_firmware.sh --air)"
#endif

// The vibration motor (docs/pins/firmware.md «Vibration motor»): the
// DA7280 haptic driver, probed at boot in this order (da7280.h
// HapticMotor): on the hardware Wire bus D4/D5 (the Keyring Air carrier
// and the Air test bench, docs/pins/air-ship-mode.md «P1 vs Air»), on
// software I2C D6/D7 (the P1 Grove edition), and when neither answers the
// old Grove module on A0/D0 (D0 = P0.02; gestures-spec.md), a digital
// level with its own transistor, kept for P1 kits built before 2026-09-30.
// One image for both boards. -DRAILY_MOTOR=0 leaves D0, D4-D7 untouched
// (the panel still uses D4/D5); -DRAILY_MOTOR_ACTIVE_LOW=1 if the bench
// finds the Grove module active low (then D0 idles high).
#ifndef RAILY_MOTOR
#define RAILY_MOTOR 1
#endif
#ifndef RAILY_MOTOR_PIN
#define RAILY_MOTOR_PIN D0
#endif
#ifndef RAILY_MOTOR_ACTIVE_LOW
#define RAILY_MOTOR_ACTIVE_LOW 0
#endif
// The DA7280's lines on the Expansion Board's Grove UART port: SDA = D6,
// SCL = D7. Sources, all agreeing:
// - Seeed Grove System wiki (wiki.seeedstudio.com/Grove_System): a Grove
//   UART port is labelled from the base unit, pin 1 (yellow) = RX, pin 2
//   (white) = TX; a Grove I2C port is pin 1 = SCL, pin 2 = SDA.
// - The Seeed core's XIAO nRF52840 variant.h: PIN_SERIAL1_RX 7 (D7),
//   PIN_SERIAL1_TX 6 (D6). So the UART port carries D7 on pin 1, D6 on pin 2.
// - The wiring (owner, 2026-10-09): one Grove-to-Qwiic cable, 100 mm
//   (Adafruit 4528), from the UART port straight into the DA7280; no hub.
//   It is a Grove I2C cable: pin 1 yellow = SCL, pin 2 blue = SDA, and it
//   has no white wire (Adafruit 4528 product page).
// - hardware/raily-pin-public/case/motor-options.md: yellow = D7 = SCL,
//   blue = D6 = SDA.
// Not checked against the Expansion Board schematic itself. The boot probe
// also tries the swapped order once (da7280.h HapticMotor), and the boot
// line says which order answered.
#ifndef RAILY_HAPTIC_SDA_PIN
#define RAILY_HAPTIC_SDA_PIN D6
#endif
#ifndef RAILY_HAPTIC_SCL_PIN
#define RAILY_HAPTIC_SCL_PIN D7
#endif

// Serial `i` reports it: 0 on a bare XIAO, before the probe, or in the
// LED-only build.
static bool oledPresentNow() {
#if RAILY_OLED
  return oledPresent();
#else
  return false;
#endif
}

// device_info `oled` (device_info.h): unknown until loop() has probed the
// panel, then 1 or 0; the LED-only build has no panel from the start.
#if RAILY_OLED
static int8_t oledReported = DEVICE_INFO_OLED_UNKNOWN;
#else
static int8_t oledReported = 0;
#endif

// Server pass state. The BLE write-authorize callback (BLE task) verifies
// passes; loop() owns every flash write, the sealing and the actions.
// passMutex guards the secret, the owned flag and the nonce between them.
static const char SECRET_DIR[] = "/raily";
static const char SECRET_PATH[] = "/raily/device_secret.bin";
static const char SECRET_TEMP_PATH[] = "/raily/device_secret.new";
static uint8_t deviceSecret[SECRET_LENGTH];
static bool hasSecret = false;
static bool secretOwned = false;
static PassNonce passNonce = {};
static SemaphoreHandle_t passMutex;
// The seal (ECDH + AES-CCM on the CryptoCell) needs more stack than the
// Arduino loop task has: the core gives it 1024 words (LOOP_STACK_SZ), and
// on hardware the first seal ran it to 0 free words and hung genKeyPair
// (P4 bench, 0.2.4-qa). It runs on its own task instead; the loop task
// hands it one job at a time and waits (sealOnWorker).
static const uint16_t SEAL_TASK_STACK_WORDS = 2048;  // 8 KB, from the heap
static TaskHandle_t sealTask = NULL;
// Free words on the Bluefruit BLE task (1280 words), measured inside the
// pass callbacks that run there (the pass MAC frame is ~730 bytes).
static volatile uint32_t bleTaskLowWater = 0;
static SemaphoreHandle_t sealDone = NULL;
static struct {
  uint8_t secret[SECRET_LENGTH];
  uint8_t blob[SEAL_MAX_BLOB];
  size_t length;
} sealJob;
static uint8_t sealedSecret[SEAL_MAX_BLOB];
static size_t sealedSecretLength = 0;
enum PassAction : uint8_t {
  PASS_ACTION_NONE = 0,
  PASS_ACTION_REBOOT = 1,
  PASS_ACTION_ENTER_DFU = 2,
  PASS_ACTION_CONFIRM = 3,
  PASS_ACTION_RELEASE = 4,
  PASS_ACTION_FORGET_ALL = 5,
};
static std::atomic<uint8_t> pendingPassAction(PASS_ACTION_NONE);
static std::atomic<bool> linkSetupPending(false);
// Bumped under passMutex at every disconnect; a seal made for one link is
// published only if no disconnect happened meanwhile.
static uint32_t linkGeneration = 0;
// The next nonce, made in loop() (where the RNG may wait) so the BLE read
// callback only swaps it in and never depends on the RNG pool depth.
static uint8_t spareNonce[PASS_NONCE_LENGTH];
static bool spareNonceReady = false;
// Bind window (bind_window.h). The reset reason is the one the core's
// init() saved (readResetReason()). The window is guarded by passMutex:
// the pass_challenge read callback (BLE task) reads it.
static uint32_t bootResetReason = 0;
static bool freshSecretThisBoot = false;
static BindWindow bindWindow = {};
// screen_state mailbox, guarded by passMutex like the owned flag: the
// write-authorize callback (BLE task) checks ownership and stores an
// accepted payload in one critical section, a release clears it in the
// same section as it drops ownership, and loop() takes it.
static ScreenPayload screenMailbox = {};
static bool screenMailboxFull = false;
static bool screenMailboxTrusted = false;
// The screen's state machine (screen_state.h) and the mascot's flash
// record (mascot_record.h): loop task only. Built in both builds, so
// screen_state behaves the same with or without the panel.
static ScreenState screen = {};
static MascotPersist mascotStore = {};
// Serial `i`'s "scr" (screen_bench.h). The verdicts are counted in the
// write-authorize callback (BLE task), so they are atomics; the last
// applied write is loop-task RAM, written where loop() applies it. A
// release clears all of it with the mailbox (replaceSecret).
static std::atomic<uint32_t> screenWritesAccepted(0);
static std::atomic<uint32_t> screenWritesBadFormat(0);
static std::atomic<uint32_t> screenWritesBadValue(0);
static std::atomic<uint32_t> screenWritesNotOwned(0);
static ScreenBenchApplied screenLastApplied = {};
static const char MASCOT_PATH[] = "/raily/mascot.bin";
static void enforceBondCap();  // defined next to loop()
// BLE bonding stage 1 (docs/pins/ble-bonding.md, bond_table.h). Bluefruit
// pairs any phone (LESC Just Works) and keeps its bond files; the admitted
// table says which bonds the server admitted with an admit pass. The table
// is guarded by passMutex like the owned flag, so a release clears both in
// one section; loop() owns every flash write and the bond files. The BLE
// task only changes the table in RAM (an admission, a revoke on a fresh
// pairing) and marks it dirty.
static const char BONDS_PATH[] = "/raily/bonds.bin";
static const char BONDS_TEMP_PATH[] = "/raily/bonds.new";
static BondTable bondTable = {};
static bool bondTableDirty = false;
static std::atomic<uint8_t> bondFileCount(0);  // loop() counts; link_security and serial i read
// A pairing just completed: loop() enforces the bond cap a moment later,
// after Bluefruit's own deferred save has written the new bond file.
static const uint32_t BOND_CAP_DELAY_MS = 2000;
static std::atomic<bool> bondCapPending(false);
static std::atomic<uint32_t> bondCapDueMs(0);
static std::atomic<bool> serviceChangedCheckPending(false);
// The first attribute handle the application owns, from the SoftDevice after
// Bluefruit.begin(); 0 = unknown, and no Service Changed is sent. The range
// starts here, not at 0x0001: S140 6.1.1 refuses handles of its own GAP and
// GATT services with BLE_ERROR_INVALID_ATTR_HANDLE (0x3003, bench 2026-10-09).
static uint16_t serviceChangedStartHandle = 0;
// The Service Changed CCCD in the SoftDevice's GATT service (below the first
// user handle); 0 = not found. Read per link: the phone's subscription.
static uint16_t serviceChangedCccdHandle = 0;

// The CCCD after the Service Changed characteristic, as Nordic's peer
// manager finds it (service_changed_cccd, SDK 16+): attributes by UUID.
static uint16_t findServiceChangedCccd(uint16_t firstUserHandle) {
  bool scFound = false;
  for (uint16_t h = 1; h < firstUserHandle; h++) {
    ble_uuid_t uuid = {};
    if (sd_ble_gatts_attr_get(h, &uuid, NULL) != NRF_SUCCESS) continue;
    if (!scFound && uuid.uuid == BLE_UUID_GATT_CHARACTERISTIC_SERVICE_CHANGED) scFound = true;
    else if (scFound && uuid.uuid == BLE_UUID_DESCRIPTOR_CLIENT_CHAR_CONFIG) return h;
  }
  return 0;
}
// The link a Service Changed confirmation arrived on: loop() stores the build
// only when it is the link the indication was sent on.
static std::atomic<uint16_t> serviceChangedConfirmedHandle(BLE_CONN_HANDLE_INVALID);
// A bond file whose CCCD field Bluefruit wrote one byte late (D-5,
// bond_cccd_file.h): the BLE task put it back in place before Bluefruit
// read it; loop() prints what it did (bondCccdRepairLog: bit 15 the field
// was kept, bit 14 the rewrite worked, bits 0-7 how many bytes late).
static std::atomic<bool> bondCccdRepairPending(false);
static std::atomic<uint16_t> bondCccdRepairLog(0);
static const size_t BOND_FILE_MAX = 256;  // a bond file is about 130 bytes: keys, name, CCCDs
// The phone wrote the Service Changed CCCD (its value): loop() logs it.
static std::atomic<bool> serviceChangedCccdPending(false);
static std::atomic<uint16_t> serviceChangedCccdValue(0);
// The single peripheral link as the BLE task last saw it, for loop(): the
// BLE task deletes a BLEConnection on disconnect, so loop() never touches
// one and reads this copy instead (guarded by passMutex).
static LinkSecurity linkCache = {};
static uint16_t linkCacheHandle = BLE_CONN_HANDLE_INVALID;
// A release or serial x erased the bond of a bonded link that is still up:
// drop that link after the write answer is on the air, or Bluefruit would
// save its next CCCD write as a keyless bond file (BLEGatt.cpp saveCccd).
static volatile bool dropLinkArmed = false;
static volatile uint32_t dropLinkAtMs = 0;
static uint16_t dropLinkHandle = BLE_CONN_HANDLE_INVALID;  // loop task only
// BLE bonding stage 2 (ble-bonding.md §9.1, press_hold.h): presses and acks
// only on a trusted link. The last press (what a trusted button_event read
// returns) and the one held press, both guarded by passMutex; never a flash
// write inside those sections.
static uint8_t lastPressPayload[PRESS_PAYLOAD_LENGTH] = {};
static HeldPress heldPress = {};
// Writes the pin ignored because their link was not trusted (serial `i`).
static std::atomic<uint32_t> untrustedAcks(0);
static std::atomic<uint32_t> untrustedPressCommands(0);
// Bumped (passMutex) on every connect and disconnect: a held press is tied
// to the link it was made on (press_hold.h), so a handle the SoftDevice
// reuses for the next link never inherits it.
static uint32_t linkEpoch = 0;
static bool pressLinkReady(uint16_t* handle, uint32_t* epoch);  // defined next to linkTrustedLocked()

struct ButtonEventPayload {
  uint32_t counter;
  uint32_t uptime_ms;
  uint8_t press_type;
} __attribute__((packed));
static_assert(sizeof(ButtonEventPayload) == PRESS_PAYLOAD_LENGTH, "button_event is 9 bytes (press_hold.h)");

static void ledsOff() {
  digitalWrite(LED_RED, HIGH);
  digitalWrite(LED_GREEN, HIGH);
  digitalWrite(LED_BLUE, HIGH);
}

// Single builder for the device-info document: the GATT characteristic
// serves the bare form, the serial 'i' command serves it extended with
// live link state — one field list per mode, shared constants.
// Whole seconds this pin still serves its seal (device_info bind_s): 0 when
// owned, secretless or outside the window.
static uint32_t bindSecondsNow() {
  if (!passMutex) return 0;
  xSemaphoreTake(passMutex, portMAX_DELAY);
  uint32_t now = millis();
  bool serving = bindWindowServes(hasSecret, secretOwned, bindWindow.isOpen(now));
  uint32_t seconds = bindWindowSeconds(serving, bindWindow.remainingMs(now));
  xSemaphoreGive(passMutex);
  return seconds;
}

static int buildDeviceInfoJson(char* buf, size_t size, bool linkState) {
  uint32_t bindSeconds = bindSecondsNow();
  if (linkState) {
    bool owned = false;
    uint32_t windowSeconds = 0;  // the window's own clock, owned or not (bench check)
    if (passMutex) {
      xSemaphoreTake(passMutex, portMAX_DELAY);
      owned = secretOwned;
      windowSeconds = bindWindowSeconds(true, bindWindow.remainingMs(millis()));
      xSemaphoreGive(passMutex);
    }
    // Free stack words (high-water marks): the loop task and the seal task.
    return snprintf(buf, size,
                    "{\"device_id\":\"%s\",\"fw\":\"%s\",\"ble_conn\":%d,\"ble_adv\":%d,"
                    "\"owned\":%d,\"seal\":\"%s\",\"loop_hw\":%lu,\"seal_hw\":%lu,\"ble_hw\":%lu,"
                    "\"reply_err\":%lu,\"bind_s\":%lu,\"window_s\":%lu,\"reset\":\"0x%08lx\",\"oled\":%d,"
                    "\"bl\":\"%s\"}",
                    deviceId, FW_VERSION,
                    Bluefruit.connected(), Bluefruit.Advertising.isRunning(),
                    owned ? 1 : 0, SEAL_KEY_NAME,
                    (unsigned long)uxTaskGetStackHighWaterMark(NULL),
                    (unsigned long)(sealTask ? uxTaskGetStackHighWaterMark(sealTask) : 0),
                    (unsigned long)bleTaskLowWater,
                    (unsigned long)authorizeReplyFailures,
                    (unsigned long)bindSeconds, (unsigned long)windowSeconds,
                    (unsigned long)bootResetReason, oledPresentNow() ? 1 : 0, bootloaderKindName(bootloaderKind));
  }
  return formatDeviceInfo(buf, size, deviceId, FW_VERSION, bindSeconds, oledReported,
                          bootloaderKindName(bootloaderKind), RAILY_SHIP_MODE != 0);
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
    pendingScreenPressFailed.store(true, std::memory_order_relaxed);
    Serial.println("counter persist FAILED - press dropped");
    RAILY_WAKE_LOOP();
    return;
  }
  const uint32_t now = millis();
  ButtonEventPayload evt = {eventCounter, now, pressType};
  // Stage 2 (press_hold.h): notified only on a ready link, held while a
  // link is not ready yet, «didn't work» with no link at all.
  uint16_t handle = BLE_CONN_HANDLE_INVALID;
  uint32_t epoch = 0;
  const bool ready = pressLinkReady(&handle, &epoch);
  uint8_t earlier[PRESS_PAYLOAD_LENGTH];
  xSemaphoreTake(passMutex, portMAX_DELAY);
  memcpy(lastPressPayload, &evt, PRESS_PAYLOAD_LENGTH);
  // The link checked above is still the link: a disconnect in between
  // makes this a press with no link.
  const bool sameLink = handle != BLE_CONN_HANDLE_INVALID && handle == linkCacheHandle && epoch == linkEpoch;
  const PressRoute route = routePress(sameLink, ready);
  // A press still held goes out first: the phone never sees a counter go
  // back (the server would refuse the older one as a replay). One past its
  // deadline or from another link is given up instead.
  const HeldPressTick earlierTick = route == PRESS_NOTIFY ? heldPress.poll(true, true, now, epoch) : HELD_NONE;
  const bool sendEarlier = earlierTick == HELD_DELIVER;
  if (sendEarlier) memcpy(earlier, heldPress.payload, sizeof(earlier));
  const bool replaced = route == PRESS_HOLD && heldPress.hold((const uint8_t*)&evt, now, epoch);
  xSemaphoreGive(passMutex);
  bool sent = false;
  if (earlierTick == HELD_EXPIRED) pendingScreenPressFailed.store(true, std::memory_order_relaxed);
  if (route == PRESS_NOTIFY) {
    if (sendEarlier && !noteNotify(buttonEventChr.notify(handle, earlier, sizeof(earlier)))) {
      pendingScreenPressFailed.store(true, std::memory_order_relaxed);
    }
    // A link that dropped since the check fails the notify: «didn't work».
    sent = noteNotify(buttonEventChr.notify(handle, &evt, sizeof(evt)));
  }
  if (sent) {
    pendingScreenPress.store(true, std::memory_order_relaxed);
  } else if (route != PRESS_HOLD || replaced) {
    pendingScreenPressFailed.store(true, std::memory_order_relaxed);
  }
  if (emitMutex) xSemaphoreGive(emitMutex);
  RAILY_WAKE_LOOP();
  Serial.print("press counter=");
  Serial.print(eventCounter);
  Serial.println(sent ? "" : route == PRESS_HOLD ? " held (link not ready)" : " not sent");
}

// loop(): the held press goes out once its link is ready, or is given up
// after PRESS_HOLD_MS or a drop (press_hold.h).
static void serviceHeldPress() {
  // Advisory: the usual pass makes no SoftDevice call. poll() below decides
  // on whatever is held by then.
  xSemaphoreTake(passMutex, portMAX_DELAY);
  const bool held = heldPress.held;
  xSemaphoreGive(passMutex);
  if (!held) return;
  // Serialized with emitButtonEvent (same lock order: emitMutex, then
  // passMutex), so a held press and a newer one never go out of order.
  if (emitMutex) xSemaphoreTake(emitMutex, portMAX_DELAY);
  uint16_t handle = BLE_CONN_HANDLE_INVALID;
  uint32_t epoch = 0;
  const bool ready = pressLinkReady(&handle, &epoch);
  uint8_t payload[PRESS_PAYLOAD_LENGTH];
  xSemaphoreTake(passMutex, portMAX_DELAY);
  const bool sameLink = handle != BLE_CONN_HANDLE_INVALID && handle == linkCacheHandle && epoch == linkEpoch;
  const HeldPressTick tick = heldPress.poll(sameLink, ready, millis(), epoch);
  memcpy(payload, heldPress.payload, sizeof(payload));
  xSemaphoreGive(passMutex);
  bool sent = tick == HELD_DELIVER && noteNotify(buttonEventChr.notify(handle, payload, sizeof(payload)));
  bool retry = false;
  if (tick == HELD_DELIVER && !sent) {
    // A notify that failed (no buffer, a moment's race) is tried again on
    // the next pass, still bounded by the press's own deadline.
    xSemaphoreTake(passMutex, portMAX_DELAY);
    retry = heldPress.restore();
    xSemaphoreGive(passMutex);
  }
  if (emitMutex) xSemaphoreGive(emitMutex);
  if (retry) return;
  if (sent) {
    pendingScreenPress.store(true, std::memory_order_relaxed);
    Serial.println("press: held press sent (link ready)");
  } else if (tick != HELD_NONE) {
    pendingScreenPressFailed.store(true, std::memory_order_relaxed);
    Serial.println("press: held press given up");
  }
}

#if RAILY_VOICE
// The voice block is defined next to the motor (it needs the link cache and
// the rhythm player); the button hands it the debounced press and release.
static bool voiceCapable = false;  // the boot probe found the Sense IMU (and so the mic)
static void voiceHoldBegan(uint32_t atMs, bool fromSerial);
static void voiceHoldEnded(uint32_t atMs, bool quiet);
static void onVoiceBleEvent(ble_evt_t* evt);
#endif

#if RAILY_SHIP_MODE
static void openBindWindow(const char* why);  // defined next to restoreMascot()
// The 3 s wake / bind hold (ship_mode.h). Loop task only.
static ShipHold shipHold = {};
// Serial [ and ]: the bench's hand on D1 (Elena's bench has the Expansion
// Board's button but nobody to press it). USB is physical access, as for
// serial `w`. RAM only: a reset lets go.
static bool benchButtonDown = false;

static bool pinOwnedNow() {
  xSemaphoreTake(passMutex, portMAX_DELAY);
  const bool owned = secretOwned;
  xSemaphoreGive(passMutex);
  return owned;
}

// Shipping (asleep): unowned and outside the bind window.
static bool pinShippingNow() {
  xSemaphoreTake(passMutex, portMAX_DELAY);
  const bool shipping = shipShipping(secretOwned, bindWindow.isOpen(millis()));
  xSemaphoreGive(passMutex);
  return shipping;
}

// The 3 s mark: the bind window (the radio follows on this pass), one ▬ on
// the motor and the screen's «set me up» (an unowned pin's only scene).
static void shipOpenWindow() {
  openBindWindow("button hold 3 s");
  pendingRhythmAck.store(3, std::memory_order_relaxed);
  screen.wake(millis());  // a dark screen wakes; an awake one keeps its scene
  RAILY_WAKE_LOOP();
}

// A release on a ship-mode Air (ship_mode.h): true when it must stay
// silent. A 3 s press loop() only reads at its release still opens the
// window here.
static bool shipReleaseQuiet(uint32_t pressedAtMs, uint32_t releasedAtMs) {
  if (shipHold.poll(true, pressedAtMs, releasedAtMs, pinOwnedNow())) shipOpenWindow();
  const bool opened = shipHold.release();
  const bool quiet = shipReleaseSilent(opened, pinShippingNow());
  if (quiet && !opened) Serial.println("button: asleep (shipping), press ignored");
  return quiet;
}
#endif

#if RAILY_BUTTON
// The Air button on D1 (P0.03, active low, 10 k pull-up R3; the internal
// pull-up too, so a board without the button never floats into presses).
// The interrupt only stamps the edge and wakes loop(); loop() debounces
// (idle_sleep.h) and emits a tap through emitButtonEvent, which writes
// flash, so it never runs in the interrupt. Under 2 s it scans; 2 s or
// more never does (idle_sleep.h).
static const uint8_t BUTTON_PIN = D1;
static ButtonEdgeRing buttonEdges = {};
static ButtonDebounce buttonDebounce = {};  // loop task only

static void onButtonEdge() {
  const bool firstOfBurst = buttonEdges.empty();
  buttonEdges.push(digitalRead(BUTTON_PIN) == LOW, (uint32_t)tick2ms(xTaskGetTickCountFromISR()));
#if RAILY_IDLE_SLEEP
  // One wake per burst of edges (contact bounce): loop() drains the ring,
  // and the debounce deadline brings it back when the level has settled.
  if (firstOfBurst) wakeLoopFromISR();
#else
  (void)firstOfBurst;
#endif
}

static void startButton() {
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  attachInterrupt(BUTTON_PIN, onButtonEdge, CHANGE);
}

#if !RAILY_VOICE
static void onButton(ButtonEvent event) {
  if (buttonScans(event)) {
    emitButtonEvent(0);  // press_type 0: the hardware button, as serial `p`
  } else if (event == BUTTON_LONG) {
    // No scan, no counter: «didn't work» on the screen and the red LED.
    pendingFeedback.store(4, std::memory_order_relaxed);
    pendingScreenPressFailed.store(true, std::memory_order_relaxed);
    Serial.println("button: held 2 s or more, no scan");
  }
}
#endif

// One debounced sample. With voice built in, the push-to-talk gesture
// (voice_gesture.h) decides what a press does: it gets the press at its
// debounced edge and the release at its edge. Without an armed voice link
// the gesture is 0.3.0's rule exactly (under 2 s = scan on release, 2 s or
// more = «didn't work»), so a plain XIAO, or a Sense pin with no subscribed
// trusted phone, behaves as before.
//
// Ship mode (Air): a release is silent while the pin ships (asleep) or when
// the press reached 3 s and opened the bind window; never a scan from a
// long press either way (B3a).
static void buttonSample(bool level, uint32_t atMs) {
  const bool wasPressed = buttonDebounce.pressed;
  const uint32_t edgeAtMs = buttonDebounce.candidateAtMs;  // the edge a settle in this sample reports
  const uint32_t pressedAtMs = buttonDebounce.pressedAtMs;
  const ButtonEvent event = buttonDebounce.sample(level, atMs);
#if RAILY_SHIP_MODE
  const bool quiet = event != BUTTON_NONE && shipReleaseQuiet(pressedAtMs, edgeAtMs);
#else
  const bool quiet = false;
  (void)pressedAtMs;
#endif
#if RAILY_VOICE
  if (!wasPressed && buttonDebounce.pressed) voiceHoldBegan(buttonDebounce.pressedAtMs, false);
  if (event != BUTTON_NONE) voiceHoldEnded(edgeAtMs, quiet);
#else
  (void)wasPressed;
  (void)edgeAtMs;
  if (!quiet) onButton(event);
#endif
}

// loop(): the interrupt's edges in the order they happened, then the pin
// as it is now.
static __attribute__((noinline)) void serviceButton() {
  bool level = false;
  uint32_t at = 0;
  while (buttonEdges.pop(&level, &at)) buttonSample(level, at);
#if RAILY_SHIP_MODE
  buttonSample(digitalRead(BUTTON_PIN) == LOW || benchButtonDown, millis());
  // The 3 s mark while still held. A release the debounce has not settled
  // yet stops the press's clock, so a press that ended under 3 s never
  // opens the window late.
  uint32_t holdNow = millis();
  if (buttonDebounce.pressed && buttonDebounce.unsettled()) holdNow = buttonDebounce.candidateAtMs;
  if (shipHold.poll(buttonDebounce.pressed, buttonDebounce.pressedAtMs, holdNow, pinOwnedNow())) shipOpenWindow();
#else
  buttonSample(digitalRead(BUTTON_PIN) == LOW, millis());
#endif
}
#endif

#if RAILY_SHIP_MODE
// ---- System OFF on the cell (ship_mode.h, docs/pins/air-ship-mode.md §8) ----

// A button wake that reached 3 s: setup() opens the bind window.
static bool shipWokeByHold = false;

// VBUS before the SoftDevice runs (it owns NRF_POWER afterwards: then
// readPowerByte()).
static bool vbusPresentRaw() {
  return (NRF_POWER->USBREGSTATUS & POWER_USBREGSTATUS_VBUSDETECT_Msk) != 0;
}

// Power off: D1 stays an input with its internal pull-up and senses low
// (the pin configuration survives System OFF), VBUS rising wakes it too.
// Only ever called with the button released. Returns only when the
// SoftDevice refused (the caller stays awake).
static void systemOffNow(bool softDeviceRuns) {
  nrf_gpio_cfg_sense_input(g_ADigitalPinMap[BUTTON_PIN], NRF_GPIO_PIN_PULLUP, NRF_GPIO_PIN_SENSE_LOW);
  if (softDeviceRuns) {
    const uint32_t err = sd_power_system_off();
    Serial.printf("ship: System OFF refused (0x%lx)\n", (unsigned long)err);
    return;
  }
  NRF_POWER->SYSTEMOFF = 1;
  __DSB();
  for (;;) __WFE();
}

// First thing in setup() after a button wake from System OFF: the 3 s hold
// is timed here, before the SoftDevice, the file system or the 4 s USB
// serial wait. Let go early: straight back to System OFF, nothing lit.
static void shipCheckWakeAtBoot() {
  if (!shipWokeByButton(readResetReason())) return;
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  ShipWakeCheck check = {};
  for (;;) {
    switch (check.sample(digitalRead(BUTTON_PIN) == LOW, vbusPresentRaw(), millis())) {
      case SHIP_WAKE_AWAKE:
        shipWokeByHold = true;
        return;
      case SHIP_WAKE_USB:
        return;
      case SHIP_WAKE_SLEEP:
        systemOffNow(false);
        return;
      default:
        delay(5);
        break;
    }
  }
}
#endif

// The pin was dropped: «Упал. Было больно» on the screen (keyring-oled PR 5,
// ScreenState::onFall). gestures S1's gesture 3 detector (free fall, then
// the impact, on the Sense IMU) calls it when the pin lands; until the IMU
// ships, serial `of` does. Safe from any task: it only raises a flag that
// loop() drains. The screen only, by the owner's decision (2026-09-28): no
// motor, no LED, no BLE (tests/check_fall_path.py).
static void screenOnFall() {
  pendingScreenFall.store(true, std::memory_order_relaxed);
  RAILY_WAKE_LOOP();
}

// One event_ack byte: the LED keeps today's normalisation (an empty or 0
// write flashes once), the motor maps the raw byte (rhythm.h). Serial `c`
// feeds the same path on the bench.
static void publishEventAck(const uint8_t* data, uint16_t len) {
  EventAckBytes bytes = eventAckBytes(data, len);
  pendingFeedback.store(bytes.led, std::memory_order_relaxed);
  if (bytes.rhythm == ScreenState::ACK_FOUND_YOU) pendingScreenFoundYou.store(true, std::memory_order_relaxed);
  if (rhythmForEventAck(bytes.rhythm) != RHYTHM_NONE) {
    pendingRhythmAck.store(bytes.rhythm, std::memory_order_relaxed);
  }
  RAILY_WAKE_LOOP();
}

static bool linkTrustedNow(uint16_t connHdl);  // defined next to linkTrustedLocked()

// Stage 2 (ble-bonding.md §9.1): an ack only from a trusted link, so a
// stranger cannot play a rhythm or a fake «found you» (0x11).
static void onAckWrite(uint16_t connHdl, BLECharacteristic*, uint8_t* data, uint16_t len) {
  if (!linkTrustedNow(connHdl)) {
    untrustedAcks.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  publishEventAck(data, len);
}

// pressCommand accepts exactly one value: 1 = in-app press. The byte is
// untrusted radio input — anything else is dropped, and writes closer than
// the cooldown to the last emitted event are ignored (WRITE_WO_RESP flood).
static const uint8_t PRESS_TYPE_IN_APP = 1;
static PressCommandGate pressCommandGate = {};

static void onPressCommand(uint16_t connHdl, BLECharacteristic*, uint8_t* data, uint16_t len) {
  // Stage 2: an in-app press only from a trusted link (ble-bonding.md §9.1).
  if (!linkTrustedNow(connHdl)) {
    untrustedPressCommands.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  uint32_t now = millis();
  if (len == 1 && connected.load(std::memory_order_relaxed)
      && data[0] == PRESS_TYPE_IN_APP && pressCommandGate.allows(now)) {
    pressCommandGate.record(now);
    emitButtonEvent(PRESS_TYPE_IN_APP);
  }
}

// Restart and DFU entry arrive as control passes (server_pass.h), never
// as open writes: device_control stays in the table (bonded iOS GATT
// caches, the later bonding design) but refuses every write. A restart is
// still rate-limited to one per 60 s of uptime (the app mirrors it with
// PinRestartGate). Both are armed, not executed, in the BLE callback so
// the write response is on the air before loop() resets ~300 ms later.
static const uint32_t DEVICE_CONTROL_MIN_INTERVAL_MS = 60000;
static const uint32_t DEVICE_CONTROL_DRAIN_MS = 300;
// OTAFIX/Adafruit bootloader: this GPREGRET value starts BLE OTA DFU.
static const uint8_t DFU_MAGIC_OTA_RESET_VALUE = 0xA8;
// Read from loop() — volatile so the armed deadline cannot be cached in a
// register across contexts. A bool flag (not rebootAtMs != 0) marks the
// armed state so the once-per-49d wrap-to-0 case cannot lose a reset.
static volatile bool rebootArmed = false;
static volatile bool enterDfuArmed = false;
static volatile uint32_t rebootAtMs = 0;

// The one rule for whether a control action can be armed now; the BLE
// callback (before spending the nonce) and loop() (when arming) share it.
static bool controlArmable(uint8_t arg, uint32_t now) {
  if (rebootArmed || enterDfuArmed) return false;
  return arg != PASS_ARG_REBOOT || now >= DEVICE_CONTROL_MIN_INTERVAL_MS;
}

static void sendAuthorizeReply(uint16_t connHdl, const ble_gatts_rw_authorize_reply_params_t& reply) {
  if (sd_ble_gatts_rw_authorize_reply(connHdl, &reply) != NRF_SUCCESS) {
    authorizeReplyFailures = authorizeReplyFailures + 1;
    pendingFeedback.store(4, std::memory_order_relaxed);
    RAILY_WAKE_LOOP();
  }
}

// Refusals only. SoftDevice status 0x0180 + n goes on air as ATT error
// 0x80 + n (the application range): pass errors reach the phone as 0x80-0x83.
static void rejectWriteAuthorize(uint16_t connHdl, uint16_t gattStatus) {
  ble_gatts_rw_authorize_reply_params_t reply = {};
  reply.type = BLE_GATTS_AUTHORIZE_TYPE_WRITE;
  reply.params.write.gatt_status = gattStatus;
  reply.params.write.update = 0;
  sendAuthorizeReply(connHdl, reply);
}

// An accepted write must carry update = 1 and the written bytes: the
// SoftDevice refuses a success reply with update = 0 (ble_gatts.h, s140
// 6.1.1: for write authorize replies update "must always be set"), and the
// phone then gets no answer at all. 0.2.4-qa..0.2.7-qa did exactly that for
// every accepted pass (P4 bench, 2026-09-27).
static void acceptWriteAuthorize(uint16_t connHdl, const ble_gatts_evt_write_t* request) {
  ble_gatts_rw_authorize_reply_params_t reply = {};
  reply.type = BLE_GATTS_AUTHORIZE_TYPE_WRITE;
  reply.params.write.gatt_status = BLE_GATT_STATUS_SUCCESS;
  reply.params.write.update = 1;
  reply.params.write.offset = request->offset;
  reply.params.write.len = request->len;
  reply.params.write.p_data = request->data;
  sendAuthorizeReply(connHdl, reply);
}

// After a rejected pass the next one on this link is refused unchecked for
// PASS_REJECT_COOLDOWN_MS, so a flood of forged passes costs no HMAC work.
static const uint32_t PASS_REJECT_COOLDOWN_MS = 250;
// Plain variables: only BLE callbacks touch them (onPassAuthorize in the
// BLE task, onConnect between links), never loop(). A stale read at a link
// boundary costs at most one 250 ms refusal.
static bool passRejectedRecently = false;
static uint32_t lastPassRejectMs = 0;

// One link's security as the SoftDevice sees it (LinkSecurity in
// bond_table.h). Safe from the BLE task and loop().
static LinkSecurity readLinkSecurity(uint16_t connHdl) {
  LinkSecurity link = {};
  BLEConnection* conn = Bluefruit.Connection(connHdl);
  if (conn == NULL || !conn->connected()) return link;
  ble_gap_conn_sec_t sec = {};
  if (sd_ble_gap_conn_sec_get(connHdl, &sec) == NRF_SUCCESS && sec.sec_mode.sm == 1) {
    link.level = sec.sec_mode.lv;
    link.keySize = sec.encr_key_size;
  }
  link.bonded = conn->bonded();
  if (link.bonded) {
    ble_gap_addr_t id = conn->getPeerAddr();  // the bond's identity address once bonded
    link.addrType = id.addr_type;
    memcpy(link.addr, id.addr, BOND_ADDR_LENGTH);
  }
  return link;
}

// Called with passMutex held: the rule of ble-bonding.md D-3.
static bool linkTrustedLocked(const LinkSecurity& link) {
  const bool admitted = link.bonded && bondTable.contains(link.addr);
  return linkTrusted(secretOwned, link.level, link.keySize, link.bonded, admitted);
}

static LinkSecurity currentLink(uint16_t* handle);  // defined next to clearBondsLocked()

// The trust rule for a write on `connHdl`, from the linkCache: the write
// callbacks run in the Ada callback task, which must not read a
// BLEConnection the BLE task may be deleting. A write from any link but
// the cached one is not trusted.
static bool linkTrustedNow(uint16_t connHdl) {
  uint16_t handle = BLE_CONN_HANDLE_INVALID;
  const LinkSecurity link = currentLink(&handle);
  if (handle == BLE_CONN_HANDLE_INVALID || handle != connHdl) return false;
  xSemaphoreTake(passMutex, portMAX_DELAY);
  const bool trusted = linkTrustedLocked(link);
  xSemaphoreGive(passMutex);
  return trusted;
}

// The single link a press may go to (press_hold.h): its handle, or
// BLE_CONN_HANDLE_INVALID with no link; true when it is trusted and
// subscribed to button_event. From the linkCache, never a BLEConnection:
// it runs in loop() (serial `p`, the held press) as well as the Ada task.
// The admission and the owned flag are read live, so an admission that
// just landed counts; the CCCD is read from the SoftDevice.
static bool pressLinkReady(uint16_t* handle, uint32_t* epoch) {
  xSemaphoreTake(passMutex, portMAX_DELAY);
  const LinkSecurity link = linkCache;
  *handle = linkCacheHandle;
  *epoch = linkEpoch;
  const bool trusted = *handle != BLE_CONN_HANDLE_INVALID && linkTrustedLocked(link);
  xSemaphoreGive(passMutex);
  return trusted && buttonEventChr.notifyEnabled(*handle);
}

static void onDeviceControlAuthorize(uint16_t connHdl, BLECharacteristic*, ble_gatts_evt_write_t*) {
  rejectWriteAuthorize(connHdl, BLE_GATT_STATUS_ATTERR_WRITE_NOT_PERMITTED);
}

// Runs synchronously in the BLE task (useAdaCallback = false): the verdict
// becomes the ATT write response. Only the decision happens here; the
// action waits for loop().
static void onPassAuthorize(uint16_t connHdl, BLECharacteristic*, ble_gatts_evt_write_t* request) {
  if (request->op != BLE_GATTS_OP_WRITE_REQ || request->offset != 0) {
    rejectWriteAuthorize(connHdl, BLE_GATT_STATUS_ATTERR_APP_BEGIN + (PASS_BAD_FORMAT - 0x80));
    return;
  }
  PassCommand command = {};
  uint32_t now = millis();
  // One action at a time: while loop() has not run the last one, a new
  // pass is refused unchecked (its nonce stays alive) and the app retries.
  if (pendingPassAction.load(std::memory_order_relaxed) != PASS_ACTION_NONE ||
      (passRejectedRecently && (uint32_t)(now - lastPassRejectMs) < PASS_REJECT_COOLDOWN_MS)) {
    rejectWriteAuthorize(connHdl, BLE_GATT_STATUS_ATTERR_APP_BEGIN + (PASS_BUSY - 0x80));
    return;
  }
  // A control pass that could not be armed right now is refused before it
  // spends the nonce, so the app can retry the same pass: a restart in the
  // first 60 s of uptime, or any reset already armed.
  if (request->len == PASS_LENGTH && request->data[1] == PASS_CLASS_CONTROL &&
      !controlArmable(request->data[2], now)) {
    rejectWriteAuthorize(connHdl, BLE_GATT_STATUS_ATTERR_APP_BEGIN + (PASS_BUSY - 0x80));
    return;
  }
  // An admit pass needs this link encrypted (16-byte key) and bonded
  // (ble-bonding.md D-2); read before the section, it is a SoftDevice call.
  const LinkSecurity link = readLinkSecurity(connHdl);
  const bool admitReady = linkReadyForAdmission(link.level, link.keySize, link.bonded);
  bool admitted = false;
  xSemaphoreTake(passMutex, portMAX_DELAY);
  PassResult result = verifyPass(request->data, request->len, deviceId, deviceSecret, hasSecret,
                                 secretOwned, passNonce, now, command, admitReady);
  bleTaskLowWater = uxTaskGetStackHighWaterMark(NULL);  // BLE task, after the MAC
  // Admitted in the section that checked ownership, so a release (which
  // clears the table in its own section) can never keep this admission.
  if (result == PASS_OK && command.passClass == PASS_CLASS_ADMIT) {
    bool evicted = false;
    if (bondTable.admit(link.addr, link.addrType, NULL, &evicted)) bondTableDirty = true;
    admitted = true;
    // A ninth phone pushed the oldest out of the table: its bond file goes
    // with the next cap pass (an unadmitted bond is evicted first).
    if (evicted) {
      bondCapDueMs.store(now, std::memory_order_relaxed);
      bondCapPending.store(true, std::memory_order_relaxed);
    }
  }
  xSemaphoreGive(passMutex);
  passRejectedRecently = result != PASS_OK;
  if (result != PASS_OK) lastPassRejectMs = now;
  if (result == PASS_NEEDS_ENCRYPTION) {
    // Standard ATT 0x0F: iOS pairs on it. The nonce is spent (spec D-2).
    rejectWriteAuthorize(connHdl, BLE_GATT_STATUS_ATTERR_INSUF_ENCRYPTION);
    return;
  }
  if (result != PASS_OK) {
    rejectWriteAuthorize(connHdl, BLE_GATT_STATUS_ATTERR_APP_BEGIN + (result - 0x80));
    return;
  }
  if (admitted) {  // nothing for loop() to run: the table is persisted there
    RAILY_WAKE_LOOP();
    acceptWriteAuthorize(connHdl, request);
    return;
  }
  // A genuine DFU-entry pass on a bootloader that cannot finish a BLE update
  // (bootloader_kind.h): jumping would park the pin with no app and no USB.
  // Refused with its own code so the app says «updates over USB»; the
  // nonce is spent (the pass was genuine) and no reject backoff starts.
  if (command.passClass == PASS_CLASS_CONTROL && command.arg == PASS_ARG_ENTER_DFU &&
      !bootloaderTakesBleUpdate(bootloaderKind)) {
    rejectWriteAuthorize(connHdl, BLE_GATT_STATUS_ATTERR_APP_BEGIN + (PASS_NO_BLE_UPDATE - 0x80));
    return;
  }
  uint8_t action = PASS_ACTION_NONE;
  if (command.passClass == PASS_CLASS_CONTROL) {
    action = command.arg == PASS_ARG_ENTER_DFU ? PASS_ACTION_ENTER_DFU : PASS_ACTION_REBOOT;
  } else if (command.passClass == PASS_CLASS_CONFIRM) {
    action = PASS_ACTION_CONFIRM;
  } else if (command.passClass == PASS_CLASS_RELEASE) {
    action = PASS_ACTION_RELEASE;
  } else if (command.passClass == PASS_CLASS_FORGET_ALL) {
    action = PASS_ACTION_FORGET_ALL;
  }
  if (action == PASS_ACTION_NONE) {  // cannot happen while passShapeValid() holds
    rejectWriteAuthorize(connHdl, BLE_GATT_STATUS_ATTERR_APP_BEGIN + (PASS_BAD_FORMAT - 0x80));
    return;
  }
  pendingPassAction.store(action, std::memory_order_relaxed);
  RAILY_WAKE_LOOP();
  acceptWriteAuthorize(connHdl, request);
}

// screen_state (7B1E0014): the verdict is the ATT write response
// (screenWriteVerdict: 0x84 bad format, 0x85 bad value, 0x86 not owned).
// Runs in the BLE task: it only validates and hands an accepted payload to
// loop() through the mailbox; no screen, flash or timer work here. The
// ownership check and the store share one passMutex section with the
// release (replaceSecret), so a write racing an unbind is either taken
// before it (and wiped by it) or refused.
static void onScreenStateAuthorize(uint16_t connHdl, BLECharacteristic*, ble_gatts_evt_write_t* request) {
  // A trusted link (encrypted, bonded, admitted: docs/pins/ble-bonding.md
  // D-3) = a trusted write (keyring-oled.md §10): counts and wake only then.
  const LinkSecurity link = readLinkSecurity(connHdl);
  ScreenPayload accepted = {};
  xSemaphoreTake(passMutex, portMAX_DELAY);
  const bool trusted = linkTrustedLocked(link);
  uint8_t verdict = screenWriteVerdict(request->op == BLE_GATTS_OP_WRITE_REQ, request->offset, request->data,
                                       request->len, secretOwned, trusted, &accepted);
  if (verdict == 0) {
    screenMailbox = accepted;  // latest wins
    screenMailboxTrusted = trusted;
    screenMailboxFull = true;
  }
  // Counted in the same section, so a release (which zeroes the counters
  // here too) never keeps a count of a former owner's write.
  std::atomic<uint32_t>& counter = verdict == 0                       ? screenWritesAccepted
                                   : verdict == SCREEN_ATT_BAD_FORMAT ? screenWritesBadFormat
                                   : verdict == SCREEN_ATT_NOT_OWNED  ? screenWritesNotOwned
                                                                      : screenWritesBadValue;
  counter.fetch_add(1, std::memory_order_relaxed);
  xSemaphoreGive(passMutex);
  if (verdict != 0) {
    rejectWriteAuthorize(connHdl, BLE_GATT_STATUS_ATTERR_APP_BEGIN + (verdict - 0x80));
    return;
  }
  RAILY_WAKE_LOOP();
  acceptWriteAuthorize(connHdl, request);
}

// Non-blocking RNG read for the BLE task: takes bytes only if the pool
// already holds them (no delay, no retry loop).
static bool tryRandomNow(uint8_t* out, uint8_t length) {
  uint8_t available = 0;
  return sd_rand_application_bytes_available_get(&available) == NRF_SUCCESS &&
         available >= length && sd_rand_application_vector_get(out, length) == NRF_SUCCESS;
}

// A read-authorize answer with the value built at read time.
static void replyRead(uint16_t connHdl, const ble_gatts_evt_read_t* request, const uint8_t* value, uint16_t length) {
  ble_gatts_rw_authorize_reply_params_t reply = {};
  reply.type = BLE_GATTS_AUTHORIZE_TYPE_READ;
  if (request->offset > length) {
    reply.params.read.gatt_status = BLE_GATT_STATUS_ATTERR_INVALID_OFFSET;
  } else {
    reply.params.read.gatt_status = BLE_GATT_STATUS_SUCCESS;
    reply.params.read.update = 1;
    reply.params.read.offset = request->offset;
    reply.params.read.len = length - request->offset;
    reply.params.read.p_data = value + request->offset;
  }
  sendAuthorizeReply(connHdl, reply);
}

// button_event read (stage 2): the last press for a trusted link only,
// 9 zero bytes for any other.
static void onButtonEventReadAuthorize(uint16_t connHdl, BLECharacteristic*, ble_gatts_evt_read_t* request) {
  const LinkSecurity link = readLinkSecurity(connHdl);
  uint8_t value[PRESS_PAYLOAD_LENGTH] = {};
  xSemaphoreTake(passMutex, portMAX_DELAY);
  if (linkTrustedLocked(link)) memcpy(value, lastPressPayload, sizeof(value));
  xSemaphoreGive(passMutex);
  replyRead(connHdl, request, value, sizeof(value));
}

// pass_challenge is built at read time (BLE task, useAdaCallback = false),
// so a phone always reads the nonce the pin will check: never the previous
// link's value. No usable nonce → a new one; an empty RNG pool → zeros, and
// the phone reads again.
static void onPassChallengeAuthorize(uint16_t connHdl, BLECharacteristic*, ble_gatts_evt_read_t* request) {
#if RAILY_IDLE_SLEEP
  bool spentSpare = false;
#endif
  uint8_t value[PASS_CHALLENGE_LENGTH];
  xSemaphoreTake(passMutex, portMAX_DELAY);
  uint32_t now = millis();
  if (!passNonce.usable(now)) {
    passNonce.clear();  // an expired nonce is never shown again; zeros if the RNG is empty
  }
  if (hasSecret && !passNonce.valid) {
    if (spareNonceReady) {
      passNonce.issue(spareNonce, now);
      spareNonceReady = false;
#if RAILY_IDLE_SLEEP
      spentSpare = true;
#endif
    } else {
      uint8_t fresh[PASS_NONCE_LENGTH];
      if (tryRandomNow(fresh, sizeof(fresh))) passNonce.issue(fresh, now);
      memset(fresh, 0, sizeof(fresh));
    }
  }
  buildPassChallenge(passNonce, secretOwned, value);
  if (!hasSecret) value[PASS_NONCE_LENGTH] |= PASS_FLAG_NO_SECRET;
  if (bindWindowServes(hasSecret, secretOwned, bindWindow.isOpen(now))) {
    value[PASS_NONCE_LENGTH] |= PASS_FLAG_BIND_WINDOW;
  }
  xSemaphoreGive(passMutex);
#if RAILY_IDLE_SLEEP
  if (spentSpare) wakeLoop();  // loop() refills the spare nonce
#endif
  replyRead(connHdl, request, value, sizeof(value));
}

// link_security (7B1E0010, ble-bonding.md D-3). The permission is an
// encrypted read, so the SoftDevice refuses it on an open link before this
// runs (that refusal is what makes iOS pair); the value is built now.
static void onLinkSecurityAuthorize(uint16_t connHdl, BLECharacteristic*, ble_gatts_evt_read_t* request) {
  const LinkSecurity link = readLinkSecurity(connHdl);
  uint8_t value[LINK_SECURITY_LENGTH];
  xSemaphoreTake(passMutex, portMAX_DELAY);
  buildLinkSecurity(link.bonded, linkTrustedLocked(link), secretOwned,
                    bondFileCount.load(std::memory_order_relaxed), bondTable.count, value);
  xSemaphoreGive(passMutex);
  replyRead(connHdl, request, value, sizeof(value));
}

// One peripheral link (Bluefruit.begin() default), so one nonce and one
// setup flag are enough; a second link would need per-handle state.
static void onConnect(uint16_t connHdl) {
  (void)connHdl;
  connected.store(true, std::memory_order_relaxed);
  passRejectedRecently = false;
  linkSetupPending.store(true, std::memory_order_relaxed);
  RAILY_WAKE_LOOP();
}

static void onDisconnect(uint16_t, uint8_t) {
  connected.store(false, std::memory_order_relaxed);
  linkSetupPending.store(false, std::memory_order_relaxed);
  // The next link must not read this link's seal: until loop() reseals, a
  // read returns empty and the phone reads again. Under passMutex with the
  // generation bump, so a seal still in flight is not published afterwards.
  xSemaphoreTake(passMutex, portMAX_DELAY);
  passNonce.clear();  // a nonce belongs to one link
  linkGeneration++;
  sealedSecretLength = 0;  // serial s must not print a seal the pin no longer serves
  deviceSecretChr.write(sealedSecret, 0);
  xSemaphoreGive(passMutex);
  RAILY_WAKE_LOOP();  // the LED and the screen follow the link
}

// Runs in the BLE task after Bluefruit's own handlers (so a new pairing's
// bond identity is already on the connection). Only flags and RAM: loop()
// does the flash work.
// `newLink`: a connect or a disconnect, which bumps linkEpoch in the same
// section as the cache (a reader never sees a new handle with an old
// epoch) and, on a disconnect, drops the held press: it belongs to that
// link. Returns true when a held press was dropped.
static bool cacheLink(uint16_t connHdl, bool up, bool newLink = false) {
  const LinkSecurity link = up ? readLinkSecurity(connHdl) : LinkSecurity();
  xSemaphoreTake(passMutex, portMAX_DELAY);
  linkCache = link;
  linkCacheHandle = up ? connHdl : BLE_CONN_HANDLE_INVALID;
  if (newLink) linkEpoch++;
  const bool dropped = !up && heldPress.held;
  if (!up) heldPress.clear();
  xSemaphoreGive(passMutex);
  return dropped;
}

static void bondFilePath(char* path, size_t size, const uint8_t addr[BOND_ADDR_LENGTH]) {
  snprintf(path, size, "%s/%02X%02X%02X%02X%02X%02X", BOND_DIR_PRPH, addr[0], addr[1], addr[2], addr[3], addr[4],
           addr[5]);
}

// BLE task, at BLE_GAP_EVT_SEC_INFO_REQUEST of a bonded phone (D-5).
// Bluefruit has just found the phone's keys and reads its CCCD field only
// at the following CONN_SEC_UPDATE, and the SoftDevice keeps the first
// sd_ble_gatts_sys_attr_set() of a link (a later one returns success and
// changes nothing: bench 2026-10-09). So a field Bluefruit's own save wrote
// one byte late is put back in place here, before that read, and Bluefruit
// restores the CCCDs itself. A file that is already in place is only read.
// Raw LittleFS under the file system's lock, read to close in one section,
// opened without create: a bond being replaced (remove, then create, on the
// Ada task) or removed by loop() is never recreated or half-written.
static void repairBondCccdFile(uint16_t connHdl) {
  static uint8_t file[BOND_FILE_MAX];  // BLE task only
  static char path[48];
  static lfs_file_t handle;
  BLEConnection* conn = Bluefruit.Connection(connHdl);
  if (conn == NULL || !conn->bonded()) return;
  const ble_gap_addr_t id = conn->getPeerAddr();
  bondFilePath(path, sizeof(path), id.addr);
  BondCccdPlan plan = {};
  bool keep = false;
  bool ok = false;
  InternalFS._lockFS();
  lfs_t* fs = InternalFS._getFS();
  if (lfs_file_open(fs, &handle, path, LFS_O_RDWR) == 0) {
    const lfs_soff_t size = lfs_file_size(fs, &handle);
    if (size > 0 && (size_t)size <= sizeof(file) && lfs_file_read(fs, &handle, file, size) == size) {
      plan = planBondCccd(file, (size_t)size);
    }
    if (plan.rewrite) {
      keep = plan.action == BOND_CCCD_RESTORE;
      const lfs_off_t end = keep ? plan.start + 1 + plan.length : plan.start;
      ok = lfs_file_seek(fs, &handle, plan.start, LFS_SEEK_SET) >= 0;
      if (ok && keep) {
        memmove(file + plan.start + 1, file + plan.blob, plan.length);  // the blob is never before its place
        file[plan.start] = plan.length;
        ok = lfs_file_write(fs, &handle, file + plan.start, 1 + plan.length) == (lfs_ssize_t)(1 + plan.length);
      }
      ok = ok && lfs_file_truncate(fs, &handle, end) == 0;
    }
    ok = lfs_file_close(fs, &handle) == 0 && ok;
  }
  InternalFS._unlockFS();
  memset(file, 0, sizeof(file));
  if (!plan.rewrite) return;
  const unsigned late = keep ? (unsigned)(plan.blob - plan.start - 1) : 0u;
  bondCccdRepairLog.store((uint16_t)((keep ? 0x8000u : 0u) | (ok ? 0x4000u : 0u) | (late & 0xFFu)),
                          std::memory_order_relaxed);
  bondCccdRepairPending.store(true, std::memory_order_relaxed);
  RAILY_WAKE_LOOP();
}

static void onBleEvent(ble_evt_t* evt) {
  const uint16_t connHdl = evt->evt.common_evt.conn_handle;
  switch (evt->header.evt_id) {
    case BLE_GAP_EVT_CONNECTED:
      cacheLink(connHdl, true, true);
      RAILY_WAKE_LOOP();
      break;
    case BLE_GAP_EVT_DISCONNECTED: {
      // A held press belongs to this link (press_hold.h): never to the next.
      if (cacheLink(connHdl, false, true)) pendingScreenPressFailed.store(true, std::memory_order_relaxed);
      // A confirmation still waiting for loop() belongs to this link only.
      serviceChangedConfirmedHandle.store(BLE_CONN_HANDLE_INVALID, std::memory_order_relaxed);
      RAILY_WAKE_LOOP();
      break;
    }
    case BLE_GAP_EVT_AUTH_STATUS: {
      const ble_gap_evt_auth_status_t& status = evt->evt.gap_evt.params.auth_status;
      if (status.auth_status != BLE_GAP_SEC_STATUS_SUCCESS) break;
      // Every completed pairing revokes that identity's admission (D-2): the
      // keys are new, so the phone is admitted again by its app. Not only a
      // bonding one: Bluefruit saves the keys of a pairing without bonding
      // too, under the peer's own (possibly copied) address.
      const LinkSecurity link = readLinkSecurity(connHdl);
      xSemaphoreTake(passMutex, portMAX_DELAY);
      if (link.bonded && bondTable.revoke(link.addr)) bondTableDirty = true;
      linkCache = link;
      linkCacheHandle = connHdl;
      xSemaphoreGive(passMutex);
      bondCapDueMs.store(millis() + BOND_CAP_DELAY_MS, std::memory_order_relaxed);
      bondCapPending.store(true, std::memory_order_relaxed);
      RAILY_WAKE_LOOP();
      break;
    }
    case BLE_GAP_EVT_SEC_INFO_REQUEST:
      repairBondCccdFile(connHdl);  // before Bluefruit reads the CCCD field at CONN_SEC_UPDATE
      break;
    case BLE_GAP_EVT_CONN_SEC_UPDATE:
      cacheLink(connHdl, true);
      serviceChangedCheckPending.store(true, std::memory_order_relaxed);
      RAILY_WAKE_LOOP();
      break;
    case BLE_GATTS_EVT_SC_CONFIRM:
      serviceChangedConfirmedHandle.store(connHdl, std::memory_order_relaxed);
      RAILY_WAKE_LOOP();
      break;
    case BLE_GATTS_EVT_WRITE: {
      // The phone's Service Changed subscription, a CCCD of the SoftDevice's
      // own GATT service. At the link's encryption it still reads 0 (Bluefruit
      // restores no CCCD from the bond file here, bench 2026-10-09), so the
      // first try gets 0x8; iOS writes it again about 0.6 s later, on every
      // link, and the indication goes then.
      const ble_gatts_evt_write_t& write = evt->evt.gatts_evt.params.write;
      if (serviceChangedCccdHandle == 0 || write.handle != serviceChangedCccdHandle) break;
      const uint16_t cccd = write.len >= 2 ? (uint16_t)(write.data[0] | (write.data[1] << 8)) : 0;
      serviceChangedCccdValue.store(cccd, std::memory_order_relaxed);
      serviceChangedCccdPending.store(true, std::memory_order_relaxed);
      if (cccd & BLE_GATT_HVX_INDICATION) serviceChangedCheckPending.store(true, std::memory_order_relaxed);
      RAILY_WAKE_LOOP();
      break;
    }
    default:
      break;
  }
#if RAILY_VOICE
  onVoiceBleEvent(evt);
#endif
}

// SoftDevice RNG, retried while its pool refills; gives up after ~400 ms
// instead of stalling boot, and the caller treats that as "no secret".
static bool fillRandom(uint8_t* out, uint8_t length) {
  uint8_t filled = 0;
  for (int attempt = 0; attempt < 200 && filled < length; attempt++) {
    uint8_t available = 0;
    if (sd_rand_application_bytes_available_get(&available) == NRF_SUCCESS && available > 0) {
      uint8_t take = (uint8_t)min((int)available, (int)(length - filled));
      if (sd_rand_application_vector_get(out + filled, take) == NRF_SUCCESS) filled += take;
    } else {
      delay(2);
    }
  }
  return filled == length;
}

// Written to a temp file, then renamed over the record: LittleFS renames
// atomically, so a power loss leaves either the old or the new record,
// never a torn one (which would mint a new secret on the next boot).
static bool writeSecretFile(const uint8_t secret[SECRET_LENGTH], bool owned) {
  if (!InternalFS.exists(SECRET_DIR)) InternalFS.mkdir(SECRET_DIR);
  SecretRecord rec = makeSecretRecord(secret, owned);
  File file(InternalFS);
  if (!file.open(SECRET_TEMP_PATH, FILE_O_WRITE)) return false;
  // FILE_O_WRITE opens read-write + create and seeks to the end (not
  // O_APPEND), so seek(0) rewrites the record in place, as the counter
  // slots do.
  file.seek(0);
  size_t written = file.write((const uint8_t*)&rec, sizeof(rec));
  // A longer foreign file must not survive past the record, or the next
  // boot would reject it again and mint a new secret every time.
  bool truncated = written == sizeof(rec) && file.truncate(sizeof(rec));
  file.close();
  memset(&rec, 0, sizeof(rec));
  return truncated && InternalFS.rename(SECRET_TEMP_PATH, SECRET_PATH);
}

static bool eraseMascotFile() {
  return !InternalFS.exists(MASCOT_PATH) || InternalFS.remove(MASCOT_PATH);
}

// The current link for loop() (see linkCache).
static LinkSecurity currentLink(uint16_t* handle) {
  xSemaphoreTake(passMutex, portMAX_DELAY);
  const LinkSecurity link = linkCache;
  if (handle) *handle = linkCacheHandle;
  xSemaphoreGive(passMutex);
  return link;
}

// After a bond erase: a bonded link still up is dropped 300 ms later
// (dropLinkArmed). Called with passMutex held.
static void armBondedLinkDropLocked() {
  if (linkCacheHandle != BLE_CONN_HANDLE_INVALID && linkCache.bonded) {
    dropLinkHandle = linkCacheHandle;
    dropLinkAtMs = millis() + DEVICE_CONTROL_DRAIN_MS;
    dropLinkArmed = true;
  }
}

// Every bond file and every admission (ble-bonding.md D-6). Called with
// passMutex held, from the loop task: a release, a fresh secret, serial x.
static void clearBondsLocked() {
  bondTable.clear();
  bondTableDirty = false;
  if (InternalFS.exists(BONDS_PATH) && !InternalFS.remove(BONDS_PATH)) Serial.println("bond table: erase failed");
  bond_clear_prph();
  bondFileCount.store(0, std::memory_order_relaxed);
  armBondedLinkDropLocked();
}

// A fresh secret, unowned, persisted before it replaces the one in RAM.
// The same critical section drops the screen_state mailbox and erases the
// mascot record (spec §10, §15): a former owner's phone cannot repaint the
// pin, and the mascot survives a reboot but not a change of owner.
// loop()'s ScreenState forgets the payload on its next pass (owned = 0).
static bool replaceSecret() {
  uint8_t fresh[SECRET_LENGTH];
  if (!fillRandom(fresh, sizeof(fresh)) || !writeSecretFile(fresh, false)) {
    memset(fresh, 0, sizeof(fresh));
    return false;
  }
  xSemaphoreTake(passMutex, portMAX_DELAY);
  memcpy(deviceSecret, fresh, SECRET_LENGTH);
  hasSecret = true;
  secretOwned = false;
  passNonce.clear();
  screenMailboxFull = false;
  // Serial `i`'s "scr" too (spec §15: nothing screen-related survives a
  // change of owner, USB included). The record is loop-task RAM and every
  // caller of replaceSecret() runs on the loop task.
  screenLastApplied.clear();
  screenWritesAccepted.store(0, std::memory_order_relaxed);
  screenWritesBadFormat.store(0, std::memory_order_relaxed);
  screenWritesBadValue.store(0, std::memory_order_relaxed);
  screenWritesNotOwned.store(0, std::memory_order_relaxed);
  mascotStore.erase();
  if (!eraseMascotFile()) Serial.println("mascot record: erase failed");
  // A change of owner erases every pairing and admission in the same
  // section (ble-bonding.md D-6): the trust rule reads the table, so a link
  // that was trusted is not from here on.
  clearBondsLocked();
  xSemaphoreGive(passMutex);
  memset(fresh, 0, sizeof(fresh));
  return true;
}

// Called once from setup() after Bluefruit.begin() (the RNG needs the
// SoftDevice). A missing, torn or foreign record means a fresh pin.
static void restoreSecret() {
  File file(InternalFS);
  if (file.open(SECRET_PATH, FILE_O_READ)) {
    uint8_t buffer[sizeof(SecretRecord) + 1];
    size_t length = file.read(buffer, sizeof(buffer));
    file.close();
    SecretRecord rec;
    if (readSecretRecord(buffer, length, rec)) {
      memcpy(deviceSecret, rec.secret, SECRET_LENGTH);
      hasSecret = true;
      secretOwned = (rec.flags & SECRET_FLAG_OWNED) != 0;
      memset(&rec, 0, sizeof(rec));
      return;
    }
  }
  // A new secret means someone just flashed or erased this pin: it opens
  // the bind window (bind_window.h).
  if (replaceSecret()) {
    freshSecretThisBoot = true;
  } else {
    Serial.println("device secret: RNG or flash failed - passes disabled");
  }
}

// The head of one phone's Bluefruit bond file (its key field) into `head`;
// the length read, 0 when there is no file. Its own frame (the File object),
// so the hash below does not stack on top of it.
static const size_t BOND_HEAD_LENGTH = 1 + BOND_KEYS_LENGTH;
static __attribute__((noinline)) size_t readBondFileHead(const uint8_t addr[BOND_ADDR_LENGTH],
                                                         uint8_t head[BOND_HEAD_LENGTH]) {
  static char path[48];
  bondFilePath(path, sizeof(path), addr);
  File file(InternalFS);
  if (!file.open(path, FILE_O_READ)) return 0;
  size_t length = file.read(head, BOND_HEAD_LENGTH);
  file.close();
  return length;
}

// bondKeyFingerprint() of one phone's bond file, 0 when there is none.
// Loop task only (static buffer).
static uint32_t readBondKeyPrint(const uint8_t addr[BOND_ADDR_LENGTH]) {
  static uint8_t head[BOND_HEAD_LENGTH];
  const size_t length = readBondFileHead(addr, head);
  const uint32_t print = bondKeyFingerprint(head, length);
  memset(head, 0, sizeof(head));
  return print;
}

// The admitted table (bond_table.h), loop task only: an owned pin loads
// it; an unowned one keeps no admission. An entry stays only while its bond
// file holds the keys it was admitted with (ble-bonding.md D-2).
static __attribute__((noinline)) void restoreBondTable() {
  File file(InternalFS);
  static BondTable loaded;  // setup() only; static keeps it off the loop task's stack
  bool ok = false;
  if (secretOwned && file.open(BONDS_PATH, FILE_O_READ)) {
    static uint8_t buffer[BOND_RECORD_LENGTH + 1];
    size_t length = file.read(buffer, sizeof(buffer));
    file.close();
    ok = decodeBondTable(buffer, length, loaded);
  }
  if (ok) {
    static uint8_t addrs[BOND_TABLE_MAX][BOND_ADDR_LENGTH];
    const size_t n = loaded.count;
    for (size_t i = 0; i < n; i++) memcpy(addrs[i], loaded.phones[i].addr, BOND_ADDR_LENGTH);
    size_t dropped = 0;
    for (size_t i = 0; i < n; i++) {
      if (loaded.keepIfSameKeys(addrs[i], readBondKeyPrint(addrs[i]))) dropped++;
    }
    if (dropped > 0) Serial.printf("bond: %u admissions dropped (keys changed or missing)\n", (unsigned)dropped);
    xSemaphoreTake(passMutex, portMAX_DELAY);
    bondTable = loaded;
    bondTableDirty = dropped > 0;
    xSemaphoreGive(passMutex);
    return;
  }
  xSemaphoreTake(passMutex, portMAX_DELAY);
  if (!secretOwned) {
    clearBondsLocked();  // a release cut short by a power loss finishes here (D-6)
  } else {
    bondTable.clear();  // torn or missing: every phone is admitted again
  }
  xSemaphoreGive(passMutex);
}

// Temp file + rename, as the secret record: a power loss keeps the old
// table or the new one, never a torn one.
static __attribute__((noinline)) bool writeBondTableFile(const uint8_t* rec, size_t length) {
  if (!InternalFS.exists(SECRET_DIR)) InternalFS.mkdir(SECRET_DIR);
  File file(InternalFS);
  if (!file.open(BONDS_TEMP_PATH, FILE_O_WRITE)) return false;
  file.seek(0);  // FILE_O_WRITE seeks to the end
  bool ok = file.write(rec, length) == length && file.truncate(length);
  file.close();
  return ok && InternalFS.rename(BONDS_TEMP_PATH, BONDS_PATH);
}

// The mascot byte's flash record (mascot_record.h): loop task only.
static bool writeMascotFile(uint8_t value) {
  if (!InternalFS.exists(SECRET_DIR)) InternalFS.mkdir(SECRET_DIR);
  uint8_t rec[MASCOT_RECORD_LENGTH];
  mascotRecordEncode(value, rec);
  File file(InternalFS);
  if (!file.open(MASCOT_PATH, FILE_O_WRITE)) return false;
  file.seek(0);  // FILE_O_WRITE seeks to the end
  bool ok = file.write(rec, sizeof(rec)) == sizeof(rec) && file.truncate(sizeof(rec));
  file.close();
  return ok;
}

// Called once from setup() after restoreSecret(): an owned pin shows its
// mascot from the first wake; an unowned one must not keep a record.
static void restoreMascot() {
  if (!secretOwned) {
    eraseMascotFile();
    return;
  }
  File file(InternalFS);
  if (!file.open(MASCOT_PATH, FILE_O_READ)) return;
  uint8_t buffer[MASCOT_RECORD_LENGTH + 1];
  size_t length = file.read(buffer, sizeof(buffer));
  file.close();
  uint8_t value = MASCOT_DEFAULT;
  if (mascotRecordDecode(buffer, length, &value)) {
    screen.restoreMascot(value);
    mascotStore.loaded(value);
  }
}

static void openBindWindow(const char* why) {
  xSemaphoreTake(passMutex, portMAX_DELAY);
  bindWindow.openAt(millis());
  xSemaphoreGive(passMutex);
  Serial.printf("bind window: open %lu s (%s)\n", (unsigned long)(BIND_WINDOW_MS / 1000), why);
}

// Free stack words of both tasks, for the boot log and every reseal: the
// device boot smoke records them (docs/pins/firmware.md, Seal task).
static void printStackMarks(const char* when) {
  Serial.printf("stack %s: loop_hw=%lu seal_hw=%lu words free\n", when,
                (unsigned long)uxTaskGetStackHighWaterMark(NULL),
                (unsigned long)(sealTask ? uxTaskGetStackHighWaterMark(sealTask) : 0));
}

static void sealWorker(void*) {
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    sealJob.length = sealDeviceSecret(deviceId, sealJob.secret, sealJob.blob, sizeof(sealJob.blob));
    memset(sealJob.secret, 0, sizeof(sealJob.secret));
    xSemaphoreGive(sealDone);
  }
}

// Seals on the worker task and waits for it. Only the loop task calls it
// (setup() and loop() share that task), so one job slot is enough.
static size_t sealOnWorker(const uint8_t secret[SECRET_LENGTH], uint8_t* blob) {
  if (sealTask == NULL || sealDone == NULL) return 0;
  memcpy(sealJob.secret, secret, SECRET_LENGTH);
  xTaskNotifyGive(sealTask);
  xSemaphoreTake(sealDone, portMAX_DELAY);
  size_t length = sealJob.length;
  memcpy(blob, sealJob.blob, length);
  memset(sealJob.blob, 0, sizeof(sealJob.blob));
  return length;
}

// Refreshes device_secret: a new seal (a new ephemeral key per link) while
// the pin serves it (unowned, in its bind window), otherwise empty.
static void resealSecret() {
  uint8_t secret[SECRET_LENGTH];
  static uint8_t blob[SEAL_MAX_BLOB];  // loop task only; one reseal at a time
  xSemaphoreTake(passMutex, portMAX_DELAY);
  bool ready = bindWindowServes(hasSecret, secretOwned, bindWindow.isOpen(millis()));
  uint32_t generation = linkGeneration;
  memcpy(secret, deviceSecret, SECRET_LENGTH);
  xSemaphoreGive(passMutex);
  size_t length = ready ? sealOnWorker(secret, blob) : 0;
  memset(secret, 0, sizeof(secret));
  if (ready && length == 0) Serial.println("device secret: sealing failed");
  if (!ready) memset(blob, 0, sizeof(blob));
  xSemaphoreTake(passMutex, portMAX_DELAY);
  if (generation == linkGeneration) {  // no disconnect while sealing
    memcpy(sealedSecret, blob, length);
    sealedSecretLength = length;
    deviceSecretChr.write(sealedSecret, sealedSecretLength);
  }
  xSemaphoreGive(passMutex);
  printStackMarks("reseal");
}

static void runPendingPassAction() {
  // The action stays pending until it is done, so onPassAuthorize answers
  // busy meanwhile: no pass can land while a release rewrites the secret.
  uint8_t action = pendingPassAction.load(std::memory_order_relaxed);
  if (action == PASS_ACTION_NONE) return;
  uint32_t now = millis();
  switch (action) {
    case PASS_ACTION_REBOOT:
      if (controlArmable(PASS_ARG_REBOOT, now)) {
        rebootAtMs = now + DEVICE_CONTROL_DRAIN_MS;
        rebootArmed = true;
        Serial.println("pass: reboot armed");
      } else {
        Serial.println("pass: reboot dropped (60 s uptime gate or reset pending)");
      }
      break;
    case PASS_ACTION_ENTER_DFU:
      if (controlArmable(PASS_ARG_ENTER_DFU, now)) {
        rebootAtMs = now + DEVICE_CONTROL_DRAIN_MS;
        enterDfuArmed = true;
        Serial.println("pass: DFU entry armed");
      } else {
        Serial.println("pass: DFU entry dropped (reset pending)");
      }
      break;
    case PASS_ACTION_CONFIRM: {
      uint8_t secret[SECRET_LENGTH];
      xSemaphoreTake(passMutex, portMAX_DELAY);
      bool already = secretOwned;
      memcpy(secret, deviceSecret, SECRET_LENGTH);
      xSemaphoreGive(passMutex);
      // Owned only once it is on flash: a failed write leaves the pin
      // unowned and the app confirms again.
      if (!already && writeSecretFile(secret, true)) {
        xSemaphoreTake(passMutex, portMAX_DELAY);
        secretOwned = true;
        xSemaphoreGive(passMutex);
        Serial.println("pass: owner confirmed");
      } else if (!already) {
        pendingFeedback.store(4, std::memory_order_relaxed);  // error rhythm; the app confirms again
        Serial.println("pass: confirm not saved");
      } else {
        Serial.println("pass: confirm, already owned");
      }
      memset(secret, 0, sizeof(secret));
      break;
    }
    case PASS_ACTION_FORGET_ALL:
      // ble-bonding.md §9.5: every bond and admission, like serial `x`; a
      // bonded link still up is dropped after the write answer.
      xSemaphoreTake(passMutex, portMAX_DELAY);
      clearBondsLocked();
      heldPress.clear();
      xSemaphoreGive(passMutex);
      Serial.println("pass: every phone forgotten");
      break;
    case PASS_ACTION_RELEASE:
      if (replaceSecret()) {
        Serial.println("pass: released, new secret");
#if RAILY_SHIP_MODE
        // Keyring Air (air-ship-mode.md §2): the owner's app releases it
        // while connected, so the window opens now, before the bonded link
        // drops, and «add again» finds the pin; it sleeps only once the
        // window closes unbound.
        openBindWindow("release");
#endif
        resealSecret();
      } else {
        pendingFeedback.store(4, std::memory_order_relaxed);  // still owned; the app releases again
        Serial.println("pass: release not saved");
      }
      break;
    default:
      break;
  }
  pendingPassAction.store(PASS_ACTION_NONE, std::memory_order_relaxed);
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

#if RAILY_MOTOR
static RhythmPlayer rhythm = {};
static bool motorReady = false;
static bool motorOn = false;  // the state the motor last took
static bool motorQuiet = false;
// A DA7280 write failed: the wanted state is written again, even when it
// equals motorOn (a lost ACK may hide a drive that did land), until it
// takes. Every MOTOR_RETRY_MS, every MOTOR_RETRY_SLOW_MS after
// MOTOR_RETRY_FAST failures in a row (an unplugged board). The GPIO
// fallback never fails.
static bool motorRetry = false;
static bool motorRetryOn = false;  // state requested by the failed write
static uint32_t motorRetryAtMs = 0;
static uint8_t motorFailStreak = 0;
static const uint32_t MOTOR_RETRY_MS = 20;
static const uint32_t MOTOR_RETRY_SLOW_MS = 1000;
static const uint8_t MOTOR_RETRY_FAST = 10;
static const uint8_t MOTOR_ON_LEVEL = RAILY_MOTOR_ACTIVE_LOW ? LOW : HIGH;
static const uint8_t MOTOR_OFF_LEVEL = RAILY_MOTOR_ACTIVE_LOW ? HIGH : LOW;

// D6/D7 as open-drain lines for soft_i2c.h: S0D1 (pull low or let go,
// never drive high) with the input buffer connected, so a read sees the
// wire. OUT is set before the pin becomes an output: no low glitch.
struct HapticPins {
  uint32_t sdaPin = 0;
  uint32_t sclPin = 0;
  static void openDrain(uint32_t pin) {
    nrf_gpio_pin_set(pin);
    nrf_gpio_cfg(pin, NRF_GPIO_PIN_DIR_OUTPUT, NRF_GPIO_PIN_INPUT_CONNECT, NRF_GPIO_PIN_NOPULL,
                 NRF_GPIO_PIN_S0D1, NRF_GPIO_PIN_NOSENSE);
  }
  void begin(bool swapped) {
    sdaPin = g_ADigitalPinMap[swapped ? RAILY_HAPTIC_SCL_PIN : RAILY_HAPTIC_SDA_PIN];
    sclPin = g_ADigitalPinMap[swapped ? RAILY_HAPTIC_SDA_PIN : RAILY_HAPTIC_SCL_PIN];
    openDrain(sdaPin);
    openDrain(sclPin);
  }
  // Back to the reset default (a disconnected input), as on an old build.
  void end() {
    nrf_gpio_cfg_default(sdaPin);
    nrf_gpio_cfg_default(sclPin);
  }
  void sda(bool release) { nrf_gpio_pin_write(sdaPin, release ? 1 : 0); }
  void scl(bool release) { nrf_gpio_pin_write(sclPin, release ? 1 : 0); }
  bool readSda() { return nrf_gpio_pin_read(sdaPin) != 0; }
  bool readScl() { return nrf_gpio_pin_read(sclPin) != 0; }
  __attribute__((noinline)) void halfPeriod() { delayMicroseconds(5); }  // ~100 kHz
};

// The old Grove module on D0: a plain output at the off level.
struct GroveMotorPin {
  void begin() {
    pinMode(RAILY_MOTOR_PIN, OUTPUT);
    digitalWrite(RAILY_MOTOR_PIN, MOTOR_OFF_LEVEL);
  }
  void drive(bool on) { digitalWrite(RAILY_MOTOR_PIN, on ? MOTOR_ON_LEVEL : MOTOR_OFF_LEVEL); }
};

// The DA7280 on the hardware Wire bus, D4/D5 (Keyring Air): the bus the
// OLED shares (0x3C; on the Expansion Board also the RTC 0x51). begin()
// checks the pull-ups first (the core's Wire busy-waits); end() hands the
// bus back to the OLED probe as if nothing had run. Loop task only, like
// the panel, so the two never meet on the bus.
static const uint32_t HAPTIC_WIRE_HZ = 400000;  // the panel's rate (oled_render.cpp)
struct HapticWire {
  bool begin() {
    if (!wireBusPulledUp()) return false;
    Wire.begin();
    Wire.setClock(HAPTIC_WIRE_HZ);
    return true;
  }
  void end() {
    Wire.end();
    releaseWirePins();
  }
  bool writeRegister(uint8_t address, uint8_t reg, uint8_t value) {
    Wire.beginTransmission(address);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
  }
  bool readRegister(uint8_t address, uint8_t reg, uint8_t* value) {
    Wire.beginTransmission(address);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(address, (uint8_t)1) != 1) return false;
    *value = (uint8_t)Wire.read();
    return true;
  }
};

static HapticPins hapticPins;
static GroveMotorPin groveMotor;
static HapticWire hapticWire;
static HapticMotor<HapticPins, GroveMotorPin, HapticWire> hapticMotor(hapticPins, groveMotor, &hapticWire);

// Its own frame: the bit-bang stays out of the updateOutputs stack budget.
static __attribute__((noinline)) bool driveMotor(bool on) { return hapticMotor.drive(on); }
#endif

// The motor holds the Wire bus (the Air's DA7280): the OLED probe must not
// end it.
static bool motorHoldsWire() {
#if RAILY_MOTOR
  return hapticMotor.holdsWire();
#else
  return false;
#endif
}

// The motor, from setup() after BLE advertises (the pre-flash gate:
// nothing before Bluefruit.begin() may fault). Probes the DA7280 on Wire
// D4/D5, then on D6/D7 (a few ms, every wait bounded), else makes D0 a
// plain output at the off level. Until then D0, D6 and D7 are the reset
// default, unconnected inputs, and D4/D5 wait for the panel probe.
static __attribute__((noinline)) void startMotorPin() {
#if RAILY_MOTOR
  const HapticBackend backend = hapticMotor.begin();
  if (backend == HAPTIC_BACKEND_DA7280_WIRE) {
    Serial.printf("haptic: DA7280 chip 0x%02X at 0x%02X on Wire (D4/D5)\n", hapticMotor.chipRev(),
                  da7280::ADDRESS);
  } else if (backend == HAPTIC_BACKEND_DA7280) {
    const bool swapped = hapticMotor.swapped();
    Serial.printf("haptic: DA7280 chip 0x%02X at 0x%02X, SDA=D%u SCL=D%u%s\n", hapticMotor.chipRev(),
                  da7280::ADDRESS, (unsigned)(swapped ? RAILY_HAPTIC_SCL_PIN : RAILY_HAPTIC_SDA_PIN),
                  (unsigned)(swapped ? RAILY_HAPTIC_SDA_PIN : RAILY_HAPTIC_SCL_PIN),
                  swapped ? " (swapped: the cable is reversed)" : "");
  } else {
    Serial.printf("haptic: no DA7280 at 0x%02X on Wire (id 0x%02X) or D%u/D%u (id 0x%02X), motor on D0 (Grove)\n",
                  da7280::ADDRESS, hapticMotor.wireChipRev(), (unsigned)RAILY_HAPTIC_SDA_PIN,
                  (unsigned)RAILY_HAPTIC_SCL_PIN, hapticMotor.chipRev());
  }
  motorReady = true;
#endif
}

// One pass of the motor: the owner's rhythms (rhythm.h) from the raw
// event_ack byte, silenced by screen_state quiet hours (flag bit 1, which
// only a trusted write carries). Loop task only.
static void updateMotor(uint32_t now, uint8_t rawAck) {
#if RAILY_MOTOR
  if (!motorReady) return;
  const bool quiet = screen.quietHours();
  if (quiet != motorQuiet) {
    rhythm.setQuietHours(quiet, now);
    motorQuiet = quiet;
  }
  if (rawAck != 0) rhythm.trigger(rhythmForEventAck(rawAck), now);
  const bool on = rhythm.tick(now);
  const bool due = !motorRetry || (int32_t)(now - motorRetryAtMs) >= 0;
  const bool offSupersedesOnRetry = hapticOffSupersedesRetry(motorRetry, motorRetryOn, on);
  if ((on != motorOn || motorRetry) && (due || offSupersedesOnRetry)) {
    if (driveMotor(on)) {
      motorOn = on;
      motorRetry = false;
      motorRetryOn = false;
      motorFailStreak = 0;
    } else {
      motorRetry = true;
      motorRetryOn = on;
      if (motorFailStreak < 0xFF) motorFailStreak++;
      motorRetryAtMs = now + hapticRetryDelayMs(on, motorFailStreak, MOTOR_RETRY_MS, MOTOR_RETRY_SLOW_MS,
                                                MOTOR_RETRY_FAST);
    }
  }
#else
  (void)now;
  (void)rawAck;
#endif
}

#if RAILY_VOICE
// ---- Voice from the pin (docs/pins/voice-from-pin.md, slice V2) ----
//
// The PDM interrupt copies each finished 20 ms EasyDMA buffer into voicePcm
// (voice_capture.h) and wakes loop(); loop() runs the high-pass, encodes
// into the frame ring (voice_ring.h) and notifies voice_stream while the
// SoftDevice has TX credits. The gesture (voice_gesture.h) decides when the
// mic runs: only between an armed press and its release, the 60 s cap, a
// phone STOP or a lost link. Loop task only, except the interrupt (voicePcm,
// the DMA indices) and the BLE task (the MTU, PHY and data length it saw,
// the TX credits it returned, a STOP).

// The Sense module's LSM6DS3TR-C on its own I2C lines (Wire1's pins, read
// here over soft_i2c.h: the sketch never starts Wire1), powered from P1.08.
static const uint8_t SENSE_IMU_ADDRESS = 0x6A;
static const uint8_t SENSE_IMU_WHO_AM_I_REG = 0x0F;
static const uint8_t SENSE_IMU_WHO_AM_I = 0x6A;
static uint8_t senseImuWho = 0;  // what the probe read, 0 when nothing answered

struct SenseImuPins {
  uint32_t sdaPin = 0;
  uint32_t sclPin = 0;
  static void openDrain(uint32_t pin) {
    nrf_gpio_pin_set(pin);
    nrf_gpio_cfg(pin, NRF_GPIO_PIN_DIR_OUTPUT, NRF_GPIO_PIN_INPUT_CONNECT, NRF_GPIO_PIN_NOPULL,
                 NRF_GPIO_PIN_S0D1, NRF_GPIO_PIN_NOSENSE);
  }
  void begin() {
    sdaPin = g_ADigitalPinMap[PIN_WIRE1_SDA];  // P0.07
    sclPin = g_ADigitalPinMap[PIN_WIRE1_SCL];  // P0.27
    openDrain(sdaPin);
    openDrain(sclPin);
  }
  void end() {
    nrf_gpio_cfg_default(sdaPin);
    nrf_gpio_cfg_default(sclPin);
  }
  void sda(bool release) { nrf_gpio_pin_write(sdaPin, release ? 1 : 0); }
  void scl(bool release) { nrf_gpio_pin_write(sclPin, release ? 1 : 0); }
  bool readSda() { return nrf_gpio_pin_read(sdaPin) != 0; }
  bool readScl() { return nrf_gpio_pin_read(sclPin) != 0; }
  __attribute__((noinline)) void halfPeriod() { delayMicroseconds(5); }
};

// The runtime board probe of voice-from-pin.md §9: the IMU and the mic come
// together on the Sense module, a plain XIAO has neither. P1.08 at high
// drive (at standard drive it cannot hold up the IMU rail, which also feeds
// the I2C pull-ups: docs/pins/air-factory-test.md row 7), 100 ms for the
// rail and the sensor's boot, WHO_AM_I, then P1.08 back to the reset
// default, as 0.3.0 leaves it. Without pull-ups (a plain XIAO) the soft bus
// fails at once. From setup(), after Bluefruit.begin().
static __attribute__((noinline)) bool probeSenseBoard() {
  const uint32_t power = g_ADigitalPinMap[PIN_LSM6DS3TR_C_POWER];  // P1.08
  nrf_gpio_pin_set(power);
  nrf_gpio_cfg(power, NRF_GPIO_PIN_DIR_OUTPUT, NRF_GPIO_PIN_INPUT_DISCONNECT, NRF_GPIO_PIN_NOPULL,
               NRF_GPIO_PIN_H0H1, NRF_GPIO_PIN_NOSENSE);
  delay(100);
  SenseImuPins pins;
  pins.begin();
  SoftI2c<SenseImuPins> bus(pins);
  uint8_t who = 0;
  const bool answered = bus.readRegister(SENSE_IMU_ADDRESS, SENSE_IMU_WHO_AM_I_REG, &who);
  pins.end();
  nrf_gpio_cfg_default(power);
  senseImuWho = answered ? who : 0;
  return answered && who == SENSE_IMU_WHO_AM_I;
}

// PDM: explicit PSEL from voice_board.h, never a variant's PIN_PDM_*.
static const VoicePdmConfig& voiceMic = VOICE_PDM_BOARD;
static int16_t voiceDma[2][VOICE_FRAME_SAMPLES];
static volatile uint8_t voiceDmaFilling = 0xFF;  // the buffer EasyDMA fills now (0xFF before the first)
static volatile uint8_t voiceDmaNext = 0;        // the buffer SAMPLE.PTR names for the one after
static VoicePcmFifo<VOICE_PCM_FIFO_FRAMES> voicePcm;
static VoiceHighPass voiceHpf;
static bool voiceMicRunning = false;
static uint8_t voiceGain = VOICE_PDM_XIAO_SENSE.gainRegister;  // GAINL/GAINR, the bench may change it (G)
static bool voiceMicHighDrive = true;  // the mic power pin at H0H1 (the bench may change it: d)
static uint32_t voiceMicStartMs = 0;
static uint32_t voiceOverrunsSeen = 0;  // PCM FIFO overruns already acted on in this capture
static bool batteryCriticalNow();       // battery telemetry, defined after the voice block

extern "C" void PDM_IRQHandler(void) {
  if (NRF_PDM->EVENTS_END) {
    NRF_PDM->EVENTS_END = 0;
    if (voiceDmaFilling < 2) voicePcm.push(voiceDma[voiceDmaFilling]);
#if RAILY_IDLE_SLEEP
    wakeLoopFromISR();
#endif
  }
  if (NRF_PDM->EVENTS_STARTED) {
    NRF_PDM->EVENTS_STARTED = 0;
    // SAMPLE.PTR was latched for this buffer: name the next one.
    voiceDmaFilling = voiceDmaNext;
    voiceDmaNext = (uint8_t)(voiceDmaNext ^ 1);
    NRF_PDM->SAMPLE.PTR = (uint32_t)voiceDma[voiceDmaNext];
  }
  (void)NRF_PDM->EVENTS_STARTED;  // the clears reach the peripheral before the interrupt returns
}

// Mic on: the HFXO for an exact 1.280 MHz PDM clock (without it the PDM
// runs from the RC oscillator, about 1 % off), the mic's power pin, the
// PDM peripheral. Loop task only.
static __attribute__((noinline)) void voiceMicStart() {
  if (voiceMicRunning || !voiceMic.present) return;
  voicePcm.reset();
  voiceOverrunsSeen = 0;
  voiceHpf.reset();
  sd_clock_hfclk_request();
  if (voiceMic.powerPin != VOICE_PDM_NO_PIN) {
    nrf_gpio_pin_write(voiceMic.powerPin, voiceMic.powerActiveHigh ? 1 : 0);
    nrf_gpio_cfg(voiceMic.powerPin, NRF_GPIO_PIN_DIR_OUTPUT, NRF_GPIO_PIN_INPUT_DISCONNECT, NRF_GPIO_PIN_NOPULL,
                 voiceMicHighDrive ? NRF_GPIO_PIN_H0H1 : NRF_GPIO_PIN_S0S1, NRF_GPIO_PIN_NOSENSE);
  }
  nrf_gpio_pin_clear(voiceMic.clkPin);
  nrf_gpio_cfg_output(voiceMic.clkPin);
  nrf_gpio_cfg_input(voiceMic.dataPin, NRF_GPIO_PIN_NOPULL);
  NRF_PDM->PSEL.CLK = voiceMic.clkPin;
  NRF_PDM->PSEL.DIN = voiceMic.dataPin;
  NRF_PDM->PDMCLKCTRL = voiceMic.pdmClkCtrl;
  NRF_PDM->RATIO = voiceMic.ratio == 80 ? PDM_RATIO_RATIO_Ratio80 : PDM_RATIO_RATIO_Ratio64;
  NRF_PDM->MODE = (PDM_MODE_OPERATION_Mono << PDM_MODE_OPERATION_Pos) |
                  ((voiceMic.leftFalling ? PDM_MODE_EDGE_LeftFalling : PDM_MODE_EDGE_LeftRising) << PDM_MODE_EDGE_Pos);
  NRF_PDM->GAINL = voiceGain;
  NRF_PDM->GAINR = voiceGain;
  voiceDmaFilling = 0xFF;
  voiceDmaNext = 0;
  NRF_PDM->SAMPLE.PTR = (uint32_t)voiceDma[0];
  NRF_PDM->SAMPLE.MAXCNT = VOICE_FRAME_SAMPLES;
  NRF_PDM->EVENTS_STARTED = 0;
  NRF_PDM->EVENTS_END = 0;
  NRF_PDM->EVENTS_STOPPED = 0;
  NRF_PDM->INTENSET = PDM_INTENSET_STARTED_Msk | PDM_INTENSET_END_Msk;
  NVIC_SetPriority(PDM_IRQn, 7);  // the lowest application priority, as the core's PDM library
  NVIC_ClearPendingIRQ(PDM_IRQn);
  NVIC_EnableIRQ(PDM_IRQn);
  NRF_PDM->ENABLE = 1;
  NRF_PDM->TASKS_START = 1;
  voiceMicRunning = true;
  voiceMicStartMs = millis();
}

// Mic off: the PDM stops (at most one frame), the pins go back to the reset
// default with the mic unpowered, the HFXO request is released.
static __attribute__((noinline)) void voiceMicStop() {
  if (!voiceMicRunning) return;
  // The interrupt off first: a frame still filling is not wanted, and the
  // STOPPED event is polled here, not cleared there.
  NRF_PDM->INTENCLR = 0xFFFFFFFFUL;
  NVIC_DisableIRQ(PDM_IRQn);
  NRF_PDM->EVENTS_STOPPED = 0;
  NRF_PDM->TASKS_STOP = 1;
  const uint32_t start = millis();
  while (!NRF_PDM->EVENTS_STOPPED && (uint32_t)(millis() - start) < 30) delayMicroseconds(100);
  NRF_PDM->EVENTS_STOPPED = 0;
  NRF_PDM->EVENTS_STARTED = 0;
  NRF_PDM->EVENTS_END = 0;
  NVIC_ClearPendingIRQ(PDM_IRQn);
  NRF_PDM->ENABLE = 0;
  NRF_PDM->PSEL.CLK = 0xFFFFFFFFUL;  // disconnected
  NRF_PDM->PSEL.DIN = 0xFFFFFFFFUL;
  nrf_gpio_cfg_default(voiceMic.clkPin);
  if (voiceMic.powerPin != VOICE_PDM_NO_PIN) {
    nrf_gpio_pin_write(voiceMic.powerPin, voiceMic.powerActiveHigh ? 0 : 1);
    nrf_gpio_cfg_default(voiceMic.powerPin);
  }
  sd_clock_hfclk_release();
  voiceMicRunning = false;
}

// The uplink, the gesture and where the frames go.
static VoiceUplink<VOICE_RING_FRAMES> voiceUplink;
static VoiceGesture voiceHold = voiceGesture();
static bool voiceSinkSerial = false;  // the current hold is the bench's `v`: frames go out as VF lines
// The bench's `v` on an armed BLE link (a trusted phone that subscribed):
// the frames go to the phone as a button hold's would, and each one is also
// printed as a VF line for the record.
static bool voiceMirrorSerial = false;
static uint32_t voiceUplinkEpoch = 0;  // the link the queued frames belong to
// From the BLE task: the link's ATT MTU, PHY and LL data length as the
// SoftDevice granted them (serial `i`), and the notifications in flight.
static std::atomic<uint16_t> voiceLinkMtu(BLE_GATT_ATT_MTU_DEFAULT);
static std::atomic<uint8_t> voiceLinkPhy(BLE_GAP_PHY_1MBPS);
static std::atomic<uint16_t> voiceLinkDataLength(27);
// The SoftDevice's notify queue is VOICE_HVN_QUEUE deep (set in setup()):
// voice sends only while fewer than VOICE_INFLIGHT_MAX notifications of any
// kind are in it (hvnInflight), so a press always has a slot and notify()
// never waits for one. At 3 deep (BANDWIDTH_MAX) only 2 frames went per
// connection event: the 2 s pre-roll drained at about 1.3x real time, the
// last frame reached the phone 3 s after the release (bench 2026-10-09).
static const uint8_t VOICE_HVN_QUEUE = 8;
static const uint8_t VOICE_INFLIGHT_MAX = VOICE_HVN_QUEUE - 1;
// voice_control STOP from the phone (BLE task), taken by loop(): the
// utterance it was written for, +1 (0 = none). A STOP for an utterance that
// is no longer the live one (written while idle, or before a reconnect) is
// dropped, never applied to the next hold.
static std::atomic<uint16_t> pendingVoiceStop(0);
static std::atomic<uint8_t> voiceStopReason(0);  // the phone's STOP reason byte, for the log
// The 30-45 ms idle interval after END is retried until the SoftDevice takes
// it (it refuses with BUSY while the START request is still pending).
static bool voiceIdleIntervalPending = false;
static uint16_t voiceIdleIntervalHandle = BLE_CONN_HANDLE_INVALID;

// The START cue and its zeroed frames (voice_capture.h): none at the 2 s
// talk hold, where START lands mid-sentence.
static VoiceStartCue voiceStartCue = {};
// Serial `i` and the bench: since boot.
static uint32_t voiceUtterances = 0;
static uint32_t voiceOverruns = 0;
#if RAILY_VOICE_BENCH
static bool voiceRawDump = false;
#endif

// Apple's rules (voice-from-pin.md §4): min a multiple of 15 ms, max at
// least min + 15 ms, supervision timeout 6 s. Units of 1.25 ms and 10 ms.
static uint32_t voiceRequestInterval(uint16_t handle, bool streaming) {
  if (handle == BLE_CONN_HANDLE_INVALID) return NRF_SUCCESS;
  ble_gap_conn_params_t params = {};
  params.min_conn_interval = streaming ? 12 : 24;  // 15 ms : 30 ms
  params.max_conn_interval = streaming ? 24 : 36;  // 30 ms : 45 ms
  params.slave_latency = 0;
  params.conn_sup_timeout = 600;
  const uint32_t err = sd_ble_gap_conn_param_update(handle, &params);
  if (err != NRF_SUCCESS && err != NRF_ERROR_BUSY) {
    Serial.printf("voice: interval request refused (0x%lx)\n", (unsigned long)err);
  }
  return err;
}

// The link a button hold talks on: the trust rule of pressLinkReady
// (owned, encrypted, bonded, admitted) plus a voice_stream subscription and
// an MTU that carries one AUDIO frame per notification. The bench's `v`
// holds talk to the USB serial port instead.
static VoiceLink voiceLinkNow(uint16_t* handle, uint32_t* epoch) {
  xSemaphoreTake(passMutex, portMAX_DELAY);
  const LinkSecurity link = linkCache;
  *handle = linkCacheHandle;
  *epoch = linkEpoch;
  const bool trusted = *handle != BLE_CONN_HANDLE_INVALID && linkTrustedLocked(link);
  xSemaphoreGive(passMutex);
  VoiceLink out = {voiceCapable, *handle != BLE_CONN_HANDLE_INVALID, false};
  out.armed = trusted && voiceStreamChr.notifyEnabled(*handle) &&
              voiceLinkMtu.load(std::memory_order_relaxed) >= VOICE_MIN_ATT_MTU;
#if RAILY_VOICE_BENCH
  if (voiceSinkSerial) {
    out.linkUp = true;
    out.armed = true;
  }
  if (voiceRawDump) out.armed = false;  // the raw dump owns the mic
#endif
  return out;
}

// «Didn't work»: the red LED and the screen, as 0.3.0 does for a 2 s
// press. The motor's ▬▬▬ only when a talk failed (the mic was on: the cap,
// a lost link, a full ring, a phone STOP), so a press that never armed the
// mic stays exactly 0.3.0's.
static void voiceFailCue(bool talkFailed) {
  pendingFeedback.store(4, std::memory_order_relaxed);
  pendingScreenPressFailed.store(true, std::memory_order_relaxed);
  if (talkFailed) {
    pendingRhythmAck.store(4, std::memory_order_relaxed);
  } else {
    Serial.println("button: held 2 s or more, no scan");
  }
  RAILY_WAKE_LOOP();
}

// One encoded 20 ms frame from the PCM FIFO into the uplink (or, on the
// bench, out as raw PCM). False when the ring is full.
static __attribute__((noinline)) bool voiceTakeFrame(uint32_t now) {
  static int16_t pcm[VOICE_FRAME_SAMPLES];
  const int16_t* front = voicePcm.front();
  if (front == 0) return true;
  memcpy(pcm, front, sizeof(pcm));
  voicePcm.pop();
  voiceHpf.run(pcm, VOICE_FRAME_SAMPLES);
#if RAILY_VOICE_BENCH
  if (voiceRawDump) {
    static char line[2 * sizeof(pcm) + 8];
    const uint16_t n = voiceHexLine(line, "VP", (const uint8_t*)pcm, sizeof(pcm));
    Serial.write(line, n);
    return true;
  }
#endif
  if (voiceStartCue.zeroes(now)) memset(pcm, 0, sizeof(pcm));
  return voiceUplink.capture(pcm);
}

static void voiceApply(const VoiceGestureOut& out, uint32_t now, uint16_t handle, bool micWasOn = true);

// Every frame the mic has given so far goes into the uplink; a full ring
// ends the utterance (END 2).
static void voiceDrain(uint32_t now, uint16_t handle) {
  while (!voicePcm.empty()) {
    if (!voiceTakeFrame(now)) {
      voiceApply(voiceHold.stop(VOICE_END_OVERFLOW), now, handle);
      break;
    }
  }
}

// What the gesture said, in its order: mic, frames, then the scan or the
// cue. micWasOn: the mic ran before this step (a FAIL is then a failed talk).
static void voiceApply(const VoiceGestureOut& out, uint32_t now, uint16_t handle, bool micWasOn) {
  if (out.actions == VOICE_ACT_NONE) return;
  if (out.has(VOICE_ACT_MIC_ON)) voiceMicStart();
  if (out.has(VOICE_ACT_SEND_PRESS)) {
    voiceUtterances++;
    if (!voiceUplink.press()) Serial.println("voice: two utterances still queued, the older ones given up");
  }
  if (out.has(VOICE_ACT_SEND_START)) {
    voiceDrain(now, handle);  // the pre-roll so far, before the cue zeroes anything
    voiceUplink.start(voicePdmGainHalfDb(voiceGain));
    // ▮ when audio starts leaving the pin, only for a short talk hold.
    if (voiceStartCue.start(now, VOICE_TALK_HOLD_MS)) pendingRhythmAck.store(1, std::memory_order_relaxed);
    if (!voiceSinkSerial) voiceRequestInterval(handle, true);
    RAILY_WAKE_LOOP();
  }
  if (out.has(VOICE_ACT_MIC_OFF)) {
    voiceMicStop();
    if (!out.has(VOICE_ACT_DROP_AUDIO)) voiceDrain(now, handle);  // what the mic gave up to the release
    voiceOverruns += voicePcm.overruns;
    voicePcm.reset();
    voiceOverrunsSeen = 0;
  }
  if (out.has(VOICE_ACT_DROP_AUDIO)) voiceUplink.drop();
  if (out.has(VOICE_ACT_SEND_END)) {
    voiceUplink.finish(out.endReason);
    if (!voiceSinkSerial) {
      voiceIdleIntervalPending = voiceRequestInterval(handle, false) == NRF_ERROR_BUSY;
      voiceIdleIntervalHandle = handle;
    }
    Serial.printf("voice: END reason %u, %u frames queued or sent, %u dropped, %lu PCM overruns\n",
                  (unsigned)out.endReason, (unsigned)(voiceUplink.current.framesSent + voiceUplink.ring.count),
                  (unsigned)voiceUplink.current.framesDropped, (unsigned long)voiceOverruns);
  }
  if (out.has(VOICE_ACT_SCAN)) emitButtonEvent(0);  // press_type 0: the hardware button, as serial `p`
  if (out.has(VOICE_ACT_FAIL)) voiceFailCue(micWasOn);
}

// The hold sources: the D1 button (debounced edges) and the bench's v / V.
static void voiceHoldBegan(uint32_t atMs, bool fromSerial) {
  if (voiceHold.held()) return;
  uint16_t handle = BLE_CONN_HANDLE_INVALID;
  uint32_t epoch = 0;
  voiceSinkSerial = false;
  VoiceLink link = voiceLinkNow(&handle, &epoch);
  // A bench hold goes to the phone when the phone could take a button hold.
  voiceMirrorSerial = fromSerial && link.armed;
  if (fromSerial && !link.armed) {
    voiceSinkSerial = true;
    link = voiceLinkNow(&handle, &epoch);
  }
  if (!voiceSinkSerial && voiceUplinkEpoch != epoch) {
    voiceUplink.reset();  // frames from another link never go to this one
    voiceUplinkEpoch = epoch;
  }
  voiceApply(voiceHold.holdBegan(atMs, link), millis(), handle);
}

// quiet: the ship-mode rule silenced this release (ship_mode.h): no scan
// and no «didn't work». It can only come with the mic off (an unowned pin
// never talks); a talk that ran still ends as usual.
static void voiceHoldEnded(uint32_t atMs, bool quiet) {
  uint16_t handle = BLE_CONN_HANDLE_INVALID;
  uint32_t epoch = 0;
  const VoiceLink link = voiceLinkNow(&handle, &epoch);
  const bool micWasOn = voiceHold.micOn();
  VoiceGestureOut out = voiceHold.holdEnded(atMs, link);
  if (quiet && !micWasOn) out.actions = (uint16_t)(out.actions & ~(VOICE_ACT_SCAN | VOICE_ACT_FAIL));
  voiceApply(out, millis(), handle, micWasOn);
}

// Something is still to go out (idleSleepMs). Its own frame: the copy is static.
static __attribute__((noinline)) bool voiceUplinkBusy() {
  static uint8_t frame[VOICE_AUDIO_BYTES];
  return voiceUplink.peek(frame) != 0;
}

// Notifications while the SoftDevice has room, or VF lines on the bench.
static __attribute__((noinline)) void voiceSend(uint16_t handle, bool armed) {
  static uint8_t frame[VOICE_AUDIO_BYTES];
#if RAILY_VOICE_BENCH
  static char line[2 * VOICE_AUDIO_BYTES + 8];
#endif
  for (uint8_t i = 0; i < 8; i++) {
    const uint8_t n = voiceUplink.peek(frame);
    if (n == 0) return;
#if RAILY_VOICE_BENCH
    if (voiceSinkSerial) {
      Serial.write(line, voiceHexLine(line, "VF", frame, n));
      voiceUplink.consume();
      continue;
    }
#endif
    if (!armed || hvnInflight.load(std::memory_order_relaxed) >= VOICE_INFLIGHT_MAX) return;
    if (!noteNotify(voiceStreamChr.notify(handle, frame, n))) return;  // no buffer now: the next pass
#if RAILY_VOICE_BENCH
    if (voiceMirrorSerial) Serial.write(line, voiceHexLine(line, "VF", frame, n));
#endif
    voiceUplink.consume();
  }
}

// One loop() pass of voice: a phone STOP, the gesture's clock, the mic's
// frames, the link.
static __attribute__((noinline)) void serviceVoice() {
  if (!voiceCapable) return;
  const uint32_t now = millis();
  uint16_t handle = BLE_CONN_HANDLE_INVALID;
  uint32_t epoch = 0;
  const VoiceLink link = voiceLinkNow(&handle, &epoch);
  const uint16_t stopFor = pendingVoiceStop.exchange(0, std::memory_order_relaxed);
  if (stopFor != 0) {
    Serial.printf("voice: phone STOP reason %u for utterance %u (live %u, mic %u)\n",
                  (unsigned)voiceStopReason.load(std::memory_order_relaxed), (unsigned)(uint8_t)(stopFor - 1),
                  (unsigned)voiceUplink.utterance, voiceHold.micOn() ? 1u : 0u);
  }
  if (stopFor != 0 && (uint8_t)(stopFor - 1) == voiceUplink.utterance && voiceHold.micOn() && !voiceSinkSerial) {
    voiceApply(voiceHold.stop(VOICE_END_PHONE_STOP), now, handle);
  }
  // 3. A critical battery (battery_curve.h, from VDD) ends a talk: END 3.
  if (voiceHold.micOn() && batteryCriticalNow()) {
    voiceApply(voiceHold.stop(VOICE_END_LOW_BATTERY), now, handle);
  }
  // 5. A PCM frame the FIFO had no room for is a hole the phone cannot see
  // (the next frames get the next seq): end the utterance as an overflow.
  const uint32_t overruns = voicePcm.overruns;
  if (overruns != voiceOverrunsSeen) {
    const uint32_t lost = overruns - voiceOverrunsSeen;
    voiceOverrunsSeen = overruns;
    if (voiceHold.micOn()) {
      voiceUplink.current.framesDropped = (uint16_t)(voiceUplink.current.framesDropped + lost);
      voiceApply(voiceHold.stop(VOICE_END_OVERFLOW), now, handle);
    }
  }
  // 7. The idle interval after END, until the SoftDevice takes it.
  if (voiceIdleIntervalPending) {
    if (voiceIdleIntervalHandle != handle || voiceHold.micOn()) {
      voiceIdleIntervalPending = false;
    } else if (voiceRequestInterval(handle, false) != NRF_ERROR_BUSY) {
      voiceIdleIntervalPending = false;
    }
  }
  // A button hold's clock stops at a release the debounce has not settled
  // yet, so a press that ends just under 2 s never starts talking.
  uint32_t tickAt = now;
#if RAILY_BUTTON
  if (!voiceSinkSerial && buttonDebounce.pressed && buttonDebounce.unsettled()) tickAt = buttonDebounce.candidateAtMs;
#endif
  voiceApply(voiceHold.tick(tickAt, link), now, handle);
  voiceDrain(now, handle);
  // A link that can no longer carry voice, or another link, never gets
  // what was queued for the old one (never a replay after a reconnect).
  if (!voiceSinkSerial && (!link.armed || epoch != voiceUplinkEpoch)) {
    voiceUplink.reset();
    voiceUplinkEpoch = epoch;
  }
  voiceSend(handle, link.armed);
}

// From onBleEvent (BLE task, after Bluefruit's own handlers): what the
// SoftDevice granted this link, and the notifications it has sent. On a
// voice-capable pin every new link is asked for the LL data length and the
// 2M PHY (§4); the phone may say no, 1M works too.
static void onVoiceBleEvent(ble_evt_t* evt) {
  const uint16_t connHdl = evt->evt.common_evt.conn_handle;
  switch (evt->header.evt_id) {
    case BLE_GAP_EVT_CONNECTED: {
      voiceLinkMtu.store(BLE_GATT_ATT_MTU_DEFAULT, std::memory_order_relaxed);
      voiceLinkPhy.store(BLE_GAP_PHY_1MBPS, std::memory_order_relaxed);
      voiceLinkDataLength.store(27, std::memory_order_relaxed);
      hvnInflight.store(0, std::memory_order_relaxed);
      pendingVoiceStop.store(0, std::memory_order_relaxed);
      if (!voiceCapable) break;
      sd_ble_gap_data_length_update(connHdl, NULL, NULL);
      const ble_gap_phys_t phys = {BLE_GAP_PHY_2MBPS, BLE_GAP_PHY_2MBPS};
      sd_ble_gap_phy_update(connHdl, &phys);
      break;
    }
    case BLE_GAP_EVT_DISCONNECTED:
      hvnInflight.store(0, std::memory_order_relaxed);
      pendingVoiceStop.store(0, std::memory_order_relaxed);
      voiceLinkMtu.store(BLE_GATT_ATT_MTU_DEFAULT, std::memory_order_relaxed);
      RAILY_WAKE_LOOP();
      break;
    case BLE_GATTS_EVT_EXCHANGE_MTU_REQUEST: {
      BLEConnection* conn = Bluefruit.Connection(connHdl);
      if (conn) voiceLinkMtu.store(conn->getMtu(), std::memory_order_relaxed);
      RAILY_WAKE_LOOP();
      break;
    }
    case BLE_GAP_EVT_DATA_LENGTH_UPDATE:
      voiceLinkDataLength.store(evt->evt.gap_evt.params.data_length_update.effective_params.max_tx_octets,
                                std::memory_order_relaxed);
      break;
    case BLE_GAP_EVT_PHY_UPDATE:
      voiceLinkPhy.store(evt->evt.gap_evt.params.phy_update.tx_phy, std::memory_order_relaxed);
      break;
    case BLE_GATTS_EVT_HVN_TX_COMPLETE: {
      const uint8_t done = evt->evt.gatts_evt.params.hvn_tx_complete.count;
      // loop() and the Ada task add while this subtracts: one atomic step,
      // floored at 0.
      uint8_t inflight = hvnInflight.load(std::memory_order_relaxed);
      while (!hvnInflight.compare_exchange_weak(inflight, inflight > done ? (uint8_t)(inflight - done) : 0,
                                                std::memory_order_relaxed)) {
      }
      if (inflight > 0) RAILY_WAKE_LOOP();  // room for the next frame
      break;
    }
    default:
      break;
  }
}

// voice_control (7B1E000B): exactly one opcode, 0x01 STOP + a reason byte,
// from a trusted link only (the ATT layer already needs encryption). No
// opcode can start the mic. The reason is the phone's (voice-from-pin.md
// §4), logged, not acted on: every STOP ends the utterance with END 4.
static void onVoiceControlAuthorize(uint16_t connHdl, BLECharacteristic*, ble_gatts_evt_write_t* request) {
  if (!linkTrustedNow(connHdl)) {
    rejectWriteAuthorize(connHdl, BLE_GATT_STATUS_ATTERR_INSUF_AUTHORIZATION);
    return;
  }
  if (request->offset != 0 || request->len != 2 || request->data[0] != 0x01) {
    rejectWriteAuthorize(connHdl, BLE_GATT_STATUS_ATTERR_WRITE_NOT_PERMITTED);
    return;
  }
  acceptWriteAuthorize(connHdl, request);
  // The live utterance id, read across tasks: a stale id only drops the STOP.
  voiceStopReason.store(request->data[1], std::memory_order_relaxed);
  pendingVoiceStop.store((uint16_t)(voiceUplink.utterance + 1), std::memory_order_relaxed);
  RAILY_WAKE_LOOP();
}

// The boot line: what the probe found and what voice will use.
static __attribute__((noinline)) void printVoiceBoot() {
  if (!voiceCapable) {
    Serial.printf("voice: no Sense IMU (read 0x%02X): no mic, voice off, the button keeps 0.3.0's rule\n",
                  (unsigned)senseImuWho);
    return;
  }
  Serial.printf("voice: Sense board (IMU WHO_AM_I 0x%02X), mic CLK P%u.%02u DATA P%u.%02u PWR P%u.%02u, "
                "talk hold %lu ms, voice_stream + voice_control\n",
                (unsigned)senseImuWho, (unsigned)(voiceMic.clkPin >> 5), (unsigned)(voiceMic.clkPin & 31),
                (unsigned)(voiceMic.dataPin >> 5), (unsigned)(voiceMic.dataPin & 31),
                (unsigned)(voiceMic.powerPin >> 5), (unsigned)(voiceMic.powerPin & 31),
                (unsigned long)VOICE_TALK_HOLD_MS);
}

// Serial `i`'s voice line: the probe, the mic, the link as voice sees it.
static __attribute__((noinline)) void printVoiceBench() {
  Serial.printf("voice: {\"cap\":%u,\"imu\":\"0x%02X\",\"talk_ms\":%lu,\"mic\":%u,\"gain\":\"0x%02X\",\"drive\":\"%s\","
                "\"mtu\":%u,\"phy\":%u,\"dl\":%u,\"utt\":%lu,\"ovr\":%lu,\"bench\":%u}\n",
                voiceCapable ? 1u : 0u, (unsigned)senseImuWho, (unsigned long)VOICE_TALK_HOLD_MS,
                voiceMicRunning ? 1u : 0u, (unsigned)voiceGain, voiceMicHighDrive ? "h0h1" : "s0s1",
                (unsigned)voiceLinkMtu.load(std::memory_order_relaxed),
                (unsigned)voiceLinkPhy.load(std::memory_order_relaxed),
                (unsigned)voiceLinkDataLength.load(std::memory_order_relaxed), (unsigned long)voiceUtterances,
                (unsigned long)voiceOverruns, RAILY_VOICE_BENCH ? 1u : 0u);
}

#if RAILY_VOICE_BENCH
// Serial `A`: every attribute the SoftDevice serves, from handle 1 until it
// says there is no more. One line each: handle, UUID (a 128-bit vendor UUID
// as its type index and the 16-bit part, 000A = voice_stream), the read and
// write security mode/level (1/1 open, 1/2 encrypted, 0/0 no access) and
// the authorize bits. The phone usually holds the only link, so this is the
// bench's view of the GATT table and its permissions.
static __attribute__((noinline)) void printGattTable() {
  for (uint16_t handle = 1; handle < 0x200; handle++) {
    ble_uuid_t uuid = {};
    ble_gatts_attr_md_t md = {};
    if (sd_ble_gatts_attr_get(handle, &uuid, &md) != NRF_SUCCESS) break;
    Serial.printf("GATT 0x%04X type %u uuid %04X read %u/%u write %u/%u auth r%u w%u\n", (unsigned)handle,
                  (unsigned)uuid.type, (unsigned)uuid.uuid, (unsigned)md.read_perm.sm, (unsigned)md.read_perm.lv,
                  (unsigned)md.write_perm.sm, (unsigned)md.write_perm.lv, (unsigned)md.rd_auth, (unsigned)md.wr_auth);
  }
  Serial.println("GATT end");
}

// Serial v / V / m / G<hh> / d / A (voice_bench.h). Returns false when c is
// not a voice bench command.
static VoiceBench voiceBench = {};

// A half-typed G<hh> owns the next characters: without this, the `c` of
// bench_serial.h would take the C of `G3C` as an event_ack command.
static bool voiceSerialPending() { return voiceBench.gainStage != 0; }

static __attribute__((noinline)) bool voiceSerialChar(char c) {
  VoiceBenchCommand command;
  const bool consumed = voiceBench.feed(c, &command);
  switch (command.action) {
    case VOICE_BENCH_HOLD_BEGIN:
      if (!voiceCapable) {
        Serial.println("voice: no Sense mic on this board");
      } else if (voiceRawDump) {
        Serial.println("voice: raw dump running, m stops it");
      } else {
        voiceHoldBegan(millis(), true);
        Serial.println(voiceSinkSerial ? "voice: hold began (bench v, frames as VF lines)"
                                       : "voice: hold began (bench v, to the phone on voice_stream, mirrored as VF lines)");
      }
      break;
    case VOICE_BENCH_HOLD_END:
      if (voiceHold.held()) {
        voiceHoldEnded(millis(), false);
        Serial.println("voice: hold ended (bench V)");
      }
      break;
    case VOICE_BENCH_RAW_TOGGLE:
      if (!voiceCapable) {
        Serial.println("voice: no Sense mic on this board");
      } else if (voiceHold.held() || voiceUplink.capturing()) {
        Serial.println("voice: a hold is running");
      } else if (!voiceRawDump) {
        voiceRawDump = true;
        voiceMicStart();
        Serial.printf("voice: raw PCM on, gain 0x%02X, drive %s\n", (unsigned)voiceGain,
                      voiceMicHighDrive ? "h0h1" : "s0s1");
      } else {
        const uint32_t ms = millis() - voiceMicStartMs;
        voiceMicStop();
        voiceDrain(millis(), BLE_CONN_HANDLE_INVALID);
        const uint32_t frames = voicePcm.pushed + voicePcm.overruns;
        Serial.printf("VS {\"samples\":%lu,\"ms\":%lu,\"overruns\":%lu,\"gain\":\"0x%02X\"}\n",
                      (unsigned long)(frames * VOICE_FRAME_SAMPLES), (unsigned long)ms,
                      (unsigned long)voicePcm.overruns, (unsigned)voiceGain);
        voiceOverruns += voicePcm.overruns;
        voicePcm.reset();
        voiceOverrunsSeen = 0;
        voiceRawDump = false;
      }
      break;
    case VOICE_BENCH_GAIN:
      voiceGain = command.gain;
      Serial.printf("voice: gain 0x%02X (%d.%d dB) from the next mic start\n", (unsigned)voiceGain,
                    voicePdmGainHalfDb(voiceGain) / 2, abs(voicePdmGainHalfDb(voiceGain) % 2) * 5);
      break;
    case VOICE_BENCH_GAIN_BAD:
      Serial.println("G: two hex digits, 00 (-20 dB) to 50 (+20 dB)");
      break;
    case VOICE_BENCH_DRIVE_TOGGLE:
      voiceMicHighDrive = !voiceMicHighDrive;
      Serial.printf("voice: mic power drive %s from the next mic start\n", voiceMicHighDrive ? "h0h1" : "s0s1");
      break;
    case VOICE_BENCH_GATT_DUMP:
      printGattTable();
      break;
    default:
      break;
  }
  return consumed;
}
#endif
#endif  // RAILY_VOICE

// Battery telemetry (docs/pins/battery-telemetry-design.md; pure parts
// battery_curve.h and vdd_log.h). Loop task only (setup() shares it), never
// a BLE callback. Pins by the Seeed variant's index (g_ADigitalPinMap), not
// the port number: VBAT_ENABLE 14 = P0.14, PIN_VBAT 32 = P0.31 (AIN7),
// 23 = P0.17.
#if !defined(PIN_VBAT) || !defined(VBAT_ENABLE)
#error "the variant has no PIN_VBAT / VBAT_ENABLE: check the core (battery-telemetry-design.md «Traps» 3)"
#endif
static const uint8_t BATTERY_CHG_PIN = 23;  // D23 is P0.17, the BQ25101 ~CHG (LOW = charging)
static uint32_t batteryMv = 0;
static uint8_t batteryPct = 0;
static uint8_t batteryPower = 0;     // the byte served: USB, charging, low, critical
static uint8_t batteryPowerRaw = 0;  // USB and charging only: what a 1 s poll compares
#if !RAILY_BAT_ON_XIAO
static BatteryLowWatch batteryLow = {};  // low / critical from VDD (the Expansion Board kit)
#endif
static uint32_t batteryNextMs = 0;  // the next 60 s reading (idleSleepMs wakes for it)
static uint32_t batteryPollMs = 0;  // the last power-byte poll
static BatteryNotifyMark batteryMark = {};

// USB from the SoftDevice («Traps» 2: it owns NRF_POWER). Charging from
// ~CHG only where the cell is on the XIAO; on the Expansion Board kit the
// XIAO's own charger has no cell, so the bit stays 0.
static uint8_t readPowerByte() {
  uint32_t usbReg = 0;
  const bool usb = sd_power_usbregstatus_get(&usbReg) == NRF_SUCCESS &&
                   (usbReg & POWER_USBREGSTATUS_VBUSDETECT_Msk) != 0;
#if RAILY_BAT_ON_XIAO
  return batteryPowerByte(usb, digitalRead(BATTERY_CHG_PIN) == LOW);
#else
  return batteryPowerByte(usb, false);
#endif
}

// The ADC settings P0.31 is read with: 12-bit, internal 2.4 V range, 40 µs
// acquisition (the divider's ~338 kΩ source reads low at the core's
// default 3 µs). The core keeps them in globals with no getters, so every
// reader sets its own and the VDD reader puts these back after it.
static void adcForVbat() {
  analogReference(AR_INTERNAL_2_4);
  analogReadResolution(12);
  analogSampleTime(40);
}

// P0.31 in mV after the divider's inverse gain, 8 samples.
static __attribute__((noinline)) uint32_t readVbatMv() {
  adcForVbat();
  uint32_t sum = 0;
  for (uint32_t i = 0; i < BATTERY_SAMPLES; i++) sum += analogRead(PIN_VBAT);
  return batteryMillivolts(sum, BATTERY_SAMPLES);
}

// The nRF's own VDD on the SAADC's internal VDD input (no pin): 8 samples,
// 12-bit, internal 3.6 V range (gain 1/6: the 2.4 V range would clamp a
// 3.3 V rail). The P0.31 settings are put back afterwards.
static __attribute__((noinline)) uint32_t readVddMv() {
  analogReference(AR_INTERNAL);
  analogReadResolution(12);
  analogSampleTime(40);
  uint32_t sum = 0;
  for (uint32_t i = 0; i < BATTERY_SAMPLES; i++) sum += analogReadVDD();
  adcForVbat();
  return vddMillivolts(sum, BATTERY_SAMPLES);
}

// One reading. Both values are written every time (a read is always
// current); a notify goes out only on a change («Traps» 7).
#if RAILY_VOICE
// The power byte's critical bit (from VDD on the Expansion Board kit): a talk
// in progress ends with END 3 (voice-from-pin.md §1).
static bool batteryCriticalNow() { return (batteryPower & BATTERY_POWER_CRITICAL) != 0; }
#endif

static __attribute__((noinline)) void sampleBattery(uint32_t now, uint8_t power) {
  batteryMv = readVbatMv();
  batteryPct = batteryPercentFromMv(batteryMv);
  batteryPowerRaw = power;
#if RAILY_BAT_ON_XIAO
  batteryPower = power;
#else
  batteryLow.reading(readVddMv(), (power & BATTERY_POWER_USB) != 0);
  batteryPower = (uint8_t)(power | batteryLow.powerBits());
#endif
  batteryNextMs = now + BATTERY_SAMPLE_PERIOD_MS;
  powerStateChr.write8(batteryPower);
  // notify() is false with no subscriber or no free buffer: the mark stays,
  // so the value goes out on the next reading.
#if RAILY_BAT_ON_XIAO
  blebas.write(batteryPct);
  if (batteryMark.percentDue(batteryPct) && noteNotify(blebas.notify(batteryPct))) batteryMark.percentNotified(batteryPct);
#endif
  if (batteryMark.powerDue(batteryPower) && noteNotify(powerStateChr.notify8(batteryPower))) {
    batteryMark.powerNotified(batteryPower);
  }
}

// From loop(): the power byte once a second, a reading every 60 s or at once
// when USB or charging flips.
static __attribute__((noinline)) void serviceBattery() {
  const uint32_t now = millis();
  if ((uint32_t)(now - batteryPollMs) < BATTERY_POWER_POLL_MS && (int32_t)(now - batteryNextMs) < 0) return;
  batteryPollMs = now;
  const uint8_t power = readPowerByte();
  if (batteryReadingDue(now, batteryNextMs, power, batteryPowerRaw)) sampleBattery(now, power);
}

// Serial `i`'s "bat" (the last P0.31 reading) and "vdd" (VDD now, mV). Its
// own frame, so printDeviceInfo stays within its budget.
static __attribute__((noinline)) void printBatteryBench() {
  Serial.printf(",\"bat\":{\"mv\":%lu,\"pct\":%u,\"usb\":%u,\"chg\":%u,\"low\":%u},\"vdd\":%lu",
                (unsigned long)batteryMv, (unsigned)batteryPct, (batteryPower & BATTERY_POWER_USB) ? 1u : 0u,
                (batteryPower & BATTERY_POWER_CHARGING) ? 1u : 0u,
                (batteryPower & BATTERY_POWER_CRITICAL) ? 2u : (batteryPower & BATTERY_POWER_LOW) ? 1u : 0u,
                (unsigned long)readVddMv());
}

#if RAILY_VDD_BENCH
// The VDD discharge bench (vdd_log.h, battery-telemetry-design.md
// «Expansion Board kit»): a record a minute in InternalFS while serial `V1`
// is set, a steady LED load while `L1` is set; both survive a reboot.
// Loop task only. The files live in their own directory, so the
// per-minute directory commit stays off the root's metadata.
static const char VDD_DIR[] = "/vddlog";
static const char VDD_CONFIG_PATH[] = "/vddlog/cfg.bin";
static const char* VDD_SEGMENT_PATHS[2] = {"/vddlog/a.bin", "/vddlog/b.bin"};
static VddConfig vddConfig = {false, false, 0};
static uint16_t vddBoot = 1;           // this boot's number: the last logged boot + 1
static uint8_t vddPendingFlags = 0;    // VDD_FLAG_BOOT / VDD_FLAG_START for the next record
static uint32_t vddLogNextMs = 0;      // the next record (idleSleepMs wakes for it)
static uint32_t vddActiveRecords = 0;  // records in the current segment
static uint32_t vddSkipped = 0;        // records not written since boot (floor, space, flash)

static bool writeVddConfig() {
  if (!InternalFS.exists(VDD_DIR)) InternalFS.mkdir(VDD_DIR);
  uint8_t rec[VDD_CONFIG_LENGTH];
  vddConfigEncode(vddConfig, rec);
  File file(InternalFS);
  if (!file.open(VDD_CONFIG_PATH, FILE_O_WRITE)) return false;
  file.seek(0);  // FILE_O_WRITE seeks to the end
  bool ok = file.write(rec, sizeof(rec)) == sizeof(rec) && file.truncate(sizeof(rec));
  file.close();
  return ok;
}

// Whole records in a segment file (a missing file has none).
static uint32_t vddSegmentRecords(uint8_t segment) {
  File file(InternalFS);
  if (!file.open(VDD_SEGMENT_PATHS[segment & 1], FILE_O_READ)) return 0;
  const uint32_t records = file.size() / VDD_RECORD_LENGTH;
  file.close();
  return records;
}

// Free InternalFS blocks, counted under the filesystem's own lock (the BLE
// task writes bond files through the same littlefs). The callback touches
// nothing but the counter.
static int countVddBlock(void* data, lfs_block_t block) {
  (void)block;
  (*(uint32_t*)data)++;
  return 0;
}
static uint32_t vddFreeBlocks() {
  uint32_t used = 0;
  InternalFS._lockFS();
  const int err = lfs_traverse(InternalFS._getFS(), countVddBlock, &used);
  InternalFS._unlockFS();
  if (err < 0 || used >= VDD_LOG_FS_BLOCKS) return 0;
  return VDD_LOG_FS_BLOCKS - used;
}

// Calls visit(record) for every record, older segment first. Reads 8
// records at a time with the file closed in between, so nothing stays open
// while slow USB output prints.
// A plain function pointer with header types only: the Arduino prototype
// pass puts declarations above the sketch's own typedefs and templates.
static __attribute__((noinline)) void forEachVddRecord(void (*visit)(const VddRecord& record)) {
  static uint8_t chunk[8 * VDD_RECORD_LENGTH];
  for (int n = 0; n < 2; n++) {
    const uint8_t segment = (uint8_t)((vddConfig.active + 1 + n) & 1);
    uint32_t offset = 0;
    for (;;) {
      File file(InternalFS);
      if (!file.open(VDD_SEGMENT_PATHS[segment], FILE_O_READ)) break;
      size_t got = 0;
      if (file.seek(offset)) got = file.read(chunk, sizeof(chunk));
      file.close();
      const size_t whole = got / VDD_RECORD_LENGTH;
      for (size_t i = 0; i < whole; i++) {
        VddRecord rec;
        if (vddRecordDecode(chunk + i * VDD_RECORD_LENGTH, &rec)) visit(rec);
      }
      if (whole < 8) break;
      offset += sizeof(chunk);
    }
  }
}

// setup(): read-only. The settings, the current segment's length and this
// boot's number (the last boot's + 1). Nothing is written at boot: a cell
// browning out in a boot loop must not write flash on every boot. The boot
// record is the first one, a minute in.
static uint16_t vddLastBoot = 0;
static void noteVddBoot(const VddRecord& r) {
  if (r.boot > vddLastBoot) vddLastBoot = r.boot;
}
static __attribute__((noinline)) void restoreVddBench() {
  uint8_t rec[VDD_CONFIG_LENGTH + 1];
  size_t length = 0;
  File file(InternalFS);
  if (file.open(VDD_CONFIG_PATH, FILE_O_READ)) {
    length = file.read(rec, sizeof(rec));
    file.close();
  }
  vddConfig = vddConfigDecode(rec, length);
  vddActiveRecords = vddSegmentRecords(vddConfig.active);
  vddLastBoot = 0;
  forEachVddRecord(noteVddBoot);
  vddBoot = (uint16_t)(vddLastBoot + 1);
  vddPendingFlags = VDD_FLAG_BOOT;
  vddLogNextMs = millis() + VDD_LOG_PERIOD_MS;
  Serial.printf("vddlog: %s, load %s, boot %u, %lu records in segment %u\n", vddConfig.log ? "on" : "off",
                vddConfig.load ? "on" : "off", (unsigned)vddBoot, (unsigned long)vddActiveRecords,
                (unsigned)vddConfig.active);
}

// One append, after the floor check: drops the older segment when the
// current one is full or space is low, skips when even that cannot help.
static __attribute__((noinline)) bool appendVddRecord(const VddRecord& rec) {
  const uint8_t older = (uint8_t)(vddConfig.active ^ 1);
  const VddAppendPlan plan =
      vddPlanAppend(vddActiveRecords, InternalFS.exists(VDD_SEGMENT_PATHS[older]), vddFreeBlocks());
  if (plan == VDD_SKIP_NO_SPACE) {
    Serial.println("vddlog: filesystem too full, record skipped");
    return false;
  }
  if (plan == VDD_SWITCH) {
    // Safe across a power cut: until the settings name the new segment, the
    // next append sees the full one and switches again. A failed settings
    // write leaves that file unchanged, so RAM must keep naming it too.
    const uint8_t previous = vddConfig.active;
    InternalFS.remove(VDD_SEGMENT_PATHS[older]);
    vddConfig.active = older;
    if (!writeVddConfig()) {
      vddConfig.active = previous;
      Serial.println("vddlog: settings write failed, record skipped");
      return false;
    }
    vddActiveRecords = 0;
  }
  if (!InternalFS.exists(VDD_DIR)) InternalFS.mkdir(VDD_DIR);
  uint8_t bytes[VDD_RECORD_LENGTH];
  vddRecordEncode(rec, bytes);
  File file(InternalFS);
  if (!file.open(VDD_SEGMENT_PATHS[vddConfig.active], FILE_O_WRITE)) return false;  // opens at the end
  const bool ok = file.write(bytes, sizeof(bytes)) == sizeof(bytes);
  file.close();
  if (ok) vddActiveRecords++;
  return ok;
}

// From loop(): a record a minute while logging. Every path moves the
// deadline on first, so the sleep build never spins on a past deadline.
static __attribute__((noinline)) void serviceVddLog() {
  if (!vddConfig.log) return;
  const uint32_t now = millis();
  if ((int32_t)(now - vddLogNextMs) < 0) return;
  vddLogNextMs = now + VDD_LOG_PERIOD_MS;
  VddRecord rec;
  rec.uptimeS = now / 1000;
  rec.vddMv = (uint16_t)readVddMv();
  rec.p031Mv = (uint16_t)readVbatMv();
  rec.boot = vddBoot;
  rec.flags = (uint8_t)(vddPendingFlags | ((readPowerByte() & BATTERY_POWER_USB) ? VDD_FLAG_USB : 0) |
                        (vddConfig.load ? VDD_FLAG_LOAD : 0));
  rec.reset = vddPackResetReason(bootResetReason);
  if (rec.vddMv < VDD_LOG_FLOOR_MV) {
    vddSkipped++;
    Serial.printf("vddlog: VDD %u mV under the %lu mV floor, not written\n", (unsigned)rec.vddMv,
                  (unsigned long)VDD_LOG_FLOOR_MV);
    return;
  }
  if (appendVddRecord(rec)) {
    vddPendingFlags = 0;
  } else {
    vddSkipped++;
  }
}

// Serial `v`: the log as CSV, older records first. A boot, a log-on and a
// change of the load are `#` comment lines before their row.
static int vddDumpLastLoad = -1;
static void printVddRow(const VddRecord& r) {
  if (r.flags & VDD_FLAG_BOOT) Serial.printf("# boot %u reset 0x%02x\n", (unsigned)r.boot, (unsigned)r.reset);
  if (r.flags & VDD_FLAG_START) Serial.printf("# log on (boot %u)\n", (unsigned)r.boot);
  const int load = (r.flags & VDD_FLAG_LOAD) ? 1 : 0;
  if (load != vddDumpLastLoad) Serial.printf("# load %d\n", load);
  vddDumpLastLoad = load;
  Serial.printf("%lu,%u,%u,%u\n", (unsigned long)r.uptimeS, (unsigned)r.vddMv, (unsigned)r.p031Mv,
                (r.flags & VDD_FLAG_USB) ? 1u : 0u);
}
static __attribute__((noinline)) void dumpVddLog() {
  Serial.printf("# vddlog fw=%s boot=%u log=%u load=%u records=%lu+%lu floor_mv=%lu skipped=%lu\n", FW_VERSION,
                (unsigned)vddBoot, vddConfig.log ? 1u : 0u, vddConfig.load ? 1u : 0u,
                (unsigned long)vddSegmentRecords((uint8_t)(vddConfig.active ^ 1)), (unsigned long)vddActiveRecords,
                (unsigned long)VDD_LOG_FLOOR_MV, (unsigned long)vddSkipped);
  Serial.println("t,vdd,p031,usb");
  vddDumpLastLoad = -1;
  forEachVddRecord(printVddRow);
  Serial.println("# end");
}

// Serial `v`, `V1`, `V0`, `Vx`, `L1`, `L0` (vdd_log.h VddSerial). Returns
// whether c was part of one of them.
static __attribute__((noinline)) bool vddSerialChar(char c) {
  static VddSerial parser = {};
  VddCommand command;
  const bool consumed = parser.feed(c, &command);
  switch (command) {
    case VDD_CMD_DUMP:
      dumpVddLog();
      break;
    case VDD_CMD_LOG_ON:
      vddConfig.log = true;
      vddPendingFlags |= VDD_FLAG_START;
      vddLogNextMs = millis() + VDD_LOG_PERIOD_MS;
      Serial.printf("vddlog: on%s\n", writeVddConfig() ? "" : " (settings write failed: off after a reboot)");
      break;
    case VDD_CMD_LOG_OFF:
      vddConfig.log = false;
      Serial.printf("vddlog: off%s\n", writeVddConfig() ? "" : " (settings write failed)");
      break;
    case VDD_CMD_ERASE:
      InternalFS.remove(VDD_SEGMENT_PATHS[0]);
      InternalFS.remove(VDD_SEGMENT_PATHS[1]);
      vddConfig.active = 0;
      vddActiveRecords = 0;
      Serial.printf("vddlog: erased%s\n", writeVddConfig() ? "" : " (settings write failed)");
      break;
    case VDD_CMD_LOAD_ON:
    case VDD_CMD_LOAD_OFF:
      vddConfig.load = command == VDD_CMD_LOAD_ON;
      Serial.printf("vddlog: LED load %s%s\n", vddConfig.load ? "on (red, green and blue steady)" : "off",
                    writeVddConfig() ? "" : " (settings write failed)");
      break;
    default:
      break;
  }
  return consumed;
}
#endif  // RAILY_VDD_BENCH

// Reads the bootloader's image once (bootloader_kind.h). Before
// Bluefruit.begin(): with the SoftDevice off nothing here is restricted, and
// it is plain memory reads. An ACL region that forbids reading any of it
// would fault the scan, so it is checked first and the kind stays unknown
// (no bootloader of ours sets one; the bench read printed 8 empty ACLs).
static __attribute__((noinline)) BootloaderKind detectBootloader() {
  const uint32_t start = bootloaderStart(NRF_UICR->NRFFW[0]);
  if (start == 0) return BOOTLOADER_UNKNOWN;
  for (uint32_t i = 0; i < 8; i++) {
    const uint32_t addr = NRF_ACL->ACL[i].ADDR, size = NRF_ACL->ACL[i].SIZE;
    const bool readBlocked = (NRF_ACL->ACL[i].PERM & ACL_ACL_PERM_READ_Msk) != 0;
    if (size != 0 && readBlocked && addr < BOOTLOADER_FLASH_END && addr + size > start) return BOOTLOADER_UNKNOWN;
  }
  return classifyBootloader((const uint8_t*)start, BOOTLOADER_FLASH_END - start);
}

void setup() {
#if RAILY_IDLE_SLEEP
  loopTask = xTaskGetCurrentTaskHandle();  // before any BLE callback can wake it
#endif
  // Every build: P0.14 (VBAT_ENABLE) LOW for good, never the variant's HIGH,
  // which exposes P0.31 to the cell (battery-telemetry-design.md «Traps» 1;
  // the divider then costs ~2.8 µA). Never toggled, never an input.
  // initVariant() has already driven it HIGH before setup(): the level goes
  // LOW first, then the direction is (re)stated.
  digitalWrite(VBAT_ENABLE, LOW);
  pinMode(VBAT_ENABLE, OUTPUT);
#if RAILY_BAT_ON_XIAO
  pinMode(BATTERY_CHG_PIN, INPUT_PULLUP);  // ~CHG is open drain
#endif
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(LED_BLUE, OUTPUT);
  ledsOff();
#if RAILY_SHIP_MODE
  shipCheckWakeAtBoot();  // a short press from System OFF ends here, back off
#endif
  Serial.begin(115200);
  uint32_t serialWait = millis();
#if RAILY_SHIP_MODE
  // On the cell nobody can open the port: no 4 s wait after a 3 s hold.
  const bool waitForSerial = vbusPresentRaw();
#else
  const bool waitForSerial = true;
#endif
  while (waitForSerial && !Serial && millis() - serialWait < 4000) {
    delay(10);
  }

  snprintf(
      deviceId,
      sizeof(deviceId),
      "rp1-%08lx%08lx",
      (unsigned long)NRF_FICR->DEVICEID[0],
      (unsigned long)NRF_FICR->DEVICEID[1]);
  bootloaderKind = detectBootloader();
  buildDeviceInfoJson(deviceInfoJson, sizeof(deviceInfoJson), false);

  InternalFS.begin();
  restoreCounter();
  emitMutex = xSemaphoreCreateMutex();
  passMutex = xSemaphoreCreateMutex();
  sealDone = xSemaphoreCreateBinary();
  if (xTaskCreate(sealWorker, "seal", SEAL_TASK_STACK_WORDS, NULL, TASK_PRIO_LOW, &sealTask) != pdPASS) {
    sealTask = NULL;  // resealSecret() then publishes no seal; passes stay off
    Serial.println("seal task: not created - no sealed secret");
  }

#if RAILY_VOICE
  // Voice (voice-from-pin.md §4): ATT MTU 247 (one 168-byte AUDIO frame per
  // notification), a long connection event (100 x 1.25 ms) and a notify
  // queue of VOICE_HVN_QUEUE, so a connection event carries several frames
  // and the 2 s pre-roll catches up within about a second. BANDWIDTH_MAX
  // with a deeper queue; it raises the SoftDevice's RAM, begin() says if it
  // fits.
  Bluefruit.configPrphConn(247, 100, VOICE_HVN_QUEUE, BLE_GATTC_WRITE_CMD_TX_QUEUE_SIZE_DEFAULT);
#endif
  if (!Bluefruit.begin()) Serial.println("ble: Bluefruit.begin() FAILED (SoftDevice RAM?)");
  // The core's init() (wiring.c) already read POWER->RESETREAS for this
  // boot and cleared it, before setup() and before the SoftDevice: the
  // register itself reads 0 by now, so only readResetReason() is valid.
  bootResetReason = readResetReason();
  Bluefruit.setTxPower(4);
  Bluefruit.setName(DEVICE_NAME);
  Bluefruit.Periph.setConnectCallback(onConnect);
  Bluefruit.Periph.setDisconnectCallback(onDisconnect);
  // LESC Just Works (ble-bonding.md D-1): Bluefruit's defaults, stated here.
  // The admission, not the pairing, is the authentication.
  Bluefruit.Security.setIOCaps(false, false, false);
  Bluefruit.Security.setMITM(false);
  Bluefruit.setEventCallback(onBleEvent);
#if RAILY_IDLE_SLEEP
  // Bluefruit's own blue blink while advertising: a timer wake every 500 ms
  // and a lit LED. The Air LED is quiet (quietLights).
  Bluefruit.autoConnLed(false);
#endif
  if (sd_ble_gatts_initial_user_handle_get(&serviceChangedStartHandle) != NRF_SUCCESS) serviceChangedStartHandle = 0;
  serviceChangedCccdHandle = findServiceChangedCccd(serviceChangedStartHandle);
  Serial.printf("bond: service changed range 0x%04x-0xffff, cccd 0x%04x\n", (unsigned)serviceChangedStartHandle,
                (unsigned)serviceChangedCccdHandle);

  bledis.setManufacturer("Raily");
  bledis.setModel("Pin P1");
  bledis.setFirmwareRev(FW_VERSION);
  bledis.begin();

  // After Bluefruit.begin(): the secret needs the SoftDevice RNG.
  restoreSecret();
  restoreMascot();
  restoreBondTable();
  enforceBondCap();  // counts the bond files for link_security and serial i
  Serial.printf("reset reason 0x%08lx\n", (unsigned long)bootResetReason);
  if (bindWindowOpensAtBoot(bootResetReason, freshSecretThisBoot)) {
    openBindWindow(freshSecretThisBoot ? "new secret" : "power-on or RESET");
  } else {
    Serial.println("bind window: closed (software reset)");
  }
#if RAILY_SHIP_MODE
  if (shipWokeByHold) {
    // Woken from System OFF by a 3 s hold: the window, ▬, «set me up». The
    // press already counted: the debounce starts as an ongoing press that
    // began before the app, so its release (even one before the first
    // loop() sample) is a release event, which is silent and clears the
    // hold for the next press, and no voice hold begins for it.
    shipOpenWindow();
    shipHold.fired = true;
    buttonDebounce.pressed = true;
    buttonDebounce.candidate = true;
    buttonDebounce.seen = true;
    buttonDebounce.pressedAtMs = 0;
    buttonDebounce.candidateAtMs = 0;
    buttonDebounce.lastSampleMs = 0;
  }
#endif
  // device_info carries bind_s: built once the window is known.
  buildDeviceInfoJson(deviceInfoJson, sizeof(deviceInfoJson), false);
#if RAILY_VOICE
  // Before the service: the voice characteristics exist only on a Sense
  // board (voice-from-pin.md §9).
  voiceCapable = probeSenseBoard();
  printVoiceBoot();
#endif

  railyService.begin();

  deviceInfoChr.setProperties(CHR_PROPS_READ);
  deviceInfoChr.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  deviceInfoChr.setMaxLen(sizeof(deviceInfoJson) - 1);
  deviceInfoChr.begin();
  deviceInfoChr.write(deviceInfoJson, strlen(deviceInfoJson));

  // Open permission on purpose (ble-bonding.md §9.1): an encrypted CCCD
  // would make iOS show «Pair» on any subscribe. The trust rule gates the
  // notify (emitButtonEvent) and the read (onButtonEventReadAuthorize).
  // No stored value on purpose: every read is answered by that callback.
  buttonEventChr.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
  buttonEventChr.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  buttonEventChr.setFixedLen(sizeof(ButtonEventPayload));
  // Before begin(), like every authorize callback (check_ble_setup.py).
  buttonEventChr.setReadAuthorizeCallback(onButtonEventReadAuthorize, false);
  buttonEventChr.begin();

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
  // Before begin(): Bluefruit hands rd_auth/wr_auth to the SoftDevice
  // inside begin(); set later, the callback is never called (P4 bench).
  deviceControlChr.setWriteAuthorizeCallback(onDeviceControlAuthorize, false);
  deviceControlChr.begin();

  passChallengeChr.setProperties(CHR_PROPS_READ);
  passChallengeChr.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  passChallengeChr.setFixedLen(PASS_CHALLENGE_LENGTH);
  passChallengeChr.setReadAuthorizeCallback(onPassChallengeAuthorize, false);
  passChallengeChr.begin();

  // Write-with-response: the verdict is the ATT response (server_pass.h).
  passChr.setProperties(CHR_PROPS_WRITE);
  passChr.setPermission(SECMODE_OPEN, SECMODE_OPEN);
  passChr.setMaxLen(32);
  passChr.setWriteAuthorizeCallback(onPassAuthorize, false);
  passChr.begin();

  // About 120 bytes: phones read it with a long read at the default MTU.
  deviceSecretChr.setProperties(CHR_PROPS_READ);
  deviceSecretChr.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  deviceSecretChr.setMaxLen(SEAL_MAX_BLOB);
  deviceSecretChr.begin();
  resealSecret();

  // screen_state: write with response, 9 bytes, open before bonding (the
  // pin strips plaintext writes itself). The verdict is the ATT answer.
  screenStateChr.setProperties(CHR_PROPS_WRITE);
  screenStateChr.setPermission(SECMODE_NO_ACCESS, SECMODE_OPEN);
  screenStateChr.setFixedLen(SCREEN_PAYLOAD_LENGTH);
  // Before begin(), like every authorize callback (check_ble_setup.py).
  screenStateChr.setWriteAuthorizeCallback(onScreenStateAuthorize, false);
  screenStateChr.begin();

  // link_security (ble-bonding.md D-3): an encrypted read. On an open link
  // the SoftDevice refuses it (Insufficient Authentication) and iOS pairs;
  // the value is built at read time.
  linkSecurityChr.setProperties(CHR_PROPS_READ);
  linkSecurityChr.setPermission(SECMODE_ENC_NO_MITM, SECMODE_NO_ACCESS);
  linkSecurityChr.setFixedLen(LINK_SECURITY_LENGTH);
  // Before begin(), like every authorize callback (check_ble_setup.py).
  linkSecurityChr.setReadAuthorizeCallback(onLinkSecurityAuthorize, false);
  linkSecurityChr.begin();

  // Power state (battery-telemetry-design.md): last in the Raily service, so
  // every earlier handle stays where it was; open read, no write.
  powerStateChr.setProperties(CHR_PROPS_READ | CHR_PROPS_NOTIFY);
  powerStateChr.setPermission(SECMODE_OPEN, SECMODE_NO_ACCESS);
  powerStateChr.setFixedLen(1);
  powerStateChr.begin();
#if RAILY_VOICE
  if (voiceCapable) {
    // After power_state, so every 0.3.0 handle stays where it was. Encrypted
    // at the ATT level (the CCCD takes the read permission, Bluefruit
    // BLECharacteristic::begin): an open link cannot even subscribe. The
    // pin streams only to a trusted link that subscribed (voiceLinkNow).
    voiceStreamChr.setProperties(CHR_PROPS_NOTIFY);
    voiceStreamChr.setPermission(SECMODE_ENC_NO_MITM, SECMODE_NO_ACCESS);
    voiceStreamChr.setMaxLen(VOICE_AUDIO_BYTES);
    voiceStreamChr.begin();
    // Write with response: the verdict is the ATT answer. Before begin(),
    // like every authorize callback (check_ble_setup.py).
    voiceControlChr.setProperties(CHR_PROPS_WRITE);
    voiceControlChr.setPermission(SECMODE_NO_ACCESS, SECMODE_ENC_NO_MITM);
    voiceControlChr.setMaxLen(2);
    voiceControlChr.setWriteAuthorizeCallback(onVoiceControlAuthorize, false);
    voiceControlChr.begin();
  }
#endif
#if RAILY_BAT_ON_XIAO
  // The Battery Service after the Raily characteristics (a characteristic
  // joins the service begun last).
  blebas.begin();
#endif
  // The first reading before advertising: a phone never reads the
  // zero-initialised 0 % (an update gate would refuse it).
  sampleBattery(millis(), readPowerByte());
  batteryPollMs = millis();
  Serial.printf("battery: %lu mV, %u%%, usb %u, chg %u, vdd %lu mV%s\n", (unsigned long)batteryMv,
                (unsigned)batteryPct, (batteryPower & BATTERY_POWER_USB) ? 1u : 0u,
                (batteryPower & BATTERY_POWER_CHARGING) ? 1u : 0u, (unsigned long)readVddMv(),
                RAILY_BAT_ON_XIAO ? "" : " (no 0x180F: the cell is not on the XIAO)");
#if RAILY_VDD_BENCH
  restoreVddBench();
#endif

  startAdv();
  startMotorPin();
#if RAILY_BUTTON
  startButton();  // after advertising, like the motor pin (the pre-flash gate)
#endif
  Serial.println(deviceInfoJson);
  Serial.println("Type p + Enter to simulate a button press, i + Enter to reprint device info, s + Enter to print the sealed secret, w + Enter to open the bind window, r + Enter to reset, o<scene>[shape[material[locale]]] + Enter to show a screen scene, of + Enter to drop the pin (the fall), c<hex byte> + Enter to play an event_ack byte (motor rhythm), x + Enter to erase every BLE bond");
#if RAILY_VDD_BENCH
  Serial.println("VDD bench: v to dump the log, V1/V0 to start/stop it, Vx to erase it, L1/L0 for the LED load");
#endif
#if RAILY_SHIP_MODE
  Serial.println("Ship mode (Air): [ / ] press / release the button (bench); hold 3 s to wake or open the bind window");
#endif
#if RAILY_VOICE_BENCH
  Serial.println("Voice bench: v / V to begin / end a fake hold (frames as VF lines), m for raw PCM (VP lines) "
                 "on/off, G<hh> for the PDM gain (00-50), d for the mic power drive, A for the GATT table");
#endif
  printStackMarks("boot");
  Serial.println("ready");
}

// Bond files above the cap go (ble-bonding.md D-5, selectBondEvictions);
// also counts them for link_security and serial i. Loop task only. The file
// list is static: loop() owns it.
static const size_t BOND_SCAN_MAX = 24;
static __attribute__((noinline)) void enforceBondCap() {
  static uint8_t files[BOND_SCAN_MAX][BOND_ADDR_LENGTH];
  static size_t evict[BOND_SCAN_MAX];
  static BondTable snapshot;
  static char path[48];
  // The current link's bond is never removed.
  uint8_t keep[BOND_ADDR_LENGTH] = {0};
  const LinkSecurity link = currentLink(NULL);
  if (link.bonded) memcpy(keep, link.addr, sizeof(keep));
  size_t total = 0;
  size_t removedAll = 0;
  // Each pass lists every bond file (keeping the first BOND_SCAN_MAX
  // addresses) and removes enough of the listed ones to bring the total to
  // the cap; more than BOND_SCAN_MAX files take a few passes.
  for (int pass = 0; pass < 8; pass++) {
    size_t listed = 0;
    total = 0;
    File dir(BOND_DIR_PRPH, FILE_O_READ, InternalFS);
    if (dir) {
      File entry(InternalFS);
      while ((entry = dir.openNextFile(FILE_O_READ))) {
        if (!entry.isDirectory()) {
          total++;
          if (listed < BOND_SCAN_MAX && parseBondFileName(entry.name(), files[listed])) listed++;
        }
        entry.close();
      }
      dir.close();
    }
    if (total <= BOND_TABLE_MAX) break;
    const size_t excess = total - BOND_TABLE_MAX;
    const size_t keepListed = listed > excess ? listed - excess : 0;
    xSemaphoreTake(passMutex, portMAX_DELAY);
    snapshot = bondTable;
    xSemaphoreGive(passMutex);
    const size_t n = selectBondEvictions(files, listed, snapshot, link.bonded ? keep : NULL, keepListed, evict,
                                         BOND_SCAN_MAX);
    size_t removed = 0;
    for (size_t i = 0; i < n; i++) {
      const uint8_t* a = files[evict[i]];
      bondFilePath(path, sizeof(path), a);
      if (InternalFS.remove(path)) removed++;
      xSemaphoreTake(passMutex, portMAX_DELAY);
      if (bondTable.revoke(a)) bondTableDirty = true;  // an admitted phone evicted with its bond
      xSemaphoreGive(passMutex);
    }
    removedAll += removed;
    total -= removed;
    if (removed == 0) break;  // nothing more can go (only the kept bond, or a flash error)
  }
  bondFileCount.store((uint8_t)(total > 255 ? 255 : total), std::memory_order_relaxed);
  if (removedAll > 0) Serial.printf("bond: %u evicted, %u bonds left\n", (unsigned)removedAll, (unsigned)total);
}

// Service Changed after every boot (ble-bonding.md D-7): once per admitted
// phone per boot, recorded in RAM on the phone's confirmation.
static ServiceChangedBoot serviceChangedBoot = {};  // under passMutex, like bondTable
static bool serviceChangedSent = false;
static uint16_t serviceChangedHandle = BLE_CONN_HANDLE_INVALID;
static uint8_t serviceChangedAddr[BOND_ADDR_LENGTH];

static __attribute__((noinline)) void sendServiceChangedIfDue() {
  uint16_t conn = BLE_CONN_HANDLE_INVALID;
  const LinkSecurity link = currentLink(&conn);
  if (conn == BLE_CONN_HANDLE_INVALID) return;
  if (!linkReadyForAdmission(link.level, link.keySize, link.bonded)) return;
  xSemaphoreTake(passMutex, portMAX_DELAY);
  const bool due = serviceChangedBoot.due(bondTable, link.addr);
  xSemaphoreGive(passMutex);
  if (!due) return;
  if (serviceChangedStartHandle == 0) return;  // never 0x0001: the SoftDevice's own GAP service
  // The application's whole range, as Nordic's peer manager sends it
  // (gscm_service_changed_ind_send, SDK 17.1): first user handle to 0xFFFF.
  const uint32_t err = sd_ble_gatts_service_changed(conn, serviceChangedStartHandle, 0xFFFF);
  if (err == NRF_SUCCESS) {
    memcpy(serviceChangedAddr, link.addr, sizeof(serviceChangedAddr));
    serviceChangedHandle = conn;
    serviceChangedSent = true;
    Serial.println("bond: service changed sent");
  } else {
    uint16_t cccd = 0xFFFF;
    ble_gatts_value_t value = {sizeof(cccd), 0, (uint8_t*)&cccd};
    const uint32_t readErr = serviceChangedCccdHandle ? sd_ble_gatts_value_get(conn, serviceChangedCccdHandle, &value) : NRF_ERROR_NOT_FOUND;
    // 0x8 with cccd 0: not subscribed yet; the phone's CCCD write tries again.
    Serial.printf("bond: service changed not sent (0x%lx, cccd 0x%04x read 0x%lx), waits for the subscription\n",
                  (unsigned long)err, (unsigned)cccd, (unsigned long)readErr);
  }
}

// One pass of the bond work: Service Changed, the cap, and the table's
// record. Not inlined, so its frame stays off loop()'s budget.
static __attribute__((noinline)) void serviceBonds() {
  const uint32_t now = millis();
  // A pending confirmation first, then a new indication, so a confirmation
  // can only ever match the indication sent on its own link.
  const uint16_t confirmedOn = serviceChangedConfirmedHandle.exchange(BLE_CONN_HANDLE_INVALID, std::memory_order_relaxed);
  uint16_t linkNow = BLE_CONN_HANDLE_INVALID;
  currentLink(&linkNow);
  // A sent indication belongs to its link: gone with it, it is sent again on
  // the next encrypted link of that phone.
  if (serviceChangedSent && linkNow != serviceChangedHandle) serviceChangedSent = false;
  if (confirmedOn != BLE_CONN_HANDLE_INVALID && serviceChangedSent && confirmedOn == serviceChangedHandle) {
    serviceChangedSent = false;
    xSemaphoreTake(passMutex, portMAX_DELAY);
    serviceChangedBoot.confirm(bondTable, serviceChangedAddr);  // RAM only: the next boot sends it again
    xSemaphoreGive(passMutex);
    Serial.println("bond: service changed confirmed");
  }
  if (bondCccdRepairPending.exchange(false, std::memory_order_relaxed)) {
    const uint16_t log = bondCccdRepairLog.load(std::memory_order_relaxed);
    Serial.printf("bond: cccd field %s (%u bytes late)%s\n", (log & 0x8000u) ? "put back in place" : "dropped",
                  (unsigned)(log & 0xFFu), (log & 0x4000u) ? "" : ", rewrite failed");
  }
  if (serviceChangedCccdPending.exchange(false, std::memory_order_relaxed)) {
    Serial.printf("bond: phone wrote service changed cccd 0x%04x\n",
                  (unsigned)serviceChangedCccdValue.load(std::memory_order_relaxed));
  }
  if (serviceChangedCheckPending.exchange(false, std::memory_order_relaxed)) sendServiceChangedIfDue();
  if (bondCapPending.load(std::memory_order_relaxed) &&
      (int32_t)(now - bondCapDueMs.load(std::memory_order_relaxed)) >= 0) {
    bondCapPending.store(false, std::memory_order_relaxed);
    enforceBondCap();
  }
  // A new admission gets its pairing's key fingerprint before the record is
  // written (at most one file read a second while one is pending: Bluefruit
  // saves the bond file on its own task, normally long before an admission).
  static uint32_t lastPrintTryMs = 0;
  if ((uint32_t)(now - lastPrintTryMs) >= 1000) {
    lastPrintTryMs = now;
    uint8_t addr[BOND_ADDR_LENGTH];
    xSemaphoreTake(passMutex, portMAX_DELAY);
    const int at = bondTable.pendingKeyPrint();
    if (at >= 0) memcpy(addr, bondTable.phones[at].addr, sizeof(addr));
    xSemaphoreGive(passMutex);
    if (at >= 0) {
      const uint32_t print = readBondKeyPrint(addr);
      xSemaphoreTake(passMutex, portMAX_DELAY);
      if (bondTable.setKeyPrint(addr, print)) bondTableDirty = true;
      xSemaphoreGive(passMutex);
    }
  }
  // The admitted table's record; a failed write is retried after 5 s.
  static uint32_t lastWriteFailMs = 0;
  static bool writeFailed = false;
  if (writeFailed && (uint32_t)(now - lastWriteFailMs) < 5000) return;
  static uint8_t rec[BOND_RECORD_LENGTH];
  xSemaphoreTake(passMutex, portMAX_DELAY);
  const bool dirty = bondTableDirty;
  if (dirty) encodeBondTable(bondTable, rec);
  bondTableDirty = false;
  xSemaphoreGive(passMutex);
  if (!dirty) return;
  writeFailed = !writeBondTableFile(rec, sizeof(rec));
  if (writeFailed) {
    lastWriteFailMs = now;
    xSemaphoreTake(passMutex, portMAX_DELAY);
    bondTableDirty = true;
    xSemaphoreGive(passMutex);
    Serial.println("bond table: write failed");
  }
}

// Serial `x`: every bond and admission, keeps the secret, the owned flag
// and the counter (USB is physical access, a bench aid like `w`).
static void eraseBondsFromSerial() {
  xSemaphoreTake(passMutex, portMAX_DELAY);
  clearBondsLocked();
  heldPress.clear();
  xSemaphoreGive(passMutex);
  Serial.println("bond: every bond erased (serial x)");
}

// Serial `i`'s "bond" (ble-bonding.md): bond files, admitted phones, this
// link. Its own frame, so printDeviceInfo stays within its budget.
static __attribute__((noinline)) void printBondBench(char* line, size_t size) {
  const LinkSecurity link = currentLink(NULL);
  xSemaphoreTake(passMutex, portMAX_DELAY);
  const bool trusted = linkTrustedLocked(link);
  const size_t admitted = bondTable.count;
  xSemaphoreGive(passMutex);
  int n = formatBondBenchJson(line, size, bondFileCount.load(std::memory_order_relaxed), admitted,
                              link.level >= LINK_SECURITY_LEVEL_ENCRYPTED, link.level, link.keySize, trusted);
  if (n > 0 && n < (int)size) {
    Serial.print(',');
    Serial.print(line);
  }
  // Stage 2 (ble-bonding.md §9.1): acks and in-app presses ignored because
  // their link was not trusted, since boot.
  n = snprintf(line, size, ",\"untr\":{\"ack\":%lu,\"cmd\":%lu}",
               (unsigned long)untrustedAcks.load(std::memory_order_relaxed),
               (unsigned long)untrustedPressCommands.load(std::memory_order_relaxed));
  if (n > 0 && n < (int)size) Serial.print(line);
}

// Serial `i`'s "haptic" (docs/pins/firmware.md «Vibration motor»): which
// motor the boot probe chose, "da7280" ("da7280-swapped" when the cable is
// reversed), "d0" (the Grove fallback) or "off" (-DRAILY_MOTOR=0), and the
// DA7280 writes that failed since boot. Bench only: the GATT device_info
// value is unchanged.
static __attribute__((noinline)) void printHapticBench() {
#if RAILY_MOTOR
  const HapticBackend backend = hapticMotor.backend();
  Serial.printf(",\"haptic\":\"%s%s\",\"hap_err\":%lu",
                backend == HAPTIC_BACKEND_DA7280_WIRE ? "da7280-wire"
                : backend == HAPTIC_BACKEND_DA7280    ? "da7280"
                : backend == HAPTIC_BACKEND_GPIO      ? "d0"
                                                      : "none",
                hapticMotor.swapped() ? "-swapped" : "", (unsigned long)hapticMotor.errors());
#else
  Serial.print(",\"haptic\":\"off\"");
#endif
}

// Serial `i`: the extended device-info line. Its 256-byte buffer lives in
// this frame, not in loop()'s (stack_budget.json). The line ends with
// "scr" (screen_bench.h): the same buffer is printed twice, the document
// without its closing brace, then `,"scr":…}`, because both do not fit
// 256 bytes at once (the document reaches 243 bytes with "bl":"unknown", "scr" 152 with every
// field at its top: tests/test_screen_bench.cpp). Runs in loop(), which
// owns screenLastApplied.
static __attribute__((noinline)) void printDeviceInfo() {
  char line[256];
  int n = buildDeviceInfoJson(line, sizeof(line), true);
  if (n < 2 || n >= (int)sizeof(line)) return;
  line[n - 1] = '\0';  // the closing brace: "scr" goes before it
  Serial.print(line);
  const ScreenBenchCounts counts = {
      screenWritesAccepted.load(std::memory_order_relaxed),
      screenWritesBadFormat.load(std::memory_order_relaxed),
      screenWritesBadValue.load(std::memory_order_relaxed),
      screenWritesNotOwned.load(std::memory_order_relaxed),
  };
  n = formatScreenBenchJson(line, sizeof(line), screenLastApplied, counts, millis());
  if (n > 0 && n < (int)sizeof(line)) {
    Serial.print(',');
    Serial.print(line);
  }
  printBondBench(line, sizeof(line));
  printHapticBench();
  printBatteryBench();
  Serial.println('}');
}

// Bench only (USB is physical access); the same bytes device_secret serves.
// Runs in loop(), like resealSecret(), so the buffer is never mid-write.
static void printSealedSecret() {
  for (size_t i = 0; i < sealedSecretLength; i++) {
    if (sealedSecret[i] < 0x10) Serial.print('0');
    Serial.print(sealedSecret[i], HEX);
  }
  Serial.println();
}

#if RAILY_OLED
// The panel is probed from loop(), once, this long after the first pass:
// BLE advertises by then, so a fault on the bus can still be seen over USB
// and BLE (the pre-flash gate), and a failed probe leaves it inert.
static const uint32_t OLED_PROBE_AFTER_MS = 500;
static const uint32_t OLED_FRAME_MS = 83;  // at most ~12 fps (spec §3)
#endif

// The mascot record is written from here: on change, from a trusted write,
// at most hourly (mascot_record.h), and only while the pin is owned.
static void persistMascot(uint32_t now, bool owned) {
  uint8_t value = 0;
  if (!owned || !mascotStore.due(now, &value)) return;
  bool ok = writeMascotFile(value);
  mascotStore.written(now, value, ok);
  if (!ok) Serial.println("mascot record: write failed");
}

// One pass of the screen: the same event_ack byte the LED got this pass,
// and the latest accepted screen_state write. The state machine runs in
// both builds; only drawing needs the panel.
static void updateScreen(uint32_t now, uint8_t ack, bool linkUp) {
#if RAILY_OLED
  static bool loopStarted = false;
  static uint32_t loopStartMs = 0;
  static bool probeDone = false;
  if (!loopStarted) {
    loopStarted = true;
    loopStartMs = now;
  }
  if (!probeDone && (uint32_t)(now - loopStartMs) >= OLED_PROBE_AFTER_MS) {
    probeDone = true;
    const bool present = oledProbe(motorHoldsWire());
    oledReported = present ? 1 : 0;  // loop() republishes device_info with it
    Serial.printf("oled: %s\n", present ? "present" : "absent (no pull-ups or no answer at 0x3C)");
  }
#endif
  bool press = pendingScreenPress.exchange(false, std::memory_order_relaxed);
  bool pressFailed = pendingScreenPressFailed.exchange(false, std::memory_order_relaxed);
  bool fall = pendingScreenFall.exchange(false, std::memory_order_relaxed);
  bool foundYou = pendingScreenFoundYou.exchange(false, std::memory_order_relaxed);
  // The owned flag and the mailbox in one section: a release clears the
  // mailbox in the section that drops ownership (replaceSecret).
  ScreenPayload write = {};
  bool hasWrite = false;
  bool trusted = false;
  xSemaphoreTake(passMutex, portMAX_DELAY);
  const bool owned = secretOwned;
  if (screenMailboxFull) {
    write = screenMailbox;
    trusted = screenMailboxTrusted;
    hasWrite = true;
    screenMailboxFull = false;
  }
  xSemaphoreGive(passMutex);
  if (hasWrite && owned) {
    screen.onWrite(now, write, trusted);
    mascotStore.offer(write.mascot, trusted);
    // Bench proof (serial `i` "scr"): the flags as applied, after the
    // plaintext strip; never the counts or the notify kind.
    screenLastApplied.record(write, trusted, now);
    Serial.printf(SCREEN_APPLIED_LINE_FORMAT, (unsigned)write.agent, (unsigned)write.flags,
                  (unsigned)write.mascot, (unsigned)write.locale, trusted ? 1u : 0u);
  }
  // Before tick(): a press in the same pass wins over the fall.
  if (fall) screen.onFall(now);
  ScreenFrame frame = screen.tick(now, press, ack, linkUp, owned, pressFailed, foundYou);
  persistMascot(now, owned);
#if RAILY_OLED
  if (!oledPresent()) return;
  static ScreenFrame drawn = {};
  static bool drawnOnce = false;
  static uint32_t drawnAtMs = 0;
  bool changed = !drawnOnce || !frame.sameImage(drawn) || frame.contrast != drawn.contrast;
#if RAILY_IDLE_SLEEP
  // The panel goes dark at once: the sleep build may not wake again for 1 s
  // (screenBusy() is false once the scene is off).
  const bool throttled = frame.scene != OLED_SCENE_OFF && drawnOnce && (uint32_t)(now - drawnAtMs) < OLED_FRAME_MS;
  if (!changed || throttled) return;
#else
  if (!changed || (drawnOnce && (uint32_t)(now - drawnAtMs) < OLED_FRAME_MS)) return;
#endif
  oledShow(frame);
  drawn = frame;
  drawnOnce = true;
  drawnAtMs = now;
#else
  (void)frame;
#endif
}

// Serial `o<scene>[shape[material[locale]]]` (a bench scene without a
// phone, held 30 s; `om` = modest) and `c<hh>` (one event_ack byte in hex,
// fed to the same path as a phone's write: LED, screen and motor rhythm;
// gestures-spec.md reserves `c`). Parsed by bench_serial.h, one parser for
// both, so neither leaves a pending state behind the other. A character
// that does not fit falls through and is handled as usual: `o` + a
// non-scene prints the scenes, `c` + a non-hex digit prints a hint, and
// `op` / `cp` still press. Returns false when c is not part of a command.
static __attribute__((noinline)) bool benchSerialChar(char c) {
  static BenchSerial bench = {};
  BenchCommand command;
  const bool consumed = bench.feed(c, RAILY_OLED != 0, &command);
  switch (command.action) {
    case BENCH_DEMO:
#if RAILY_OLED
      screen.showDemo(millis(), command.scene, command.shape, command.material, command.locale, command.modest);
      Serial.printf("oled demo: scene %u shape %u material %u locale %u%s%s\n", command.scene, command.shape,
                    command.material, command.locale, command.modest ? " modest" : "",
                    oledPresent() ? "" : " (no panel)");
#endif
      break;
    case BENCH_FALL:
      screenOnFall();  // the real path, as gestures S1's gesture 3 will call it
#if RAILY_OLED
      Serial.printf("oled: fall (bench)%s\n", oledPresent() ? "" : " (no panel)");
#endif
      break;
    case BENCH_DEMO_UNKNOWN:
      Serial.printf("oled demo: scenes are %s, %c = modest, %c = a real fall\n", kOledDemoLetters,
                    OLED_DEMO_MODEST_LETTER, kOledDemoLetters[OLED_SCENE_FELL]);
      break;
    case BENCH_DEMO_UNAVAILABLE:
      Serial.println("oled: not in this build (RAILY_OLED=0)");
      break;
    case BENCH_ACK: {
      const uint8_t ack = command.ack;
      publishEventAck(&ack, 1);
      Serial.printf("event_ack 0x%02x (bench)%s\n", ack, RAILY_MOTOR ? "" : ", no motor in this build");
      break;
    }
    case BENCH_ACK_BAD:
      Serial.println("c: two hex digits, e.g. c11");
      break;
    default:
      break;
  }
  return consumed;
}

// The LED, the screen and the motor. One read of the latest event_ack
// byte feeds all three (the motor takes the raw byte). Not inlined, so its frame stays off loop()'s stack budget.
static __attribute__((noinline)) void updateOutputs() {
  uint32_t now = millis();
  uint8_t ack = pendingFeedback.exchange(0, std::memory_order_relaxed);
  uint8_t rawAck = pendingRhythmAck.exchange(0, std::memory_order_relaxed);
  bool linkUp = connected.load(std::memory_order_relaxed);
  FeedbackLights lights = feedback.tick(now, linkUp, ack);
#if RAILY_IDLE_SLEEP
  lights = quietLights(lights);
#endif
  // The VDD bench's load (serial L1): all three LEDs steady, over the quiet
  // Air LED and the feedback patterns, until L0.
#if RAILY_VDD_BENCH
  if (vddConfig.load) lights = FeedbackLights{true, true, true};
#endif
  // Magenta for exactly as long as the mic is powered (voice-from-pin.md
  // §7), over the quiet Air LED and any pattern.
#if RAILY_VOICE
  if (voiceMicRunning) lights = FeedbackLights{true, false, true};
#endif
  if (lights.red != renderedLights.red) digitalWrite(LED_RED, lights.red ? LOW : HIGH);
  if (lights.green != renderedLights.green) digitalWrite(LED_GREEN, lights.green ? LOW : HIGH);
  if (lights.blue != renderedLights.blue) digitalWrite(LED_BLUE, lights.blue ? LOW : HIGH);
  renderedLights = lights;
  updateScreen(now, ack, linkUp);  // before the motor: it applies quiet hours
  // «Found you» vibrates only when the screen took it (owned, once a
  // minute, not in quiet hours): event_ack is open until bonding. The
  // screen's decision, not the motor mailbox, plays its ▬ ▬, so a later
  // ack in the same pass cannot drop it (it wins over that ack instead).
  if (rawAck == ScreenState::ACK_FOUND_YOU) rawAck = 0;
  if (screen.foundYouNow) rawAck = ScreenState::ACK_FOUND_YOU;
  updateMotor(now, rawAck);
}

#if RAILY_IDLE_SLEEP
// Serial `i`'s second line in a sleep build: time blocked and wakes since
// boot (a duty-cycle bench aid, not a current measurement).
static uint32_t idleBlockedMs = 0;
static uint32_t idleBlocks = 0;
static uint32_t idleNotified = 0;

// How long loop() may block now (idle_sleep.h): 0 while work raised for it
// is still waiting, otherwise until its nearest deadline, at most 1 s
// (10 ms on USB). tests/check_idle_wake.py checks this list against the
// sketch's flags and deadlines.
static __attribute__((noinline)) uint32_t idleSleepMs(uint32_t now) {
  IdleWake wake = IdleWake::start(now, TinyUSBDevice.mounted());
  wake.pending(pendingFeedback.load(std::memory_order_relaxed) != 0 ||
               pendingRhythmAck.load(std::memory_order_relaxed) != 0 ||
               pendingScreenPress.load(std::memory_order_relaxed) ||
               pendingScreenPressFailed.load(std::memory_order_relaxed) ||
               pendingScreenFall.load(std::memory_order_relaxed) ||
               pendingScreenFoundYou.load(std::memory_order_relaxed) ||
               pendingPassAction.load(std::memory_order_relaxed) != PASS_ACTION_NONE ||
               linkSetupPending.load(std::memory_order_relaxed) ||
               serviceChangedCheckPending.load(std::memory_order_relaxed) ||
               bondCccdRepairPending.load(std::memory_order_relaxed) ||
               serviceChangedCccdPending.load(std::memory_order_relaxed) ||
               serviceChangedConfirmedHandle.load(std::memory_order_relaxed) != BLE_CONN_HANDLE_INVALID);
  xSemaphoreTake(passMutex, portMAX_DELAY);
  const bool mailbox = screenMailboxFull;
  const bool held = heldPress.held;
  const bool spare = spareNonceReady;
  const bool serving = bindWindowServes(hasSecret, secretOwned, bindWindow.isOpen(now));
  const uint32_t windowLeftMs = bindWindow.remainingMs(now);
#if RAILY_SHIP_MODE
  const bool ownedNow = secretOwned;
#endif
  xSemaphoreGive(passMutex);
  // bondTableDirty is not a reason: a BLE-side change wakes loop(), a
  // loop-side one is written in the same pass, and after a failed write the
  // 5 s retry rides the backstop (a pending() here would spin all 5 s).
  wake.pending(mailbox);
  // A held press waits for its link (the CCCD write does not wake loop()),
  // a spent nonce for the RNG: short ticks until they are done.
  wake.tick(held || !spare);
  wake.due(rebootArmed || enterDfuArmed, rebootAtMs);  // the 300 ms drain
  wake.due(dropLinkArmed, dropLinkAtMs);
  wake.due(bondCapPending.load(std::memory_order_relaxed), bondCapDueMs.load(std::memory_order_relaxed));
  wake.due(true, batteryNextMs);  // the 60 s reading (a power flip rides the 1 s backstop)
#if RAILY_VDD_BENCH
  wake.due(vddConfig.log, vddLogNextMs);  // the VDD bench's record a minute
#endif
  if (serving) wake.cap(windowLeftMs);  // the window closes: device_secret goes empty
  wake.tick(feedbackBusy(feedback));
#if RAILY_OLED
  // An awake screen redraws at most every OLED_FRAME_MS; with no panel its
  // timers run on timestamps and the backstop catches them up.
  if (screenBusy(screen) && oledPresent()) wake.cap(OLED_FRAME_MS);
#endif
#if RAILY_MOTOR
  wake.tick(rhythmBusy(rhythm));
  wake.due(motorRetry, motorRetryAtMs);  // a failed DA7280 write, written again
#endif
#if RAILY_BUTTON
  wake.pending(!buttonEdges.empty());
  wake.due(buttonDebounce.unsettled(), buttonDebounce.settleAtMs());
#endif
#if RAILY_SHIP_MODE
  // The 3 s hold's mark (ship_mode.h). The radio follows the window, whose
  // close is a deadline above (serving), and a link change wakes loop().
  wake.due(shipHold.armed(buttonDebounce.pressed, ownedNow), ShipHold::dueAtMs(buttonDebounce.pressedAtMs));
#endif
#if RAILY_VOICE
  // A phone STOP, mic frames waiting, the gesture's next deadline (the talk
  // threshold, the 60 s cap). While the mic runs or frames are queued,
  // 20 ms steps: the PDM interrupt and the BLE task's TX credits wake it
  // too, the steps are the backstop.
  wake.pending(pendingVoiceStop.load(std::memory_order_relaxed) != 0 || !voicePcm.empty());
  const uint32_t voiceLeftMs = voiceHold.nextDeadlineMs(now);
  wake.due(voiceLeftMs != VOICE_NO_DEADLINE, now + voiceLeftMs);
  wake.tick(voiceMicRunning || voiceUplinkBusy());
#endif
  // Everything else loop() does on a timer (bind_s each second, the 5 s
  // secret retry and a failed bond record's 5 s retry, the 1 s bond key
  // print, the hourly mascot record, the panel probe, the battery power-byte
  // poll) rides the 1 s backstop.
  return wake.ms;
}

// The end of every loop() pass in a sleep build: block until a notify or
// the nearest deadline. The idle task then sleeps the CPU (tickless idle,
// sd_app_evt_wait); the SoftDevice keeps the link.
static __attribute__((noinline)) void idleBlock() {
  const uint32_t start = millis();
  const uint32_t ms = idleSleepMs(start);
  if (ms == 0) return;
  const bool notified = ulTaskNotifyTake(pdTRUE, ms2tick(ms)) != 0;
  idleBlockedMs += millis() - start;
  idleBlocks++;
  if (notified) idleNotified++;
}

static void printIdleBench() {
  Serial.printf("idle: {\"up_ms\":%lu,\"blocked_ms\":%lu,\"blocks\":%lu,\"notified\":%lu,\"btn_dropped\":%lu}\n",
                (unsigned long)millis(), (unsigned long)idleBlockedMs, (unsigned long)idleBlocks,
                (unsigned long)idleNotified,
#if RAILY_BUTTON
                (unsigned long)buttonEdges.dropped
#else
                0ul
#endif
  );
}
#endif

#if RAILY_SHIP_MODE
// One pass of the ship radio rule (ship_mode.h): advertising off while the
// pin ships, back on when it wakes (the 3 s hold, serial `w`, a boot that
// opens the window). A link is never dropped. Loop task only.
static int8_t shipLogged = -1;
static __attribute__((noinline)) void serviceShipRadio() {
  const bool shipping = pinShippingNow();
  if ((int8_t)shipping != shipLogged) {
    shipLogged = (int8_t)shipping;
    Serial.println(shipping ? "ship: asleep (unowned, bind window closed): radio off, hold the button 3 s to wake"
                            : "ship: awake");
  }
  switch (shipRadio(shipping, Bluefruit.Advertising.isRunning(), connected.load(std::memory_order_relaxed))) {
    case SHIP_RADIO_START:
      Bluefruit.Advertising.start(0);
      break;
    case SHIP_RADIO_STOP:
      Bluefruit.Advertising.stop();
      break;
    default:
      break;
  }
}

// System OFF once the pin ships on the cell and everything is quiet
// (ship_mode.h shipPowerOffReady). Runs after the outputs pass: a screen that
// went dark this pass has had its panel put to sleep.
static __attribute__((noinline)) void servicePowerOff() {
  ShipOffInputs in = {};
  in.shipping = pinShippingNow();
  if (!in.shipping) return;
  in.vbus = (readPowerByte() & BATTERY_POWER_USB) != 0;
  in.linkUp = connected.load(std::memory_order_relaxed);
  in.buttonIdle = !buttonDebounce.pressed && !buttonDebounce.unsettled() && buttonEdges.empty() &&
                  !benchButtonDown && digitalRead(BUTTON_PIN) == HIGH;
  in.outputsIdle = !screenBusy(screen) && !feedbackBusy(feedback);
#if RAILY_OLED
  // The panel runs on the cell and keeps its state across an MCU reset: it
  // is put to sleep by its first probe (initDisplay ends dark, pump off),
  // so never power off before that probe ran this boot.
  in.outputsIdle = in.outputsIdle && oledReported != DEVICE_INFO_OLED_UNKNOWN;
#endif
#if RAILY_MOTOR
  in.outputsIdle = in.outputsIdle && !rhythmBusy(rhythm) && !motorOn && !motorRetry;
#endif
#if RAILY_VOICE
  in.outputsIdle = in.outputsIdle && !voiceMicRunning && !voiceHold.held();
#endif
  xSemaphoreTake(passMutex, portMAX_DELAY);
  const bool held = heldPress.held;
  const bool dirty = bondTableDirty;
  // No secret yet (the RNG or flash failed at boot): loop() retries every
  // 5 s, so stay up until it has one.
  const bool secretMissing = !hasSecret;
  xSemaphoreGive(passMutex);
#if RAILY_VDD_BENCH
  // A running discharge log or LED load is an experiment: never cut it.
  in.outputsIdle = in.outputsIdle && !vddConfig.log && !vddConfig.load;
#endif
  in.workPending = held || dirty || secretMissing || rebootArmed || enterDfuArmed || dropLinkArmed ||
                   bondCapPending.load(std::memory_order_relaxed) ||
                   pendingPassAction.load(std::memory_order_relaxed) != PASS_ACTION_NONE;
  if (!shipPowerOffReady(in)) return;
  Serial.println("ship: System OFF (on the cell): hold the button 3 s to wake");
  ledsOff();
  detachInterrupt(BUTTON_PIN);
  systemOffNow(true);
  startButton();  // the SoftDevice refused: stay awake, the button as before
}

// Serial `i`'s ship line (bench).
static void printShipBench() {
  Serial.printf("ship: {\"shipping\":%d,\"adv\":%d,\"hold_ms\":%lu,\"bench_button\":%d}\n", pinShippingNow() ? 1 : 0,
                Bluefruit.Advertising.isRunning() ? 1 : 0, (unsigned long)SHIP_HOLD_MS, benchButtonDown ? 1 : 0);
}
#endif

void loop() {
#if RAILY_IDLE_SLEEP
  // Notifies that arrived before this pass reads its flags (the loop task's
  // own, a burst during the last pass) are answered by this pass, not by
  // an empty one after it.
  ulTaskNotifyTake(pdTRUE, 0);
#endif
  if ((rebootArmed || enterDfuArmed) && (int32_t)(millis() - rebootAtMs) >= 0) {
    Serial.println(enterDfuArmed ? "pass: enter DFU" : "pass: reboot");
    Serial.flush();
    delay(20);
    if (enterDfuArmed) {
      // POWER is restricted while the SoftDevice runs: set GPREGRET through it.
      // Without the flag a reset would just reboot the app, so on failure
      // stay up (error rhythm) and let the app try again.
      if (sd_power_gpregret_clr(0, 0xFF) != NRF_SUCCESS ||
          sd_power_gpregret_set(0, DFU_MAGIC_OTA_RESET_VALUE) != NRF_SUCCESS) {
        enterDfuArmed = false;
        pendingFeedback.store(4, std::memory_order_relaxed);
        Serial.println("pass: DFU flag not set - staying in the app");
        return;
      }
    }
    NVIC_SystemReset();
  }
  runPendingPassAction();
  if (dropLinkArmed && (int32_t)(millis() - dropLinkAtMs) >= 0) {
    dropLinkArmed = false;
    Bluefruit.disconnect(dropLinkHandle);
    Serial.println("bond: bonded link dropped after the bond erase");
  }
  serviceBonds();
#if RAILY_BUTTON
  serviceButton();
#endif
#if RAILY_VOICE
  serviceVoice();
#endif
#if RAILY_SHIP_MODE
  serviceShipRadio();
#endif
  serviceHeldPress();
  serviceBattery();
#if RAILY_VDD_BENCH
  serviceVddLog();
#endif
  // A secret that could not be made at boot (RNG or flash) is retried
  // every 5 s instead of waiting for a reboot.
  static uint32_t lastSecretRetryMs = 0;
  if (!hasSecret && (uint32_t)(millis() - lastSecretRetryMs) >= 5000) {
    lastSecretRetryMs = millis();
    if (replaceSecret()) {
      openBindWindow("new secret");
      resealSecret();
    }
  }
  // Keep one nonce ready for the next pass_challenge read.
  xSemaphoreTake(passMutex, portMAX_DELAY);
  bool needSpare = !spareNonceReady;
  xSemaphoreGive(passMutex);
  if (needSpare) {
    // One non-blocking try per loop pass: the pool refills in the background.
    uint8_t fresh[PASS_NONCE_LENGTH];
    if (tryRandomNow(fresh, sizeof(fresh))) {
      xSemaphoreTake(passMutex, portMAX_DELAY);
      memcpy(spareNonce, fresh, sizeof(fresh));
      spareNonceReady = true;
      xSemaphoreGive(passMutex);
    }
    memset(fresh, 0, sizeof(fresh));
  }
  // A new link gets a fresh seal (or none outside the window); the nonce is
  // swapped in at read time. The window opening or closing, a confirm or a
  // release changes what device_secret serves, so it is refreshed then too.
  static bool servedLastPass = false;
  xSemaphoreTake(passMutex, portMAX_DELAY);
  bool serving = bindWindowServes(hasSecret, secretOwned, bindWindow.isOpen(millis()));
  xSemaphoreGive(passMutex);
  if (linkSetupPending.exchange(false, std::memory_order_relaxed) || serving != servedLastPass) {
    resealSecret();
  }
  servedLastPass = serving;
  // device_info bind_s follows the window second by second, and oled
  // once the panel probe has run.
  static uint32_t publishedBindSeconds = 0xFFFFFFFFu;
  static int8_t publishedOled = DEVICE_INFO_OLED_UNKNOWN;
  uint32_t bindSeconds = bindSecondsNow();
  if (bindSeconds != publishedBindSeconds || oledReported != publishedOled) {
    publishedBindSeconds = bindSeconds;
    publishedOled = oledReported;
    int n = buildDeviceInfoJson(deviceInfoJson, sizeof(deviceInfoJson), false);
    if (n > 0 && n < (int)sizeof(deviceInfoJson)) deviceInfoChr.write(deviceInfoJson, n);
  }
  updateOutputs();
#if RAILY_SHIP_MODE
  servicePowerOff();
#endif
  while (Serial.available()) {
    char c = (char)Serial.read();
#if RAILY_VOICE_BENCH
    if (voiceSerialPending() && voiceSerialChar(c)) continue;  // the hex digits of G<hh>, before `c`
#endif
    if (benchSerialChar(c)) continue;
#if RAILY_VDD_BENCH
    if (vddSerialChar(c)) continue;  // after the bench parser: `ov` stays a scene
#endif
#if RAILY_VOICE_BENCH
    if (voiceSerialChar(c)) continue;  // after the bench parser too: `ov` and `om` stay scenes
#endif
    if (c == 'p' || c == 'P') emitButtonEvent(0);
    if (c == 'i' || c == 'I') {
      printDeviceInfo();
#if RAILY_IDLE_SLEEP
      printIdleBench();
#endif
#if RAILY_VOICE
      printVoiceBench();
#endif
#if RAILY_SHIP_MODE
      printShipBench();
#endif
    }
    if (c == 's' || c == 'S') printSealedSecret();
    // USB is physical access: it opens the bind window like a RESET press.
    if (c == 'w' || c == 'W') openBindWindow("serial w");
    if (c == 'x' || c == 'X') eraseBondsFromSerial();
#if RAILY_SHIP_MODE
    // The bench's hand on D1: [ presses, ] releases (debounced as the pin).
    if (c == '[' || c == ']') {
      benchButtonDown = c == '[';
      Serial.println(benchButtonDown ? "bench: button down" : "bench: button up");
    }
#endif
    if (c == 'r' || c == 'R') {
      delay(50);
      NVIC_SystemReset();
    }
  }
#if RAILY_IDLE_SLEEP
  idleBlock();
#endif
}
