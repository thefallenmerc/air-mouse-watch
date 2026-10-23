"""Stage the pinned BLE library with project-local HID registration/security fixes."""
import hashlib
import pathlib
import shutil


def source_files(root):
    return [root / 'library.properties'] + sorted((root / 'src').rglob('*'))


def digest(root):
    h = hashlib.sha256()
    for path in source_files(root):
        if path.is_file():
            h.update(path.relative_to(root).as_posix().encode() + b'\0')
            h.update(path.read_bytes() + b'\0')
    return h.hexdigest()


def patch_service(source):
    old = '''#if defined(CONFIG_NIMBLE_ENABLED)
  if (pExisting != nullptr) {
    pExisting->m_removed = 0;
  } else
#endif
  {'''
    new = '''#if defined(CONFIG_NIMBLE_ENABLED)
  // A service may contain multiple HID Reports (0x2a4d). Only resurrect
  // the same object; distinct objects with the same UUID must be registered.
  if (pExisting == pCharacteristic) {
    pExisting->m_removed = 0;
  } else
#endif
  {'''
    if source.count(old) != 1:
        raise ValueError('Pinned BLEService duplicate-characteristic patch no longer applies')
    return source.replace(old, new)


def prepare(source, destination, expected):
    if digest(source) != expected:
        raise ValueError('Installed BLE library differs from the pinned source; review before building')
    patches = {
        'BLEService.cpp': patch_service((source / 'src/BLEService.cpp').read_text()),
        'BLEHIDDevice.cpp': patch_hid((source / 'src/BLEHIDDevice.cpp').read_text()),
    }
    # Copy only library sources/metadata, preserving unchanged timestamps.
    for path in source_files(source):
        if not path.is_file():
            continue
        target = destination / path.relative_to(source)
        payload = patches[path.name].encode() if path.name in patches else path.read_bytes()
        if not target.exists() or target.read_bytes() != payload:
            target.parent.mkdir(parents=True, exist_ok=True)
            if path.name in patches:
                target.write_bytes(payload)
            else:
                shutil.copy2(path, target)
    return destination


def patch_hid(source):
    old = 'm_reportMapCharacteristic = m_hidService->createCharacteristic((uint16_t)0x2a4b, BLECharacteristic::PROPERTY_READ);'
    new = '''// Hold HID enumeration at the report-map read until pairing completes.
  // macOS otherwise tries to create its mouse driver before encryption is ready.
  m_reportMapCharacteristic = m_hidService->createCharacteristic((uint16_t)0x2a4b, BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_READ_ENC);'''
    if source.count(old) != 1:
        raise ValueError('Pinned HID report-map security patch no longer applies')
    return source.replace(old, new)
