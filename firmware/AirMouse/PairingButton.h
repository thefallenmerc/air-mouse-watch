#pragma once
#include <cstdint>

namespace air {
// BOOT is a strapping pin: wait for a stable RELEASE before requesting reboot.
// Ignore a button already held at startup until it has first been released.
class PairingButton {
  bool raw_ = false, down_ = false, initialized_ = false, enabled_ = false, armed_ = false;
  uint32_t changed_ = 0, pressedAt_ = 0;
 public:
  static constexpr uint32_t DebounceMs = 30, HoldMs = 3000;
  bool down() const { return enabled_ && down_; }
  bool armed() const { return armed_; }
  bool update(bool pressed, uint32_t now) {
    if (!initialized_) { initialized_ = true; raw_ = down_ = pressed; changed_ = now; return false; }
    if (pressed != raw_) { raw_ = pressed; changed_ = now; }
    if (now - changed_ < DebounceMs) return false;
    if (!enabled_) { if (!raw_) { enabled_ = true; down_ = false; } return false; }
    if (raw_ != down_) {
      down_ = raw_;
      if (down_) { pressedAt_ = now; armed_ = false; }
      else { bool fire = armed_; armed_ = false; return fire; }
    }
    if (down_ && now - pressedAt_ >= HoldMs) armed_ = true;
    return false;
  }
};
}
