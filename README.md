# Air Mouse Watch

Independent experiment for the **Waveshare ESP32-S3-Touch-AMOLED-2.06** and a
native **macOS 13+ menu bar companion**. Firmware and client sources are here;
no other watch project is required to build them.

The watch sends standard Bluetooth LE HID mouse reports directly to macOS.
The companion configures the watch and transfers images; it does not synthesize
mouse events and needs no Accessibility permission. First-time Bluetooth pairing
is still required. This revision is a **touch-only mouse**: gyro pointing and
calibration have been removed. The previous revision established an encrypted
HID link on this Mac, including with the companion disconnected. See
[verification](docs/VERIFICATION.md) for checks on the latest installed build.
Static image upload, image persistence after restart, and touch cursor movement
have been verified on the watch. Pairing reset and use on another host have also
been confirmed. GIF display, detailed gesture behavior and OpenMouse browser
access still need physical testing.

## Behavior

| Input | Action |
| --- | --- |
| Wrist rotation | No cursor movement |
| Deliberate finger movement | Touchpad-style cursor movement |
| Fast edge-to-opposite-edge swipe | Vertical wheel or horizontal pan burst |
| Tap | Left click |
| Stationary hold | One right click, with no extra click on release |
| Hold BOOT for 3 seconds, then release | Clear pairing and restart for a new host |

To switch devices, **while the watch is already running**, hold its **BOOT**
button for three seconds. When the screen says **Release to reset pairing**,
release it. A shorter press cancels. The watch restarts, deletes Bluetooth bonds,
and saves a fresh random static identity so the previous host cannot immediately
reclaim it. Select **Air Mouse Watch** in the new device's Bluetooth settings.
No companion is required. Only one active host is supported; this is re-pairing,
not a multi-device slot switcher.

Settings and the saved image/GIF are retained. The previous device may retain an
obsolete watch entry; remove it with its Bluetooth **Forget** action. This button
resets pairing only, not all preferences. BOOT is GPIO0, active low, per the
[Waveshare hardware documentation](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-2.06).
Do not hold it while powering on: that selects firmware download mode. Firmware
waits for release before restarting for the same reason. The power button keeps
its hardware power behavior.

Interior finger movement starts immediately after a 10-pixel movement threshold.
Edge contacts wait up to the configured swipe window (420 ms by default) before
becoming cursor movement. This avoids moving the pointer during a quick scroll.
Touch movement does not hold the left button for dragging desktop objects.

## Build and run the Mac companion

Requires Xcode or Command Line Tools with Swift 6.0+. No third-party Swift
packages are used. Builds on the current machine target its architecture.

```sh
git clone https://github.com/thefallenmerc/air-mouse-watch.git
cd air-mouse-watch
python3 tools/macos.py build
open 'dist/Air Mouse.app'
```

The resulting app is ad-hoc signed for local use, not notarized for distribution.
Its menu bar icon opens settings for connection, touch and display.
Grant Bluetooth access when macOS asks. Pair **Air Mouse Watch** in macOS
Bluetooth settings, then connect it in the app's Watch tab. Save settings with
**Save to watch**; sliders edit a draft until saved.

The watch saves settings in NVS. The Mac remembers only the selected peripheral
identifier in UserDefaults and reconnects to it. Closing the app does not remove
the watch's saved configuration or images. Pause is a runtime state that resets
on a Bluetooth disconnect or watch restart.

Use `open 'dist/Air Mouse.app' --args --preview` for a disconnected UI preview
that does not initialize Bluetooth.

## Images and GIFs

Choose an image in Display, select **Fit**, **Fill / crop**, or **Resize**, and
send it to the watch. Crop position sliders choose the retained area in Fill
mode. Transparent regions are composited on black.

The Mac decodes media using ImageIO, resizes it to 205×251, converts to RGB565,
and encodes run-length-compressed frames. The watch expands each pixel 2× to
fill its 410×502 panel. GIFs are sampled at up to 15 frames per second and loop;
limits are 60 seconds, 120 output frames and 3 MiB encoded size. Some detailed
GIFs exceed the storage limit and must be shortened or sent as a still frame.
The preview shows the first encoded frame. Photo metadata orientation and unusual
GIF disposal modes still need broader fixture coverage.

Uploads are acknowledged in chunks and can take several minutes. The previous
image remains active until length, CRC and frame structure all validate. Two
reserved flash banks protect the previous image from incomplete transfers.
Flash erases and final validation can briefly affect pointer latency during an
upload; normal pointing does not require media transfer or the client.

## Build the watch firmware

Requires Arduino CLI (or the CLI bundled with Arduino IDE) and
`esp32:esp32@3.3.10`. Vendored display and sensor libraries are included and
verified against `dependencies.lock.json`.

```sh
arduino-cli core update-index --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
arduino-cli core install esp32:esp32@3.3.10 --additional-urls https://espressif.github.io/arduino-esp32/package_esp32_index.json
python3 tools/build.py
```

Set `ARDUINO_CLI` and optionally `ARDUINO_CONFIG_FILE` for a non-default toolchain.
The build helper requires only the pinned ESP32 core version in that configured
core directory. Build output and checksum manifest go to `build/firmware/`.

The BLE stack comes from Arduino-ESP32 3.3.10's bundled NimBLE-backed BLE library.
The sketch uses its checked-in 32 MiB partition table. The preserved app0 remains
12.5 MiB; app1 starts at `0xC90000` and is 4 MiB. The separate `airmedia` data
partition starts at `0x1090000` and contains two 4 MiB banks. These retain the
original reserved bank addresses, but are outside executable partitions so ESP32
flash protection permits image writes. NVS, shared SPIFFS and coredump locations
are unchanged. The builder validates this exact table and rejects firmware beyond
the current bootloader's 16 MiB boundary. Do not use arbitrary partition defaults.

## Optional OpenMouse compatibility build

The ordinary build keeps its own experimental identity. A separate build adds
the moddoMOUSE feature-report protocol and matching discovery VID so unmodified
OpenMouse can attempt to select its existing driver:

```sh
python3 tools/build.py --openmouse-compat
```

This implements persistent X/Y DPI scaling; 1600 = 1× pointer sensitivity. It
does **not** implement an optical sensor, higher polling rates, or lift-off
settings. It is experimental and has not been verified through macOS/Chrome
Bluetooth feature-report transport. Read [OpenMouse details](docs/OPENMOUSE.md)
before using it. The companion works with either firmware build.

## Explicit installation and restore

Build commands never flash. To install when ready, pass the exact serial port:

```sh
python3 tools/flash.py install --port /dev/cu.usbmodem11401
# Or, after building the alternative profile:
python3 tools/flash.py install --port /dev/cu.usbmodem11401 --openmouse-compat
```

The installer reads a complete 32 MiB backup, validates the existing partition
table, then writes the new firmware to **app1**, installs the dedicated media
partition table, and changes the OTA boot selector. Only the original vendor
layout and this project's exact media layout are accepted.
This replaces the previous app1 experiment; the original app0 stays untouched.
The backup includes both apps, settings, images, boot metadata and other flash
contents. It is kept in this project's timestamped `backups/` folder.

The installer verifies the bytes written. That does not verify the display,
touchscreen, Bluetooth connection or touch behavior physically. If writing fails, it
attempts to restore the previous partition table and select the preserved app0.
To restore the full original device state:

```sh
python3 tools/flash.py restore --port /dev/cu.usbmodem11401 --backup backups/TIMESTAMP
```

Restore overwrites all flash with the selected checksum-verified backup, including
settings changed since that backup. Keep backups private and out of developer ZIPs.

## Tests and source map

```sh
python3 tools/test.py
```

| Path | Responsibility |
| --- | --- |
| `firmware/AirMouse/Pointer.h` | Settings and touch gesture classification |
| `firmware/AirMouse/AirMouse.ino` | BLE HID, encrypted configuration commands and scheduling |
| `firmware/AirMouse/Hardware.h` | BOOT input, FT3168 touch and battery I²C access |
| `firmware/AirMouse/PairingButton.h` | Debounced hold/release pairing reset |
| `firmware/AirMouse/MediaStore.h` | Two-bank flash storage and validation |
| `firmware/AirMouse/Display.h` | Chunked AMOLED updates and frame playback |
| `firmware/AirMouse/OpenMouseProfile.h` | Experimental feature-report codec and pointer scaling |
| `macos/Sources/AirMouse/WatchClient.swift` | CoreBluetooth, acknowledgements and reconnects |
| `macos/Sources/AirMouse/MediaEncoder.swift` | Image/GIF preparation and compression |
| `macos/Sources/AirMouse/AirMouseApp.swift` | Native menu bar and settings UI |

See [protocol](docs/PROTOCOL.md) and [verification](docs/VERIFICATION.md) for exact
formats and the distinction between implemented, software-tested and hardware-tested.

Hardware reference: [Waveshare board documentation](https://www.waveshare.com/wiki/ESP32-S3-Touch-AMOLED-2.06).

Vendored library sources and their existing notices are retained under `vendor/`;
their source revision and file checksums are pinned in `dependencies.lock.json`.
Device backups, diagnostic logs, generated apps and build caches stay local and
are excluded from this repository. Verification notes reference those local logs;
they are not distributed with the source.
