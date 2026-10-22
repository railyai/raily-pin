#pragma once

#include "ship_mode.h"

// The Bluetooth name of the device: the entry a person sees in iOS
// Settings → Bluetooth, and the name in the scan response. It is the model
// name (owner, 2026-10-22): «Raily Air» on the Keyring Air image
// (build_firmware.sh --air, -DRAILY_SHIP_MODE=1), «Raily P1» on every other
// image (the P1 and the DIY kit; --diy builds no ship mode). It is the same
// switch device_info.h reads for its "air":1 key (RAILY_SHIP_MODE != 0), so
// the name and the app's `PinDeviceInfo.isAir` never disagree.
//
// Firmware before 0.4.3 advertised «Raily Device» (0.2.19-qa to 0.4.2) and
// «Raily Pin P1» (before 0.2.19-qa); the app and the Mac tools accept all
// four names, and the service UUID and the manufacturer marker stay the
// primary match. Only one name string is built into an image: the Mac OTA
// tool finds FW_VERSION by the name that follows it
// (hardware/tools/pin_ble_ota.py APP_NAMES_BIN), so the sketch defines
// `static const char DEVICE_NAME[]` from this literal right beside
// FW_VERSION, where it always sat.
//
// The name sits alone in the scan response (startAdv): 2 bytes of AD
// header plus the characters, of 31 (DEVICE_NAME_PAYLOAD_MAX). The
// advertisement itself carries flags (3), the manufacturer marker (7) and
// the 128-bit service UUID (18) = 28 of 31 and does not depend on the name.
#if RAILY_SHIP_MODE
#define RAILY_DEVICE_NAME "Raily Air"
#else
#define RAILY_DEVICE_NAME "Raily P1"
#endif

// A BLE legacy advertisement and its scan response are 31 bytes each.
static const unsigned DEVICE_NAME_PAYLOAD_MAX = 31;
// The AD structure of a complete local name: a length byte, a type byte, the name.
static const unsigned DEVICE_NAME_AD_OVERHEAD = 2;
