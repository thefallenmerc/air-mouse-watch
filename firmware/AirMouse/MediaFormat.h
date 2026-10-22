#pragma once
#include "Pointer.h"
namespace air {
constexpr size_t MaxMedia = 3 * 1024 * 1024;
// AMF1: 205x251, <=120 frames. Each frame: delay:u16, byteCount:u32, (run:u16,color:u16)*.
inline bool validMedia(const uint8_t *p, size_t size) {
  if (size < 12 || size > MaxMedia || memcmp(p, "AMF1", 4) || u16(p+4) != 205 || u16(p+6) != 251 ||
      u16(p+8) < 1 || u16(p+8) > 120 || u16(p+10)) return false;
  size_t pos = 12;
  for (int frame = 0; frame < u16(p+8); ++frame) {
    if (size - pos < 6) return false;
    auto delay = u16(p+pos); auto count = u32(p+pos+2); pos += 6;
    if (delay < 67 || delay > 60000 || count % 4 || count > size - pos) return false;
    uint32_t pixels = 0;
    for (size_t end = pos + count; pos < end; pos += 4) {
      uint16_t run = u16(p+pos); if (!run || pixels + run > 205 * 251) return false; pixels += run;
    }
    if (pixels != 205 * 251) return false;
  }
  return pos == size;
}
}
