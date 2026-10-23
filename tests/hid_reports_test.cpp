#include "../firmware/AirMouse/HidReports.h"
#include <cassert>
#include <iostream>
int main() {
  BLEHIDDevice hid;
  auto mouse=secureHidReport(hid,1,true);
  assert(mouse->uuid==0x2a4d);
  assert(mouse->flags & BLECharacteristic::PROPERTY_READ_ENC);
  assert(mouse->flags & BLECharacteristic::PROPERTY_NOTIFY);
  assert(!(mouse->flags & BLECharacteristic::PROPERTY_WRITE));
  assert(mouse->reference->uuid.value==0x2908);
  assert(mouse->reference->permissions==ESP_GATT_PERM_READ);
  assert((mouse->reference->value==std::vector<uint8_t>{1,1}));
  for(uint8_t id=2;id<=4;++id){
    auto feature=secureHidReport(hid,id,false);
    assert(feature->flags & BLECharacteristic::PROPERTY_READ_ENC);
    assert(feature->flags & BLECharacteristic::PROPERTY_WRITE_ENC);
    assert(!(feature->flags & BLECharacteristic::PROPERTY_NOTIFY));
    assert((feature->reference->value==std::vector<uint8_t>{id,3}));
  }
  std::cout<<"HID data requires encryption without a companion; report metadata remains discoverable.\n";
}
