"""Checks for this watch's known partition layout and dual-sector OTA selector."""
import binascii
import hashlib
import struct

APP0, APP1, OTA = 0x10000, 0xC90000, 0xE000
SLOT_SIZE = 0xC80000
ORIGINAL_LAYOUT = {"nvs": (1, 2, 0x9000, 0x5000), "otadata": (1, 0, OTA, 0x2000),
                   "app0": (0, 0x10, APP0, SLOT_SIZE), "app1": (0, 0x11, APP1, SLOT_SIZE),
                   "spiffs": (1, 0x82, 0x1910000, 0x6C0000), "coredump": (1, 3, 0x1FF0000, 0x10000)}
MEDIA_LAYOUT = dict(ORIGINAL_LAYOUT, app1=(0, 0x11, APP1, 0x400000),
                    airmedia=(1, 0x40, 0x1090000, 0x800000))
# Keep preview images below the 16-MiB flash boundary: a larger raw app1 image
# was rejected by this board's current bootloader. No bootloader replacement.
MAX_FIRMWARE = 0x1000000-APP1


def parse_table(table):
    if len(table) not in (0xC00, 0x1000): raise ValueError("Invalid partition table size")
    entries, checksum_seen = {}, False
    for offset in range(0, len(table), 32):
        magic = struct.unpack_from("<H", table, offset)[0]
        if magic == 0xFFFF: break
        if magic == 0xEBEB:
            if hashlib.md5(table[:offset]).digest() != table[offset+16:offset+32]:
                raise ValueError("Partition table checksum mismatch")
            checksum_seen = True
            break
        if magic != 0x50AA: raise ValueError("Unrecognized partition table")
        _, kind, subtype, start, length, label, flags = struct.unpack_from("<HBBII16sI", table, offset)
        name = label.split(b"\0", 1)[0].decode("ascii")
        if name in entries: raise ValueError("Duplicate partition label")
        if flags: raise ValueError("Encrypted/flagged partitions are unsupported")
        entries[name] = (kind, subtype, start, length)
    if not checksum_seen or entries not in (ORIGINAL_LAYOUT, MEDIA_LAYOUT):
        raise ValueError("Unsupported partition layout; nothing will be flashed")
    return entries


def validate_system(data):
    if len(data) != 0x9000: raise ValueError("Incomplete system backup")
    parse_table(data[:0x1000])
    if data[0x8000] != 0xE9 or not 1 <= data[0x8001] <= 16:
        raise ValueError("app0 has no recognizable firmware header")
    original = data[0x6000:0x8000]
    return original, select_preview(original)


def select_preview(original):
    return select_slot(original, 1)


def select_slot(original, slot):
    if slot not in (0, 1): raise ValueError("Invalid app slot")
    if len(original) != 8192: raise ValueError("OTA metadata must be exactly 8192 bytes")
    entries = []
    for sector in range(2):
        offset = sector*4096
        seq = struct.unpack_from("<I", original, offset)[0]
        state, crc = struct.unpack_from("<II", original, offset+24)
        valid_crc = binascii.crc32(struct.pack("<I", seq), 0xFFFFFFFF) & 0xFFFFFFFF
        if seq not in (0, 0xFFFFFFFF) and state not in (3, 4) and crc == valid_crc:
            entries.append((seq, sector))
    if not entries: raise ValueError("No valid OTA boot selection")
    if len(entries) == 2 and entries[0][0] == entries[1][0]:
        raise ValueError("Ambiguous equal-sequence OTA entries")
    seq, sector = max(entries)
    if seq >= 0xFFFFFFFC: raise ValueError("OTA sequence exhausted; manual maintenance required")
    new_seq = seq+1
    if (new_seq-1) % 2 != slot: new_seq += 1
    data = bytearray(original)
    offset = (1-sector)*4096
    data[offset:offset+4096] = b"\xff"*4096
    struct.pack_into("<I", data, offset, new_seq)
    struct.pack_into("<II", data, offset+24, 2,
                     binascii.crc32(struct.pack("<I", new_seq), 0xFFFFFFFF) & 0xFFFFFFFF)
    return bytes(data)
