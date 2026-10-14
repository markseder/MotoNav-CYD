#include <assert.h>
#include <initializer_list>
#include "../../firmware/motonav/MotoNav_CYD_V1_1_2_Stable/touch_packet.h"

int main() {
  int16_t x = -1, y = -1;
  uint8_t packet[5] = {};
  assert(decodeTouchPacket(packet, x, y) == TouchState::Released);
  // All portrait corners must become the correct landscape corners.
  for (uint16_t rawX : {uint16_t(0), uint16_t(239)}) {
    for (uint16_t rawY : {uint16_t(0), uint16_t(319)}) {
      packet[0] = 1;
      packet[1] = 0x80 | (rawX >> 8); // contact event
      packet[2] = rawX & 0xff;
      packet[3] = 0x10 | (rawY >> 8); // touch ID must not enter coordinate
      packet[4] = rawY & 0xff;
      assert(decodeTouchPacket(packet, x, y) == TouchState::Pressed);
      assert(x == rawY && y == 239 - rawX);
    }
  }
  packet[1] = 0x40; // explicit lift event
  assert(decodeTouchPacket(packet, x, y) == TouchState::Released);
  packet[1] = 0xc0; // reserved event
  assert(decodeTouchPacket(packet, x, y) == TouchState::Invalid);
  packet[1] = 0;
  packet[2] = 240; // out-of-range X
  assert(decodeTouchPacket(packet, x, y) == TouchState::Invalid);
  packet[2] = 0;
  packet[3] = 1;
  packet[4] = 64; // out-of-range Y (320)
  assert(decodeTouchPacket(packet, x, y) == TouchState::Invalid);
  packet[0] = 3; // corrupt point count
  assert(decodeTouchPacket(packet, x, y) == TouchState::Invalid);
  packet[0] = 2; // two touches: use the first point
  packet[3] = packet[4] = 0;
  assert(decodeTouchPacket(packet, x, y) == TouchState::Pressed);
}
