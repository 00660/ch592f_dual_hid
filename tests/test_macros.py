from pathlib import Path
import struct
import sys
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from protocol import (COMMAND, STATUS, MACRO_STATUS, macro_records, macro_upload_packets,
                      macro_start, macro_status_decode)
from client import upload_macro


def status_packet(count=2):
    result = bytearray(20)
    result[:6] = bytes([1, 2, 0, 0, count, count])
    struct.pack_into('<4H2h', result, 6, 1, 0, 64, 11, 0, 0)
    return bytes(result)


class MacroProtocolTests(unittest.TestCase):
    def test_mouse_keyboard_and_wait_encoding(self):
        records = macro_records([
            {'type': 'mouse', 'buttons': 1, 'dx': -17, 'dy': 300, 'delay_ms': 80},
            {'type': 'mouse'},
            {'type': 'keyboard', 'modifiers': 8, 'keys': [44], 'delay_ms': 60},
            {'type': 'keyboard'},
            {'type': 'wait', 'delay_ms': 2000}])
        self.assertEqual(len(records), 55)
        self.assertEqual(records[:11], struct.pack('<HBBhhbbB', 80, 2, 1, -17, 300, 0, 0, 0))
        self.assertEqual(records[22:33], bytes([60, 0, 1, 8, 0, 44, 0, 0, 0, 0, 0]))
        self.assertEqual(records[44:], struct.pack('<HB8x', 2000, 0))

    def test_invalid_or_unreleased_sequences_rejected(self):
        for steps in [[], [{'type': 'wait', 'delay_ms': 100}], [{'type': 'mouse', 'buttons': 1}],
                      [{'type': 'keyboard', 'keys': [4]}], [{'type': 'touch'}],
                      [{'type': 'mouse', 'x': 50}], [{'type': 'mouse', 'dx': 0.5}],
                      [{'type': 'mouse', 'delay_ms': 65536}], [{'type': 'mouse', 'delay_ms': -1}],
                      [{'type': 'mouse'}] * 65]:
            with self.subTest(steps=steps), self.assertRaises(ValueError):
                macro_records(steps)

    def test_upload_crc_offsets_and_minimum_att_mtu(self):
        records = macro_records([{'type': 'mouse', 'dx': 1}] * 64)
        packets = macro_upload_packets(records)
        self.assertEqual(packets[0], struct.pack('<BBI', 0x10, 64, zlib.crc32(records)))
        self.assertEqual(packets[-1], b'\x12')
        self.assertTrue(all(len(packet) <= 20 for packet in packets))
        self.assertEqual(b''.join(packet[3:] for packet in packets[1:-1]), records)
        self.assertEqual([struct.unpack_from('<H', packet, 1)[0] for packet in packets[1:-1]],
                         list(range(0, 704, 11)))
        for invalid in [b'', b'\x00' * 12, b'\x00' * 715]:
            with self.assertRaises(ValueError):
                macro_upload_packets(invalid)

    def test_start_interval_and_zero_touch_jitter(self):
        self.assertEqual(macro_start(0, 1500, 3600000), struct.pack('<BHHHHI', 0x13, 0, 1500, 0, 0, 3600000))
        for args in [(-1, 0, 0), (65536, 0, 0), (1, 65536, 0), (1, 0, 3600001), (1, 0.5, 0)]:
            with self.assertRaises(ValueError):
                macro_start(*args)

    def test_status_format(self):
        status = macro_status_decode(status_packet())
        self.assertEqual((status['state'], status['records'], status['capacity']), ('ready', 2, 64))
        for invalid in [status_packet()[:-1], b'\x02' + status_packet()[1:], bytes(20)]:
            with self.assertRaises(ValueError):
                macro_status_decode(invalid)


class MacroTransportTests(unittest.IsolatedAsyncioTestCase):
    async def test_upload_requires_confirmation_and_never_starts(self):
        class Client:
            writes = []
            confirmed = True

            async def read_gatt_char(self, uuid):
                if uuid == STATUS:
                    return bytes([1, 2, 1, 1, 3, 0]) + bytes(14)
                self_check = status_packet(2 if self.confirmed else 1)
                if uuid != MACRO_STATUS:
                    raise AssertionError('Unexpected characteristic')
                return self_check

            async def write_gatt_char(self, uuid, packet, response):
                if uuid != COMMAND or not response:
                    raise AssertionError('Macro upload must use acknowledged command writes')
                self.writes.append(packet)

        client = Client()
        records = macro_records([{'type': 'mouse', 'buttons': 2}, {'type': 'mouse'}])
        result = await upload_macro(client, records)
        self.assertEqual(result['state'], 'ready')
        self.assertEqual(client.writes, macro_upload_packets(records))
        self.assertNotIn(0x13, [packet[0] for packet in client.writes])
        client.confirmed = False
        with self.assertRaises(RuntimeError):
            await upload_macro(client, records)


if __name__ == '__main__':
    unittest.main()
