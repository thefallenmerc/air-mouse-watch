#pragma once
#include <Arduino.h>
#include <Wire.h>
#include "Pointer.h"
class Hardware {
 public:
  bool touchReady = false;
  bool read(uint8_t address, uint8_t reg, uint8_t *data, size_t n) {
    Wire.beginTransmission(address); Wire.write(reg);
    if (Wire.endTransmission(false) || Wire.requestFrom(address, n, true) != n) return false;
    for (size_t i = 0; i < n; ++i) data[i] = Wire.read(); return true;
  }
  void begin() {
    pinMode(0, INPUT_PULLUP); // BOOT button: active low.
    Wire.begin(15, 14, 400000); Wire.setTimeOut(5);
    pinMode(9, OUTPUT); digitalWrite(9, HIGH); delay(1); digitalWrite(9, LOW); delay(20); digitalWrite(9, HIGH); delay(50);
    pinMode(38, INPUT);
    // FT3168 can take longer than 50 ms to wake after a warm reset.
    for (int attempt = 0; attempt < 5 && !touchReady; ++attempt) {
      Wire.beginTransmission(0x38); Wire.write(0xa5); Wire.write(0); touchReady = Wire.endTransmission() == 0;
      if (!touchReady) delay(50);
    }
  }
  bool pairingButtonPressed() const { return digitalRead(0) == LOW; }

  air::Sample touch() {
    uint8_t d[5]; if (!read(0x38, 2, d, 5)) return {false, false, 0, 0};
    touchReady = true;
    int count = d[0] & 15;
    if (count > 1) return {false, false, 0, 0};
    return {true, count == 1, int((d[1] & 15) << 8 | d[2]), int((d[3] & 15) << 8 | d[4])};
  }
};
