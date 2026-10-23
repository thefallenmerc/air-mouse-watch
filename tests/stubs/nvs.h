#pragma once
#include <cstddef>
using nvs_handle_t = unsigned;
constexpr int ESP_OK=0, ESP_ERR_NVS_NOT_FOUND=4354, NVS_READONLY=0;
int nvs_open(const char*, int, nvs_handle_t*);
int nvs_get_blob(nvs_handle_t, const char*, void*, size_t*);
void nvs_close(nvs_handle_t);
