import binascii,hashlib,importlib.util,pathlib,struct,unittest
path=pathlib.Path(__file__).resolve().parents[1]/'tools/boot.py'
spec=importlib.util.spec_from_file_location('boot',path);boot=importlib.util.module_from_spec(spec);spec.loader.exec_module(boot)
def selector(sequence=1):
    data=bytearray(b'\xff'*8192);struct.pack_into('<I',data,0,sequence)
    struct.pack_into('<II',data,24,2,binascii.crc32(struct.pack('<I',sequence),0xffffffff)&0xffffffff)
    return bytes(data)
def table_for(layout):
    data=b''.join(struct.pack('<HBBII16sI',0x50aa,*entry,name.encode(),0) for name,entry in layout.items())
    data+=b'\xeb\xeb'+b'\xff'*14+hashlib.md5(data).digest()
    return data.ljust(0x1000,b'\xff')
class BootTests(unittest.TestCase):
    def test_original_and_media_layouts(self):
        for layout in [boot.ORIGINAL_LAYOUT,boot.MEDIA_LAYOUT]:
            table=table_for(layout)
            self.assertEqual(boot.parse_table(table),layout)
            self.assertEqual(boot.parse_table(table[:0xc00]),layout)
            system=bytearray(b'\xff'*0x9000)
            system[:0x1000]=table;system[0x6000:0x8000]=selector();system[0x8000:0x8002]=b'\xe9\x06'
            self.assertEqual(boot.validate_system(system),(selector(),boot.select_preview(selector())))
        for name in ['nvs','otadata','app0','spiffs','coredump']:
            self.assertEqual(boot.ORIGINAL_LAYOUT[name],boot.MEDIA_LAYOUT[name])
        self.assertEqual(boot.MEDIA_LAYOUT['airmedia'][2],boot.APP1+0x400000)
    def test_changed_layout_or_checksum_rejected(self):
        for name,entry in boot.MEDIA_LAYOUT.items():
            changed=dict(boot.MEDIA_LAYOUT);changed[name]=(*entry[:2],entry[2]+0x1000,entry[3])
            with self.assertRaises(ValueError):boot.parse_table(table_for(changed))
        corrupt=bytearray(table_for(boot.MEDIA_LAYOUT));corrupt[12]^=1
        with self.assertRaises(ValueError):boot.parse_table(corrupt)
        with self.assertRaises(ValueError):boot.parse_table(b'\xff'*0x1000)
    def test_select_slot_preserves_previous_entry(self):
        original=selector()
        for slot in [0,1]:
            changed=boot.select_slot(original,slot);self.assertEqual(changed[:4096],original[:4096])
            seq=struct.unpack_from('<I',changed,4096)[0];self.assertEqual((seq-1)%2,slot)
            self.assertEqual(struct.unpack_from('<I',changed,4124)[0],binascii.crc32(struct.pack('<I',seq),0xffffffff)&0xffffffff)
    def test_invalid_selectors_rejected(self):
        for data in [b'',b'\xff'*8192,b'\0'*8192]:
            with self.assertRaises(ValueError):boot.select_slot(data,1)
        with self.assertRaises(ValueError):boot.select_slot(selector(),2)
        with self.assertRaises(ValueError):boot.select_slot(selector(0xfffffffc),1)
    def test_unknown_partition_table_rejected(self):
        with self.assertRaises(ValueError):boot.validate_system(b'\xff'*0x9000)
if __name__=='__main__':unittest.main()
