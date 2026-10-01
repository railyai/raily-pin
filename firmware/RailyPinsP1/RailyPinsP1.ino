#include <Adafruit_TinyUSB.h>
#include <bluefruit.h>
#include <nrf.h>
#include <atomic>
#include <Adafruit_LittleFS.h>
#include <InternalFileSystem.h>
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
#include "oled_render.h"
#include "screen_state.h"
#include "mascot_record.h"
#include "rhythm.h"
#include "bench_serial.h"
#include "screen_bench.h"
#include "bond_table.h"
#include "press_hold.h"

using namespace Adafruit_LittleFS_Namespace;

static const char DEVICE_NAME[] = "Raily Device";
static const char FW_VERSION[] = "0.2.20-qa";

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
BLEDis bledis;
// The Nordic legacy DFU service (BLEDfu) is gone on purpose: its control
// point let any nearby radio jump the pin into the bootloader. DFU entry
// is now a control pass (enterDfuArmed below).

char deviceId[24];
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

// The vibration motor on Grove A0/D0 (D0 = P0.02; gestures-spec.md): a
// digital level, the module has its own transistor. -DRAILY_MOTOR=0 leaves
// the pin untouched; -DRAILY_MOTOR_ACTIVE_LOW=1 if the bench finds the
// module active low (then the pin idles high).
#ifndef RAILY_MOTOR
#define RAILY_MOTOR 1
#endif
#ifndef RAILY_MOTOR_PIN
#define RAILY_MOTOR_PIN D0
#endif
#ifndef RAILY_MOTOR_ACTIVE_LOW
#define RAILY_MOTOR_ACTIVE_LOW 0
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
static uint32_t firmwareBuildHash = 0;  // firmwareHash(FW_VERSION), for Service Changed (D-7)
static std::atomic<uint8_t> bondFileCount(0);  // loop() counts; link_security and serial i read
// A pairing just completed: loop() enforces the bond cap a moment later,
// after Bluefruit's own deferred save has written the new bond file.
static const uint32_t BOND_CAP_DELAY_MS = 2000;
static std::atomic<bool> bondCapPending(false);
static std::atomic<uint32_t> bondCapDueMs(0);
static std::atomic<bool> serviceChangedCheckPending(false);
// The link a Service Changed confirmation arrived on: loop() stores the build
// only when it is the link the indication was sent on.
static std::atomic<uint16_t> serviceChangedConfirmedHandle(BLE_CONN_HANDLE_INVALID);
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
                    "\"reply_err\":%lu,\"bind_s\":%lu,\"window_s\":%lu,\"reset\":\"0x%08lx\",\"oled\":%d}",
                    deviceId, FW_VERSION,
                    Bluefruit.connected(), Bluefruit.Advertising.isRunning(),
                    owned ? 1 : 0, SEAL_KEY_NAME,
                    (unsigned long)uxTaskGetStackHighWaterMark(NULL),
                    (unsigned long)(sealTask ? uxTaskGetStackHighWaterMark(sealTask) : 0),
                    (unsigned long)bleTaskLowWater,
                    (unsigned long)authorizeReplyFailures,
                    (unsigned long)bindSeconds, (unsigned long)windowSeconds,
                    (unsigned long)bootResetReason, oledPresentNow() ? 1 : 0);
  }
  return formatDeviceInfo(buf, size, deviceId, FW_VERSION, bindSeconds, oledReported);
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
    if (sendEarlier && !buttonEventChr.notify(handle, earlier, sizeof(earlier))) {
      pendingScreenPressFailed.store(true, std::memory_order_relaxed);
    }
    // A link that dropped since the check fails the notify: «didn't work».
    sent = buttonEventChr.notify(handle, &evt, sizeof(evt));
  }
  if (sent) {
    pendingScreenPress.store(true, std::memory_order_relaxed);
  } else if (route != PRESS_HOLD || replaced) {
    pendingScreenPressFailed.store(true, std::memory_order_relaxed);
  }
  if (emitMutex) xSemaphoreGive(emitMutex);
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
  bool sent = tick == HELD_DELIVER && buttonEventChr.notify(handle, payload, sizeof(payload));
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

// The pin was dropped: «Упал. Было больно» on the screen (keyring-oled PR 5,
// ScreenState::onFall). gestures S1's gesture 3 detector (free fall, then
// the impact, on the Sense IMU) calls it when the pin lands; until the IMU
// ships, serial `of` does. Safe from any task: it only raises a flag that
// loop() drains. The screen only, by the owner's decision (2026-09-28): no
// motor, no LED, no BLE (tests/check_fall_path.py).
static void screenOnFall() {
  pendingScreenFall.store(true, std::memory_order_relaxed);
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
    if (bondTable.admit(link.addr, link.addrType, firmwareBuildHash, NULL, &evicted)) bondTableDirty = true;
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
    acceptWriteAuthorize(connHdl, request);
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

static void onBleEvent(ble_evt_t* evt) {
  const uint16_t connHdl = evt->evt.common_evt.conn_handle;
  switch (evt->header.evt_id) {
    case BLE_GAP_EVT_CONNECTED:
      cacheLink(connHdl, true, true);
      break;
    case BLE_GAP_EVT_DISCONNECTED: {
      // A held press belongs to this link (press_hold.h): never to the next.
      if (cacheLink(connHdl, false, true)) pendingScreenPressFailed.store(true, std::memory_order_relaxed);
      // A confirmation still waiting for loop() belongs to this link only.
      serviceChangedConfirmedHandle.store(BLE_CONN_HANDLE_INVALID, std::memory_order_relaxed);
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
      break;
    }
    case BLE_GAP_EVT_CONN_SEC_UPDATE:
      cacheLink(connHdl, true);
      serviceChangedCheckPending.store(true, std::memory_order_relaxed);
      break;
    case BLE_GATTS_EVT_SC_CONFIRM:
      serviceChangedConfirmedHandle.store(connHdl, std::memory_order_relaxed);
      break;
    default:
      break;
  }
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
  snprintf(path, sizeof(path), "%s/%02X%02X%02X%02X%02X%02X", BOND_DIR_PRPH, addr[0], addr[1], addr[2], addr[3],
           addr[4], addr[5]);
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
static bool motorOn = false;
static bool motorQuiet = false;
static const uint8_t MOTOR_ON_LEVEL = RAILY_MOTOR_ACTIVE_LOW ? LOW : HIGH;
static const uint8_t MOTOR_OFF_LEVEL = RAILY_MOTOR_ACTIVE_LOW ? HIGH : LOW;
#endif

// A plain output at the off level, from setup() after BLE advertises (the
// pre-flash gate: nothing before Bluefruit.begin() may fault). Until then
// the pin is the reset default, an unconnected input.
static void startMotorPin() {
#if RAILY_MOTOR
  pinMode(RAILY_MOTOR_PIN, OUTPUT);
  digitalWrite(RAILY_MOTOR_PIN, MOTOR_OFF_LEVEL);
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
  if (on != motorOn) {
    digitalWrite(RAILY_MOTOR_PIN, on ? MOTOR_ON_LEVEL : MOTOR_OFF_LEVEL);
    motorOn = on;
  }
#else
  (void)now;
  (void)rawAck;
#endif
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
  passMutex = xSemaphoreCreateMutex();
  sealDone = xSemaphoreCreateBinary();
  if (xTaskCreate(sealWorker, "seal", SEAL_TASK_STACK_WORDS, NULL, TASK_PRIO_LOW, &sealTask) != pdPASS) {
    sealTask = NULL;  // resealSecret() then publishes no seal; passes stay off
    Serial.println("seal task: not created - no sealed secret");
  }

  Bluefruit.begin();
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
  firmwareBuildHash = firmwareHash(FW_VERSION);

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
  // device_info carries bind_s: built once the window is known.
  buildDeviceInfoJson(deviceInfoJson, sizeof(deviceInfoJson), false);

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

  startAdv();
  startMotorPin();
  Serial.println(deviceInfoJson);
  Serial.println("Type p + Enter to simulate a button press, i + Enter to reprint device info, s + Enter to print the sealed secret, w + Enter to open the bind window, r + Enter to reset, o<scene>[shape[material[locale]]] + Enter to show a screen scene, of + Enter to drop the pin (the fall), c<hex byte> + Enter to play an event_ack byte (motor rhythm), x + Enter to erase every BLE bond");
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
      snprintf(path, sizeof(path), "%s/%02X%02X%02X%02X%02X%02X", BOND_DIR_PRPH, a[0], a[1], a[2], a[3], a[4],
               a[5]);
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

// Service Changed after an update (ble-bonding.md D-7): once per admitted
// phone per build, stored only on the phone's confirmation.
static bool serviceChangedSent = false;
static uint16_t serviceChangedHandle = BLE_CONN_HANDLE_INVALID;
static uint8_t serviceChangedAddr[BOND_ADDR_LENGTH];

static __attribute__((noinline)) void sendServiceChangedIfDue() {
  uint16_t conn = BLE_CONN_HANDLE_INVALID;
  const LinkSecurity link = currentLink(&conn);
  if (conn == BLE_CONN_HANDLE_INVALID) return;
  if (!linkReadyForAdmission(link.level, link.keySize, link.bonded)) return;
  xSemaphoreTake(passMutex, portMAX_DELAY);
  const bool due = bondTable.serviceChangedDue(link.addr, firmwareBuildHash);
  xSemaphoreGive(passMutex);
  if (!due) return;
  const uint32_t err = sd_ble_gatts_service_changed(conn, 0x0001, 0xFFFF);
  if (err == NRF_SUCCESS) {
    memcpy(serviceChangedAddr, link.addr, sizeof(serviceChangedAddr));
    serviceChangedHandle = conn;
    serviceChangedSent = true;
    Serial.println("bond: service changed sent");
  } else {
    Serial.printf("bond: service changed not sent (0x%lx), next link\n", (unsigned long)err);
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
    if (bondTable.markServiceChanged(serviceChangedAddr, firmwareBuildHash)) bondTableDirty = true;
    xSemaphoreGive(passMutex);
    Serial.println("bond: service changed confirmed");
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

// Serial `i`: the extended device-info line. Its 256-byte buffer lives in
// this frame, not in loop()'s (stack_budget.json). The line ends with
// "scr" (screen_bench.h): the same buffer is printed twice, the document
// without its closing brace, then `,"scr":…}`, because both do not fit
// 256 bytes at once (the document reaches 227 bytes, "scr" 152 with every
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
    const bool present = oledProbe();
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
  if (!changed || (drawnOnce && (uint32_t)(now - drawnAtMs) < OLED_FRAME_MS)) return;
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

void loop() {
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
  serviceHeldPress();
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
  while (Serial.available()) {
    char c = (char)Serial.read();
    if (benchSerialChar(c)) continue;
    if (c == 'p' || c == 'P') emitButtonEvent(0);
    if (c == 'i' || c == 'I') printDeviceInfo();
    if (c == 's' || c == 'S') printSealedSecret();
    // USB is physical access: it opens the bind window like a RESET press.
    if (c == 'w' || c == 'W') openBindWindow("serial w");
    if (c == 'x' || c == 'X') eraseBondsFromSerial();
    if (c == 'r' || c == 'R') {
      delay(50);
      NVIC_SystemReset();
    }
  }
}
