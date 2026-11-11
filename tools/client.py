import argparse
import asyncio
from contextlib import asynccontextmanager
import json
from pathlib import Path
import struct
import time
import zlib

from protocol import (SERVICE, COMMAND, STATUS, OTA, BOARD_ID, INFO, DATA, COMMIT,
                      ABORT, ERASING, RECEIVING, VERIFYING, READY, FAILED,
                      Image, status_decode, version_string, frame, keyboard, mouse, touch,
                      pointer_drag, touch_swipe, MACRO_STATUS, MACRO_RECORD_SIZE,
                      macro_records, macro_upload_packets, macro_start, macro_status_decode)


class SerialTransport:
    chunk = 128

    def __init__(self, port):
        import serial
        self.serial = serial.Serial(port, 115200, timeout=0.2, write_timeout=2)
        self.serial.reset_input_buffer()

    def close(self):
        self.serial.close()

    def transact(self, payload):
        # Stop-and-wait keeps the bootloader's small FIFO from being overrun.
        self.serial.write(frame(payload))
        deadline = time.monotonic() + 4
        buffer = bytearray()
        while time.monotonic() < deadline:
            buffer.extend(self.serial.read(max(1, self.serial.in_waiting)))
            index = buffer.find(b'\xa5\x5a')
            if index < 0:
                buffer[:] = buffer[-1:]
                continue
            del buffer[:index]
            if len(buffer) < 4:
                continue
            size = int.from_bytes(buffer[2:4], 'little')
            if not 1 <= size <= 133:
                del buffer[0]
                continue
            if len(buffer) < size + 8:
                continue
            data = bytes(buffer[4:4+size])
            crc = int.from_bytes(buffer[4+size:8+size], 'little')
            del buffer[:8+size]
            if zlib.crc32(data) == crc and len(data) == 20 and data[1] == payload[0]:
                return status_decode(data)
        raise TimeoutError('No valid UART reply; verify the port and firmware protocol')

    async def send(self, payload):
        reply = await asyncio.to_thread(self.transact, payload)
        if reply['result']:
            raise RuntimeError(f'OTA rejected: {reply}')
        return reply

    async def status(self):
        return await self.send(bytes([INFO]))


class BleTransport:
    def __init__(self, client):
        self.client = client
        self.chunk = min(128, (client.mtu_size - 8) & ~3)
        if self.chunk < 4:
            raise RuntimeError('ATT MTU too small')

    async def send(self, payload):
        await self.client.write_gatt_char(OTA, payload, response=True)

    async def status(self):
        return status_decode(bytes(await self.client.read_gatt_char(OTA)))


async def wait_for(transport, target, allowed, timeout=45):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        await asyncio.sleep(0.08)
        try:
            state = await transport.status()
        except TimeoutError:
            continue
        if state['result'] or state['state'] == FAILED:
            raise RuntimeError(f'OTA failed: {state}')
        if state['state'] == target:
            return state
        if state['state'] not in allowed:
            raise RuntimeError(f'Unexpected OTA state: {state}')
    raise TimeoutError('OTA state transition timed out')


async def upgrade(transport, image):
    await transport.send(image.begin())
    await wait_for(transport, RECEIVING, {ERASING})
    last_percent = -1
    for offset in range(0, len(image.data), transport.chunk):
        payload = bytes([DATA]) + struct.pack('<I', offset) + image.data[offset:offset+transport.chunk]
        for attempt in range(3):
            try:
                await transport.send(payload)
                break
            except TimeoutError:
                if attempt == 2:
                    raise
                await asyncio.sleep(0.2)
        percent = min(100, (offset+transport.chunk)*100 // len(image.data))
        if percent // 10 != last_percent:
            last_percent = percent // 10
            print(f'Upload {percent}%')
    status = await transport.status()
    if status['offset'] != len(image.data):
        raise RuntimeError(f'Device offset mismatch: {status}')
    await transport.send(bytes([COMMIT]))
    await wait_for(transport, READY, {VERIFYING})
    print('Image CRC verified and commit persisted. Device will reboot; verify the running version with info.')


async def wait_hid_idle(client, report_id=0):
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        status = bytes(await client.read_gatt_char(STATUS))
        if len(status) != 20 or status[0] != 1 or status[1] != 2:
            raise RuntimeError('Controller role or status protocol changed')
        required = 1 << max(0, report_id-1)
        if report_id and (not status[2] or status[4] & required != required):
            raise RuntimeError('HID host has not enabled this report; check pairing and platform support')
        if not status[5]:
            return
        await asyncio.sleep(0.01)
    raise TimeoutError('HID output queue did not drain')


async def play_reports(client, reports):
    reports = list(reports)
    timed_touch = len(reports) > 2 and all(len(p) == 8 and p[0] == 3 for p, _ in reports)
    deadline = time.monotonic()
    try:
        for index, (packet, delay) in enumerate(reports):
            if timed_touch:
                deadline += delay
                # Coalesce only late absolute moves within the same contact.
                # Always send contact transitions and the final position.
                if (0 < index < len(reports)-1 and packet[1] == 1 and
                        reports[index-1][0][:3] == packet[:3] == reports[index+1][0][:3] and
                        time.monotonic() >= deadline):
                    continue
            await client.write_gatt_char(COMMAND, packet, response=True)
            await wait_hid_idle(client, packet[0] if packet[0] <= 3 else 0)
            if timed_touch:
                if index == 0:
                    deadline = time.monotonic() + delay
                await asyncio.sleep(max(0, deadline - time.monotonic()))
            else:
                await asyncio.sleep(delay)
    finally:
        if client.is_connected:
            await client.write_gatt_char(COMMAND, b'\x04', response=True)
            await wait_hid_idle(client)
            await asyncio.sleep(0.04)


async def upload_macro(client, records):
    packets = macro_upload_packets(records)
    await wait_hid_idle(client)
    for packet in packets:
        await client.write_gatt_char(COMMAND, packet, response=True)
    status = macro_status_decode(bytes(await client.read_gatt_char(MACRO_STATUS)))
    count = len(records) // MACRO_RECORD_SIZE
    if status['state'] != 'ready' or status['result'] or status['records'] != count or status['received'] != count:
        raise RuntimeError(f'Board did not confirm the complete macro: {status}')
    return status


async def macro_action(client, args):
    if args.action == 'macro-upload':
        document = json.loads(Path(args.file).read_text(encoding='utf-8-sig'))
        if not isinstance(document, dict) or 'steps' not in document:
            raise ValueError('Macro JSON must contain a steps array')
        result = await upload_macro(client, macro_records(document['steps']))
    else:
        if args.action == 'macro-start':
            packet = macro_start(args.repeats, args.delay, args.interval)
        else:
            packet = {'macro-stop': b'\x14', 'macro-pause': b'\x15',
                      'macro-resume': b'\x16', 'macro-status': None}[args.action]
        if packet is not None:
            await client.write_gatt_char(COMMAND, packet, response=True)
        if args.action == 'macro-stop':
            await wait_hid_idle(client)
        result = macro_status_decode(bytes(await client.read_gatt_char(MACRO_STATUS)))
    print(json.dumps(result, ensure_ascii=False))


@asynccontextmanager
async def controller_connection(device, **options):
    from bleak import BleakClient
    for attempt in range(2):
        client = BleakClient(device, **options)
        try:
            await client.connect()
            # Service Changed can invalidate the handles obtained during connection.
            await asyncio.sleep(0.8)
            status = bytes(await client.read_gatt_char(STATUS))
            if len(status) != 20 or int.from_bytes(status[12:16], 'little') != BOARD_ID:
                raise RuntimeError('Unexpected firmware status or board ID')
        except (OSError, RuntimeError):
            await client.disconnect()
            if attempt:
                raise
            continue
        break
    try:
        yield client
    finally:
        await client.disconnect()


async def ble_action(args):
    from bleak import BleakScanner
    if args.action == 'scan':
        devices = await BleakScanner.discover(timeout=6, return_adv=True)
        for device, advertisement in devices.values():
            if (advertisement.local_name or '').startswith('CH592F-3IN1') or SERVICE in advertisement.service_uuids:
                print(device.address, advertisement.local_name, advertisement.rssi)
        return
    if not args.address:
        raise ValueError('--address is required')
    async with controller_connection(args.address, pair=True, timeout=40,
                                     winrt={'use_cached_services': False}) as client:
        status = bytes(await client.read_gatt_char(STATUS))
        if len(status) != 20 or int.from_bytes(status[12:16], 'little') != BOARD_ID:
            raise RuntimeError('Unexpected firmware status or board ID')
        version = version_string(int.from_bytes(status[8:12], 'little'))
        print(f'role={status[1]} hid={status[2]} controller={status[3]} '
              f'ready=0x{status[4]:02x} queue={status[5]} '
              f'version={version} profile={"ios" if status[17] == 1 else "android"} '
              f'capabilities=0x{status[18]:02x}')
        if args.action == 'info':
            return
        if status[1] != 2:
            raise RuntimeError('This link is the HID slot. Connect the target HID host first, then this controller.')
        if args.action == 'ota':
            await upgrade(BleTransport(client), Image.load(args.image))
            return
        if args.action == 'abort':
            await BleTransport(client).send(bytes([ABORT]))
            return
        if args.action.startswith('macro-'):
            if int.from_bytes(status[8:12], 'little') < 0x10100:
                raise RuntimeError('Firmware does not support macros; upgrade the matching profile first')
            await macro_action(client, args)
            return
        if args.action in {'touch', 'swipe'} and not status[18] & 4:
            raise RuntimeError('This firmware has no touchscreen; use mouse/click/drag on iOS')
        hold = args.hold / 1000
        if not 0 <= hold <= 10:
            raise ValueError('--hold must be in 0..10000 ms')
        if args.action == 'key':
            reports = [(keyboard(args.modifiers, args.keys), hold)]
        elif args.action == 'mouse':
            reports = [(mouse(args.x, args.y, args.buttons, args.wheel, args.pan), hold)]
        elif args.action == 'touch':
            reports = [(touch(args.x, args.y, 1), hold), (touch(args.x, args.y, 0), 0.04)]
        elif args.action == 'click':
            reports = [(mouse(buttons=args.button), hold), (mouse(), 0.04)]
        elif args.action == 'drag':
            reports = pointer_drag(args.x, args.y, args.duration)
        elif args.action == 'swipe':
            reports = touch_swipe(args.x0, args.y0, args.x1, args.y1, args.duration)
        else:
            reports = [(b'\x04', 0.04)]
        await play_reports(client, reports)


async def main():
    parser = argparse.ArgumentParser(description='CH592F dual-link HID control and OTA')
    parser.add_argument('--address', help='BLE address/identifier, from scan')
    parser.add_argument('--port', help='UART port, e.g. COM8; only info/ota/abort')
    commands = parser.add_subparsers(dest='action', required=True)
    for name in ['scan', 'info', 'release', 'abort']:
        sub = commands.add_parser(name)
        sub.set_defaults(hold=50)
    key = commands.add_parser('key')
    key.add_argument('keys', nargs='*', type=lambda v: int(v, 0), help='USB HID usages, e.g. 4 for A')
    key.add_argument('--modifiers', type=lambda v: int(v, 0), default=0)
    key.add_argument('--hold', type=int, default=80)
    move = commands.add_parser('mouse')
    move.add_argument('x', type=int)
    move.add_argument('y', type=int)
    move.add_argument('--buttons', type=int, default=0)
    move.add_argument('--wheel', type=int, default=0)
    move.add_argument('--pan', type=int, default=0)
    move.add_argument('--hold', type=int, default=80)
    digitizer = commands.add_parser('touch')
    digitizer.add_argument('x', type=int)
    digitizer.add_argument('y', type=int)
    digitizer.add_argument('--hold', type=int, default=80)
    click = commands.add_parser('click', help='Click at the current pointer position (Android/iOS)')
    click.add_argument('--button', type=int, choices=[1, 2, 4], default=1)
    click.add_argument('--hold', type=int, default=80)
    drag = commands.add_parser('drag', help='Drag relative to the current pointer position')
    drag.add_argument('x', type=int)
    drag.add_argument('y', type=int)
    drag.add_argument('--duration', type=int, default=500)
    drag.set_defaults(hold=0)
    swipe = commands.add_parser('swipe', help='Absolute single-touch swipe; requires host digitizer support')
    for coordinate in ['x0', 'y0', 'x1', 'y1']:
        swipe.add_argument(coordinate, type=int)
    swipe.add_argument('--duration', type=int, default=500)
    swipe.set_defaults(hold=0)
    ota = commands.add_parser('ota')
    ota.add_argument('image', help='Generated .ota package')
    macro = commands.add_parser('macro-upload', help='Upload a keyboard/mouse macro without starting it')
    macro.add_argument('file', help='UTF-8 JSON with a steps array')
    macro = commands.add_parser('macro-start', help='Start the uploaded macro; repeats=0 runs until stopped')
    macro.add_argument('--repeats', type=int, default=1)
    macro.add_argument('--delay', type=int, default=0, help='Initial delay in ms, 0..65535')
    macro.add_argument('--interval', type=int, default=0, help='Delay between loops in ms, 0..3600000')
    for name in ['macro-status', 'macro-pause', 'macro-resume', 'macro-stop']:
        commands.add_parser(name)
    args = parser.parse_args()
    if args.port:
        if args.action not in {'info', 'ota', 'abort'}:
            raise ValueError('UART is reserved for OTA/info/abort')
        transport = SerialTransport(args.port)
        try:
            if args.action == 'ota':
                await upgrade(transport, Image.load(args.image))
            elif args.action == 'abort':
                await transport.send(bytes([ABORT]))
            else:
                print(await transport.status())
        finally:
            transport.close()
    else:
        await ble_action(args)


if __name__ == '__main__':
    try:
        asyncio.run(main())
    except (RuntimeError, ValueError, TimeoutError) as error:
        raise SystemExit(str(error))
