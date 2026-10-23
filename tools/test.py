#!/usr/bin/env python3
import pathlib,subprocess,sys
root=pathlib.Path(__file__).resolve().parents[1];(root/'build').mkdir(exist_ok=True)
subprocess.run([sys.executable,str(root/'tools/macos.py'),'test'],check=True)
subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined','-g',str(root/'tests/pointer_test.cpp'),'-o',str(root/'build/pointer-tests')],check=True)
subprocess.run([str(root/'build/pointer-tests'),str(root/'build/test-media.amf')],check=True)
subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined','-g','-I'+str(root/'tests/stubs'),str(root/'tests/media_store_test.cpp'),'-o',str(root/'build/media-store-tests')],check=True)
subprocess.run([str(root/'build/media-store-tests')],check=True)
subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined','-g','-I'+str(root/'tests/stubs'),str(root/'tests/hid_reports_test.cpp'),'-o',str(root/'build/hid-reports-tests')],check=True)
subprocess.run([str(root/'build/hid-reports-tests')],check=True)
subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined','-g','-I'+str(root/'tests/stubs'),str(root/'tests/peer_address_cache_test.cpp'),'-o',str(root/'build/peer-address-cache-tests')],check=True)
subprocess.run([str(root/'build/peer-address-cache-tests')],check=True)
subprocess.run([sys.executable,'-m','unittest','discover','-s',str(root/'tests'),'-p','test_*.py'],check=True)
