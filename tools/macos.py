#!/usr/bin/env python3
"""Build/test/package the native menu bar app with caches inside this project."""
import argparse, os, pathlib, plistlib, shutil, subprocess
ROOT=pathlib.Path(__file__).resolve().parents[1]
def swift(action, release=False):
    env=os.environ.copy()
    env['CLANG_MODULE_CACHE_PATH']=str(ROOT/'build/clang-cache')
    env['SWIFTPM_MODULECACHE_OVERRIDE']=str(ROOT/'build/module-cache')
    env['AIR_MOUSE_TEST_MEDIA']=str(ROOT/'build/test-media.amf')
    args=['swift',action,'--disable-sandbox','--package-path',str(ROOT/'macos'),'--scratch-path',str(ROOT/'build/swift'),
          '--cache-path',str(ROOT/'build/swift-cache'),'--config-path',str(ROOT/'build/swift-config'),'--security-path',str(ROOT/'build/swift-security')]
    if release: args+=['-c','release']
    subprocess.run(args,env=env,check=True)
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('action',choices=['build','test']);args=p.parse_args()
    if args.action=='test': swift('test');return
    swift('build',True)
    app=ROOT/'dist/Air Mouse.app'; contents=app/'Contents'; (contents/'MacOS').mkdir(parents=True,exist_ok=True)
    shutil.copy2(ROOT/'build/swift/release/AirMouse',contents/'MacOS/AirMouse')
    info={'CFBundleExecutable':'AirMouse','CFBundleIdentifier':'dev.experiments.airmouse','CFBundleName':'Air Mouse',
          'CFBundleDisplayName':'Air Mouse','CFBundlePackageType':'APPL','CFBundleShortVersionString':'0.2.2',
          'CFBundleVersion':'4','LSMinimumSystemVersion':'13.0','LSUIElement':True,
          'NSBluetoothAlwaysUsageDescription':'Air Mouse uses Bluetooth to configure your watch and transfer images. Mouse input works independently.',
          'NSHighResolutionCapable':True}
    with (contents/'Info.plist').open('wb') as f: plistlib.dump(info,f)
    subprocess.run(['codesign','--force','--sign','-',str(app)],check=True)
    subprocess.run(['codesign','--verify','--strict',str(app)],check=True)
    print('Created '+str(app)+' (ad-hoc signed for local use, not notarized).')
if __name__=='__main__':main()
