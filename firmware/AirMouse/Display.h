#pragma once
#include <Arduino_GFX_Library.h>
#include "MediaStore.h"
class Display {
  Arduino_ESP32QSPI bus_{12,11,4,5,6,7};
  Arduino_CO5300 panel_{&bus_,8,0,410,502,22,0,0,0};
  uint16_t *pixels_ = nullptr;
  size_t pos_ = 12;
  int frame_ = 0, row_ = 502;
  uint32_t nextFrame_ = 0;
  bool ready_ = false;
  String previousStatus_;
 public:
  void begin(uint8_t brightness) {
    ready_ = panel_.begin(); if (!ready_) return;
    pixels_ = (uint16_t*)ps_malloc(410 * 502 * 2);
    panel_.setBrightness(brightness); panel_.fillScreen(0);
  }
  void brightness(uint8_t value) { if (ready_) panel_.setBrightness(value); }
  void reset() { previousStatus_ = ""; pos_ = 12; frame_ = 0; row_ = 502; nextFrame_ = 0; }
  void status(const char *message) {
    if (!ready_ || previousStatus_ == message) return;
    previousStatus_ = message;
    panel_.fillScreen(0); panel_.setTextColor(0xa6ff); panel_.setTextSize(3);
    panel_.setCursor(42, 180); panel_.print("AIR MOUSE");
    panel_.setTextSize(2); panel_.setCursor(24, 240); panel_.print(message);
  }
  void tick(MediaStore &store, uint32_t now) {
    if (!ready_ || !pixels_ || !store.media) return;
    if (row_ < 502) {
      int rows = std::min(16, 502 - row_);
      panel_.draw16bitRGBBitmap(0, row_, pixels_ + row_ * 410, 410, rows); row_ += rows;
      return;
    }
    if (int32_t(now - nextFrame_) < 0) return;
    const uint8_t *p = store.media;
    if (frame_ >= air::u16(p + 8)) { frame_ = 0; pos_ = 12; }
    uint16_t duration = air::u16(p + pos_); uint32_t bytes = air::u32(p + pos_ + 2); pos_ += 6;
    size_t pixel = 0;
    for (size_t end = pos_ + bytes; pos_ < end; pos_ += 4) {
      auto run = air::u16(p + pos_); auto color = air::u16(p + pos_ + 2);
      while (run--) {
        size_t y = pixel / 205 * 2, x = pixel % 205 * 2; ++pixel;
        pixels_[y * 410 + x] = pixels_[y * 410 + x + 1] = color;
        pixels_[(y + 1) * 410 + x] = pixels_[(y + 1) * 410 + x + 1] = color;
      }
    }
    ++frame_; row_ = 0; nextFrame_ = now + duration;
    if (air::u16(p + 8) == 1) nextFrame_ = now + 60000; // Static images do not repaint continuously.
  }
};
