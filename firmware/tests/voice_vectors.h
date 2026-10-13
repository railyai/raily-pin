// Test-only helpers for the voice golden vectors (tests/vectors/adpcm_*.bin).
//
// Each case <name> has three files:
//   adpcm_<name>_pcm.bin      the PCM the pin encodes: s16le, 16 kHz mono
//   adpcm_<name>_frames.bin   the voice_stream notifications it sends, as
//                             records: u16le length, then the payload
//                             (PRESS 4 B, START 10 B, AUDIO 168 B, END 9 B)
//   adpcm_<name>_decoded.bin  what a decoder makes of every AUDIO frame, in
//                             order, each decoded from its own header: s16le
// The Swift decoder (V3a) and hardware/tools/voice_capture.py must turn the
// frames file into the decoded file, bit for bit.
#pragma once

#include <stdint.h>
#include <stdio.h>

#include <string>
#include <vector>

static const char* const VOICE_VECTOR_CASES[] = {"sine", "silence", "step_clip", "sweep", "stream"};
static const int VOICE_VECTOR_CASE_COUNT = 5;

static inline std::string voiceVectorPath(const std::string& dir, const std::string& name, const char* part) {
  return dir + "/adpcm_" + name + "_" + part + ".bin";
}

static inline bool voiceReadFile(const std::string& path, std::vector<uint8_t>* out) {
  FILE* file = fopen(path.c_str(), "rb");
  if (!file) return false;
  out->clear();
  uint8_t buffer[4096];
  size_t got;
  while ((got = fread(buffer, 1, sizeof(buffer), file)) > 0) out->insert(out->end(), buffer, buffer + got);
  fclose(file);
  return true;
}

static inline bool voiceWriteFile(const std::string& path, const std::vector<uint8_t>& bytes) {
  FILE* file = fopen(path.c_str(), "wb");
  if (!file) return false;
  const bool ok = bytes.empty() || fwrite(bytes.data(), 1, bytes.size(), file) == bytes.size();
  return fclose(file) == 0 && ok;
}

static inline std::vector<uint8_t> voicePcmBytes(const std::vector<int16_t>& pcm) {
  std::vector<uint8_t> out;
  for (size_t i = 0; i < pcm.size(); i++) {
    const uint16_t value = (uint16_t)pcm[i];
    out.push_back((uint8_t)(value & 0xFF));
    out.push_back((uint8_t)(value >> 8));
  }
  return out;
}

static inline bool voicePcmFromBytes(const std::vector<uint8_t>& bytes, std::vector<int16_t>* out) {
  if (bytes.size() % 2 != 0) return false;
  out->clear();
  for (size_t i = 0; i < bytes.size(); i += 2) out->push_back((int16_t)(uint16_t)(bytes[i] | (bytes[i + 1] << 8)));
  return true;
}

static inline void voiceAppendRecord(std::vector<uint8_t>* records, const uint8_t* payload, uint16_t length) {
  records->push_back((uint8_t)(length & 0xFF));
  records->push_back((uint8_t)(length >> 8));
  records->insert(records->end(), payload, payload + length);
}

static inline bool voiceSplitRecords(const std::vector<uint8_t>& records, std::vector<std::vector<uint8_t> >* out) {
  out->clear();
  size_t at = 0;
  while (at < records.size()) {
    if (records.size() - at < 2) return false;
    const size_t length = (size_t)(records[at] | (records[at + 1] << 8));
    at += 2;
    if (records.size() - at < length) return false;
    out->push_back(std::vector<uint8_t>(records.begin() + at, records.begin() + at + length));
    at += length;
  }
  return true;
}
