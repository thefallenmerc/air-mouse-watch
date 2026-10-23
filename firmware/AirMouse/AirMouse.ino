#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEHIDDevice.h>
#include <BLESecurity.h>
#include <BLE2902.h>
#include <Preferences.h>
#include <atomic>
#include <services/gap/ble_svc_gap.h>
#include <host/ble_store.h>
#include "PairingButton.h"
#include "TransferClock.h"
#include "HidReports.h"
#include "PairingLifecycle.h"
#include "PeerAddressCache.h"
#include "Hardware.h"
#include "Display.h"
#include "OpenMouseProfile.h"
#ifndef AIR_MOUSE_OPENMOUSE_COMPAT
#define AIR_MOUSE_OPENMOUSE_COMPAT 0
#endif

const char *ServiceUUID = "b8571000-7f12-4c31-9638-37b2d28a6421";
const char *CommandUUID = "b8571001-7f12-4c31-9638-37b2d28a6421";
const char *ReplyUUID = "b8571002-7f12-4c31-9638-37b2d28a6421";
const char *SettingsUUID = "b8571003-7f12-4c31-9638-37b2d28a6421";
Hardware hardware; Display display; MediaStore media;
air::Settings config; air::Gesture gesture; air::PairingButton pairingButton; Preferences preferences;
air::PointerScale pointerScale;
BLECharacteristic *compatConfig=nullptr, *compatBattery=nullptr;
portMUX_TYPE compatMutex = portMUX_INITIALIZER_UNLOCKED;
uint8_t compatSnapshot[11] = {};
BLECharacteristic *mouseReport, *reply, *settingsValue;
BLEHIDDevice *hid;
std::atomic<bool> connected{false}, encrypted{false}, subscribed{false}, resetLink{false};
std::atomic<bool> suspended{false};
std::atomic<uint16_t> activeConnection{BLE_HS_CONN_HANDLE_NONE};
std::atomic<uint16_t> subscribedConnection{BLE_HS_CONN_HANDLE_NONE};
std::atomic<uint16_t> failedConnection{BLE_HS_CONN_HANDLE_NONE};
std::atomic<uint32_t> connectionEpoch{0};
air::PairingLifecycle pairingLifecycle;
air::TransferClock transferClock;
bool paused = false, erasePending = false, pairingMode = false;
uint8_t eraseReply[3] = {};
uint32_t lastInput = 0, lastStatus = 0;
uint32_t touchSamples = 0, motionReports = 0;
int pendingX = 0, pendingY = 0, pendingWheel = 0, pendingPan = 0;
uint8_t pressed = 0;
uint32_t releaseAt = 0;
struct Command { uint16_t size; uint16_t connection; uint8_t bytes[512]; };
QueueHandle_t commands;

// Standard mouse: 5 buttons, signed 16-bit relative X/Y, signed 8-bit wheel and AC Pan.
// Report ID belongs in the 0x2908 descriptor, not the notification payload.
uint8_t reportMap[] = {
  0x05,0x01,0x09,0x02,0xa1,0x01,0x85,0x01,0x09,0x01,0xa1,0x00,
  0x05,0x09,0x19,0x01,0x29,0x05,0x15,0x00,0x25,0x01,0x95,0x05,0x75,0x01,0x81,0x02,
  0x95,0x01,0x75,0x03,0x81,0x03,
  0x05,0x01,0x09,0x30,0x09,0x31,0x16,0x01,0x80,0x26,0xff,0x7f,0x75,0x10,0x95,0x02,0x81,0x06,
  0x09,0x38,0x15,0x81,0x25,0x7f,0x75,0x08,0x95,0x01,0x81,0x06,
  0x05,0x0c,0x0a,0x38,0x02,0x15,0x81,0x25,0x7f,0x75,0x08,0x95,0x01,0x81,0x06,0xc0,0xc0,
#if AIR_MOUSE_OPENMOUSE_COMPAT
  // Separate top-level configuration collection, outside the protected mouse collection.
  0x06,0xff,0x00,0x09,0x01,0xa1,0x01,0x15,0x00,0x26,0xff,0x00,0x75,0x08,
  0x85,0x02,0x09,0x01,0x95,0x0b,0xb1,0x02,
  0x85,0x03,0x09,0x02,0x95,0x08,0xb1,0x02,
  0x85,0x04,0x09,0x03,0x95,0x04,0xb1,0x02,0xc0
#endif
};
void clearInput() { pendingX = pendingY = pendingWheel = pendingPan = 0; pressed = 0; gesture.reset(); pointerScale.reset(); }
bool canSend() { return connected && encrypted && subscribed && !suspended && !paused; }
void sendReport(uint8_t buttons, int x, int y, int wheel, int pan) {
  if (x || y || wheel || pan) ++motionReports;
  uint8_t value[7] = {buttons, uint8_t(x), uint8_t(x >> 8), uint8_t(y), uint8_t(y >> 8), uint8_t(wheel), uint8_t(pan)};
  mouseReport->setValue(value, sizeof(value)); mouseReport->notify();
}
void enqueue(air::Output o) {
  if (!canSend()) return;
  o=pointerScale.apply(o,config);
  pendingX = constrain(pendingX + o.x, -32767, 32767); pendingY = constrain(pendingY + o.y, -32767, 32767);
  pendingWheel = constrain(pendingWheel + o.wheel, -127, 127); pendingPan = constrain(pendingPan + o.pan, -127, 127);
  if (o.click && !pressed) { pressed = o.click; releaseAt = millis() + 32; sendReport(pressed, 0, 0, 0, 0); }
}
void pumpMouse(uint32_t now) {
  if (!canSend()) return;
  if (pressed && int32_t(now - releaseAt) >= 0) { pressed = 0; sendReport(0, 0, 0, 0, 0); }
  if (pendingX || pendingY || pendingWheel || pendingPan) {
    int wheel = constrain(pendingWheel, -3, 3), pan = constrain(pendingPan, -3, 3);
    sendReport(pressed, pendingX, pendingY, wheel, pan);
    pendingX = pendingY = 0; pendingWheel -= wheel; pendingPan -= pan;
  }
}
// Use the live link descriptor, including on bonded reconnects. Never treat a
// restored HID subscription or a cached GATT read as proof of encryption.
bool updateSecurity(const ble_gap_conn_desc *desc) {
  if (!desc || desc->conn_handle != activeConnection.load()) return false;
  const bool secure = desc->sec_state.encrypted;
  if (encrypted.exchange(secure) != secure) resetLink = true;
  return secure;
}
void refreshSecurity() {
  const uint16_t handle = activeConnection.load();
  if (handle == BLE_HS_CONN_HANDLE_NONE) return;
  ble_gap_conn_desc desc{};
  if (ble_gap_conn_find(handle, &desc) == 0) updateSecurity(&desc);
}
class MouseCallbacks: public BLECharacteristicCallbacks {
  void onSubscribe(BLECharacteristic*, ble_gap_conn_desc *desc, uint16_t value) override {
    if (!desc) return;
    const uint16_t active = activeConnection.load();
    if (active != BLE_HS_CONN_HANDLE_NONE && desc->conn_handle != active) return;
    // NimBLE may restore the bonded CCCD before delivering onConnect.
    subscribedConnection = (value & 1) ? desc->conn_handle : BLE_HS_CONN_HANDLE_NONE;
    updateSecurity(desc);
    subscribed = (value & 1) != 0; resetLink = true;
    Serial.printf("Mouse notifications: %u\n", value);
  }
} mouseCallbacks;
class ControlCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c) override { auto v = c->getValue(); if (v.length() == 1) { suspended = v[0] == 0; resetLink = true; } }
} controlCallbacks;
class CommandCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c, ble_gap_conn_desc *desc) override {
    if (!updateSecurity(desc)) { Serial.println("Config write rejected: link not encrypted"); return; }
    auto value = c->getValue(); if (value.length() < 3 || value.length() > 512) return;
    Command packet{}; packet.connection = desc->conn_handle; packet.size = value.length(); memcpy(packet.bytes, value.c_str(), packet.size);
    if (xQueueSend(commands, &packet, 0) != pdTRUE) Serial.println("Config queue full");
  }
} commandCallbacks;
void publishSettings() {
  settingsValue->setValue((uint8_t*)&config, sizeof(config));
  if(compatConfig){
    uint8_t value[11];air::moddoConfig(config,value);
    portENTER_CRITICAL(&compatMutex);memcpy(compatSnapshot,value,11);portEXIT_CRITICAL(&compatMutex);
    compatConfig->setValue(value,11);
  }
}
class CompatCallbacks: public BLECharacteristicCallbacks {
  void onWrite(BLECharacteristic *c, ble_gap_conn_desc *desc) override {
    auto value=c->getValue();
    uint8_t previous[11];portENTER_CRITICAL(&compatMutex);memcpy(previous,compatSnapshot,11);portEXIT_CRITICAL(&compatMutex);
    c->setValue(previous,11); // Until validation/persistence succeed, reads return the last accepted settings.
    if(!updateSecurity(desc) || value.length()!=11)return;
    Command packet{};packet.connection=desc->conn_handle;packet.size=14;packet.bytes[0]=9;memcpy(packet.bytes+3,value.c_str(),11);xQueueSend(commands,&packet,0);
  }
} compatCallbacks;
class ServerCallbacks: public BLEServerCallbacks {
  void onConnect(BLEServer *server, ble_gap_conn_desc *desc) override {
    if (!desc) return;
    Serial.printf("Bluetooth connect callback handle=%u active=%u encrypted=%u bonded=%u\n", desc->conn_handle, activeConnection.load(), desc->sec_state.encrypted, desc->sec_state.bonded);
    uint16_t expected = BLE_HS_CONN_HANDLE_NONE;
    if (!activeConnection.compare_exchange_strong(expected, desc->conn_handle)) {
      Serial.printf("Rejecting extra host handle=%u active=%u\n", desc->conn_handle, expected);
      server->disconnect(desc->conn_handle); return; // This mouse supports one host.
    }
    failedConnection = BLE_HS_CONN_HANDLE_NONE; ++connectionEpoch;
    connected = true; encrypted = desc->sec_state.encrypted; suspended = false; subscribed = subscribedConnection.load() == desc->conn_handle; resetLink = true;
    Serial.printf("Bluetooth connected handle=%u encrypted=%d\n", desc->conn_handle, int(encrypted.load()));
  }
  void onDisconnect(BLEServer*, ble_gap_conn_desc *desc) override {
    if (desc) Serial.printf("Bluetooth disconnect callback handle=%u active=%u encrypted=%u bonded=%u\n", desc->conn_handle, activeConnection.load(), desc->sec_state.encrypted, desc->sec_state.bonded);
    if (!desc || desc->conn_handle != activeConnection.load()) return;
    activeConnection = BLE_HS_CONN_HANDLE_NONE; subscribedConnection = BLE_HS_CONN_HANDLE_NONE;
    connected = false; encrypted = false; subscribed = false; resetLink = true;
    ble_svc_gap_device_appearance_set(HID_MOUSE); BLEDevice::startAdvertising();
  }
};
class SecurityCallbacks: public BLESecurityCallbacks {
  bool onSecurityRequest() override { return true; }
  void onAuthenticationComplete(ble_gap_conn_desc *desc) override {
    if (desc) Serial.printf("Bluetooth authentication handle=%u encrypted=%u bonded=%u keysize=%u\n", desc->conn_handle, desc->sec_state.encrypted, desc->sec_state.bonded, desc->sec_state.key_size);
    updateSecurity(desc);
  }
};
class BondStoreCallbacks: public BLEDeviceCallbacks {
  int onStoreStatus(ble_store_status_event *event, void *arg) override {
    const int rc = air::PeerAddressCache::onStatus(event, arg);
    const int type = event->event_code == BLE_STORE_EVENT_OVERFLOW ? event->overflow.obj_type : event->full.obj_type;
    Serial.printf("Bluetooth bond store event=%u type=%d result=%d\n", event->event_code, type, rc);
    return rc;
  }
} bondStoreCallbacks;
int logBluetoothEvent(ble_gap_event *event, void*) {
  if (event->type == BLE_GAP_EVENT_ENC_CHANGE) {
    Serial.printf("Bluetooth encryption handle=%u status=%d\n", event->enc_change.conn_handle, event->enc_change.status);
    if (event->enc_change.status != 0) failedConnection = event->enc_change.conn_handle;
  }
  else if (event->type == BLE_GAP_EVENT_DISCONNECT)
    Serial.printf("Bluetooth disconnected reason=%d\n", event->disconnect.reason);
  else if (event->type == BLE_GAP_EVENT_REPEAT_PAIRING)
    Serial.println("Bluetooth peer requested replacement bond");
  return 0;
}
void startBluetooth() {
  commands = xQueueCreate(8, sizeof(Command));
  if (!commands) { display.status("Queue allocation failed"); while (true) delay(1000); }
  BLEDevice::init("Air Mouse Watch"); BLEDevice::setMTU(247);
  BLEDevice::setCustomGapHandler(logBluetoothEvent);
  BLEDevice::setDeviceCallbacks(&bondStoreCallbacks);
  const bool freshPairing = preferences.getBool("reset-pairing", false);
  pairingMode = freshPairing;
  if (freshPairing && (air::PeerAddressCache::clear() != 0 || ble_store_clear() != 0)) {
    Serial.println("Pairing reset: bond store clear failed");
    display.status("Pair reset failed: reboot"); while (true) delay(1000);
  }
  // A separate, persistent identity keeps macOS's cached GATT table for other
  // watch experiments (and the standard profile) separate from this profile.
  uint8_t address[6];
  const char *addressKey = AIR_MOUSE_OPENMOUSE_COMPAT ? "ble-compat" : "ble-standard";
  if (freshPairing || preferences.getBytesLength(addressKey) != sizeof(address)) {
    esp_fill_random(address, sizeof(address)); address[5] |= 0xc0;
    if (preferences.putBytes(addressKey, address, sizeof(address)) != sizeof(address)) {
      display.status("Bluetooth storage error"); while (true) delay(1000);
    }
  } else { preferences.getBytes(addressKey, address, sizeof(address)); }
  if (!BLEDevice::setOwnAddr(address) || !BLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM)) {
    display.status("Bluetooth identity error"); while (true) delay(1000);
  }
  if (freshPairing) {
    // Clear the durable request only after both bond erasure and identity persistence.
    if (preferences.putBool("reset-pairing", false) != 1) {
      display.status("Pair reset storage error"); while (true) delay(1000);
    }
    Serial.println("Pairing reset: bonds cleared, fresh identity saved");
  }
  Serial.printf("Bluetooth identity: %02X:%02X:%02X:%02X:%02X:%02X\n",
    address[5], address[4], address[3], address[2], address[1], address[0]);
  BLEDevice::setSecurityCallbacks(new SecurityCallbacks());
  BLESecurity::setAuthenticationMode(ESP_LE_AUTH_REQ_SC_BOND);
  BLESecurity::setCapability(ESP_IO_CAP_NONE); // Normal mouse pairing, no companion or passkey entry required.
  BLESecurity::setInitEncryptionKey(); BLESecurity::setRespEncryptionKey();
  // Allow discovery without unsolicited pairing. The protected input report
  // triggers security when the OS reads/subscribes, without a companion service.
  BLESecurity::setForceAuthentication(false);
  auto server = BLEDevice::createServer(); server->setCallbacks(new ServerCallbacks());
  server->advertiseOnDisconnect(false); // Only the active host disconnect restarts advertising.
  // createServer() resets GAP defaults, so set appearance after creating it.
  ble_svc_gap_device_appearance_set(HID_MOUSE);
  hid = new BLEHIDDevice(server);
  // The getter creates this optional characteristic; the setter alone dereferences null.
  hid->manufacturer()->setValue("Air Mouse Experiment");
  // Normal builds use an unassigned experimental identity. Compatibility builds deliberately
  // emulate the moddo registry's VID so unmodified OpenMouse can select its existing driver.
  // Core 3.3.10's pnp() emits big-endian integers; DIS requires little-endian.
  const uint16_t vendor = AIR_MOUSE_OPENMOUSE_COMPAT ? 0x2fe3 : 0xffff;
  uint8_t identity[] = {0x02, uint8_t(vendor), uint8_t(vendor >> 8), 0x01, 0xa1, 0x00, 0x01};
  hid->deviceInfo()->getCharacteristic(BLEUUID(uint16_t(0x2a50)))->setValue(identity, sizeof(identity));
  hid->hidInfo(0, 0x02);
  hid->reportMap(reportMap, sizeof(reportMap));
  mouseReport = secureHidReport(*hid, 1, true); mouseReport->setCallbacks(&mouseCallbacks);
  hid->hidControl()->setCallbacks(&controlCallbacks);
  uint8_t zero[7] = {}; mouseReport->setValue(zero, sizeof(zero));
  auto service = server->createService(ServiceUUID);
  auto command = service->createCharacteristic(CommandUUID, BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_ENC);
  command->setCallbacks(&commandCallbacks);
  reply = service->createCharacteristic(ReplyUUID, BLECharacteristic::PROPERTY_NOTIFY | BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_READ_ENC);
  reply->addDescriptor(new BLE2902());
  settingsValue = service->createCharacteristic(SettingsUUID, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_READ_ENC);
#if AIR_MOUSE_OPENMOUSE_COMPAT
  compatConfig=secureHidReport(*hid,2,false);compatConfig->setCallbacks(&compatCallbacks);
  auto version=secureHidReport(*hid,3,false);uint8_t firmware[8]={0,0,0,0,0,2,2,1};version->setValue(firmware,8);
  compatBattery=secureHidReport(*hid,4,false);uint8_t unknownBattery[4]={255,0,0,0};compatBattery->setValue(unknownBattery,4);
#endif
  publishSettings();
  service->start(); hid->startServices();
  auto advertising = BLEDevice::getAdvertising();
  // Present the complete mouse identity in the primary packet (28 bytes).
  // Keep the optional configuration UUID in the scan response (18 bytes),
  // independently discoverable by the companion without crowding mouse discovery.
  BLEAdvertisementData advertisement, scanResponse;
  advertisement.setFlags(0x06);
  advertisement.setAppearance(HID_MOUSE);
  advertisement.setCompleteServices(BLEUUID(uint16_t(0x1812)));
  advertisement.setName("Air Mouse Watch");
  scanResponse.setCompleteServices(BLEUUID(ServiceUUID));
  if (advertisement.getPayload().length() != 28 || scanResponse.getPayload().length() != 18 ||
      !advertising->setAdvertisementData(advertisement) || !advertising->setScanResponseData(scanResponse)) {
    Serial.println("Bluetooth advertising setup failed");
    display.status("Bluetooth setup error"); while (true) delay(1000);
  }
  advertising->setScanResponse(true);
  if (!advertising->start()) {
    Serial.println("Bluetooth advertising start failed");
    display.status("Bluetooth start failed"); while (true) delay(1000);
  }
  Serial.println("Bluetooth advertising started: primary HID/name, scan response configuration");
}
void respond(const uint8_t *request, uint8_t result) {
  // <=20 bytes so replies work at the minimum ATT MTU.
  uint32_t checksum=air::crc32((uint8_t*)&config,sizeof(config));
  uint8_t data[17] = {uint8_t(request[0] | 0x80), request[1], request[2], result,
    0, uint8_t(subscribed.load()), uint8_t(paused), 0, // Retired motion fields.
    uint8_t(hardware.touchReady), uint8_t(media.available), uint8_t(media.media != nullptr), 1,
    uint8_t(checksum),uint8_t(checksum>>8),uint8_t(checksum>>16),uint8_t(checksum>>24),AIR_MOUSE_OPENMOUSE_COMPAT};
  reply->setValue(data, sizeof(data)); if (encrypted) reply->notify();
}
void process(const Command &cmd) {
  const uint8_t *p = cmd.bytes, *body = p + 3; size_t n = cmd.size - 3;
  uint8_t result = 0;
  if (erasePending && p[0] != 1 && p[0] != 7) { respond(p, 3); return; }
  switch (p[0]) {
    case 1: if (n) result = 1; break; // Status
    case 2: { // Validate everything before replacing saved settings.
      air::Settings candidate;
      if (n != sizeof(candidate)) { result = 1; break; }
      memcpy(&candidate, body, sizeof(candidate));
      if (!candidate.valid()) { result = 1; break; }
      candidate.disableMotion();
      if (preferences.putBytes("settings", &candidate, sizeof(candidate)) != sizeof(candidate)) { result = 2; break; }
      if (canSend()) sendReport(0,0,0,0,0);
      config = candidate; clearInput(); display.brightness(config.brightness);
      publishSettings(); break;
    }
    case 3: result = 1; break; // Retired gyro calibration command.
    case 4:
      if (n != 8) { result = 1; break; }
      if (!media.start(air::u32(body), air::u32(body + 4))) { result = 2; break; }
      Serial.printf("Upload begin bytes=%lu crc=%08lx\n", (unsigned long)air::u32(body), (unsigned long)air::u32(body+4));
      memcpy(eraseReply, p, 3); erasePending = true; transferClock.touch(millis()); return;
    case 5:
      if (n <= 4 || !media.append(air::u32(body), body + 4, n - 4)) {
        result = 2; Serial.println("Upload chunk rejected");
      } else {
        transferClock.touch(millis());
        if (air::u32(body) / 16384 != (air::u32(body) + n - 4) / 16384)
          Serial.printf("Upload received=%lu\n", (unsigned long)(air::u32(body) + n - 4));
      } break;
    case 6:
      if (n || !media.commit()) { result = 2; media.abort(); }
      else { display.reset(); Serial.printf("Upload committed bytes=%u\n", unsigned(media.size)); }
      transferClock.clear(); break;
    case 7: if (n) result = 1; else { media.abort(); erasePending = false; transferClock.clear(); } break;
    case 8:
      if (n != 1 || body[0] > 1) { result = 1; break; }
      if (canSend()) sendReport(0,0,0,0,0);
      paused = body[0]; clearInput(); break;
    case 9: {
      air::Settings candidate=config;
      if(AIR_MOUSE_OPENMOUSE_COMPAT && air::applyModdo(body,n,candidate) &&
          preferences.putBytes("settings",&candidate,sizeof(candidate))==sizeof(candidate)) {
        if(canSend())sendReport(0,0,0,0,0);
        config=candidate;clearInput();
      }
      publishSettings();return; // HID feature writes are confirmed by readback, not GATT command acknowledgements.
    }
    default: result = 1;
  }
  respond(p, result);
}
void setup() {
  Serial.begin(115200); Serial.setTxTimeoutMs(0); // Diagnostics must never stall the BLE task when USB is unread.
  preferences.begin("air-mouse", false);
  if (preferences.getBytesLength("settings") == sizeof(config)) {
    air::Settings saved; preferences.getBytes("settings", &saved, sizeof(saved)); if (saved.valid()) config = saved;
  }
  if (config.flags & 1) {
    config.disableMotion();
    if (preferences.putBytes("settings", &config, sizeof(config)) != sizeof(config))
      Serial.println("Legacy settings migration could not be saved; gyro is still disabled");
  }
  display.begin(config.brightness); display.status("Touch mouse starting");
  hardware.begin(); media.begin(); startBluetooth();
  lastInput = millis();
  Serial.println("Air Mouse 0.2.2: protected HID pairing; touch-only; hold BOOT 3 sec then release to reset pairing");
}
void loop() {
  uint32_t now = millis();
  refreshSecurity();
  const uint16_t handle = activeConnection.load();
  const bool pairingFailed = failedConnection.exchange(BLE_HS_CONN_HANDLE_NONE) == handle && handle != BLE_HS_CONN_HANDLE_NONE;
  if (pairingLifecycle.update(connectionEpoch.load(), connected.load(), encrypted.load(), pairingFailed, now)) {
    const int rc = ble_gap_terminate(handle, BLE_ERR_REM_USER_CONN_TERM);
    Serial.printf("Releasing incomplete pairing handle=%u failed=%d rc=%d\n", handle, int(pairingFailed), rc);
  }
  const bool wasButtonDown = pairingButton.down();
  if (pairingButton.update(hardware.pairingButtonPressed(), now)) {
    if (canSend()) sendReport(0,0,0,0,0);
    clearInput(); media.abort(); erasePending = false; transferClock.clear();
    if (preferences.putBool("reset-pairing", true) != 1) {
      display.status("Pair reset storage error"); delay(2000); display.reset();
    } else {
      display.status("Restarting for pairing");
      Serial.println("Pairing reset: button released; rebooting");
      delay(100); ESP.restart();
    }
  }
  if (pairingButton.down()) {
    if (!wasButtonDown) { if (canSend()) sendReport(0,0,0,0,0); clearInput(); }
    display.status(pairingButton.armed() ? "Release to reset pairing" : "Hold BOOT 3s to pair");
  } else if (wasButtonDown) { clearInput(); display.reset(); }
  if (pairingMode && connected && encrypted && subscribed) { pairingMode = false; display.reset(); }
  if (resetLink.exchange(false)) {
    clearInput();
    if (canSend()) sendReport(0,0,0,0,0);
    if (!connected) { media.abort(); erasePending = false; transferClock.clear(); xQueueReset(commands); paused = false; }
  }
  if (now - lastInput >= 8) {
    lastInput = now;
    auto sample = hardware.touch();
    if (sample.valid && sample.down) ++touchSamples;
    if (!paused && !pairingButton.down()) enqueue(gesture.update(sample, now, config)); else gesture.reset();
    pumpMouse(now);
  }
  Command packet;
  if (xQueueReceive(commands, &packet, 0) == pdTRUE && encrypted && packet.connection == activeConnection.load()) process(packet);
  if (erasePending) {
    if (!media.eraseStep()) { erasePending = false; respond(eraseReply, 2); }
    else if (!media.erasing()) { erasePending = false; transferClock.touch(millis()); Serial.println("Upload erase complete"); respond(eraseReply, 0); }
  }
  if (!erasePending && transferClock.expired(millis())) { media.abort(); transferClock.clear(); Serial.println("Upload idle timeout"); }
  if (now - lastStatus >= 1000) {
    lastStatus = now;
    static uint32_t lastDiagnostic = 0;
    if (now - lastDiagnostic >= 10000) {
      lastDiagnostic = now;
      Serial.printf("Air Mouse uptime=%lu gyro=off touch=%d boot=%d connected=%d encrypted=%d hid=%d suspended=%d paused=%d touchEnabled=%d touchSamples=%lu motionReports=%lu\n",
        static_cast<unsigned long>(now), hardware.touchReady, int(hardware.pairingButtonPressed()),
        int(connected.load()), int(encrypted.load()), int(subscribed.load()), int(suspended.load()), int(paused), int(bool(config.flags & 2)),
        static_cast<unsigned long>(touchSamples), static_cast<unsigned long>(motionReports));
    }
    // AXP2101 battery percentage register (0xA4); don't invent a fixed battery charge.
    uint8_t battery; if (hardware.read(0x34, 0xa4, &battery, 1) && battery <= 100) {
      hid->setBatteryLevel(battery);
      if(compatBattery){uint8_t value[4]={battery,0,0,1};compatBattery->setValue(value,4);}
    }
    if (!pairingButton.down() && (!media.media || pairingMode)) display.status(!hardware.touchReady ? "Touch controller error" :
      paused ? "Pointer paused" : !connected ? "Pair in Bluetooth settings" : !subscribed ? "Waiting for mouse HID" : "Touch mouse connected");
  }
  if (!pairingButton.down() && !pairingMode) display.tick(media, now);
  delay(1);
}
