#pragma once
#include <stdint.h>

enum class TouchState { Invalid, Released, Pressed };

// FT6336U registers 0x02..0x06: count, X high/low, Y high/low.
inline TouchState decodeTouchPacket(const uint8_t *data, int16_t &x, int16_t &y) {
  const uint8_t count = data[0] & 0x0f;
  if (count == 0) return TouchState::Released;
  if (count > 2) return TouchState::Invalid;
  const uint8_t event = data[1] >> 6;
  if (event == 1) return TouchState::Released;
  if (event == 3) return TouchState::Invalid;
  const uint16_t rawX = ((data[1] & 0x0f) << 8) | data[2];
  const uint16_t rawY = ((data[3] & 0x0f) << 8) | data[4];
  if (rawX >= 240 || rawY >= 320) return TouchState::Invalid;
  // Match TFT rotation 1: portrait 240x320 -> landscape 320x240.
  x = rawY;
  y = 239 - rawX;
  return TouchState::Pressed;
}
