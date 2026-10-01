#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../RailyPinsP1/server_pass.h"
#include "../RailyPinsP1/secret_record.h"
#include "../RailyPinsP1/seal_format.h"
#include "../RailyPinsP1/seal_keys.h"

static int failures = 0;
static void expect(bool condition, const char* name) {
  printf("%s %s\n", condition ? "PASS" : "FAIL", name);
  if (!condition) failures++;
}

static size_t fromHex(const char* hex, size_t hexLength, uint8_t* out) {
  size_t n = hexLength / 2;
  for (size_t i = 0; i < n; i++) {
    unsigned value = 0;
    sscanf(hex + 2 * i, "%2x", &value);
    out[i] = (uint8_t)value;
  }
  return n;
}

// Minimal reader for server_pass_vectors.json (json.dumps, indent=2):
// finds `"key": "value"` or `"key": number` after `from`.
static const char* findValue(const char* from, const char* key, char* out, size_t cap) {
  char pattern[64];
  snprintf(pattern, sizeof(pattern), "\"%s\": ", key);
  const char* at = strstr(from, pattern);
  if (!at) return NULL;
  at += strlen(pattern);
  size_t n = 0;
  if (*at == '"') {
    at++;
    while (*at && *at != '"' && n + 1 < cap) out[n++] = *at++;
  } else {
    while (*at && *at != ',' && *at != '\n' && n + 1 < cap) out[n++] = *at++;
  }
  out[n] = 0;
  return at;
}

static char* readFile(const char* path) {
  FILE* file = fopen(path, "rb");
  if (!file) return NULL;
  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  fseek(file, 0, SEEK_SET);
  char* text = (char*)calloc((size_t)size + 1, 1);
  if (fread(text, 1, (size_t)size, file) != (size_t)size) {
    free(text);
    text = NULL;
  }
  fclose(file);
  return text;
}

static const char DEVICE[] = "rp1-0123456789abcdef";
static const uint8_t SECRET[16] = {0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
                                   0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f};

static void makePass(uint8_t passClass, uint8_t arg, const uint8_t nonce[16], uint8_t out[19],
                     const char* device = DEVICE, const uint8_t* secret = SECRET) {
  out[0] = PASS_VERSION;
  out[1] = passClass;
  out[2] = arg;
  computePassMac(secret, device, PASS_VERSION, passClass, arg, nonce, out + 3);
}

static void testSharedVectors() {
  char* text = readFile("server_pass_vectors.json");
  expect(text != NULL, "vector file readable");
  if (!text) return;
  int seen = 0;
  const char* at = strstr(text, "\"passes\"");
  char value[128];
  while (at && (at = findValue(at, "secret", value, sizeof(value))) != NULL) {
    uint8_t secret[16], nonce[16], expectedPass[19];
    fromHex(value, strlen(value), secret);
    char device[32];
    char classText[8] = {0}, argText[8] = {0}, nonceText[40] = {0}, passText[48] = {0};
    at = findValue(at, "device_id", device, sizeof(device));
    if (at) at = findValue(at, "class", classText, sizeof(classText));
    if (at) at = findValue(at, "arg", argText, sizeof(argText));
    if (at) at = findValue(at, "nonce", nonceText, sizeof(nonceText));
    if (at) at = findValue(at, "pass", passText, sizeof(passText));
    if (!at || strlen(nonceText) != 32 || strlen(passText) != 38) {
      expect(false, "vector entry has device_id, class, arg, nonce and pass");
      break;
    }
    uint8_t passClass = (uint8_t)atoi(classText);
    uint8_t arg = (uint8_t)atoi(argText);
    fromHex(nonceText, 32, nonce);
    fromHex(passText, 38, expectedPass);
    uint8_t built[19];
    makePass(passClass, arg, nonce, built, device, secret);
    expect(memcmp(built, expectedPass, 19) == 0, "pass matches the shared server vector");
    PassNonce issued = {};
    issued.issue(nonce, 1000);
    PassCommand command = {};
    // An admit vector is written on an encrypted, bonded link.
    PassResult result = verifyPass(expectedPass, 19, device, secret, true, true, issued, 1001, command,
                                   passClass == PASS_CLASS_ADMIT);
    expect(result == PASS_OK && command.passClass == passClass && command.arg == arg,
           "shared vector verifies on the pin");
    seen++;
  }
  expect(seen == 6, "all six pass vectors read");

  const char* kdf = strstr(text, "\"seal_kdf\"");
  if (kdf) {
    uint8_t sharedX[32], ephemeral[65], aesKey[16], ccmNonce[13];
    kdf = findValue(kdf, "shared_x", value, sizeof(value));
    fromHex(value, strlen(value), sharedX);
    char longValue[160];
    kdf = findValue(kdf, "ephemeral", longValue, sizeof(longValue));
    fromHex(longValue, strlen(longValue), ephemeral);
    kdf = findValue(kdf, "aes_key", value, sizeof(value));
    fromHex(value, strlen(value), aesKey);
    kdf = findValue(kdf, "ccm_nonce", value, sizeof(value));
    fromHex(value, strlen(value), ccmNonce);
    SealKeys keys;
    sealDeriveKeys(sharedX, ephemeral, keys);
    expect(memcmp(keys.aesKey, aesKey, 16) == 0 && memcmp(keys.ccmNonce, ccmNonce, 13) == 0,
           "seal key derivation matches the shared vector");
  } else {
    expect(false, "seal_kdf vector present");
  }
  free(text);
}

static void testRejections() {
  uint8_t nonce[16];
  for (int i = 0; i < 16; i++) nonce[i] = (uint8_t)(0x30 + i);
  uint8_t pass[19];
  PassCommand command = {};
  PassNonce issued = {};
  issued.issue(nonce, 5000);

  makePass(PASS_CLASS_CONTROL, PASS_ARG_ENTER_DFU, nonce, pass);
  uint8_t tampered[19];
  memcpy(tampered, pass, 19);
  tampered[18] ^= 0x01;
  expect(verifyPass(tampered, 19, DEVICE, SECRET, true, true, issued, 5001, command) == PASS_BAD_MAC,
         "flipped MAC bit rejected");
  expect(issued.valid, "a rejected pass leaves the nonce alive");
  expect(verifyPass(pass, 19, "rp1-fedcba9876543210", SECRET, true, true, issued, 5001, command) ==
             PASS_BAD_MAC,
         "pass for another device rejected");
  uint8_t otherSecret[16];
  memset(otherSecret, 0x42, 16);
  expect(verifyPass(pass, 19, DEVICE, otherSecret, true, true, issued, 5001, command) == PASS_BAD_MAC,
         "pass under another secret rejected");
  uint8_t argSwap[19];
  memcpy(argSwap, pass, 19);
  argSwap[2] = PASS_ARG_REBOOT;
  expect(verifyPass(argSwap, 19, DEVICE, SECRET, true, true, issued, 5001, command) == PASS_BAD_MAC,
         "changing the argument breaks the MAC");
  expect(verifyPass(pass, 18, DEVICE, SECRET, true, true, issued, 5001, command) == PASS_BAD_FORMAT,
         "short pass rejected");
  uint8_t longPass[20] = {0};
  memcpy(longPass, pass, 19);
  expect(verifyPass(longPass, 20, DEVICE, SECRET, true, true, issued, 5001, command) == PASS_BAD_FORMAT,
         "long pass rejected");
  uint8_t badVersion[19];
  memcpy(badVersion, pass, 19);
  badVersion[0] = 2;
  expect(verifyPass(badVersion, 19, DEVICE, SECRET, true, true, issued, 5001, command) ==
             PASS_BAD_FORMAT,
         "unknown version rejected");
  uint8_t badClass[19];
  makePass(0x01, 0, nonce, badClass);
  expect(verifyPass(badClass, 19, DEVICE, SECRET, true, true, issued, 5001, command) == PASS_BAD_FORMAT,
         "reserved class 0x01 rejected");
  uint8_t badArg[19];
  makePass(PASS_CLASS_CONTROL, 0x03, nonce, badArg);
  expect(verifyPass(badArg, 19, DEVICE, SECRET, true, true, issued, 5001, command) == PASS_BAD_FORMAT,
         "unknown control argument rejected");
  uint8_t confirmWithArg[19];
  makePass(PASS_CLASS_CONFIRM, 0x01, nonce, confirmWithArg);
  expect(verifyPass(confirmWithArg, 19, DEVICE, SECRET, true, true, issued, 5001, command) ==
             PASS_BAD_FORMAT,
         "confirm with an argument rejected");
  expect(verifyPass(pass, 19, DEVICE, SECRET, false, true, issued, 5001, command) == PASS_BAD_MAC,
         "no secret on the pin: every pass rejected");
  expect(verifyPass(pass, 19, DEVICE, SECRET, true, false, issued, 5001, command) == PASS_UNOWNED,
         "control pass refused while unowned");
  expect(issued.valid, "an unowned refusal leaves the nonce alive");

  uint8_t otherNonce[16];
  memset(otherNonce, 0x77, 16);
  uint8_t otherLink[19];
  makePass(PASS_CLASS_CONTROL, PASS_ARG_ENTER_DFU, otherNonce, otherLink);
  expect(verifyPass(otherLink, 19, DEVICE, SECRET, true, true, issued, 5001, command) == PASS_BAD_MAC,
         "pass made for another link's nonce rejected");

  expect(verifyPass(pass, 19, DEVICE, SECRET, true, true, issued, 5001, command) == PASS_OK &&
             command.passClass == PASS_CLASS_CONTROL && command.arg == PASS_ARG_ENTER_DFU,
         "valid DFU pass accepted");
  expect(!issued.valid, "accepted pass consumes the nonce");
  expect(verifyPass(pass, 19, DEVICE, SECRET, true, true, issued, 5002, command) == PASS_BAD_MAC,
         "replaying the same pass rejected");

  PassNonce idle = {};
  expect(verifyPass(pass, 19, DEVICE, SECRET, true, true, idle, 5001, command) == PASS_BAD_MAC,
         "no nonce issued: pass rejected");
}

static void testUnownedClasses() {
  uint8_t nonce[16];
  memset(nonce, 0x21, 16);
  PassCommand command = {};
  uint8_t pass[19];
  PassNonce issued = {};
  issued.issue(nonce, 0);
  makePass(PASS_CLASS_CONFIRM, 0, nonce, pass);
  expect(verifyPass(pass, 19, DEVICE, SECRET, true, false, issued, 10, command) == PASS_OK &&
             command.passClass == PASS_CLASS_CONFIRM,
         "confirm accepted on an unowned pin");
  issued.issue(nonce, 20);
  makePass(PASS_CLASS_RELEASE, 0, nonce, pass);
  expect(verifyPass(pass, 19, DEVICE, SECRET, true, false, issued, 30, command) == PASS_OK &&
             command.passClass == PASS_CLASS_RELEASE,
         "release accepted regardless of owned state");
  issued.issue(nonce, 40);
  makePass(PASS_CLASS_CONTROL, PASS_ARG_REBOOT, nonce, pass);
  expect(verifyPass(pass, 19, DEVICE, SECRET, true, true, issued, 50, command) == PASS_OK &&
             command.arg == PASS_ARG_REBOOT,
         "reboot pass accepted when owned");
}

// docs/pins/ble-bonding.md D-2: admit passes need an owned pin and an
// encrypted, bonded link; a genuine one on a plaintext link spends its nonce.
static void testAdmit() {
  uint8_t nonce[16];
  memset(nonce, 0x31, 16);
  PassCommand command = {};
  uint8_t pass[19];
  makePass(PASS_CLASS_ADMIT, 0, nonce, pass);

  PassNonce issued = {};
  issued.issue(nonce, 0);
  expect(verifyPass(pass, 19, DEVICE, SECRET, true, true, issued, 10, command, true) == PASS_OK &&
             command.passClass == PASS_CLASS_ADMIT && command.arg == 0 && !issued.valid,
         "admit accepted on an owned pin over a ready link, nonce spent");

  issued.issue(nonce, 0);
  expect(verifyPass(pass, 19, DEVICE, SECRET, true, true, issued, 10, command, false) == PASS_NEEDS_ENCRYPTION &&
             !issued.valid,
         "genuine admit on a plaintext link: needs encryption, and its nonce is spent");
  expect(verifyPass(pass, 19, DEVICE, SECRET, true, true, issued, 11, command, true) == PASS_BAD_MAC,
         "that pass cannot be replayed once the link is encrypted");
  expect(verifyPass(pass, 19, DEVICE, SECRET, true, true, issued, 11, command) == PASS_BAD_MAC,
         "the default link readiness is not ready");

  issued.issue(nonce, 0);
  uint8_t forged[19];
  memcpy(forged, pass, 19);
  forged[18] ^= 0x01;
  expect(verifyPass(forged, 19, DEVICE, SECRET, true, true, issued, 10, command, false) == PASS_BAD_MAC &&
             issued.valid,
         "forged admit on a plaintext link is a bad MAC and keeps the nonce");

  expect(verifyPass(pass, 19, DEVICE, SECRET, true, false, issued, 10, command, true) == PASS_UNOWNED &&
             issued.valid,
         "admit refused on an unowned pin, nonce kept");

  uint8_t withArg[19];
  makePass(PASS_CLASS_ADMIT, 1, nonce, withArg);
  expect(verifyPass(withArg, 19, DEVICE, SECRET, true, true, issued, 10, command, true) == PASS_BAD_FORMAT,
         "admit with an argument rejected");

  uint8_t confirm[19];
  makePass(PASS_CLASS_CONFIRM, 0, nonce, confirm);
  expect(verifyPass(confirm, 19, DEVICE, SECRET, true, false, issued, 10, command, false) == PASS_OK,
         "link readiness never gates the other classes");

  issued.issue(nonce, 0);
  uint8_t asConfirm[19];
  memcpy(asConfirm, pass, 19);
  asConfirm[1] = PASS_CLASS_CONFIRM;
  expect(verifyPass(asConfirm, 19, DEVICE, SECRET, true, true, issued, 10, command, true) == PASS_BAD_MAC,
         "an admit MAC does not confirm");
}

// docs/pins/ble-bonding.md §9.5: forget-all needs an owned pin, works on
// any link (open or encrypted), and takes no argument.
static void testForgetAll() {
  uint8_t nonce[16];
  memset(nonce, 0x41, 16);
  PassCommand command = {};
  uint8_t pass[19];
  makePass(PASS_CLASS_FORGET_ALL, 0, nonce, pass);

  PassNonce issued = {};
  issued.issue(nonce, 0);
  expect(verifyPass(pass, 19, DEVICE, SECRET, true, true, issued, 10, command, false) == PASS_OK &&
             command.passClass == PASS_CLASS_FORGET_ALL && command.arg == 0 && !issued.valid,
         "forget-all accepted on an owned pin over a plaintext link, nonce spent");
  expect(verifyPass(pass, 19, DEVICE, SECRET, true, true, issued, 11, command, false) == PASS_BAD_MAC,
         "forget-all cannot be replayed");

  issued.issue(nonce, 0);
  expect(verifyPass(pass, 19, DEVICE, SECRET, true, true, issued, 10, command, true) == PASS_OK,
         "forget-all accepted on an encrypted link too");

  issued.issue(nonce, 0);
  expect(verifyPass(pass, 19, DEVICE, SECRET, true, false, issued, 10, command, false) == PASS_UNOWNED &&
             issued.valid,
         "forget-all refused on an unowned pin, nonce kept");

  uint8_t forged[19];
  memcpy(forged, pass, 19);
  forged[10] ^= 0x80;
  expect(verifyPass(forged, 19, DEVICE, SECRET, true, true, issued, 10, command, false) == PASS_BAD_MAC &&
             issued.valid,
         "forged forget-all is a bad MAC and keeps the nonce");

  uint8_t withArg[19];
  makePass(PASS_CLASS_FORGET_ALL, 1, nonce, withArg);
  expect(verifyPass(withArg, 19, DEVICE, SECRET, true, true, issued, 10, command, false) == PASS_BAD_FORMAT,
         "forget-all with an argument rejected");

  uint8_t asAdmit[19];
  memcpy(asAdmit, pass, 19);
  asAdmit[1] = PASS_CLASS_ADMIT;
  expect(verifyPass(asAdmit, 19, DEVICE, SECRET, true, true, issued, 10, command, true) == PASS_BAD_MAC,
         "a forget-all MAC does not admit");
}

static void testNonceLifetime() {
  uint8_t nonce[16];
  memset(nonce, 0x55, 16);
  PassNonce issued = {};
  issued.issue(nonce, 1000);
  expect(issued.usable(1000 + PASS_NONCE_LIFETIME_MS - 1), "nonce usable in its final millisecond");
  expect(!issued.usable(1000 + PASS_NONCE_LIFETIME_MS), "nonce expires at 60 s");
  uint8_t pass[19];
  makePass(PASS_CLASS_CONTROL, PASS_ARG_REBOOT, nonce, pass);
  PassCommand command = {};
  expect(verifyPass(pass, 19, DEVICE, SECRET, true, true, issued, 1000 + PASS_NONCE_LIFETIME_MS,
                    command) == PASS_BAD_MAC,
         "pass on an expired nonce rejected");

  PassNonce wrapped = {};
  wrapped.issue(nonce, 0xFFFFFF00u);
  expect(wrapped.usable(100), "nonce survives the millis wrap");
  expect(!wrapped.usable(0xFFFFFF00u + PASS_NONCE_LIFETIME_MS), "nonce still expires after the wrap");
  wrapped.clear();
  expect(!wrapped.usable(0xFFFFFF01u), "cleared nonce unusable");
}

static void testChallenge() {
  uint8_t nonce[16];
  memset(nonce, 0x66, 16);
  PassNonce issued = {};
  issued.issue(nonce, 0);
  uint8_t value[PASS_CHALLENGE_LENGTH];
  buildPassChallenge(issued, true, value);
  expect(memcmp(value, nonce, 16) == 0 && value[16] == PASS_FLAG_OWNED && value[17] == PASS_VERSION,
         "challenge carries nonce, owned flag and version");
  issued.clear();
  buildPassChallenge(issued, false, value);
  uint8_t zeros[16] = {0};
  expect(memcmp(value, zeros, 16) == 0 && value[16] == 0, "no nonce: zeros and unowned");
}

static void testSecretRecord() {
  SecretRecord rec = makeSecretRecord(SECRET, true);
  expect(sizeof(SecretRecord) == 28, "record is 28 bytes");
  SecretRecord out;
  expect(readSecretRecord((const uint8_t*)&rec, sizeof(rec), out) && memcmp(out.secret, SECRET, 16) == 0 &&
             (out.flags & SECRET_FLAG_OWNED),
         "record round-trips secret and owned flag");
  SecretRecord unowned = makeSecretRecord(SECRET, false);
  expect(readSecretRecord((const uint8_t*)&unowned, sizeof(unowned), out) && out.flags == 0,
         "unowned record round-trips");
  uint8_t torn[sizeof(SecretRecord)];
  memcpy(torn, &rec, sizeof(torn));
  torn[10] ^= 0xFF;
  expect(!readSecretRecord(torn, sizeof(torn), out), "corrupted secret byte rejected");
  memcpy(torn, &rec, sizeof(torn));
  torn[5] ^= SECRET_FLAG_OWNED;
  expect(!readSecretRecord(torn, sizeof(torn), out), "flipped owned flag rejected");
  expect(!readSecretRecord((const uint8_t*)&rec, sizeof(rec) - 1, out), "truncated record rejected");
  uint8_t oversized[sizeof(SecretRecord) + 1] = {0};
  memcpy(oversized, &rec, sizeof(rec));
  expect(!readSecretRecord(oversized, sizeof(oversized), out), "file longer than a record rejected");
  memcpy(torn, &rec, sizeof(torn));
  torn[0] = 'X';
  expect(!readSecretRecord(torn, sizeof(torn), out), "wrong magic rejected");
  uint8_t zeros[sizeof(SecretRecord)] = {0};
  expect(!readSecretRecord(zeros, sizeof(zeros), out), "erased file rejected");
}

static void testSealLayout() {
  uint8_t plaintext[SEAL_MAX_PLAINTEXT];
  size_t n = sealWritePlaintext(DEVICE, SECRET, plaintext);
  expect(n == 1 + 20 + 16 && plaintext[0] == 20 && memcmp(plaintext + 1, DEVICE, 20) == 0 &&
             memcmp(plaintext + 21, SECRET, 16) == 0,
         "plaintext is len, device id, secret");
  expect(sealWritePlaintext("", SECRET, plaintext) == 0, "empty device id refused");
  expect(sealWritePlaintext("rp1-0123456789abcdef0123", SECRET, plaintext) == 0,
         "device id longer than 23 refused");
  uint8_t keyId[4] = {0xAA, 0x49, 0x12, 0x3D};
  uint8_t point[65];
  memset(point, 0x04, 65);
  uint8_t header[SEAL_HEADER_LENGTH];
  expect(sealWriteHeader(keyId, point, header) == 70 && header[0] == SEAL_VERSION &&
             memcmp(header + 1, keyId, 4) == 0,
         "header is version, key id, ephemeral point");
  expect(SEAL_HEADER_LENGTH + 37 + SEAL_TAG_LENGTH == 123, "a 20-char id seals to 123 bytes");
}

// The key id in seal_keys.h is typed by hand; it must be SHA-256(point)[0..4]
// or the server cannot find the key. The prod header is checked by the
// same rule in a second build of this test (-DRAILY_SEAL_PROD).
static void testSealKeyId() {
  uint8_t digest[32];
  sha256(SEAL_PUBLIC_KEY, sizeof(SEAL_PUBLIC_KEY), digest);
  expect(memcmp(digest, SEAL_KEY_ID, 4) == 0, SEAL_KEY_NAME[0] == 'p'
                                                  ? "prod seal key id matches its public key"
                                                  : "dev seal key id matches its public key");
  expect(SEAL_PUBLIC_KEY[0] == 0x04, "seal public key is an uncompressed point");
}

int main() {
  testSealKeyId();
  testSharedVectors();
  testRejections();
  testUnownedClasses();
  testAdmit();
  testForgetAll();
  testNonceLifetime();
  testChallenge();
  testSecretRecord();
  testSealLayout();
  return failures ? 1 : 0;
}
