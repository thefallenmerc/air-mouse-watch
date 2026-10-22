#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace air {
constexpr int Width = 410, Height = 502;
// Wire format: little-endian, IEEE754 floats. Shared with macOS Settings.swift.
#pragma pack(push, 1)
struct Settings {
  uint16_t version = 1, flags = 2; // bit 0 retired; touch, reverse scrolling
  float gyroGain = 14, touchGain = 1.4f, deadzone = 0.7f, smoothingMs = 24;
  uint16_t holdMs = 550, swipeMs = 420;
  uint8_t scrollSteps = 12, brightness = 100, axisX = 5, axisY = 3;
  uint16_t edgePercent = 16, spanPercent = 68;
  uint16_t dpiX = 1600, dpiY = 1600; // Relative sensitivity scale; 1600 = 1x (not physical optical DPI).
  void disableMotion() { flags &= ~uint16_t(1); } // Migrate legacy records/writes without changing the wire layout.
  bool valid() const {
    return version == 1 && flags <= 7 && std::isfinite(gyroGain) && gyroGain >= 1 && gyroGain <= 60 &&
      std::isfinite(touchGain) && touchGain >= .2f && touchGain <= 5 &&
      std::isfinite(deadzone) && deadzone >= 0 && deadzone <= 5 &&
      std::isfinite(smoothingMs) && smoothingMs >= 0 && smoothingMs <= 150 &&
      holdMs >= 250 && holdMs <= 1500 && swipeMs >= 150 && swipeMs <= 700 &&
      scrollSteps >= 1 && scrollSteps <= 80 && brightness >= 5 && axisX < 6 && axisY < 6 &&
      axisX % 3 != axisY % 3 && edgePercent >= 5 && edgePercent <= 25 && spanPercent >= 50 && spanPercent <= 90 &&
      dpiX >= 50 && dpiX <= 26000 && dpiX % 50 == 0 && dpiY >= 50 && dpiY <= 26000 && dpiY % 50 == 0;
  }
};
#pragma pack(pop)
static_assert(sizeof(Settings) == 36, "Settings wire layout changed");
struct Output { int x = 0, y = 0, wheel = 0, pan = 0; uint8_t click = 0; };
struct Sample { bool valid = true, down = false; int x = 0, y = 0; };

class Gesture {
  bool active_ = false, moved_ = false, consumed_ = false, pointer_ = false, edge_ = false;
  int sx_ = 0, sy_ = 0, px_ = 0, py_ = 0, x_ = 0, y_ = 0;
  uint32_t start_ = 0, lastValid_ = 0, upSince_ = 0;
  bool releasing_ = false;
  float rx_ = 0, ry_ = 0;
  Output delta(int x, int y, const Settings &c) {
    rx_ += x * c.touchGain; ry_ += y * c.touchGain;
    Output o; o.x = int(rx_); o.y = int(ry_); rx_ -= o.x; ry_ -= o.y; return o;
  }
 public:
  bool active() const { return active_; }
  void reset() { *this = Gesture(); }
  Output update(Sample s, uint32_t now, const Settings &c) {
    Output o;
    if (!(c.flags & 2)) { reset(); return o; }
    if (!s.valid) { if (active_ && now - lastValid_ > 100) reset(); return o; }
    lastValid_ = now;
    if (s.down && (s.x < 0 || s.x >= Width || s.y < 0 || s.y >= Height)) { reset(); return o; }
    if (s.down && !active_) {
      active_ = true; start_ = now; sx_ = px_ = x_ = s.x; sy_ = py_ = y_ = s.y;
      int ex = Width * c.edgePercent / 100, ey = Height * c.edgePercent / 100;
      edge_ = sx_ <= ex || sx_ >= Width - ex || sy_ <= ey || sy_ >= Height - ey;
      return o;
    }
    if (!active_) return o;
    uint32_t age = now - start_;
    if (s.down) {
      releasing_ = false; x_ = s.x; y_ = s.y;
      int dx = x_ - sx_, dy = y_ - sy_;
      if (dx * dx + dy * dy > 100) moved_ = true;
      if (consumed_) return o;
      if (!moved_ && age >= c.holdMs) { consumed_ = true; o.click = 2; return o; }
      // Edge contacts get a bounded decision window. Never move the cursor during a quick swipe.
      if (moved_ && (!edge_ || age > c.swipeMs)) {
        pointer_ = true; o = delta(x_ - px_, y_ - py_, c); px_ = x_; py_ = y_;
      }
      return o;
    }
    if (!releasing_) { releasing_ = true; upSince_ = now; return o; }
    if (now - upSince_ < 24) return o;
    age = upSince_ - start_;
    if (!consumed_) {
      int dx = x_ - sx_, dy = y_ - sy_;
      int ex = Width * c.edgePercent / 100, ey = Height * c.edgePercent / 100;
      bool horizontal = std::abs(dx) >= Width * c.spanPercent / 100 && std::abs(dy) < Height / 4 &&
        ((sx_ <= ex && x_ >= Width - ex) || (sx_ >= Width - ex && x_ <= ex));
      bool vertical = std::abs(dy) >= Height * c.spanPercent / 100 && std::abs(dx) < Width / 4 &&
        ((sy_ <= ey && y_ >= Height - ey) || (sy_ >= Height - ey && y_ <= ey));
      int sign = (c.flags & 4) ? -1 : 1;
      if (!pointer_ && edge_ && moved_ && age <= c.swipeMs && (horizontal || vertical)) {
        if (horizontal) o.pan = (dx > 0 ? 1 : -1) * c.scrollSteps * sign;
        else o.wheel = (dy > 0 ? -1 : 1) * c.scrollSteps * sign;
      } else if (!moved_) o.click = age >= c.holdMs ? 2 : 1;
      else if (!pointer_) o = delta(dx, dy, c);
    }
    reset(); return o;
  }
};

inline uint16_t u16(const uint8_t *p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
inline uint32_t u32(const uint8_t *p) { return uint32_t(u16(p)) | (uint32_t(u16(p + 2)) << 16); }
inline uint32_t crc32(const uint8_t *data, size_t n) {
  uint32_t c = ~0U; while (n--) { c ^= *data++; for (int i = 0; i < 8; ++i) c = (c >> 1) ^ (0xedb88320U & -(c & 1)); } return ~c;
}
}
