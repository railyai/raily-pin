#pragma once

#include <stdint.h>

// Per-board microphone target for voice from the pin
// (docs/pins/voice-from-pin.md §2). The voice logic (adpcm.h,
// voice_frame.h, voice_gesture.h, voice_ring.h) has no pins and no GPIO;
// everything board-specific lives here, so the next board is one new block.
//
// Pins are absolute nRF52840 pin numbers (port * 32 + pin: P1.00 = 32), not
// Arduino D numbers, and the driver (V2) writes PDM PSEL from this struct
// only, never from a variant's PIN_PDM_*: the P1 build stays on the
// non-Sense FQBN (Seeeduino:nrf52:xiaonRF52840), whose variant names no PDM
// pins, and the Plus variant names the module's Sense-mic pins, which on the
// touch-display board record silence.
//
// No entry has captured audio on our hardware yet (hardwareVerified false):
// V0a proves the P1 values, the touch board's own bench run proves its.
// Pure, host-tested in hardware/firmware/tests/test_voice_board.cpp.

struct VoicePdmConfig {
  bool present;           // false: the board has no mic we drive; voice stays off
  uint8_t clkPin;         // PDM clock out
  uint8_t dataPin;        // PDM data in
  uint8_t powerPin;       // mic power enable, VOICE_PDM_NO_PIN when always on
  bool powerActiveHigh;
  uint32_t pdmClkCtrl;    // NRF_PDM->PDMCLKCTRL
  uint32_t clockHz;       // the PDM clock that value selects
  uint8_t ratio;          // NRF_PDM->RATIO decimation, 64 or 80
  uint8_t gainRegister;   // GAINL/GAINR: 0x00 = -20 dB, 0x28 = 0 dB, 0x50 = +20 dB, 0.5 dB steps
  bool leftFalling;       // mono, the left channel (data sampled on the falling edge)
  bool hardwareVerified;  // captured clean audio with these values on our own board
};

static const uint8_t VOICE_PDM_NO_PIN = 0xFF;
static const uint32_t VOICE_PDM_CLKCTRL_1280K = 0x0A000000UL;  // 32 MHz / 25
static const uint8_t VOICE_PDM_GAIN_0DB = 0x28;

// The START frame's gain byte (0.5 dB units) for a GAIN register value.
static inline int8_t voicePdmGainHalfDb(uint8_t gainRegister) {
  return (int8_t)((int16_t)gainRegister - VOICE_PDM_GAIN_0DB);
}

static inline uint32_t voicePdmSampleRate(const VoicePdmConfig& config) {
  return config.ratio == 0 ? 0 : config.clockHz / config.ratio;
}

// P1: Seeed XIAO nRF52840 Sense, onboard MSM261D3526H1CPM PDM mic. Pins from
// the Seeed core's Sense variant (D19 = P1.10 MIC_PWR, D20 = P1.00 PDM_CLK,
// D21 = P0.16 PDM_DATA); clock, ratio, mode and power polarity from
// Seeed_Arduino_Mic's nRF52840 backend at 16 kHz (1.280 MHz, RATIO 80, mono
// left-falling, power pin driven HIGH). Gain: the doc's +20 dB start value,
// tuned on the bench in V0a; the library's own default is the raw register
// 20 (0x14, -10 dB).
static const VoicePdmConfig VOICE_PDM_XIAO_SENSE = {
    true, 32, 16, 42, true, VOICE_PDM_CLKCTRL_1280K, 1280000, 80, 0x50, true, false,
};

// Next board: Seeed XIAO 1.47" touch display (nRF52840 Plus). UNVERIFIED ON
// HARDWARE: from the DIY v2 R&D pack (diy-v2-xiao147/VOICE.md, read
// 2026-10-09) and the core's Plus variant. Its own mic, a ZTS6131 PDM on the
// screen face: CLK D0 = P0.02, DATA D1 = P0.03, SEL to GND (left channel),
// VDD hard-wired to 3V3, so there is no power pin and the privacy rule
// becomes «no PDM clock unless held». Seeed's demo for the board remaps the
// pins explicitly (PDM.setPins(D1, D0, -1)) and calls PDM.setGain(30), the
// raw GAIN register (0x1E, -5 dB); the ZTS6131 has no datasheet we found,
// so the gain is retuned on the bench. The mic pins are not the NFC pins
// (P0.09/P0.10 = D14/D15, which need UICR NFCPINS before they are GPIO).
// Which input starts a talk there (the USR1 button or the touch orb) is
// the owner's call; voice_gesture.h takes either.
static const VoicePdmConfig VOICE_PDM_XIAO147 = {
    true, 2, 3, VOICE_PDM_NO_PIN, true, VOICE_PDM_CLKCTRL_1280K, 1280000, 80, 0x1E, true, false,
};

#if defined(RAILY_BOARD_XIAO147) && RAILY_BOARD_XIAO147  // the planned --diy2 build flag
static const VoicePdmConfig& VOICE_PDM_BOARD = VOICE_PDM_XIAO147;
#else
static const VoicePdmConfig& VOICE_PDM_BOARD = VOICE_PDM_XIAO_SENSE;
#endif
