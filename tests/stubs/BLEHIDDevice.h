#pragma once
#include <cstdint>
#include <vector>
#include <memory>
constexpr uint16_t ESP_GATT_PERM_READ=1, ESP_GATT_PERM_READ_ENCRYPTED=4, ESP_GATT_PERM_WRITE_ENCRYPTED=8;
struct BLEUUID { uint16_t value; explicit BLEUUID(uint16_t v):value(v){} };
struct BLEDescriptor {
  BLEUUID uuid; uint16_t permissions=0; std::vector<uint8_t> value;
  explicit BLEDescriptor(BLEUUID u):uuid(u){}
  void setAccessPermissions(uint16_t p){permissions=p;}
  void setValue(const uint8_t *p,size_t n){value.assign(p,p+n);}
};
struct BLECharacteristic {
  static constexpr uint32_t PROPERTY_READ=1, PROPERTY_READ_ENC=2, PROPERTY_NOTIFY=4, PROPERTY_WRITE=8, PROPERTY_WRITE_ENC=16;
  uint16_t uuid; uint32_t flags; std::unique_ptr<BLEDescriptor> reference;
  BLECharacteristic(uint16_t u,uint32_t f):uuid(u),flags(f){}
  // Deliberately ignore this on the NimBLE stub, like the actual pinned library.
  void setAccessPermissions(uint16_t){}
  void addDescriptor(BLEDescriptor *p){reference.reset(p);}
};
struct BLEService {
  std::vector<std::unique_ptr<BLECharacteristic>> reports;
  BLECharacteristic *createCharacteristic(uint16_t u,uint32_t f){reports.emplace_back(new BLECharacteristic(u,f));return reports.back().get();}
};
struct BLEHIDDevice { BLEService service; BLEService *hidService(){return &service;} };
