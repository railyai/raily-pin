#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "hmac_sha256.h"

// Server pass (docs/pins/security-bonding-secure-dfu.md §3.2): a 19-byte
// write that the Raily server computes with the pin's secret S for one
// link's nonce. Only rare, online actions need one: restart, DFU entry,
// the owner confirmation, the release on unbind, admitting a phone's bond
// (docs/pins/ble-bonding.md D-2) and forgetting every phone (§9.5). Host-tested in
// hardware/firmware/tests/test_server_pass.cpp; the server computes the
// same MAC and is tested against the same vector file,
// hardware/firmware/tests/server_pass_vectors.json.

static const uint8_t PASS_VERSION = 1;
static const size_t PASS_LENGTH = 19;
static const size_t PASS_MAC_LENGTH = 16;
static const size_t PASS_NONCE_LENGTH = 16;
static const size_t PASS_SECRET_LENGTH = 16;
static const uint32_t PASS_NONCE_LIFETIME_MS = 60000;
static const char PASS_MAC_LABEL[] = "raily-pass-v1";

static const uint8_t PASS_CLASS_CONTROL = 0x02;
static const uint8_t PASS_CLASS_CONFIRM = 0x03;
static const uint8_t PASS_CLASS_RELEASE = 0x04;
// Admits the bond of the encrypted link it is written on (ble-bonding.md D-2).
static const uint8_t PASS_CLASS_ADMIT = 0x05;
// Erases every bond and admission, on any link (ble-bonding.md §9.5): a
// lost phone. The secret, the owned flag, the counter and the mascot stay.
static const uint8_t PASS_CLASS_FORGET_ALL = 0x06;

static const uint8_t PASS_ARG_REBOOT = 0x01;
static const uint8_t PASS_ARG_ENTER_DFU = 0x02;

// ATT application error codes the pin answers a rejected pass with. The
// phone re-reads pass_challenge and retries once after PASS_BAD_MAC.
enum PassResult : uint8_t {
  PASS_OK = 0x00,
  PASS_BAD_MAC = 0x80,     // wrong MAC, or no usable nonce (expired, used, none issued)
  PASS_UNOWNED = 0x81,     // a control pass on a pin the server has not confirmed yet
  PASS_BAD_FORMAT = 0x82,  // wrong length, version, class or argument
  PASS_BUSY = 0x83,        // an action is still pending or a reject backoff runs: retry shortly, same nonce
  // A genuine admit pass on a link that is not encrypted with a 16-byte key
  // and bonded. Its nonce is spent (it crossed a plaintext link), and the
  // sketch answers ATT 0x0F Insufficient Encryption: pair, then fetch a new pass.
  // Never sent as 0x80 + 4 (screen_state uses ATT 0x84 for another meaning).
  PASS_NEEDS_ENCRYPTION = 0x84,
};

// One nonce per link. It is usable for PASS_NONCE_LIFETIME_MS after issue
// and dies with the first accepted pass; a rejected pass leaves it alive.
// Unsigned subtraction keeps the lifetime right across the millis() wrap.
struct PassNonce {
  uint8_t bytes[PASS_NONCE_LENGTH];
  uint32_t issuedMs;
  bool valid;

  void issue(const uint8_t fresh[PASS_NONCE_LENGTH], uint32_t now) {
    memcpy(bytes, fresh, PASS_NONCE_LENGTH);
    issuedMs = now;
    valid = true;
  }

  bool usable(uint32_t now) const {
    return valid && (uint32_t)(now - issuedMs) < PASS_NONCE_LIFETIME_MS;
  }

  void clear() {
    memset(bytes, 0, sizeof(bytes));
    valid = false;
  }
};

struct PassCommand {
  uint8_t passClass;
  uint8_t arg;
};

// MAC input: label ‖ u8 len(device_id) ‖ device_id ‖ version ‖ class ‖ arg ‖ nonce.
static inline void computePassMac(const uint8_t secret[PASS_SECRET_LENGTH], const char* deviceId,
                                  uint8_t version, uint8_t passClass, uint8_t arg,
                                  const uint8_t nonce[PASS_NONCE_LENGTH],
                                  uint8_t mac[PASS_MAC_LENGTH]) {
  size_t idLength = strlen(deviceId);
  uint8_t idLengthByte = (uint8_t)(idLength > 255 ? 255 : idLength);
  uint8_t fields[3] = {version, passClass, arg};
  HmacSha256 ctx;
  hmacSha256Init(ctx, secret, PASS_SECRET_LENGTH);
  hmacSha256Update(ctx, (const uint8_t*)PASS_MAC_LABEL, sizeof(PASS_MAC_LABEL) - 1);
  hmacSha256Update(ctx, &idLengthByte, 1);
  hmacSha256Update(ctx, (const uint8_t*)deviceId, idLengthByte);
  hmacSha256Update(ctx, fields, sizeof(fields));
  hmacSha256Update(ctx, nonce, PASS_NONCE_LENGTH);
  uint8_t full[32];
  hmacSha256Final(ctx, full);
  memcpy(mac, full, PASS_MAC_LENGTH);
}

static inline bool passShapeValid(uint8_t passClass, uint8_t arg) {
  switch (passClass) {
    case PASS_CLASS_CONTROL:
      return arg == PASS_ARG_REBOOT || arg == PASS_ARG_ENTER_DFU;
    case PASS_CLASS_CONFIRM:
    case PASS_CLASS_RELEASE:
    case PASS_CLASS_ADMIT:
    case PASS_CLASS_FORGET_ALL:
      return arg == 0;
    default:
      return false;
  }
}

// Checks one written pass. On PASS_OK the nonce is consumed and `out`
// names the action; the caller runs it outside the BLE callback.
// admitLinkReady matters only for an admit pass: the writing link is
// encrypted with a 16-byte key and bonded (bond_table.h linkReadyForAdmission).
static inline PassResult verifyPass(const uint8_t* data, size_t length, const char* deviceId,
                                    const uint8_t secret[PASS_SECRET_LENGTH], bool hasSecret,
                                    bool owned, PassNonce& nonce, uint32_t now, PassCommand& out,
                                    bool admitLinkReady = false) {
  if (data == NULL || length != PASS_LENGTH || data[0] != PASS_VERSION) return PASS_BAD_FORMAT;
  uint8_t passClass = data[1];
  uint8_t arg = data[2];
  if (!passShapeValid(passClass, arg)) return PASS_BAD_FORMAT;
  if (!hasSecret || !nonce.usable(now)) return PASS_BAD_MAC;
  if ((passClass == PASS_CLASS_CONTROL || passClass == PASS_CLASS_ADMIT ||
       passClass == PASS_CLASS_FORGET_ALL) && !owned) {
    return PASS_UNOWNED;
  }
  uint8_t expected[PASS_MAC_LENGTH];
  computePassMac(secret, deviceId, PASS_VERSION, passClass, arg, nonce.bytes, expected);
  if (!constantTimeEqual(expected, data + 3, PASS_MAC_LENGTH)) return PASS_BAD_MAC;
  nonce.clear();
  if (passClass == PASS_CLASS_ADMIT && !admitLinkReady) return PASS_NEEDS_ENCRYPTION;
  out.passClass = passClass;
  out.arg = arg;
  return PASS_OK;
}

// pass_challenge value: nonce ‖ u8 flags ‖ u8 pass version.
static const size_t PASS_CHALLENGE_LENGTH = PASS_NONCE_LENGTH + 2;
static const uint8_t PASS_FLAG_OWNED = 0x01;
static const uint8_t PASS_FLAG_NO_SECRET = 0x02;  // the pin could not make its secret yet: no pass can work
// The bind window is open: this unowned pin serves its seal on device_secret
// now (bind_window.h). Apps before tap-to-bind reject the bit, so the app
// that knows it ships first (docs/pins/diy-claimless-bind.md §4.2).
static const uint8_t PASS_FLAG_BIND_WINDOW = 0x04;

static inline void buildPassChallenge(const PassNonce& nonce, bool owned,
                                      uint8_t out[PASS_CHALLENGE_LENGTH]) {
  if (nonce.valid) {
    memcpy(out, nonce.bytes, PASS_NONCE_LENGTH);
  } else {
    memset(out, 0, PASS_NONCE_LENGTH);
  }
  out[PASS_NONCE_LENGTH] = owned ? PASS_FLAG_OWNED : 0;
  out[PASS_NONCE_LENGTH + 1] = PASS_VERSION;
}
