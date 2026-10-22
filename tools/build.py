#!/usr/bin/env python3
"""Build only. Never connects to or flashes a watch."""
import argparse, hashlib, json, os, pathlib, shutil, subprocess, sys
import boot
ROOT = pathlib.Path(__file__).resolve().parents[1]
LOCK = json.loads((ROOT / 'dependencies.lock.json').read_text())
cli = os.environ.get('ARDUINO_CLI') or shutil.which('arduino-cli') or '/Applications/Arduino IDE.app/Contents/Resources/app/lib/backend/resources/arduino-cli'
command = [cli]
config = os.environ.get('ARDUINO_CONFIG_FILE') or str(pathlib.Path.home() / '.arduinoIDE/arduino-cli.yaml')
if pathlib.Path(config).exists(): command += ['--config-file', config]
if not pathlib.Path(cli).exists(): sys.exit('Install Arduino CLI, or set ARDUINO_CLI.')
info = json.loads(subprocess.check_output(command + ['config', 'dump', '--format', 'json']))
info = info.get('config', info)
core = pathlib.Path(info['directories']['data']) / 'packages/esp32/hardware/esp32'
if not (core / LOCK['arduino_core']).is_dir(): sys.exit('Install esp32:esp32@' + LOCK['arduino_core'])
if sorted(p.name for p in core.iterdir() if p.is_dir()) != [LOCK['arduino_core']]: sys.exit('Use an isolated Arduino config containing only esp32:esp32@'+LOCK['arduino_core'])
for relative, digest in LOCK['vendor_sha256'].items():
    if hashlib.sha256((ROOT / relative).read_bytes()).hexdigest() != digest: sys.exit('Dependency changed: ' + relative)
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--openmouse-compat',action='store_true',help='Experimental moddo feature reports and vendor identity; see docs/OPENMOUSE.md')
args=parser.parse_args()
output = ROOT / ('build/firmware-openmouse' if args.openmouse_compat else 'build/firmware')
output.mkdir(parents=True, exist_ok=True)
options=['--build-property','compiler.cpp.extra_flags=-DAIR_MOUSE_OPENMOUSE_COMPAT=1'] if args.openmouse_compat else []
subprocess.run(command + ['compile', '--fqbn', LOCK['fqbn'], '--libraries', str(ROOT/'vendor/libraries'), '--build-path', str(output), '--build-cache-path',str(ROOT/'build/arduino-cache')] + options + [str(ROOT/'firmware/AirMouse')], check=True)
binary = output/'AirMouse.ino.bin'
table = (output/'AirMouse.ino.partitions.bin').read_bytes()
if boot.parse_table(table) != boot.MEDIA_LAYOUT: sys.exit('Build did not use the dedicated media partition table; do not flash.')
if binary.stat().st_size > 0x370000: sys.exit('Firmware exceeds the existing bootloader\'s app1 boundary! Do not flash.')
(output/'manifest.json').write_text(json.dumps({'firmware_bytes':binary.stat().st_size, 'sha256':hashlib.sha256(binary.read_bytes()).hexdigest(), 'openmouse_compatibility':args.openmouse_compat, 'fqbn':LOCK['fqbn'], 'app_slot_bytes':0x400000, 'partition_sha256':hashlib.sha256(table).hexdigest(), 'media_partition_address':0x1090000, 'media_bank_offsets':[0,0x400000]}, indent=2)+'\n')
print('Built; no device was flashed.')
