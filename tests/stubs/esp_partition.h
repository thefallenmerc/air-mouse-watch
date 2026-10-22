#pragma once
#include <cstddef>
#include <cstdint>
constexpr int ESP_OK=0,ESP_FAIL=-1;
constexpr int ESP_PARTITION_TYPE_DATA=1, ESP_PARTITION_SUBTYPE_ANY=255;
struct esp_partition_t { size_t size; uint32_t address; uint8_t type; };
const esp_partition_t *esp_partition_find_first(int,int,const char*);
int esp_partition_read(const esp_partition_t*,size_t,void*,size_t);
int esp_partition_write(const esp_partition_t*,size_t,const void*,size_t);
int esp_partition_erase_range(const esp_partition_t*,size_t,size_t);
