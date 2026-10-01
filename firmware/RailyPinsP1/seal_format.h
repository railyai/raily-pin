#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include "hmac_sha256.h"

// Layout and key derivation of the sealed secret that device_secret
// returns (docs/pins/security-bonding-secure-dfu.md §3.1). ECIES on P-256:
//
//   blob = 0x01 ‖ key_id(4) ‖ E(65, ephemeral public key, uncompressed)
//          ‖ AES-128-CCM(key, nonce, aad, plaintext) ‖ tag(16)
//   plaintext = u8 len(device_id) ‖ device_id ‖ S(16)
//   aad       = 0x01 ‖ key_id ‖ E
//   key ‖ nonce = HKDF-SHA256(ikm = ECDH x-coordinate, salt = empty,
//                             info = "raily-seal-v1" ‖ E), 16 + 13 bytes
//
// This header holds the pure parts (host-tested); the ECDH and AES-CCM run
// on the CryptoCell in secret_seal.cpp. The server opens it with
// PIN_SECRET_SEAL_KEY.

static const uint8_t SEAL_VERSION = 1;
static const size_t SEAL_KEY_ID_LENGTH = 4;
static const size_t SEAL_POINT_LENGTH = 65;
static const size_t SEAL_AES_KEY_LENGTH = 16;
static const size_t SEAL_CCM_NONCE_LENGTH = 13;
static const size_t SEAL_TAG_LENGTH = 16;
static const size_t SEAL_SECRET_LENGTH = 16;
static const size_t SEAL_MAX_DEVICE_ID = 23;
static const size_t SEAL_HEADER_LENGTH = 1 + SEAL_KEY_ID_LENGTH + SEAL_POINT_LENGTH;  // also the aad
static const size_t SEAL_MAX_PLAINTEXT = 1 + SEAL_MAX_DEVICE_ID + SEAL_SECRET_LENGTH;
static const size_t SEAL_MAX_BLOB = SEAL_HEADER_LENGTH + SEAL_MAX_PLAINTEXT + SEAL_TAG_LENGTH;
static const char SEAL_INFO_LABEL[] = "raily-seal-v1";

// RFC 5869 with SHA-256; okmLength is at most 255 * 32.
static inline void hkdfSha256(const uint8_t* salt, size_t saltLength, const uint8_t* ikm,
                              size_t ikmLength, const uint8_t* info, size_t infoLength,
                              uint8_t* okm, size_t okmLength) {
  uint8_t zeroSalt[32] = {0};
  if (salt == NULL || saltLength == 0) {
    salt = zeroSalt;
    saltLength = sizeof(zeroSalt);
  }
  uint8_t prk[32];
  hmacSha256(salt, saltLength, ikm, ikmLength, prk);
  uint8_t block[32];
  size_t produced = 0;
  for (uint8_t counter = 1; produced < okmLength; counter++) {
    HmacSha256 ctx;
    hmacSha256Init(ctx, prk, sizeof(prk));
    if (counter > 1) hmacSha256Update(ctx, block, sizeof(block));
    hmacSha256Update(ctx, info, infoLength);
    hmacSha256Update(ctx, &counter, 1);
    hmacSha256Final(ctx, block);
    size_t take = okmLength - produced < sizeof(block) ? okmLength - produced : sizeof(block);
    memcpy(okm + produced, block, take);
    produced += take;
  }
}

struct SealKeys {
  uint8_t aesKey[SEAL_AES_KEY_LENGTH];
  uint8_t ccmNonce[SEAL_CCM_NONCE_LENGTH];
};

static inline void sealDeriveKeys(const uint8_t sharedX[32],
                                  const uint8_t ephemeral[SEAL_POINT_LENGTH], SealKeys& keys) {
  uint8_t info[sizeof(SEAL_INFO_LABEL) - 1 + SEAL_POINT_LENGTH];
  memcpy(info, SEAL_INFO_LABEL, sizeof(SEAL_INFO_LABEL) - 1);
  memcpy(info + sizeof(SEAL_INFO_LABEL) - 1, ephemeral, SEAL_POINT_LENGTH);
  uint8_t okm[SEAL_AES_KEY_LENGTH + SEAL_CCM_NONCE_LENGTH];
  hkdfSha256(NULL, 0, sharedX, 32, info, sizeof(info), okm, sizeof(okm));
  memcpy(keys.aesKey, okm, SEAL_AES_KEY_LENGTH);
  memcpy(keys.ccmNonce, okm + SEAL_AES_KEY_LENGTH, SEAL_CCM_NONCE_LENGTH);
}

// Writes the header (which is also the aad); returns its length.
static inline size_t sealWriteHeader(const uint8_t keyId[SEAL_KEY_ID_LENGTH],
                                     const uint8_t ephemeral[SEAL_POINT_LENGTH], uint8_t* out) {
  out[0] = SEAL_VERSION;
  memcpy(out + 1, keyId, SEAL_KEY_ID_LENGTH);
  memcpy(out + 1 + SEAL_KEY_ID_LENGTH, ephemeral, SEAL_POINT_LENGTH);
  return SEAL_HEADER_LENGTH;
}

// Writes the plaintext; returns its length, or 0 when the id is too long.
static inline size_t sealWritePlaintext(const char* deviceId,
                                        const uint8_t secret[SEAL_SECRET_LENGTH], uint8_t* out) {
  size_t idLength = strlen(deviceId);
  if (idLength == 0 || idLength > SEAL_MAX_DEVICE_ID) return 0;
  out[0] = (uint8_t)idLength;
  memcpy(out + 1, deviceId, idLength);
  memcpy(out + 1 + idLength, secret, SEAL_SECRET_LENGTH);
  return 1 + idLength + SEAL_SECRET_LENGTH;
}
