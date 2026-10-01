#include <stdio.h>
#include <string.h>
#include "../RailyPinsP1/hmac_sha256.h"
#include "../RailyPinsP1/seal_format.h"

static int failures = 0;
static void expect(bool condition, const char* name) {
  printf("%s %s\n", condition ? "PASS" : "FAIL", name);
  if (!condition) failures++;
}

static size_t fromHex(const char* hex, uint8_t* out) {
  size_t n = strlen(hex) / 2;
  for (size_t i = 0; i < n; i++) {
    unsigned value = 0;
    sscanf(hex + 2 * i, "%2x", &value);
    out[i] = (uint8_t)value;
  }
  return n;
}

static bool hexEquals(const uint8_t* bytes, size_t length, const char* hex) {
  uint8_t expected[128];
  if (strlen(hex) / 2 > sizeof(expected)) return false;
  return fromHex(hex, expected) == length && memcmp(bytes, expected, length) == 0;
}

int main() {
  uint8_t digest[32];
  sha256((const uint8_t*)"", 0, digest);
  expect(hexEquals(digest, 32, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"),
         "sha256 empty");
  sha256((const uint8_t*)"abc", 3, digest);
  expect(hexEquals(digest, 32, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"),
         "sha256 abc");
  const char* twoBlocks = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
  sha256((const uint8_t*)twoBlocks, strlen(twoBlocks), digest);
  expect(hexEquals(digest, 32, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"),
         "sha256 two blocks");
  uint8_t million[1000];
  memset(million, 'a', sizeof(million));
  Sha256 ctx;
  sha256Init(ctx);
  for (int i = 0; i < 1000; i++) sha256Update(ctx, million, sizeof(million));
  sha256Final(ctx, digest);
  expect(hexEquals(digest, 32, "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"),
         "sha256 one million a (streamed)");

  // RFC 4231 test cases 1, 2, 3, 4, 6, 7.
  uint8_t key[131];
  uint8_t data[160];
  uint8_t mac[32];
  memset(key, 0x0b, 20);
  hmacSha256(key, 20, (const uint8_t*)"Hi There", 8, mac);
  expect(hexEquals(mac, 32, "b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7"),
         "rfc4231 case 1");
  hmacSha256((const uint8_t*)"Jefe", 4, (const uint8_t*)"what do ya want for nothing?", 28, mac);
  expect(hexEquals(mac, 32, "5bdcc146bf60754e6a042426089575c75a003f089d2739839dec58b964ec3843"),
         "rfc4231 case 2");
  memset(key, 0xaa, 20);
  memset(data, 0xdd, 50);
  hmacSha256(key, 20, data, 50, mac);
  expect(hexEquals(mac, 32, "773ea91e36800e46854db8ebd09181a72959098b3ef8c122d9635514ced565fe"),
         "rfc4231 case 3");
  for (int i = 0; i < 25; i++) key[i] = (uint8_t)(i + 1);
  memset(data, 0xcd, 50);
  hmacSha256(key, 25, data, 50, mac);
  expect(hexEquals(mac, 32, "82558a389a443c0ea4cc819899f2083a85f0faa3e578f8077a2e3ff46729665b"),
         "rfc4231 case 4");
  memset(key, 0xaa, 131);
  const char* case6 = "Test Using Larger Than Block-Size Key - Hash Key First";
  hmacSha256(key, 131, (const uint8_t*)case6, strlen(case6), mac);
  expect(hexEquals(mac, 32, "60e431591ee0b67f0d8a26aacbf5b77f8e0bc6213728c5140546040f0ee37f54"),
         "rfc4231 case 6 (long key)");
  const char* case7 =
      "This is a test using a larger than block-size key and a larger than block-size data. "
      "The key needs to be hashed before being used by the HMAC algorithm.";
  hmacSha256(key, 131, (const uint8_t*)case7, strlen(case7), mac);
  expect(hexEquals(mac, 32, "9b09ffa71b942fcb27635fbcd5b0e944bfdc63644f0713938a7f51535c3a35e2"),
         "rfc4231 case 7 (long key and data)");

  // RFC 5869 test case 1 (HKDF-SHA256).
  uint8_t ikm[22];
  memset(ikm, 0x0b, sizeof(ikm));
  uint8_t salt[13];
  for (int i = 0; i < 13; i++) salt[i] = (uint8_t)i;
  uint8_t info[10];
  for (int i = 0; i < 10; i++) info[i] = (uint8_t)(0xf0 + i);
  uint8_t okm[42];
  hkdfSha256(salt, sizeof(salt), ikm, sizeof(ikm), info, sizeof(info), okm, sizeof(okm));
  expect(hexEquals(okm, 42,
                   "3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf34007208d5b8"
                   "87185865"),
         "rfc5869 case 1");
  // RFC 5869 test case 3: empty salt and info.
  hkdfSha256(NULL, 0, ikm, sizeof(ikm), NULL, 0, okm, sizeof(okm));
  expect(hexEquals(okm, 42,
                   "8da4e775a563c18f715f802a063c5a31b8a11f5c5ee1879ec3454e5f3c738d2d9d201395faa4"
                   "b61a96c8"),
         "rfc5869 case 3 (empty salt)");

  uint8_t a[4] = {1, 2, 3, 4};
  uint8_t b[4] = {1, 2, 3, 5};
  expect(constantTimeEqual(a, a, 4), "constant-time equal matches");
  expect(!constantTimeEqual(a, b, 4), "constant-time equal rejects last-byte change");
  return failures ? 1 : 0;
}
