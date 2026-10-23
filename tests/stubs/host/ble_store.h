#pragma once
#include <cstdint>
constexpr int CONFIG_BT_NIMBLE_MAX_BONDS=3;
constexpr int BLE_HS_ESTORE_FAIL=27, BLE_HS_ENOENT=5;
constexpr int BLE_STORE_EVENT_OVERFLOW=1, BLE_STORE_EVENT_FULL=2;
constexpr int BLE_STORE_OBJ_TYPE_PEER_ADDR=6, BLE_STORE_OBJ_TYPE_CCCD=3;
struct ble_addr_t { uint8_t type; uint8_t val[6]; };
struct ble_store_value_rpa_rec { ble_addr_t peer_rpa_addr, peer_addr; };
struct ble_store_key_rpa_rec { ble_addr_t peer_rpa_addr; uint8_t idx; };
union ble_store_value { ble_store_value_rpa_rec rpa_rec; };
struct ble_store_status_event {
  int event_code;
  union {
    struct { int obj_type; const ble_store_value *value; } overflow;
    struct { int obj_type; uint16_t conn_handle; } full;
  };
};
int ble_store_delete_rpa_rec(const ble_store_key_rpa_rec*);
int ble_store_util_status_rr(ble_store_status_event*, void*);
