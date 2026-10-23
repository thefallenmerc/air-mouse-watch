import pathlib
import subprocess
import sys
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
import ble_library

HID_MAP = 'm_reportMapCharacteristic = m_hidService->createCharacteristic((uint16_t)0x2a4b, BLECharacteristic::PROPERTY_READ);'

HARNESS = r'''
#include <vector>
#include <cassert>
#define CONFIG_NIMBLE_ENABLED 1
#define log_v(...)
#define log_d(...)
#define log_w(...)
struct BLECharacteristic { int uuid; int m_removed=0; int getUUID(){return uuid;} };
struct Map {
  std::vector<BLECharacteristic*> values;
  BLECharacteristic *getByUUID(int uuid){for(auto p:values)if(p->uuid==uuid)return p;return nullptr;}
  void setByUUID(BLECharacteristic *p,int){for(auto q:values)if(p==q)return;values.push_back(p);}
};
struct Server { void serviceChanged(){} };
struct BLEService {
  Map m_characteristicMap; Server server;
  Server *getServer(){return &server;}
  void addCharacteristic(BLECharacteristic*);
};
'''


class BLELibraryTests(unittest.TestCase):
    def test_real_registration_function_keeps_all_hid_reports(self):
        source = (ROOT / 'tests/fixtures/ble-service-add-characteristic.cpp').read_text()
        patched = ble_library.patch_service(source)
        with tempfile.TemporaryDirectory() as tmp:
            folder = pathlib.Path(tmp)
            # Reproduce the old failure, then run the same operations on the
            # patched upstream method: input + three feature report objects.
            for body, expected in [(source, 1), (patched, 4)]:
                test = HARNESS + body + f'''
int main(){{
  BLEService s;BLECharacteristic a{{0x2a4d}},b{{0x2a4d}},c{{0x2a4d}},d{{0x2a4d}};
  s.addCharacteristic(&a);s.addCharacteristic(&b);s.addCharacteristic(&c);s.addCharacteristic(&d);
  assert(s.m_characteristicMap.values.size()=={expected});
  a.m_removed=1;s.addCharacteristic(&a);assert(a.m_removed==0);
  assert(s.m_characteristicMap.values.size()=={expected});
}}
'''
                (folder / 'test.cpp').write_text(test)
                subprocess.run(['clang++', '-std=c++17', '-fsanitize=address,undefined',
                                str(folder / 'test.cpp'), '-o', str(folder / 'test')], check=True)
                subprocess.run([str(folder / 'test')], check=True)

    def test_mismatch_fails_without_touching_source(self):
        with tempfile.TemporaryDirectory() as tmp:
            source = pathlib.Path(tmp) / 'upstream'
            (source / 'src').mkdir(parents=True)
            (source / 'library.properties').write_text('name=BLE\n')
            fixture = (ROOT / 'tests/fixtures/ble-service-add-characteristic.cpp').read_text()
            (source / 'src/BLEService.cpp').write_text(fixture)
            (source / 'src/BLEHIDDevice.cpp').write_text(HID_MAP)
            destination = pathlib.Path(tmp) / 'staged'
            expected = ble_library.digest(source)
            ble_library.prepare(source, destination, expected)
            self.assertEqual((source / 'src/BLEService.cpp').read_text(), fixture)
            self.assertIn('pExisting == pCharacteristic', (destination / 'src/BLEService.cpp').read_text())
            mtime = (destination / 'src/BLEService.cpp').stat().st_mtime_ns
            ble_library.prepare(source, destination, expected)
            self.assertEqual((destination / 'src/BLEService.cpp').stat().st_mtime_ns, mtime)
            (source / 'src/BLEService.cpp').write_text(fixture + '// changed\n')
            with self.assertRaises(ValueError):
                ble_library.prepare(source, destination, expected)

    def test_patch_refuses_unknown_source(self):
        with self.assertRaises(ValueError):
            ble_library.patch_service('unrecognized source')
        with self.assertRaises(ValueError):
            ble_library.patch_hid('unrecognized source')

    def test_report_map_blocks_enumeration_until_encrypted(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = pathlib.Path(tmp)
            for source, expected in [(HID_MAP, 1), (ble_library.patch_hid(HID_MAP), 3)]:
                test = r'''
#include <cassert>
#include <cstdint>
struct BLECharacteristic { static constexpr int PROPERTY_READ=1,PROPERTY_READ_ENC=2; };
struct Service { int flags=0;void *createCharacteristic(uint16_t uuid,int f){assert(uuid==0x2a4b);flags=f;return this;} };
int main(){Service service;auto m_hidService=&service;void *m_reportMapCharacteristic;
''' + source + f'\nassert(service.flags=={expected});}}\n'
                (folder / 'map.cpp').write_text(test)
                subprocess.run(['clang++', '-std=c++17', '-fsanitize=address,undefined',
                                str(folder / 'map.cpp'), '-o', str(folder / 'map')], check=True)
                subprocess.run([str(folder / 'map')], check=True)


if __name__ == '__main__':
    unittest.main()
