#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "config.h"
#include "touch_packet.h"

inline bool initializeTouch() {
  pinMode(TOUCH_IRQ_PIN, INPUT);
  pinMode(TOUCH_RESET_PIN, OUTPUT);
  digitalWrite(TOUCH_RESET_PIN, LOW);
  delay(10);
  digitalWrite(TOUCH_RESET_PIN, HIGH);
  delay(500);
  if (!Wire.begin(TOUCH_SDA_PIN, TOUCH_SCL_PIN, 400000)) return false;
  Wire.setTimeOut(20);
  Wire.beginTransmission(TOUCH_I2C_ADDRESS);
  return Wire.endTransmission() == 0;
}

inline TouchState readTouch(int16_t &x, int16_t &y) {
  // Poll instead of depending on interrupt pulse timing.
  Wire.beginTransmission(TOUCH_I2C_ADDRESS);
  Wire.write(0x02);
  if (Wire.endTransmission(false) != 0) return TouchState::Invalid;
  uint8_t data[5];
  if (Wire.requestFrom(TOUCH_I2C_ADDRESS, sizeof(data)) != sizeof(data)) {
    while (Wire.available()) Wire.read();
    return TouchState::Invalid;
  }
  for (uint8_t &value : data) value = Wire.read();
  return decodeTouchPacket(data, x, y);
}
