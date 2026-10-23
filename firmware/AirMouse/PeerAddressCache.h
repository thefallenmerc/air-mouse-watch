#pragma once
#include <host/ble_store.h>
#include <nvs.h>
#include <cstdio>
#include <cstring>

namespace air {
// Arduino-ESP32 3.3.10 / ESP-IDF 5.5.4: the RPA store's finder does not
// implement wildcard reads/deletes. Consequently ble_store_clear() leaves its
// records behind, and overflow's default handler unpairs even the current host.
// Enumerate ONLY this non-secret address cache from the pinned NVS schema;
// delete via NimBLE so its in-memory table and persistent table stay in sync.
class PeerAddressCache {
  static constexpr int capacity = CONFIG_BT_NIMBLE_MAX_BONDS;
  static bool equal(const ble_addr_t &a, const ble_addr_t &b) {
    return a.type == b.type && std::memcmp(a.val, b.val, sizeof(a.val)) == 0;
  }
  static int snapshot(ble_store_value_rpa_rec (&records)[capacity], int &count) {
    count = 0;
    nvs_handle_t handle;
    const auto opened = nvs_open("nimble_bond", NVS_READONLY, &handle);
    if (opened == ESP_ERR_NVS_NOT_FOUND) return 0;
    if (opened != ESP_OK) return BLE_HS_ESTORE_FAIL;
    int result = 0;
    for (int i = 1; i <= capacity; ++i) {
      char key[16]; std::snprintf(key, sizeof(key), "rpa_rec_%d", i);
      ble_store_value_rpa_rec record{}; size_t size = sizeof(record);
      const auto rc = nvs_get_blob(handle, key, &record, &size);
      if (rc == ESP_ERR_NVS_NOT_FOUND) continue; // Deletion leaves index holes.
      if (rc != ESP_OK || size != sizeof(record)) { result = BLE_HS_ESTORE_FAIL; break; }
      records[count++] = record;
    }
    nvs_close(handle);
    return result;
  }
  static int erase(const ble_store_value_rpa_rec &record) {
    ble_store_key_rpa_rec key{}; key.peer_rpa_addr = record.peer_rpa_addr;
    return ble_store_delete_rpa_rec(&key);
  }
 public:
  static int clear() {
    ble_store_value_rpa_rec records[capacity]{}; int count;
    int rc = snapshot(records, count);
    if (rc != 0) return rc;
    for (int i = 0; i < count; ++i) {
      rc = erase(records[i]);
      if (rc != 0) return rc; // Never claim a completed reset on persistence failure.
    }
    return 0;
  }
  static int reclaim(const ble_store_value_rpa_rec &incoming) {
    ble_store_value_rpa_rec records[capacity]{}; int count;
    int rc = snapshot(records, count);
    if (rc != 0) return rc;
    // Prefer replacing an older private address for the same host. Otherwise
    // reclaim one alias, keeping all encryption keys and HID subscriptions.
    for (int pass = 0; pass < 2; ++pass) {
      for (int i = 0; i < count; ++i) {
        if (equal(records[i].peer_rpa_addr, incoming.peer_rpa_addr)) continue;
        if (pass == 0 && !equal(records[i].peer_addr, incoming.peer_addr)) continue;
        return erase(records[i]); // Success frees exactly one slot for NimBLE's retry.
      }
    }
    return BLE_HS_ESTORE_FAIL; // No progress: do not loop or unpair the active host.
  }
  static int onStatus(ble_store_status_event *event, void *arg) {
    if (event->event_code == BLE_STORE_EVENT_OVERFLOW &&
        event->overflow.obj_type == BLE_STORE_OBJ_TYPE_PEER_ADDR) {
      return reclaim(event->overflow.value->rpa_rec);
    }
    return ble_store_util_status_rr(event, arg);
  }
};
}
