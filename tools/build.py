import argparse
import concurrent.futures
import hashlib
import json
import os
import re
from pathlib import Path
import shutil
import struct
import subprocess
import zlib

ROOT = Path(__file__).resolve().parents[1]
SDK_COMMIT = 'a46e0086f1ffb5e5502703970bff94888e67f4cb'
CONFIG = {name: int(value, 0) for name, value in re.findall(
    r'^#define\s+(\w+)\s+(0x[0-9A-Fa-f]+|[0-9]+)u?\s*$',
    (ROOT / 'include/layout.h').read_text(encoding='utf-8'), re.MULTILINE)}
APP_BASE, APP_CAPACITY = CONFIG['APP_BASE'], CONFIG['APP_CAPACITY']
IAP_BASE, IAP_CAPACITY = CONFIG['IAP_BASE'], CONFIG['IAP_CAPACITY']
BOARD_ID, VERSION = CONFIG['BOARD_ID'], CONFIG['FW_VERSION']


def run(args):
    result = subprocess.run([str(a) for a in args], capture_output=True, text=True,
                            encoding='utf-8', errors='replace')
    if result.returncode:
        raise RuntimeError(' '.join(str(a) for a in args) + '\n' + result.stdout + result.stderr)
    return result.stdout + result.stderr


def hex_record(address, kind, payload):
    data = bytes([len(payload)]) + address.to_bytes(2, 'big') + bytes([kind]) + payload
    return ':' + (data + bytes([-sum(data) & 255])).hex().upper() + '\n'


def startup_mstatus(disassembly):
    value = None
    for line in disassembly.splitlines():
        match = re.match(r'^\s*[0-9a-fA-F]+:\s+[0-9a-fA-F]+\s+(\w+)\s+(.+)', line)
        if not match:
            continue
        opcode, arguments = match.groups()
        args = arguments.split('#', 1)[0].strip().split(',')
        if opcode == 'li' and args[0] == 't0':
            value = int(args[1], 0)
        elif opcode == 'lui' and args[0] == 't0':
            value = int(args[1], 0) << 12
        elif opcode == 'addi' and args[:2] == ['t0', 't0'] and value is not None:
            value += int(args[2], 0)
        elif opcode == 'csrw' and args == ['mstatus', 't0']:
            if value is None or value & 0x1800 != 0x1800:
                raise ValueError('Startup would leave machine mode before the boot handoff')
            return value
    raise ValueError('Cannot verify the linked startup mstatus')


def write_hex(path, segments):
    lines, upper = [], None
    for base, payload in sorted(segments):
        for offset in range(0, len(payload), 16):
            address = base + offset
            if address >> 16 != upper:
                upper = address >> 16
                lines.append(hex_record(0, 4, upper.to_bytes(2, 'big')))
            lines.append(hex_record(address & 65535, 0, payload[offset:offset + 16]))
    lines.append(hex_record(0, 1, b''))
    path.write_text(''.join(lines), encoding='utf-8', newline='\n')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--sdk', type=Path, default=ROOT / 'vendor/ch592')
    parser.add_argument('--toolchain', type=Path, default=Path(os.environ.get('WCH_TOOLCHAIN', 'C:/tc/gcc/bin')))
    parser.add_argument('--prefix', default='riscv-none-embed-')
    parser.add_argument('--profile', choices=['ios', 'android'], default='ios',
                        help='iOS: keyboard/mouse; Android: keyboard/mouse/touchscreen')
    options = parser.parse_args()
    sdk = options.sdk.resolve()
    suffix = '.exe' if os.name == 'nt' else ''
    gcc = options.toolchain / (options.prefix + 'gcc' + suffix)
    objcopy = options.toolchain / (options.prefix + 'objcopy' + suffix)
    size_tool = options.toolchain / (options.prefix + 'size' + suffix)
    objdump = options.toolchain / (options.prefix + 'objdump' + suffix)
    if not gcc.is_file():
        found = shutil.which(options.prefix + 'gcc')
        if not found:
            raise SystemExit('Compiler missing: pass --toolchain PATH or set WCH_TOOLCHAIN.')
        gcc = Path(found)
        objcopy = gcc.with_name(options.prefix + 'objcopy' + suffix)
        size_tool = gcc.with_name(options.prefix + 'size' + suffix)
        objdump = gcc.with_name(options.prefix + 'objdump' + suffix)
    revision = run(['git', '-C', sdk, 'rev-parse', 'HEAD']).strip()
    if revision != SDK_COMMIT:
        raise SystemExit('SDK revision mismatch: expected ' + SDK_COMMIT)
    if run(['git', '-C', sdk, 'status', '--porcelain']).strip():
        raise SystemExit('Official SDK has local changes; use a clean pinned checkout.')
    src = sdk / 'EVT/EXAM/SRC'
    ble = sdk / 'EVT/EXAM/BLE'
    build, dist = ROOT / 'build', ROOT / 'dist'
    build.mkdir(exist_ok=True)
    dist.mkdir(exist_ok=True)
    common = ['-march=rv32imac', '-mabi=ilp32', '-msmall-data-limit=8', '-Os', '-g',
              '-ffunction-sections', '-fdata-sections', '-fno-common',
              '-DHID_HAS_TOUCH=' + str(int(options.profile == 'android'))]
    includes = [ROOT / 'include', src / 'StdPeriphDriver/inc', src / 'RVMSIS',
                ble / 'HAL/include', ble / 'LIB']
    common += ['-I' + str(path) for path in includes]
    driver_names = ['clk', 'sys', 'gpio', 'uart1', 'flash', 'pwr', 'adc']
    drivers = [src / ('StdPeriphDriver/CH59x_' + name + '.c') for name in driver_names]
    drivers += [src / 'RVMSIS/core_riscv.c']
    app_startup = ble / 'BackupUpgrade_OTA/Startup/startup_CH592.S'
    boot_startup = ble / 'BackupUpgrade_IAP/Startup/startup_CH592.S'
    diagnostics = ['src/diag_format.c', 'src/diag_ch592.c', 'src/diag_trap.S']
    linker_template = (ble / 'BackupUpgrade_OTA/Ld/Link.ld').read_bytes().decode('gb18030')
    log = []

    def compile_image(name, own_files, extras, base, capacity, definitions):
        own_files = own_files + diagnostics
        definitions = definitions + ['-DDEBUG=1', '-Dprintf=diag_printf',
                                      '-DDIAG_STAGE=' + chr(34) + name.upper() + chr(34)]
        folder = build / name
        folder.mkdir(exist_ok=True)
        script = linker_template.replace('ORIGIN = 0x00001000, LENGTH = 216K',
                                         f'ORIGIN = 0x{base:08X}, LENGTH = {capacity}')
        script = script.replace('__stack_size = 512;', '__stack_size = 1536;')
        script += '\nASSERT(_ebss <= _susrstack, '+chr(34)+'RAM overlaps stack'+chr(34)+');\n'
        linker = folder / 'Link.ld'
        linker.write_text(script, encoding='utf-8', newline='\n')
        sources = [ROOT / item for item in own_files] + extras
        objects = [folder / f'{index:02d}_{p.stem}.o' for index, p in enumerate(sources)]

        def compile_one(pair):
            path, output = pair
            warnings = ['-Wall', '-Wextra', '-Werror'] if path in sources[:len(own_files)] else ['-w']
            return run([gcc, *common, *definitions, *warnings, '-c', path, '-o', output])

        with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
            futures = [pool.submit(compile_one, pair) for pair in zip(sources, objects)]
            errors = []
            for future in futures:
                try:
                    log.append(future.result())
                except RuntimeError as error:
                    errors.append(str(error))
            if errors:
                raise RuntimeError('\n'.join(errors))
        elf = dist / (name + '.elf')
        libraries = [src / 'StdPeriphDriver/libISP592.a']
        if name == 'app':
            libraries.insert(0, ble / 'LIB/LIBCH59xBLE.a')
        log.append(run([gcc, *common, '-nostartfiles', '-specs=nano.specs', '-specs=nosys.specs',
                        '-Wl,--gc-sections', '-Wl,-Map=' + str(dist / (name + '.map')),
                        '-T', linker, '-o', elf, *objects, '-Wl,--start-group', *libraries,
                        '-lc', '-lm', '-Wl,--end-group']))
        binary = dist / (name + '.bin')
        run([objcopy, '-O', 'binary', elf, binary])
        payload = binary.read_bytes()
        payload += b'\xff' * (-len(payload) % 4)
        if len(payload) > capacity:
            raise RuntimeError(f'{name}: image exceeds partition')
        binary.write_bytes(payload)
        write_hex(dist / (name + '.hex'), [(base, payload)])
        report = run([size_tool, elf])
        startup_listing = run([objdump, '-d', '--disassemble=handle_reset', elf])
        startup_mstatus(startup_listing)
        (dist / (name + '.startup.txt')).write_text(startup_listing, encoding='utf-8')
        print(report.strip())
        log.append(report)
        return payload

    definitions = ['-DPERIPHERAL_MAX_CONNECTION=2', '-DCENTRAL_MAX_CONNECTION=0',
                   '-DBLE_MEMHEAP_SIZE=12288', '-DBLE_BUFF_MAX_LEN=251', '-DBLE_BUFF_NUM=6',
                   '-DBLE_TX_NUM_EVENT=2', '-DBLE_SNV_NUM=2', '-DHAL_SLEEP=0',
                   '-DHAL_KEY=0', '-DHAL_LED=0', '-DDCDC_ENABLE=0']
    app_files = ['src/' + item + '.c' for item in
                 ['main', 'ble_app', 'services', 'router', 'ota', 'common', 'wire', 'serial_ota', 'storage_ch592']]
    hal = list((ble / 'HAL').glob('*.c'))
    app = compile_image('app', app_files, drivers + hal + [app_startup, ble / 'LIB/ble_task_scheduler.S'],
                        APP_BASE, APP_CAPACITY, definitions)
    app_crc = zlib.crc32(app)
    iap_files = ['boot/main.c'] + ['src/' + item + '.c' for item in
                 ['ota', 'common', 'wire', 'serial_ota', 'storage_ch592', 'boot_copy']]
    iap = compile_image('iap', iap_files, drivers + [boot_startup], IAP_BASE, IAP_CAPACITY,
                        ['-DDUAL_IAP=1', f'-DFACTORY_SIZE={len(app)}u', f'-DFACTORY_CRC=0x{app_crc:08X}u'])
    jump = compile_image('jump', ['boot/jump.c'], drivers + [boot_startup], 0, 4096, [])
    segments = [(0, jump), (APP_BASE, app), (IAP_BASE, iap)]
    full = bytearray(b'\xff' * 0x70000)
    for base, payload in segments:
        full[base:base + len(payload)] = payload
    (dist / 'ch592f_3in1_factory.bin').write_bytes(full)
    write_hex(dist / 'ch592f_3in1_factory.hex', segments)
    header = struct.pack('<8sIIIII', b'VHIDOTA1', BOARD_ID, APP_BASE, len(app), VERSION, app_crc)
    package = header + struct.pack('<I', zlib.crc32(header)) + app
    (dist / 'ch592f_3in1.ota').write_bytes(package)
    manifest = {
        'board': f'{BOARD_ID:08x}',
        'version': f'{VERSION >> 16}.{(VERSION >> 8) & 255}.{VERSION & 255}', 'sdk_commit': revision,
        'compiler': run([gcc, '--version']).splitlines()[0],
        'app_address': APP_BASE, 'app_bytes': len(app), 'app_capacity': APP_CAPACITY,
        'iap_bytes': len(iap), 'jump_bytes': len(jump), 'app_crc32': f'{app_crc:08x}',
        'hardware_validated': False,
        'hid_profile': options.profile,
        'hid_input_reports': [1, 2, 3] if options.profile == 'android' else [1, 2],
        'startup': {'app': str(app_startup.relative_to(sdk)),
                    'iap': str(boot_startup.relative_to(sdk)), 'jump': str(boot_startup.relative_to(sdk))},
        'diagnostics': {'uart': 1, 'rx': 'PA8', 'tx': 'PA9', 'baud': 115200,
                        'format': '8N1', 'heartbeat_ms': 5000, 'mute_on_uart_frame': True},
        'sha256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in dist.iterdir()
                   if p.suffix in ('.bin', '.hex', '.ota')}
    }
    (dist / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n', encoding='utf-8')
    (build / 'build.log').write_text('\n'.join(log), encoding='utf-8')
    print('BUILD_OK: ' + str(dist))


if __name__ == '__main__':
    main()
