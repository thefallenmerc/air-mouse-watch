/*
 * Copyright 2017-2026 Espressif Systems (Shanghai) PTE LTD
 * Copyright 2020-2025 Ryan Powell <ryan@nable-embedded.io> and
 * esp-nimble-cpp, NimBLE-Arduino contributors.
 * Copyright 2017 Neil Kolban
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
// Extracted from Arduino-ESP32 3.3.10 BLEService.cpp (Apache-2.0).
void BLEService::addCharacteristic(BLECharacteristic *pCharacteristic) {
  // We maintain a mapping of characteristics owned by this service.  These are managed by the
  // BLECharacteristicMap class instance found in m_characteristicMap.  We add the characteristic
  // to the map and then ask the service to add the characteristic at the BLE level (ESP-IDF).

  log_v(">> addCharacteristic()");
  log_d("Adding characteristic: uuid=%s to service: %s", pCharacteristic->getUUID().toString().c_str(), toString().c_str());

  // Check that we don't add the same characteristic twice.
  BLECharacteristic *pExisting = m_characteristicMap.getByUUID(pCharacteristic->getUUID());
  if (pExisting != nullptr) {
    log_w("<< Adding a new characteristic with the same UUID as a previous one");
  }

#if defined(CONFIG_NIMBLE_ENABLED)
  if (pExisting != nullptr) {
    pExisting->m_removed = 0;
  } else
#endif
  {
    // Remember this characteristic in our map of characteristics.  At this point, we can lookup by UUID
    // but not by handle.  The handle is allocated to us on the ESP_GATTS_ADD_CHAR_EVT.
    m_characteristicMap.setByUUID(pCharacteristic, pCharacteristic->getUUID());
  }

#if defined(CONFIG_NIMBLE_ENABLED)
  getServer()->serviceChanged();
#endif

  log_v("<< addCharacteristic()");
}  // addCharacteristic
