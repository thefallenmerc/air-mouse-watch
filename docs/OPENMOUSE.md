# Experimental OpenMouse profile

Target: [control.openmouse.app](https://control.openmouse.app/).
Protocol reference: [OpenMouse mouse-protocol](https://github.com/OpenMouse-Project/mouse-protocol),
revision `bac031567b28f2a55c616fc525b03f2d5fa452cc`, inspected 2026-10-22.
Relevant files are `src/moddo/index.ts`, `src/drivers/moddo/hid.ts`, and the
driver registry. This project implements the packet layout independently and
does not bundle or import OpenMouse application code.

## Why a separate firmware build

OpenMouse selects hardware-specific drivers using discovery information. A
standard mouse report descriptor is sufficient for cursor input but does not
provide a generic configuration protocol. The moddo driver accepts VID `0x2FE3`
with usage page `0x00FF` and usage `0x01` (or legacy `0x02`).

The normal Air Mouse build uses experimental VID `0xFFFF`, PID `0xA101`. The
compatibility build deliberately substitutes VID `0x2FE3`, retains PID `0xA101`,
and adds a separate configuration collection. Its product/manufacturer strings
continue to identify the Air Mouse experiment. This is local protocol emulation,
not a genuine moddo product, allocated vendor identity, certified device, or
upstream-supported Air Mouse driver. Do not ship it as a commercial identity.

Switching identities or report descriptors may require forgetting and re-pairing
the device in macOS because the OS can cache HID descriptors.

The PnP identity characteristic is encoded directly in little-endian order, as
required by the [Bluetooth Device Information Service](https://www.bluetooth.com/wp-content/uploads/Files/Specification/HTML/DIS_v1.2/out/en/index-en.html).
This avoids the reversed integer order in ESP32 core 3.3.10's `pnp()` helper.

## Implemented reports

| Feature ID | Length | Contents |
| --- | --- | --- |
| 2 | 11 | polling u16, lift u8, DPI X u16, DPI Y u16, invert/swap u8, angle i16, deep-sleep u8 |
| 3 | 8 | no dongle version, actual experiment firmware version 0.2.1 |
| 4 | 4 | battery percent, reserved, unknown charge-state code, presence flag when data is available |

All integers are little-endian. Feature payloads in GATT omit the HID report-ID
byte; the report-reference descriptor carries it. The macOS WebHID layer is
expected to add the report-ID byte when returning a feature report to OpenMouse.
That transport behavior still needs verification on the actual watch.

DPI X and Y accept 50–26000 in steps of 50. They multiply touch
cursor movement by `DPI / 1600`; this is a sensitivity scale, not optical DPI.
They are persisted in the same settings record as the native app. The app polls
the settings checksum and reads changed settings back after OpenMouse writes.
Unsaved local edits remain a draft; saving that draft replaces the watch settings.

The report returns a firmware sampling/report target of 125 Hz. BLE scheduling
and notifications may reduce actual delivered frequency. Writes requesting
250/500/1000 Hz are rejected and read back as 125 Hz. Lift-off distance is 0
(not applicable); nonzero lift-off, angle, inversion, and deep-sleep requests are
rejected. OpenMouse's existing driver may still display those unsupported controls.
The driver also labels a device without a dongle version as USB; that label does
not accurately describe this BLE watch.

## Verification still needed

1. Flash the compatibility build and pair it in macOS.
2. Confirm normal mouse operation with the companion closed.
3. Open OpenMouse in Chrome or Edge and inspect the device picker.
4. Confirm the vendor collection is selectable and feature reads succeed.
5. Change DPI X/Y, verify physical sensitivity changes, reboot, and read them back.
6. Confirm unsupported polling and lift-off changes do not alter the stored profile.
7. Open the native companion and check it reads the changed DPI values.

No end-to-end OpenMouse success is claimed yet. If macOS/Chromium does not expose
the BLE feature collection, a native OpenMouse bridge integration or an upstream
Air Mouse driver will be required. The standard mouse and native configuration
service are independent of that limitation.

The encoded default packet was decoded and round-tripped with the upstream
codec at the pinned revision. Local C++ tests cover valid DPI writes, unsupported
rate/lift rejection and the actual cursor scaling. This verifies bytes and
behavior in software, not the Bluetooth/WebHID path.
