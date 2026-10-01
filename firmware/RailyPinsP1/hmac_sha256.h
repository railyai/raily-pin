#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

// SHA-256 (FIPS 180-4) and HMAC-SHA256 (RFC 2104) in one portable header.
// The firmware and the host tests compile the same code, so the server
// pass MAC is checked against the RFC 4231 vectors on the host
// (hardware/firmware/tests/test_hmac_sha256.cpp). Deliberately software:
// no CryptoCell dependency, so pass verification is host-testable.

struct Sha256 {
  uint32_t state[8];
  uint64_t bitLength;
  uint8_t block[64];
  size_t blockLength;
};

static inline uint32_t sha256Rotr(uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

static inline void sha256Compress(Sha256& ctx, const uint8_t* data) {
  static const uint32_t k[64] = {
      0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
      0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
      0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
      0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
      0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
      0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
      0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
      0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
  uint32_t w[64];
  for (int i = 0; i < 16; i++) {
    w[i] = ((uint32_t)data[4 * i] << 24) | ((uint32_t)data[4 * i + 1] << 16) |
           ((uint32_t)data[4 * i + 2] << 8) | (uint32_t)data[4 * i + 3];
  }
  for (int i = 16; i < 64; i++) {
    uint32_t s0 = sha256Rotr(w[i - 15], 7) ^ sha256Rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
    uint32_t s1 = sha256Rotr(w[i - 2], 17) ^ sha256Rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
    w[i] = w[i - 16] + s0 + w[i - 7] + s1;
  }
  uint32_t a = ctx.state[0], b = ctx.state[1], c = ctx.state[2], d = ctx.state[3];
  uint32_t e = ctx.state[4], f = ctx.state[5], g = ctx.state[6], h = ctx.state[7];
  for (int i = 0; i < 64; i++) {
    uint32_t s1 = sha256Rotr(e, 6) ^ sha256Rotr(e, 11) ^ sha256Rotr(e, 25);
    uint32_t ch = (e & f) ^ (~e & g);
    uint32_t t1 = h + s1 + ch + k[i] + w[i];
    uint32_t s0 = sha256Rotr(a, 2) ^ sha256Rotr(a, 13) ^ sha256Rotr(a, 22);
    uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
    uint32_t t2 = s0 + maj;
    h = g;
    g = f;
    f = e;
    e = d + t1;
    d = c;
    c = b;
    b = a;
    a = t1 + t2;
  }
  ctx.state[0] += a;
  ctx.state[1] += b;
  ctx.state[2] += c;
  ctx.state[3] += d;
  ctx.state[4] += e;
  ctx.state[5] += f;
  ctx.state[6] += g;
  ctx.state[7] += h;
}

static inline void sha256Init(Sha256& ctx) {
  static const uint32_t initial[8] = {0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
                                      0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19};
  memcpy(ctx.state, initial, sizeof(initial));
  ctx.bitLength = 0;
  ctx.blockLength = 0;
}

static inline void sha256Update(Sha256& ctx, const uint8_t* data, size_t length) {
  for (size_t i = 0; i < length; i++) {
    ctx.block[ctx.blockLength++] = data[i];
    if (ctx.blockLength == 64) {
      sha256Compress(ctx, ctx.block);
      ctx.bitLength += 512;
      ctx.blockLength = 0;
    }
  }
}

static inline void sha256Final(Sha256& ctx, uint8_t digest[32]) {
  uint64_t totalBits = ctx.bitLength + (uint64_t)ctx.blockLength * 8;
  ctx.block[ctx.blockLength++] = 0x80;
  if (ctx.blockLength > 56) {
    while (ctx.blockLength < 64) ctx.block[ctx.blockLength++] = 0;
    sha256Compress(ctx, ctx.block);
    ctx.blockLength = 0;
  }
  while (ctx.blockLength < 56) ctx.block[ctx.blockLength++] = 0;
  for (int i = 7; i >= 0; i--) ctx.block[ctx.blockLength++] = (uint8_t)(totalBits >> (8 * i));
  sha256Compress(ctx, ctx.block);
  for (int i = 0; i < 8; i++) {
    digest[4 * i] = (uint8_t)(ctx.state[i] >> 24);
    digest[4 * i + 1] = (uint8_t)(ctx.state[i] >> 16);
    digest[4 * i + 2] = (uint8_t)(ctx.state[i] >> 8);
    digest[4 * i + 3] = (uint8_t)ctx.state[i];
  }
}

static inline void sha256(const uint8_t* data, size_t length, uint8_t digest[32]) {
  Sha256 ctx;
  sha256Init(ctx);
  sha256Update(ctx, data, length);
  sha256Final(ctx, digest);
}

// HMAC in two steps so a caller can stream the message without a buffer.
struct HmacSha256 {
  Sha256 inner;
  uint8_t outerKeyPad[64];
};

static inline void hmacSha256Init(HmacSha256& ctx, const uint8_t* key, size_t keyLength) {
  uint8_t keyBlock[64] = {0};
  if (keyLength > 64) {
    sha256(key, keyLength, keyBlock);
  } else {
    memcpy(keyBlock, key, keyLength);
  }
  uint8_t innerPad[64];
  for (int i = 0; i < 64; i++) {
    innerPad[i] = keyBlock[i] ^ 0x36;
    ctx.outerKeyPad[i] = keyBlock[i] ^ 0x5c;
  }
  sha256Init(ctx.inner);
  sha256Update(ctx.inner, innerPad, 64);
}

static inline void hmacSha256Update(HmacSha256& ctx, const uint8_t* data, size_t length) {
  sha256Update(ctx.inner, data, length);
}

static inline void hmacSha256Final(HmacSha256& ctx, uint8_t mac[32]) {
  uint8_t innerDigest[32];
  sha256Final(ctx.inner, innerDigest);
  Sha256 outer;
  sha256Init(outer);
  sha256Update(outer, ctx.outerKeyPad, 64);
  sha256Update(outer, innerDigest, 32);
  sha256Final(outer, mac);
}

static inline void hmacSha256(const uint8_t* key, size_t keyLength, const uint8_t* data,
                              size_t length, uint8_t mac[32]) {
  HmacSha256 ctx;
  hmacSha256Init(ctx, key, keyLength);
  hmacSha256Update(ctx, data, length);
  hmacSha256Final(ctx, mac);
}

// Compares without an early exit, so the time taken does not reveal how
// many leading bytes of a forged MAC were right.
static inline bool constantTimeEqual(const uint8_t* a, const uint8_t* b, size_t length) {
  uint8_t diff = 0;
  for (size_t i = 0; i < length; i++) diff |= (uint8_t)(a[i] ^ b[i]);
  return diff == 0;
}
