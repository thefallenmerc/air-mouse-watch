# Air Mouse protocol v1

## Bluetooth services

Standard HID service `0x1812`, Mouse appearance `0x03C2`, and input report 1:
`buttons:u8, dx:i16, dy:i16, wheel:i8, pan:i8` (7 bytes, little-endian).
Five button bits are declared; gestures currently emit left and right only.
Mouse reports require an encrypted connection and a host notification subscription.
HID Suspend/Exit Suspend is honored. Pairing uses LE Secure Connections bonding
with normal mouse-style Just Works (no MITM authentication). Bonds are stored
by the bundled NimBLE stack in NVS. Only one active host is supported by this
application's state model.

The 28-byte primary advertisement contains general-discoverable/LE-only flags,
mouse appearance, the HID service UUID and the complete device name. The 18-byte
scan response advertises the optional configuration service UUID. Both fit the
legacy 31-byte limit independently. Startup checks the packet lengths and whether
advertising actually started.

From firmware 0.2.2, HID report characteristics explicitly carry NimBLE's
`READ_ENC` / `WRITE_ENC` flags. Arduino-ESP32 3.3.10's HID factory omits these,
and its characteristic `setAccessPermissions()` has no effect with NimBLE.
Report-reference descriptors remain public for enumeration. The HID report-map
read requires encryption, making native driver creation wait for pairing rather
than racing the asynchronous notification subscription. Input reports remain
protected as well. No companion service access is needed to initiate security.
The project-local BLE build also registers every distinct report characteristic,
even though they share UUID `0x2a4d`; each has its own report-reference descriptor.
Failed security and unencrypted connections lasting 45
seconds are disconnected so advertising can resume. Established encrypted links
do not expire. The stack replaces an old bond when the same host pairs again.

The pinned stack's private-address cache has three slots and a broken wildcard
deletion path. Firmware enumerates its persisted address records and removes
them through exact-key NimBLE calls during BOOT reset. On cache overflow it
replaces an alias (preferably for the same peer), preserving encryption keys and
subscriptions instead of allowing the default handler to unpair the mouse.
This adapter depends on Arduino-ESP32 3.3.10's NVS schema and must be reviewed
when upgrading the pinned core. It does not modify global Arduino libraries.

The custom UUID base is `b8571000-7f12-4c31-9638-37b2d28a6421`:

| First UUID field | Purpose | Access |
| --- | --- | --- |
| `b8571000` | Service | — |
| `b8571001` | Commands | Encrypted write with response |
| `b8571002` | Acknowledgement/status | Encrypted read and notify |
| `b8571003` | Current settings | Encrypted read |

The native client retrieves already connected peripherals as well as scanning;
an OS-owned HID connection can therefore also carry companion configuration.
It only connects to peripherals already connected by macOS. A remembered UUID
or a scan result alone never initiates a connection, including after Forget.

## Commands

Packet: `opcode:u8, sequence:u16, payload:bytes`. At most 512 bytes total.
The client permits one outstanding request. Every public command receives a
17-byte reply: `opcode|0x80, sequence:u16, result:u8, calibrated:u8,
HID-subscribed:u8, paused:u8, IMU-ready:u8, touch-ready:u8, storage-ready:u8,
has-media:u8, protocol-version:u8, settings-crc32:u32, openmouse-mode:u8`.

Result 0 = success, 1 = invalid, 2 = storage/validation failure, 3 = busy.
Sequence and opcode must match. While awaiting an acknowledgement, the companion reads the reply characteristic
every two seconds as a fallback for a lost notification. Only matching opcode and
sequence values finish the request. It never retransmits a potentially committed
command. If the deadline still expires, the companion disconnects with an error
identifying the upload phase.

| Opcode | Payload | Action |
| --- | --- | --- |
| 1 | none | Status |
| 2 | 36-byte settings | Validate, persist, apply and publish settings |
| 3 | none | Retired; rejected with result 1 (no gyro) |
| 4 | length:u32, crc32:u32 | Begin media upload; ACK after incremental erase |
| 5 | offset:u32, bytes | Append at the exact expected offset |
| 6 | none | Verify complete media and commit its bank header |
| 7 | none | Abort incomplete upload |
| 8 | paused:u8 (0/1) | Runtime pause/resume |

Opcode 9 is internal to optional HID compatibility feature writes and is not a
public command. No response is defined for it. Commands are copied into a bounded
FreeRTOS queue; the main loop owns mutable pointer/configuration/media state.

## Settings

Fixed 36-byte packed little-endian record:

| Offset | Type | Field |
| --- | --- | --- |
| 0 | u16 | version = 1 |
| 2 | u16 | flags: retired gyro bit=1 (cleared), touch=2, reverse-scroll=4 |
| 4 | float32 | legacy gyro gain (unused) |
| 8 | float32 | touch counts per pixel |
| 12 | float32 | legacy dead zone (unused) |
| 16 | float32 | legacy smoothing (unused) |
| 20 | u16 | right-click hold ms |
| 22 | u16 | maximum quick-swipe duration ms |
| 24 | u8 | quick-scroll wheel steps |
| 25 | u8 | brightness |
| 26 | u8 | legacy horizontal gyro axis (unused) |
| 27 | u8 | legacy vertical gyro axis (unused) |
| 28 | u16 | edge start/end zone percentage |
| 30 | u16 | minimum swipe span percentage |
| 32 | u16 | X pointer scale in DPI units, 1600 = 1× |
| 34 | u16 | Y pointer scale in DPI units, 1600 = 1× |

Ranges and finite float checks are implemented in both clients. NVS namespace
`air-mouse`, key `settings`, stores the full record. Invalid saved records fall
back to defaults. The old 36-byte layout is retained so existing saved settings survive. The gyro
flag is cleared on load and configuration writes, and the firmware has no motion
sensor sampling or cursor-generation code. Status fields calibrated and IMU-ready
are reserved zeros; opcode 3 is retired. Defaults enable touch only.

## Physical pairing reset

BOOT/GPIO0 is sampled in the main loop with 30 ms debounce. A stable 3-second
hold arms reset, which executes once after a stable release. Short presses and a
button held at startup do not reset pairing. The hold prompt replaces media while
pressed, and mouse input is suppressed during that hold.

The loop releases HID buttons, abandons incomplete media upload, persists
`air-mouse/reset-pairing=true`, and restarts after release. Before advertising,
the next boot calls NimBLE `ble_store_clear()` and creates/persists a fresh
profile-specific random static address (`ble-compat` or `ble-standard`). The
request is cleared only after those operations succeed. Storage failure halts
with an error rather than claiming a successful reset. Normal reboots retain
the address. Settings and committed media are not erased. Old host-side pairing
entries must be forgotten on those hosts; the watch cannot delete remote caches.

## Media

`AMF1` file header: magic (4 bytes), width:u16=205, height:u16=251,
frame-count:u16 (1…120), reserved:u16=0.
Each frame is `duration-ms:u16, encoded-byte-count:u32` followed by
`run-length:u16, RGB565:u16` pairs. Every run is nonzero and each frame expands
to exactly 51,455 pixels. Durations are 67…60,000 ms. Trailing bytes are rejected.
Whole-file CRC is standard reflected IEEE CRC32 (test vector `123456789` =
`0xCBF43926`). Maximum file length is 3 MiB.

Each flash bank has a 16-byte header: `magic=0x314D4941, generation, length, crc32`.
All are u32. Payload begins after that header. The inactive bank is erased in
4 KiB steps, receives ordered data, then is read into PSRAM and validated. Only
then is the header written as the commit marker. At boot, the newest valid bank
is used, falling back to the older valid bank. A disconnected or idle transfer
is abandoned; the current committed bank is never erased for its replacement.

## Upload reliability (0.2.1)

Image storage is a dedicated data partition, `airmedia`, at `0x1090000` with
length `0x800000`. Bank offsets are 0 and `0x400000`, preserving the previous
physical addresses. The old layout placed these banks inside running app1:
ESP-IDF correctly aborted the first erase as a dangerous executable-partition
write. The installer now shrinks app1 to 4 MiB and installs the checked-in table;
flash protection remains enabled. Firmware refuses an unexpected media partition
type, address or size. No bootloader, app0, NVS or shared filesystem moves.

Transfer activity uses a dedicated wrap-safe clock, checked after flash operations.
An older loop timestamp must not be subtracted unsigned from a newer chunk-write
timestamp: that previously made a fresh upload look idle for billions of
milliseconds and aborted its bank immediately. Commit, abort and disconnect clear
the timer. The existing two-bank/header-last commit behavior is unchanged.

Firmware checks the active NimBLE connection descriptor for encryption and binds
queued commands to that connection. An additional host is rejected without
replacing the active host state. Command handling still requires encryption;
restored subscriptions alone do not authorize writes or mouse reports. USB logging
is nonblocking, so an unread debug port cannot hold up Bluetooth callbacks.
Bonded HID subscriptions delivered before `onConnect` are retained for that
connection handle rather than overwritten by connection initialization.
