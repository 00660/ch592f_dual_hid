import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser()
    local_tcc = ROOT / 'build/host-tools/tcc/tcc.exe'
    parser.add_argument('--cc', default=os.environ.get('CC') or shutil.which('cc') or
                        shutil.which('gcc') or str(local_tcc))
    args = parser.parse_args()
    output = ROOT / 'build/test_core.exe' if os.name == 'nt' else ROOT / 'build/test_core'
    output.parent.mkdir(exist_ok=True)
    sources = ['tests/test_core.c', 'src/router.c', 'src/common.c', 'src/ota.c',
               'src/boot_copy.c', 'src/wire.c']
    subprocess.run([args.cc, '-std=c99', '-Wall', '-Werror', '-DHID_HAS_TOUCH=1', '-I' + str(ROOT / 'include'),
                    *[str(ROOT / p) for p in sources], '-o', str(output)], check=True)
    subprocess.run([str(output)], check=True)
    for enabled, profile in [(0, 'ios'), (1, 'android')]:
        gatt = output.with_name('test_gatt_' + profile + ('.exe' if os.name == 'nt' else ''))
        subprocess.run([args.cc, '-std=c99', '-Wall', '-Werror', '-DHID_HAS_TOUCH=' + str(enabled),
                        '-I' + str(ROOT / 'tests/stubs'), '-I' + str(ROOT / 'include'),
                        '-I' + str(ROOT / 'vendor/ch592/EVT/EXAM/BLE/LIB'),
                        str(ROOT / 'tests/test_gatt.c'), str(ROOT / 'src/router.c'),
                        str(ROOT / 'src/common.c'), '-o', str(gatt)], check=True)
        subprocess.run([str(gatt), str(ROOT / 'build' / ('report_map_' + profile + '.bin'))], check=True)
    diag = output.with_name('test_diag' + ('.exe' if os.name == 'nt' else ''))
    subprocess.run([args.cc, '-std=c99', '-Wall', '-Werror', '-I' + str(ROOT / 'include'),
                    str(ROOT / 'tests/test_diag.c'), str(ROOT / 'src/diag_format.c'),
                    '-o', str(diag)], check=True)
    subprocess.run([str(diag)], check=True)
    if (ROOT / 'tests/test_protocol.py').exists():
        subprocess.run([sys.executable, '-X', 'utf8', '-m', 'unittest', 'discover',
                        '-s', 'tests', '-p', 'test_*.py', '-v'], cwd=ROOT, check=True)


if __name__ == '__main__':
    main()
