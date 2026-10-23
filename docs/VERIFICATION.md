# Verification

## Native macOS pairing repair (0.2.2)

Inspection of Arduino-ESP32 3.3.10 found that its HID report factory only calls
`setAccessPermissions()` for encryption; that characteristic method is a no-op
under NimBLE. The input report therefore lacked `READ_ENC`, although the
application refused to send mouse data before encryption. The custom configuration
service did have encrypted attributes and could trigger security independently.

`HidReports.h` now constructs protected report characteristics, with public
report-reference metadata for enumeration. `PairingLifecycle.h` releases failed
or 45-second unencrypted attempts; encrypted links do not expire. The companion
only attaches to a peripheral macOS already has connected. Remembered UUIDs and
advertisements alone cannot initiate a connection. Its optional 20-second
`--scan-diagnostics` mode prints watch advertisements without connecting.
The primary advertisement now carries the complete mouse name, appearance and
HID UUID; the optional configuration UUID occupies the scan response.

`logs/test-native-pairing.log` records passing Swift, Python and sanitizer-backed
C++ checks. Added coverage exercises the actual report factory with a NimBLE-style
no-op permission setter and pairing failure/timeout/reconnect/wraparound behavior.
The signed companion release build passed. Both firmware profiles built before
the final advertising-layout adjustment; the final OpenMouse profile also built.

The advertisement iteration's 716,288-byte OpenMouse image was installed and readback-verified on
2026-10-24, SHA-256
`c79bae00be2aa6b89a8b174d007b00e1760c25e5be278ab19c2a63c2183fc792`.
Evidence: `logs/flash-native-pairing-advertisement-corrected.log`.
The complete pre-update backup is `backups/20261023T183016.010657Z/`;
subsequent app1-only iterations preserve the replaced region under
`backups/pairing-advertisement-*/`. The last update did not write the partition
table, OTA selector, app0, NVS or media. Runtime heartbeats confirm touch ready,
gyro off and no active host (`logs/native-pairing-corrected-live.log`).

The initial 0.2.2 security repair did not resolve the missing Connect button.
After a user-triggered button reset, a scan-only diagnostic observed the fresh
watch with HID UUID 0x1812, mouse appearance 962 and connectable=true. A filtered
live macOS Bluetooth log independently classified it as BLEHID and Connectable
(`logs/advertisement-native-pairing.log`,
`logs/native-advertisement-mac-stream.log`). No native connection reached the
watch during those attempts. The user confirmed that Connect was missing for
**every nearby device**, not only the watch. System Settings was restarted and
Bluetooth cycled once without deleting any saved devices.

Native Bluetooth Settings subsequently completed Just Works pairing and enabled
encryption, but the watch disconnected immediately afterward. macOS recorded a
remote-initiated disconnect and failed key restoration on its automatic retry
(`logs/native-disconnect-detail.log`). A live retry without a watch reset produced
`Bluetooth encryption handle=1 status=0`, followed by disconnect reason 534
(local-host termination), then a failed reconnect (`logs/native-disconnect-live.log`).
This establishes native security negotiation, but **not a usable mouse connection**.
Targeted connection-ownership and bond-store diagnostics were built and installed
with readback verification (`logs/flash-native-disconnect-diagnostics.log`):
716,912 bytes, SHA-256
`cb1c5e6601817122afcb8b60d5def458c74213b99166daf7a1be07c58f1ea089`.
The app1 region backup is `backups/pairing-advertisement-20261023T194132Z/`.
Identity, bonds, settings, media and app0 were preserved. A fresh native attempt
captured two bond-store overflow callbacks (results 0 then 5), immediately followed
by successful encryption and local termination. Evidence:
`logs/native-disconnect-instrumented.log`.

The saved NVS metadata contains all three `rpa_rec_*` entries but no surviving
peer encryption keys. Inspection of the exact ESP-IDF 5.5.4 NimBLE revision
`5c1a43ca3d205c3d5183e57dedce335fb6a81155` found that
`ble_store_config_find_rpa_rec()` does not match wildcard keys, although
`ble_store_clear()` supplies one. Old private-address mappings therefore survive
pairing resets. The default `BLE_STORE_OBJ_TYPE_PEER_ADDR` overflow handler calls
`ble_gap_unpair_oldest_peer()`, which can evict the newly created bond, terminate
the link, and leave the already-full alias cache unchanged.

`PeerAddressCache.h` enumerates only the pinned `nimble_bond/rpa_rec_1..3` address
cache metadata and deletes entries by exact address through NimBLE's API. This
keeps RAM and NVS synchronized. On address-cache overflow it reclaims one alias,
preferring an older alias for the same host, without deleting any encryption keys
or subscriptions. BOOT reset clears these aliases explicitly before normal bond
reset. Errors do not report success or retry without making space. The original
handling of other store object types is preserved. Tests model repeated private
address changes, sparse NVS indexes, exact-key deletion, preservation of bonds,
and read/delete failures. The complete Swift/Python/sanitizer-backed C++ suite
passed in `logs/test-native-pairing-cache-fix.log`. Subsequent live checks are
recorded below; the overflow branch itself has not been observed in a live retry.

The cache-repair OpenMouse build was installed and readback-verified on
2026-10-24: 717,696 bytes, SHA-256
`777d3e7951ae409e68523964af1242ede166332c3fc432acba672efc763dfb3c`.
Evidence: `logs/build-native-pairing-cache-fix.log` and
`logs/flash-native-pairing-cache-fix.log`. The replaced app1 region is preserved
in `backups/pairing-advertisement-20261023T195408Z/`. The address cache was
deliberately retained for the next test, as were the watch identity, settings,
media, partition table, OTA selector and original app0.

Pinned upstream evidence: [address-cache finder](https://github.com/espressif/esp-nimble/blob/5c1a43ca3d205c3d5183e57dedce335fb6a81155/nimble/host/store/config/src/ble_store_config.c),
[wildcard clear](https://github.com/espressif/esp-nimble/blob/5c1a43ca3d205c3d5183e57dedce335fb6a81155/nimble/host/src/ble_store.c),
and [default overflow eviction](https://github.com/espressif/esp-nimble/blob/5c1a43ca3d205c3d5183e57dedce335fb6a81155/nimble/host/src/ble_store_util.c).

The user then performed a BOOT reset (observed in USB logs) and native pairing.
The link remained `connected=1 encrypted=1 hid=1` for several minutes, but the
user reported no cursor movement. This verifies stable pairing after the repaired
reset, not recovery from cache overflow without reset. Evidence:
`logs/native-cache-fix-live.log`. The companion remained closed.

macOS did not register an IOHIDDevice. Its BTLEServer log explicitly reported
“Not creating HID device as the link is not encrypted” at 01:26:53.909; encryption
and pairing completed afterward. Evidence: `logs/native-cache-fix-driver-init.log`
and `logs/native-cache-fix-gatt.log`. Input-report subscription initiated security
too late for native HID creation. A project-local patch now protects the report-map
read with `READ_ENC` so enumeration cannot complete before encryption.

Source inspection also found `BLEService::addCharacteristic()` dropping distinct
objects with duplicate UUIDs. Thus the feature reports advertised in the report
map were absent from GATT. `tools/ble_library.py` verifies the complete installed
BLE source hash, stages it under `build/libraries/BLE`, and applies narrow fixes
for duplicate report registration and report-map encryption. It never edits the
global Arduino core. A sanitizer-backed test executes the extracted upstream
registration method to reproduce one registered report before the patch and four
after it. Additional tests verify map permissions and reject unknown source.
The full suite passed before the final report-map addition, whose four focused
patch/staging tests also passed. Both final firmware profiles compiled successfully;
`logs/build-native-hid-enumeration-standard.log` records the standard profile build
(the installed watch uses the OpenMouse profile). The complete repaired firmware built successfully using the project-local BLE
library and was flashed with readback verification: 717,904 bytes, SHA-256
`3cbcf0af48753872899f380b89ecc08a8102b2423a10536be4452403129f7d60`.
Evidence: `logs/build-native-hid-enumeration-fix.log` and
`logs/flash-native-hid-enumeration-fix.log`. The replaced app1 region is preserved
in `backups/pairing-advertisement-20261023T200813Z/`. The companion is closed;
startup diagnostics confirm touch initialized and enabled, with pointer unpaused.
Native pairing then registered Air Mouse Watch in `hidutil list` with
`AppleUserHIDEventDriver`, vendor `0x2FE3` and product `0xA101`. The user confirmed
cursor movement; USB diagnostics recorded encrypted HID subscription and 401
movement reports during the initial drag. Evidence:
`logs/native-hid-enumeration-live.log` and
`logs/native-hid-enumeration-registered.txt`.

The initial successful pairing followed another observed BOOT reset. The user
then confirmed three successful native Forget/Connect attempts with working
cursor movement and no further BOOT reset. The USB capture independently records
two subsequent re-pairing cycles on the same identity, successful encryption,
and movement reports increasing to 937. Thus repeated pairing is physically
verified, but the repaired address-cache overflow branch was not triggered in
this capture.

An ordinary watch restart through `esptool run` (no flash writes or bond clearing)
automatically restored the encrypted HID connection and native mouse driver,
with no companion running and no user pairing action. Evidence:
`logs/native-hid-reboot.log`, `logs/native-hid-reconnect-live.log`, and
`logs/native-hid-reconnect-registered.txt`.

Native pairing, repeated Forget/re-pair, physical touch cursor movement and
watch-restart reconnection are now verified on this Mac with the companion closed.
Mac restart/sleep-wake, long-term private-address churn, and OpenMouse browser
feature transactions remain untested on this revision. The global missing Connect
button was a separate observed macOS-side symptom; its underlying cause is not
established by the firmware defects or scan results.

## Image upload repair (0.2.1)

A live 147,106-byte image upload reproduced an abort at the first storage erase.
`logs/upload-fixed-firmware-test.log` and its preserved `logs/upload-crash.elf`
resolve the backtrace to `esp_flash_erase_region` → `esp_partition_erase_range`
→ `MediaStore::eraseStep`. The reserved image banks were inside running app1,
which ESP-IDF correctly protects from erase. This is a confirmed crash cause,
not an inference from the companion's generic disconnect error.

The repair defines a separate 8 MiB `airmedia` data partition at the existing
bank addresses and shrinks app1 metadata to 4 MiB. Executable write protection
remains enabled. The installer accepts only the exact original or new layouts,
backs up all 32 MiB, verifies the new table/firmware/OTA selector, and preserves
app0 and shared data. A failed install attempts to restore the previous table
and select app0.

Additional fixes cover unsigned upload-timer underflow after flash operations,
encryption state on bonded reconnect, and HID subscriptions restored before the
connect callback. The companion reads a cached acknowledgement if a notification
is missed, matching both opcode and sequence without repeating storage writes.

`logs/test-upload-partition-fix.log` records six passing Swift tests, C++ tests
under ASan/UBSan, and five Python tests. Added coverage verifies idle timing
across wraparound, exact old/new partition layouts, rejection of changed protected
regions/checksums, and refusal to use a wrong media partition. The release Mac
app is 0.2.1 (build 3), signed and verified; firmware build and exact generated
partition validation passed (`logs/build-upload-partition-fix.log`).

Installed firmware SHA-256:
`a1effd08f7bfc750cfadee9f07eb7b0628103e3c0b082ec7fcfef35fb973edbd`
(715,600 bytes). Firmware, table and OTA selector all passed device readback
verification in `logs/flash-upload-partition-fix.log`. A complete pre-migration
backup is in `backups/20261022T163515.940209Z/`, SHA-256
`308b40265e987e2202f68f76e35d504528076276c88d07d9c47fa5c4d01d18f2`.

The updated companion successfully uploaded the user's selected static image:
133,294 bytes, CRC32 `b6cfc36c`. Serial confirmed erase completion, ordered chunk
progress and `Upload committed bytes=133294`; the app showed the image saved.
Watch uptime continued through the transfer and afterward with
`connected=1 encrypted=1 hid=1 gyro=off touch=1`. This verifies a real Bluetooth
transfer and durable commit path, not just a simulated storage test. Evidence:
`logs/upload-partition-fixed-live.log`. Heartbeats stayed healthy through
210 seconds of uptime. A subsequent hardware reset (no flash changes) restored
the encrypted HID subscription and the companion again reported Image saved:
`logs/reboot-upload-persistence.log` and `logs/upload-persistence-runtime.log`.
Since the boot loader for media accepts only a CRC-valid AMF bank, this confirms
reload from committed flash after reset. The user confirmed on 2026-10-22: “Image visible and touch works.” This verifies
on-watch static image display and physical cursor movement after the repair.
GIF display and maximum-size transfers were not exercised here.

## Touch-only update (0.2.0)

Gyro sampling and pointer processing were removed, together with the companion's
Motion tab and calibration actions. Legacy settings keep their wire layout; the
old gyro bit is cleared at boot and on writes. Touch remains the sole source of
cursor movement. DPI sliders moved to Touch. The Mac reconnect path now waits
for an observed advertisement or live OS connection rather than trying a stale
cached identity after a button reset.

BOOT hold/release recognition is software-tested. Bond deletion uses the pinned
NimBLE store API at the next boot, before advertising, with a persistent request
and fresh identity. Pairing feedback overrides saved media. The user confirmed on 2026-10-22 that
the physical BOOT reset restarted the watch and it appeared in the other device’s
Bluetooth settings. This verifies the physical reset and subsequent discovery.
Completed pairing and touch input on that host, and preserved image display,
remain unverified. The new Bluetooth address was not independently read back.

Installed and readback-verified in app1 on 2026-10-22. The OTA selector also
verified. `logs/flash-touch-only.log` records version 0.2.0 and healthy heartbeats
at 10, 20 and 30 seconds: `gyro=off touch=1 boot=0 connected=1 encrypted=1 hid=1`.
The current Bluetooth identity was retained and macOS reconnected without a new
pairing request. The updated companion was rebuilt, signed and restarted.
The prior installed binary and metadata are saved in `backups/pre-touch-only/`;
the original full-device backup and app0 remain preserved.

## Completed software checks

- Native Swift app compiles in debug and release configurations on Apple Silicon.
- An ad-hoc signed `dist/Air Mouse.app` bundle was generated and its signature
  verified. The bundle was also opened normally (without `--preview`) for
  Bluetooth testing. A visual UI inspection has not been completed.
- Six Swift tests pass: settings encoding/validation, image geometry, RGB565 RLE,
  GIF timing, invalid image handling, and top-row orientation.
- Host C++ tests pass under AddressSanitizer and UndefinedBehaviorSanitizer:
  tap/hold/drag separation, all four edge swipes, partial and slow swipes, release
  debounce, invalid touch samples, timestamp wraparound, settings migration, settings ranges and media format validation.
- Pairing-button tests cover short-press cancellation, 3-second arming, release-only
  firing, switch bounce, startup with BOOT already held, and timer wraparound.
- Malformed media tests include truncation at every byte boundary, invalid run
  lengths, trailing bytes, and 10,000 deterministic mutated inputs.
- A GIF generated by Swift is accepted by the actual C++ firmware media validator.
- The actual `MediaStore` class passes simulated-flash tests for interrupted
  transfers, failed commit writes, corrupted-newest-bank fallback, and preserving
  the dedicated media partition boundaries. These are software fault
  simulations, not physical power-cut tests.
- Five Python tests pass for OTA slot selection, invalid selectors, accepted
  old/new layouts, unchanged protected regions and checksum/layout rejection.
- OpenMouse profile packet bytes round-trip through its upstream moddo codec at
  revision `bac031567b28f2a55c616fc525b03f2d5fa452cc`. Local C++ tests verify DPI
  writes affect cursor scale and unsupported polling/lift values are rejected.

Reproduce automated checks with `python3 tools/test.py`. Firmware build manifests
in `build/firmware/` and `build/firmware-openmouse/` record the built image size
and SHA-256. Both configurations compiled against ESP32 core 3.3.10. The standard artifact
below predates the hardware startup repairs; the OpenMouse artifact includes them.

| Build | Binary size | SHA-256 |
| --- | --- | --- |
| firmware (before startup repairs) | 725,008 bytes | `df5c9fd09d444949a8303df51598165e7aadf9c514daeb46652ed9669c533c6f` |
| firmware-openmouse 0.2.0 (touch-only) | 714,368 bytes | `f03e1300672ead5e5b87990c7b831717c7fb36cf7f4cfb1e8a3a05438fb284e3` |

## Device installation and startup

The OpenMouse build was installed in app1 at `0xC90000`; both firmware and OTA
selector passed esptool digest readback verification. The original app0 remains
untouched. A complete checksum-verified 32 MiB backup is in
`backups/20261022T070622.335340Z/` (excluded from developer archives).

The first backup attempt at 460800 baud failed before any write. The installer
now defaults to 115200 and retries read-only backup failures. The first firmware
boot revealed a null manufacturer characteristic in the BLE library; the sketch
now explicitly creates it before assigning its value. The PnP identity also uses
correct little-endian bytes instead of the core's reversed helper encoding.

The historical `logs/pairing-fix-flash.log` contains setup and loop heartbeats at 10, 20
and 30 seconds, without a crash/reset: IMU initialized, touch initialized,
calibration complete, Bluetooth connected and encrypted. HID subscription was
still false. These diagnostics do not verify physical gesture behavior.
The Mac companion was opened normally and Bluetooth settings brought forward.
`logs/latest-install.json` records the installed binary and backup checksums.

## Discovery and pairing investigation

The follow-up fixes are installed and readback-verified (final image above).
The advertising packet is explicitly 29 bytes; the previous automatic builder
silently omitted the configuration UUID after the preferred-interval field
pushed its size past 31 bytes. The full name is in the scan response.
GAP mouse appearance is applied after server creation and after a GATT reset.
Each firmware profile now generates a random static identity once and stores its
six bytes in the `air-mouse` NVS namespace (`ble-compat` / `ble-standard`). This
separates this experiment from macOS caches belonging to the hardware's earlier
public-address firmware. Existing firmware backups remain intact.

macOS now shows the new entry with a mouse icon. Its Bluetooth log confirms
VID `0x2FE3`, PID `0xA101` and the HID service. The companion discovers this
identity. At this stage, pairing failed/timed out, and no mouse HID subscription
was observed. Earlier forced encryption produced macOS errors 4803/4827.
The current firmware lets macOS initiate encryption, while configuration
attributes and mouse reporting still require an encrypted connection. The
later serial log records a remote disconnect during encryption negotiation.
This attempt did not establish a mouse connection.

The app now displays discovered watches in its menu, scans for both the config
and HID service (filtering unrelated mice), retrieves OS-connected watches,
clears stale scan results, and times out connection setup after 60 seconds.
The new bundle builds and is ad-hoc signed. Tests in `logs/test-identity-fix.log`
passed before the final timeout/status-text adjustments; final app compilation
also passed. FT3168 initialization now retries warm-reset startup and recovers
its readiness flag on successful polling.

Final firmware startup logs show IMU/touch/calibration all ready at 10/20/30s.
Pairing-alert inspection is blocked by the Computer Use tool's restriction on
`com.apple.UserNotificationCenter`; the user subsequently accepted a fresh live pairing
request, and the connection succeeded as recorded below. Physical cursor feel,
click, scroll and OpenMouse browser tests remain pending.

## Successful pairing follow-up

After the user accepted a fresh live macOS pairing request, serial diagnostics
reported `connected=1 encrypted=1 hid=1` repeatedly. `ioreg` independently
registered **Air Mouse Watch**, manufacturer **Air Mouse Experiment**, vendor
12259 (`0x2FE3`) and product 41217 (`0xA101`) as an IOHIDDevice. The companion's
live Settings window showed **Watch connected**, **Subscribed by macOS**,
**Calibrated**, **Touch Ready**, and **Image storage Ready**.

Evidence: `logs/live-pair-accepted-serial.log`, `logs/hid-paired.txt` and the
updated `logs/latest-install.json`. This verifies encrypted pairing, HID
subscription, host driver registration, and concurrent native configuration.
The companion was then disconnected through its UI; fresh serial heartbeats
continued to show `connected=1 encrypted=1 hid=1`. The companion subsequently
reconnected without another pairing request. Evidence is in
`logs/hid-with-companion-disconnected.log`.
It does not establish physical pointer feel, gesture accuracy, or WebHID access.

## Hardware checks pending

| Check | Status |
| --- | --- |
| Flash installer and readback against the physical device | Passed; repaired firmware boot confirmed |
| Full backup restore against the physical device | Not run; backup checksum verified |
| Pairing and host HID registration | Passed after user accepted the fresh pairing request |
| First pairing with companion never opened | Not independently tested |
| Reconnect after Mac/watch restart and sleep/wake | Not run |
| HID cursor, left/right click and two-axis wheel on macOS | Not run |
| Actual wrist axis mapping, drift and perceived latency | Not run |
| Touch swipe classification on the physical FT3168 | Not run |
| Concurrent OS HID and native CoreBluetooth configuration | Passed: live app settings and encrypted HID subscription |
| Image/GIF orientation, crop and animation on AMOLED | Not run |
| Upload cancellation and physical power loss | Not run |
| Bluetooth feature reports exposed to OpenMouse in Chrome | Not run |
| OpenMouse DPI write, physical effect, reboot/readback | Not run |

## Limits

This is an experimental implementation, not a hardware-validated release. It
targets one active Mac, uses Just Works bonding, and does not implement optical
DPI, translational 3D tracking, desktop object dragging, or 1000 Hz radio reports.
There is no automatic firmware updater, login-item registration, or battery-life
optimization yet. The app is locally signed, not notarized; its current binary
is Apple Silicon, although the source can be built on an Intel Mac.

Image storage uses a 205×251 RGB565 working resolution expanded to the 410×502
screen. Detailed animations may exceed the 3 MiB limit. Media upload erases can
temporarily affect input latency. The default touch edge decision window adds
intentional latency only for movements beginning near an edge.

OpenMouse compatibility is a separate, explicitly selected firmware profile.
Its native driver has controls/labels intended for optical USB mice; those do
not all apply to this watch. See [OpenMouse notes](OPENMOUSE.md).

## Return-to-macOS pairing investigation

The user subsequently reported that the touch-only watch works on other devices,
but would not reconnect to this Mac. Specific other host models/OS versions were
not supplied. On this Mac, the watch appeared under Nearby Devices, not as a
connected mouse. Serial diagnostics showed it advertising without an active host.
A fresh companion connection reached the watch but encryption failed with NimBLE
status 13 (`BLE_HS_ETIMEOUT`); no HID subscription was established. macOS logs
recorded a Just Works request dispatched to Bluetooth Settings and a timeout,
while the user reported no visible pairing prompt. This establishes a pairing
timeout, not a confirmed firmware or macOS root cause. Evidence is in
`logs/mac-reconnect-serial.log`, `logs/mac-reconnect-attempt.log`, and
`logs/mac-reconnect-system.log`. The companion was quit to isolate native pairing.

Recovery succeeded with System Settings fully quit and the native companion
reopened. The user reported connection success. `logs/mac-companion-only-serial.log`
records encryption status 0, Mouse notifications 1, and repeated heartbeats with
`connected=1 encrypted=1 hid=1 gyro=off touch=1`. No firmware or companion code
change was needed for this recovery. Closing Settings and retrying is an observed
workaround; the underlying cause of the missing macOS pairing prompt is not
proven. The user saw no new prompt in the successful attempt.
