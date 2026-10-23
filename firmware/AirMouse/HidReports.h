#pragma once
#include <BLEHIDDevice.h>
#include <BLEDescriptor.h>

// Arduino-ESP32 3.3.10's HID factory omits NimBLE encryption flags;
// setAccessPermissions() on a characteristic is Bluedroid-only. Construct
// reports here so the OS mouse driver itself can initiate secure pairing.
inline BLECharacteristic *secureHidReport(BLEHIDDevice &hid, uint8_t id, bool input) {
  const uint32_t flags = BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_READ_ENC |
    (input ? BLECharacteristic::PROPERTY_NOTIFY : BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_ENC);
  auto report = hid.hidService()->createCharacteristic(uint16_t(0x2a4d), flags);
  report->setAccessPermissions(ESP_GATT_PERM_READ_ENCRYPTED | ESP_GATT_PERM_WRITE_ENCRYPTED);
  auto reference = new BLEDescriptor(BLEUUID(uint16_t(0x2908)));
  // Enumeration metadata stays public; report data requires encryption.
  reference->setAccessPermissions(ESP_GATT_PERM_READ);
  const uint8_t value[] = {id, uint8_t(input ? 1 : 3)};
  reference->setValue(value, sizeof(value)); report->addDescriptor(reference);
  return report;
}
