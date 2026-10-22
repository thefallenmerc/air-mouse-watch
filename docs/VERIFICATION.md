# Verification — 2026-10-22

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
