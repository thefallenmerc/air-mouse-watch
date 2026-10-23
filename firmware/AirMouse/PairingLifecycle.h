#pragma once
#include <cstdint>

namespace air {
// Owned by loop(), fed snapshots from BLE callbacks. An attempt that never
// becomes encrypted must release the radio so the host can discover it again.
class PairingLifecycle {
  uint32_t epoch_ = 0, started_ = 0;
  bool tracking_ = false, disconnecting_ = false;
 public:
  bool update(uint32_t epoch, bool connected, bool encrypted, bool failed, uint32_t now) {
    if (!connected) { tracking_ = false; return false; }
    if (!tracking_ || epoch != epoch_) {
      tracking_ = true; disconnecting_ = false; epoch_ = epoch; started_ = now;
    }
    if (encrypted || disconnecting_) return false;
    if (!failed && uint32_t(now - started_) < 45000) return false;
    disconnecting_ = true;
    return true;
  }
};
}
