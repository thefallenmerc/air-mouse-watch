#!/usr/bin/env python3
"""Explicit installation with a complete device backup. No automatic port selection."""
import argparse, datetime, hashlib, json, os, pathlib, shutil, subprocess, sys, time
import boot
ROOT=pathlib.Path(__file__).resolve().parents[1]
def sha(data): return hashlib.sha256(data).hexdigest()
def esptool():
    if os.environ.get('ESPTOOL'): return [os.environ['ESPTOOL']]
    if shutil.which('esptool'): return [shutil.which('esptool')]
    bundled=pathlib.Path.home()/'Library/Arduino15/packages/esp32/tools/esptool_py/5.3.0/esptool'
    if bundled.is_file(): return [str(bundled)]
    raise RuntimeError('Install esptool 5.3.0 or set ESPTOOL to its executable.')
def run(args):
    print('Running:', ' '.join(map(str,args)),flush=True);subprocess.run(list(map(str,args)),check=True)
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('action',choices=['install','restore'])
    parser.add_argument('--port',required=True)
    parser.add_argument('--backup',type=pathlib.Path,help='Backup directory to restore')
    parser.add_argument('--openmouse-compat',action='store_true',help='Install the experimental OpenMouse profile; see docs/OPENMOUSE.md')
    parser.add_argument('--baud',type=int,choices=[115200,230400,460800,921600],default=115200)
    args=parser.parse_args()
    base=esptool()+['--chip','esp32s3','--port',args.port,'--baud',str(args.baud)]
    if args.action=='restore':
        if not args.backup: parser.error('restore requires --backup')
        record=json.loads((args.backup/'manifest.json').read_text()); image=args.backup/'full-flash.bin'
        if image.stat().st_size != 0x2000000 or sha(image.read_bytes()) != record['backup_sha256']: raise RuntimeError('Backup checksum or length mismatch')
        run(base+['write-flash','0',image]); run(base+['verify-flash','0',image]);return
    build=ROOT/('build/firmware-openmouse' if args.openmouse_compat else 'build/firmware')
    binary=build/'AirMouse.ino.bin'
    manifest=json.loads((build/'manifest.json').read_text())
    payload=binary.read_bytes()
    table=build/'AirMouse.ino.partitions.bin'
    if sha(table.read_bytes()) != manifest['partition_sha256'] or boot.parse_table(table.read_bytes()) != boot.MEDIA_LAYOUT: raise RuntimeError('Invalid media partition table; rebuild before installing')
    if not 0 < len(payload) <= boot.MAX_FIRMWARE or sha(payload) != manifest['sha256']: raise RuntimeError('Build a valid firmware before installing')
    directory=ROOT/'backups'/datetime.datetime.now(datetime.timezone.utc).strftime('%Y%m%dT%H%M%S.%fZ')
    directory.mkdir(parents=True)
    backup=directory/'full-flash.bin'
    print('Backing up all 32 MiB before any flash write.',flush=True)
    for attempt in range(3):
        try:
            run(base+['read-flash','0','0x2000000',backup])
            break
        except subprocess.CalledProcessError:
            if attempt==2: raise
            print('Read-only backup interrupted; retrying with a fresh USB connection. No flash writes performed.',flush=True)
            time.sleep(2)
    original=backup.read_bytes()
    if len(original)!=0x2000000: raise RuntimeError('Incomplete backup; no writes performed')
    ota,selection=boot.validate_system(original[0x8000:0x11000])
    (directory/'air-mouse-otadata.bin').write_bytes(selection)
    (directory/'original-otadata.bin').write_bytes(ota)
    (directory/'original-partitions.bin').write_bytes(original[0x8000:0x9000])
    (directory/'app0-otadata.bin').write_bytes(boot.select_slot(ota,0))
    (directory/'manifest.json').write_text(json.dumps({'port':args.port,'backup_sha256':sha(original),'firmware_sha256':sha(payload)},indent=2)+'\n')
    try:
        run(base+['write-flash',hex(boot.APP1),binary,'0x8000',table,hex(boot.OTA),directory/'air-mouse-otadata.bin'])
        run(base+['verify-flash',hex(boot.APP1),binary,'0x8000',table,hex(boot.OTA),directory/'air-mouse-otadata.bin'])
    except BaseException:
        print('Installation did not finish; selecting the preserved app0 firmware.',flush=True)
        run(base+['write-flash','0x8000',directory/'original-partitions.bin',hex(boot.OTA),directory/'app0-otadata.bin'])
        raise
    print('Installed in app1. Existing app0 preserved. Complete restore backup: '+str(directory))
    print('Pair Air Mouse Watch in Bluetooth settings if needed. Hold BOOT for 3 seconds then release to reset pairing.')
    print('Flash bytes were verified; physical behavior still needs testing.')
if __name__=='__main__':
    try:main()
    except (RuntimeError,OSError,ValueError,subprocess.CalledProcessError) as error:sys.exit(str(error))
