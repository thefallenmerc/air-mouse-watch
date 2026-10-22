#pragma once
#include <esp_partition.h>
#include <esp_heap_caps.h>
#include "MediaFormat.h"

// Dedicated data partition: the former reserved banks keep their physical
// addresses, but are outside all app partitions so normal flash protection stays on.
class MediaStore {
  struct Header { uint32_t magic, generation, length, crc; };
  static constexpr uint32_t Magic = 0x314d4941, Base = 0, BankSize = 0x400000;
  const esp_partition_t *partition_ = nullptr;
  int active_ = -1, target_ = -1;
  uint32_t generation_ = 0, length_ = 0, expectedCrc_ = 0, received_ = 0, erased_ = 0, eraseEnd_ = 0;
  uint8_t *readBank(int bank, const Header &h) {
    if (h.magic != Magic || !h.length || h.length > air::MaxMedia) return nullptr;
    uint8_t *p = (uint8_t*)ps_malloc(h.length); if (!p) return nullptr;
    if (esp_partition_read(partition_, Base + bank * BankSize + sizeof(Header), p, h.length) != ESP_OK ||
        air::crc32(p, h.length) != h.crc || !air::validMedia(p, h.length)) { free(p); return nullptr; }
    return p;
  }
 public:
  uint8_t *media = nullptr; size_t size = 0;
  bool available = false;
  ~MediaStore() { free(media); }
  void begin() {
    partition_ = esp_partition_find_first(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "airmedia");
    available = partition_ && partition_->type == ESP_PARTITION_TYPE_DATA &&
      partition_->address == 0x1090000 && partition_->size == 2 * BankSize;
    if (!available) return;
    Header h[2] = {};
    for (int b = 0; b < 2; ++b) esp_partition_read(partition_, Base + b * BankSize, &h[b], sizeof(Header));
    int first = h[1].magic == Magic && h[1].generation > h[0].generation ? 1 : 0;
    for (int b : {first, 1 - first}) {
      uint8_t *p = readBank(b, h[b]);
      if (p) { media = p; size = h[b].length; active_ = b; generation_ = h[b].generation; break; }
    }
  }
  bool start(uint32_t length, uint32_t crc) {
    if (!available || !length || length > air::MaxMedia || target_ >= 0) return false;
    target_ = active_ == 0 ? 1 : 0; length_ = length; expectedCrc_ = crc; received_ = erased_ = 0;
    eraseEnd_ = (length + sizeof(Header) + 4095) & ~4095U; return true;
  }
  bool erasing() const { return target_ >= 0 && erased_ < eraseEnd_; }
  bool eraseStep() {
    if (!erasing()) return true;
    if (esp_partition_erase_range(partition_, Base + target_ * BankSize + erased_, 4096) != ESP_OK) { abort(); return false; }
    erased_ += 4096; return true;
  }
  bool append(uint32_t offset, const uint8_t *data, size_t n) {
    if (target_ < 0 || erasing() || offset != received_ || !n || n > length_ - received_) return false;
    if (esp_partition_write(partition_, Base + target_ * BankSize + sizeof(Header) + received_, data, n) != ESP_OK) return false;
    received_ += n; return true;
  }
  bool commit() {
    if (target_ < 0 || received_ != length_) return false;
    Header h = {Magic, generation_ + 1, length_, expectedCrc_};
    uint8_t *p = readBank(target_, h); if (!p) return false;
    // Header is the commit marker: interrupted transfers leave the old bank valid.
    if (esp_partition_write(partition_, Base + target_ * BankSize, &h, sizeof(h)) != ESP_OK) { free(p); return false; }
    free(media); media = p; size = length_; active_ = target_; generation_ = h.generation; abort(); return true;
  }
  void abort() { target_ = -1; received_ = erased_ = eraseEnd_ = 0; }
};
