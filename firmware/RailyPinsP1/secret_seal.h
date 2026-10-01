#pragma once

#include <stddef.h>
#include <stdint.h>

// Seals device_id ‖ S to the server key compiled in seal_keys.h (format:
// seal_format.h). Runs on the CryptoCell; call from loop(), never from a
// BLE callback (the ECDH takes tens of milliseconds). Returns the blob
// length, or 0 on failure.
size_t sealDeviceSecret(const char* deviceId, const uint8_t secret[16], uint8_t* blob,
                        size_t capacity);
