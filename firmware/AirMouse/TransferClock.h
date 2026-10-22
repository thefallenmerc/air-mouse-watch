#pragma once
#include <cstdint>
namespace air {
class TransferClock {
  uint32_t last_ = 0;
  bool active_ = false;
 public:
  void touch(uint32_t now) { last_ = now; active_ = true; }
  void clear() { active_ = false; }
  bool expired(uint32_t now) const {
    // A loop timestamp captured BEFORE a flash write is not an idle timeout.
    // Signed elapsed time also handles millis() wrap for these short intervals.
    return active_ && int32_t(now - last_) > 30000;
  }
};
}
