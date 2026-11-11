import asyncio
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from protocol import (Image, BOARD_ID, APP_BASE, CAPACITY, DATA, BEGIN, COMMIT,
                      ERASING, RECEIVING, VERIFYING, READY, FAILED,
                      keyboard, mouse, touch, frame, status_decode, pointer_drag, touch_swipe)
from client import upgrade, play_reports, SerialTransport
from build import startup_mstatus


class ProtocolTests(unittest.TestCase):
    def test_startup_requires_machine_mode_for_handoff(self):
        old = '122: 08800293 li t0,136\n126: 30029073 csrw mstatus,t0'
        with self.assertRaises(ValueError):
            startup_mstatus(old)
        for image in ['jump', 'iap', 'app']:
            listing = (ROOT / 'dist' / (image+'.startup.txt')).read_text(encoding='utf-8')
            self.assertEqual(startup_mstatus(listing), 0x1888)

    def test_keyboard(self):
        self.assertEqual(keyboard(2, [4]), bytes([1, 2, 0, 4, 0, 0, 0, 0, 0]))
        with self.assertRaises(ValueError):
            keyboard(0, list(range(7)))
        with self.assertRaises(ValueError):
            keyboard(0, [256])

    def test_mouse(self):
        self.assertEqual(mouse(-32768, 32767, 31, -127, 127),
                         b'\x02\x1f\x00\x80\xff\x7f\x81\x7f')
        with self.assertRaises(ValueError):
            mouse(wheel=-128)

    def test_touch(self):
        self.assertEqual(touch(32767, 16384, 1), b'\x03\x01\x00\xff\x7f\x00\x40\x01')
        self.assertEqual(touch(32767, 16384, 0), b'\x03\x00\x00\xff\x7f\x00\x40\x01')
        with self.assertRaises(ValueError):
            touch(32768, 0, 1)

    def test_framing(self):
        payload = bytes([DATA]) + struct.pack('<I', 4) + b'ABCD'
        result = frame(payload)
        self.assertEqual(result[:4], b'\xa5\x5a\x09\x00')
        self.assertEqual(int.from_bytes(result[-4:], 'little'), zlib.crc32(payload))

    def test_uart_response_after_startup_logs(self):
        payload = struct.pack('<4B4I', 1, 0x80, 0, 0, 0, 0, 0, 0x10001)
        class Port:
            buffer = bytearray(b'[APP] clock=60000000\r\n[BLE] start rc=00\r\n' + frame(payload))
            in_waiting = 3
            def write(self, packet):
                if packet != frame(bytes([0x80])):
                    raise AssertionError('Unexpected UART request')
            def read(self, count):
                result = bytes(self.buffer[:count])
                del self.buffer[:count]
                return result
        transport = SerialTransport.__new__(SerialTransport)
        transport.serial = Port()
        self.assertEqual(transport.transact(bytes([0x80]))['version'], 0x10001)

    def test_pointer_drag_has_exact_relative_sum_and_release(self):
        reports = pointer_drag(-301, 117, 500)
        values = [struct.unpack('<BBhhbb', packet) for packet, _ in reports]
        self.assertTrue(all(value[1] == 1 for value in values[:-1]))
        self.assertEqual((sum(v[2] for v in values), sum(v[3] for v in values)), (-301, 117))
        self.assertEqual(reports[-1][0], mouse())
        with self.assertRaises(ValueError):
            pointer_drag(0, 0, 0)

    def test_swipe_keeps_contact_until_final_position(self):
        reports = touch_swipe(100, 30000, 32000, 1000, 500)
        values = [struct.unpack('<BBBHHB', packet) for packet, _ in reports]
        self.assertEqual(reports[0][0], touch(100, 30000, 1))
        self.assertEqual(reports[-2][0], touch(32000, 1000, 1))
        self.assertEqual(reports[-1][0], touch(32000, 1000, 0))
        self.assertTrue(all(v[1] == 1 and v[2] == 0 and v[5] == 1 for v in values[:-1]))
        with self.assertRaises(ValueError):
            touch_swipe(0, 0, 0, 32768)

    def test_status(self):
        payload = struct.pack('<4B4I', 0x81, COMMIT, 0, READY, 256, 256, 0x1234, 0x10000)
        self.assertTrue(status_decode(payload)['bootloader'])
        self.assertEqual(status_decode(payload)['offset'], 256)
        with self.assertRaises(ValueError):
            status_decode(payload[:-1])

    def test_package_corruption(self):
        image = Image.load(ROOT / 'dist/ch592f_3in1.ota')
        self.assertEqual(len(image.begin()), 17)
        self.assertLessEqual(len(image.data), CAPACITY)
        source = (ROOT / 'dist/ch592f_3in1.ota').read_bytes()
        with tempfile.TemporaryDirectory() as directory:
            file = Path(directory) / 'bad.ota'
            for index in [0, 8, 12, 28, 32, len(source)-1]:
                damaged = bytearray(source)
                damaged[index] ^= 1
                file.write_bytes(damaged)
                with self.assertRaises(ValueError):
                    Image.load(file)

    def test_report_map_structure(self):
        for profile in ['ios', 'android']:
            with self.subTest(profile=profile):
                self.check_report_map(profile)

    def test_ios_report_map_matches_accepted_firmware(self):
        descriptor = (ROOT / 'build/report_map_ios.bin').read_bytes()
        accepted = (ROOT / 'releases/ios-v1.0.5/app.bin').read_bytes()
        self.assertIn(descriptor, accepted)

    def check_report_map(self, profile):
        data = (ROOT / 'build' / ('report_map_' + profile + '.bin')).read_bytes()
        self.assertLessEqual(len(data), 512)
        pos, report_id, size, count, depth, page, usage = 0, 0, 0, 0, 0, 0, 0
        bits = {}
        stack, applications, usages, inputs, features = [], [], [], [], []
        logical_max = 0
        while pos < len(data):
            prefix = data[pos]
            length = [0, 1, 2, 4][prefix & 3]
            kind, tag = (prefix >> 2) & 3, prefix >> 4
            value = int.from_bytes(data[pos+1:pos+1+length], 'little')
            if kind == 1:
                if tag == 0: page = value
                if tag == 2: logical_max = value
                if tag == 7: size = value
                if tag == 8: report_id = value
                if tag == 9: count = value
                if tag == 10: stack.append((report_id, size, count, page, logical_max))
                if tag == 11: report_id, size, count, page, logical_max = stack.pop()
            if kind == 2 and tag == 0:
                usage = value
                usages.append((page, usage))
            if kind == 0:
                if tag == 10:
                    depth += 1
                    if value == 1: applications.append((page, usage))
                if tag == 12: depth -= 1
                self.assertGreaterEqual(depth, 0)
                if tag in (8, 9, 11):
                    key = (report_id, tag)
                    bits[key] = bits.get(key, 0) + size * count
                    if tag == 8 and not value & 1:
                        inputs.extend((report_id, p, u) for p, u in usages)
                    if tag == 11 and not value & 1:
                        features.extend((report_id, p, u, logical_max) for p, u in usages)
                usages.clear()
            pos += length + 1
        self.assertEqual(depth, 0)
        self.assertEqual(stack, [])
        expected = {(1, 8): 64, (1, 9): 8, (2, 8): 56}
        if profile == 'android':
            expected.update({(3, 8): 56, (4, 11): 8})
            self.assertEqual([item for item in inputs if item[0] == 3],
                             [(3, 13, 0x42), (3, 13, 0x51),
                              (3, 1, 0x30), (3, 1, 0x31), (3, 13, 0x54)])
        self.assertEqual(features, [(4, 13, 0x55, 2)] if profile == 'android' else [])
        self.assertEqual(bits, expected)
        self.assertEqual(applications, [(1, 6), (1, 2)] + ([(13, 4)] if profile == 'android' else []))

    def test_factory_hex_and_manifest(self):
        manifest = json.loads((ROOT / 'dist/manifest.json').read_text(encoding='utf-8'))
        image = Image.load(ROOT / 'dist/ch592f_3in1.ota')
        binary = (ROOT / 'dist/ch592f_3in1_factory.bin').read_bytes()
        self.assertEqual(binary[APP_BASE:APP_BASE+len(image.data)], image.data)
        self.assertEqual(manifest['app_bytes'], len(image.data))
        upper = 0
        for line in (ROOT / 'dist/ch592f_3in1_factory.hex').read_text(encoding='utf-8').splitlines():
            raw = bytes.fromhex(line[1:])
            self.assertEqual(sum(raw) & 255, 0)
            size, offset, kind = raw[0], int.from_bytes(raw[1:3], 'big'), raw[3]
            if kind == 4: upper = int.from_bytes(raw[4:6], 'big') << 16
            if kind == 0:
                self.assertEqual(binary[upper+offset:upper+offset+size], raw[4:4+size])
                self.assertLess(upper+offset+size, 0x70001)


class UpgradeTests(unittest.IsolatedAsyncioTestCase):
    async def test_mouse_and_touch_use_their_own_ready_bits(self):
        from client import wait_hid_idle
        class Client:
            ready = 3
            async def read_gatt_char(self, uuid):
                status = bytearray(20)
                status[:5] = bytes([1, 2, 1, 1, self.ready])
                status[17] = 1
                return status
        client = Client()
        await wait_hid_idle(client, 2)
        with self.assertRaises(RuntimeError):
            await wait_hid_idle(client, 3)
        client.ready = 7
        await wait_hid_idle(client, 3)
        client.ready = 1
        with self.assertRaises(RuntimeError):
            await wait_hid_idle(client, 2)

    async def test_service_change_reconnects_before_actions_only(self):
        from client import controller_connection
        from unittest.mock import patch
        instances, closed = [], []
        class Client:
            fail_first = True
            def __init__(self, *args, **kwargs):
                self.index = len(instances)
                instances.append(self)
            async def connect(self):
                pass
            async def disconnect(self):
                closed.append(self.index)
            async def read_gatt_char(self, uuid):
                if self.fail_first and self.index == 0:
                    raise OSError('Service Changed closed the original handles')
                status = bytearray(20)
                status[12:16] = BOARD_ID.to_bytes(4, 'little')
                return status
        async def no_wait(seconds):
            pass
        actions = 0
        with patch('bleak.BleakClient', Client), patch('client.asyncio.sleep', no_wait):
            async with controller_connection('test'):
                actions += 1
            self.assertEqual((len(instances), actions, closed), (2, 1, [0, 1]))
            instances.clear()
            Client.fail_first = False
            with self.assertRaises(OSError):
                async with controller_connection('test'):
                    raise OSError('An action must never be replayed automatically')
            self.assertEqual(len(instances), 1)

    async def test_hid_waits_for_output_before_release(self):
        class Client:
            is_connected = True
            reads_left = 0
            packets = []
            async def write_gatt_char(self, uuid, packet, response):
                self.assert_idle()
                self.packets.append(packet)
                self.reads_left = 2
            def assert_idle(self):
                if self.reads_left:
                    raise AssertionError('Release discarded an unsent HID report')
            async def read_gatt_char(self, uuid):
                self.reads_left = max(0, self.reads_left-1)
                return bytes([1, 2, 1, 1, 7, self.reads_left]) + bytes(14)
        client = Client()
        await play_reports(client, [(mouse(0, -50, 1), 0), (mouse(), 0)])
        self.assertEqual(client.packets, [mouse(0, -50, 1), mouse(), b'\x04'])

    async def test_touch_timing_coalesces_late_moves_and_keeps_endpoints(self):
        from unittest.mock import patch
        clock = [0.0]
        packets = []
        class Client:
            is_connected = True
            async def write_gatt_char(self, uuid, packet, response):
                packets.append((clock[0], packet))
                clock[0] += 0.04
            async def read_gatt_char(self, uuid):
                clock[0] += 0.06
                return bytes([1, 2, 1, 1, 7, 0]) + bytes(14)
        async def sleep(seconds):
            clock[0] += seconds
        reports = touch_swipe(16384, 28000, 16384, 6000, 700)
        with patch('client.time.monotonic', lambda: clock[0]), patch('client.asyncio.sleep', sleep):
            await play_reports(Client(), reports)
        self.assertEqual(packets[0][1], touch(16384, 28000, 1))
        self.assertEqual([p for _, p in packets[-3:]],
                         [touch(16384, 6000, 1), touch(16384, 6000, 0), b'\x04'])
        self.assertTrue(all(p[:3] == b'\x03\x01\x00' for _, p in packets[:-2]))
        self.assertLess(len(packets), len(reports))
        self.assertGreaterEqual(packets[-2][0] - packets[0][0], 0.7)
        self.assertLess(packets[-2][0] - packets[0][0], 1.1)

    async def test_minimum_mtu_and_retry(self):
        class Transport:
            chunk = 12
            state = 0
            offset = 0
            dropped_reply = False
            dropped_status = False
            async def send(self, packet):
                if packet[0] == BEGIN:
                    self.state = ERASING
                elif packet[0] == DATA:
                    offset = int.from_bytes(packet[1:5], 'little')
                    if offset == self.offset:
                        self.offset += len(packet)-5
                    elif offset+len(packet)-5 != self.offset:
                        raise AssertionError('Unexpected offset')
                    if not self.dropped_reply:
                        self.dropped_reply = True
                        raise TimeoutError('Simulated lost ACK after successful write')
                elif packet[0] == COMMIT:
                    self.state = VERIFYING
            async def status(self):
                if self.state == ERASING and not self.dropped_status:
                    self.dropped_status = True
                    raise TimeoutError('UART status lost while flash erase blocks reception')
                if self.state == ERASING: self.state = RECEIVING
                if self.state == VERIFYING: self.state = READY
                return dict(result=0, state=self.state, offset=self.offset)
        transport = Transport()
        data = bytes(range(256))
        await upgrade(transport, Image(data, 0x10000, zlib.crc32(data)))
        self.assertEqual(transport.offset, 256)
        self.assertTrue(transport.dropped_reply)
        self.assertTrue(transport.dropped_status)
        self.assertEqual(transport.state, READY)


if __name__ == '__main__':
    unittest.main()
