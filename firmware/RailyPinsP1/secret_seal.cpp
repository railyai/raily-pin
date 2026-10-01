#include "secret_seal.h"

#include <Adafruit_nRFCrypto.h>
#include <string.h>
#include "nrf_cc310/include/crys_aesccm.h"
#include "seal_format.h"
#include "seal_keys.h"

// Static, not locals: the key objects alone are about 1.2 KB, and on the
// P4 bench they pushed the Arduino loop task (1024 words) into overflow.
// Only the seal task calls sealDeviceSecret(), one job at a time
// (RailyPinsP1.ino, sealOnWorker), so one set is enough. The frame left
// here is checked against hardware/firmware/stack_budget.json.
static nRFCrypto_ECC_PrivateKey ephemeralPrivate;
static nRFCrypto_ECC_PublicKey ephemeralPublic;
static nRFCrypto_ECC_PublicKey serverPublic;
static uint8_t ephemeral[SEAL_POINT_LENGTH];
static uint8_t sharedX[32];
static uint8_t plaintext[SEAL_MAX_PLAINTEXT];
static SealKeys keys;

// nRFCrypto.begin() is idempotent and never paired with end(): Bluefruit
// may use the same CryptoCell session for LE Secure Connections.
size_t sealDeviceSecret(const char* deviceId, const uint8_t secret[16], uint8_t* blob,
                        size_t capacity) {
  if (capacity < SEAL_MAX_BLOB) return 0;
  if (!nRFCrypto.begin()) return 0;

  size_t length = 0;

  do {
    if (!ephemeralPrivate.begin(CRYS_ECPKI_DomainID_secp256r1)) break;
    if (!ephemeralPublic.begin(CRYS_ECPKI_DomainID_secp256r1)) break;
    if (!serverPublic.begin(CRYS_ECPKI_DomainID_secp256r1)) break;
    if (!nRFCrypto_ECC::genKeyPair(ephemeralPrivate, ephemeralPublic)) break;
    if (!serverPublic.fromRaw((uint8_t*)SEAL_PUBLIC_KEY, sizeof(SEAL_PUBLIC_KEY))) break;
    if (ephemeralPublic.toRaw(ephemeral, sizeof(ephemeral)) != sizeof(ephemeral)) break;
    if (nRFCrypto_ECC::SVDP_DH(ephemeralPrivate, serverPublic, sharedX, sizeof(sharedX)) !=
        sizeof(sharedX)) {
      break;
    }
    sealDeriveKeys(sharedX, ephemeral, keys);
    size_t header = sealWriteHeader(SEAL_KEY_ID, ephemeral, blob);
    size_t textLength = sealWritePlaintext(deviceId, secret, plaintext);
    if (textLength == 0) break;

    CRYS_AESCCM_Key_t ccmKey;
    memset(ccmKey, 0, sizeof(ccmKey));
    memcpy(ccmKey, keys.aesKey, SEAL_AES_KEY_LENGTH);
    CRYS_AESCCM_Mac_Res_t tag;
    CRYSError_t err = CRYS_AESCCM(SASI_AES_ENCRYPT, ccmKey, CRYS_AES_Key128BitSize,
                                  keys.ccmNonce, SEAL_CCM_NONCE_LENGTH, blob, header,
                                  plaintext, textLength, blob + header, SEAL_TAG_LENGTH, tag);
    memset(ccmKey, 0, sizeof(ccmKey));
    if (err != CRYS_OK) break;
    memcpy(blob + header + textLength, tag, SEAL_TAG_LENGTH);
    length = header + textLength + SEAL_TAG_LENGTH;
  } while (false);

  ephemeralPrivate.end();
  ephemeralPublic.end();
  serverPublic.end();
  memset(sharedX, 0, sizeof(sharedX));
  memset(plaintext, 0, sizeof(plaintext));
  memset(&keys, 0, sizeof(keys));
  return length;
}
