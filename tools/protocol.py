from dataclasses import dataclass
from pathlib import Path
import struct
import zlib

SERVICE = '59200001-3c7f-4a9e-8c51-3b9d4b6a1200'
COMMAND = '59200002-3c7f-4a9e-8c51-3b9d4b6a1200'
STATUS = '59200003-3c7f-4a9e-8c51-3b9d4b6a1200'
OTA = '59200004-3c7f-4a9e-8c51-3b9d4b6a1200'
BOARD_ID = 0x592F0301
APP_BASE, CAPACITY = 0x1000, 0x36000
INFO, BEGIN, DATA, COMMIT, ABORT = range(0x80, 0x85)
IDLE, ERASING, RECEIVING, VERIFYING, READY, FAILED = range(6)
ERRORS = ['ok', 'length', 'value', 'role', 'not_ready', 'busy', 'offset', 'flash', 'crc', 'state']


def version_string(value):
    return f'{value >> 16}.{(value >> 8) & 255}.{value & 255}'


@dataclass(frozen=True)
class Image:
    data: bytes
    version: int
    crc: int

    @classmethod
    def load(cls, path):
        packet = Path(path).read_bytes()
        if len(packet) < 32:
            raise ValueError('Truncated OTA package')
        magic, board, origin, size, version, crc, header_crc = struct.unpack('<8s6I', packet[:32])
        if magic != b'VHIDOTA1' or board != BOARD_ID or origin != APP_BASE:
            raise ValueError('Wrong board, image origin or package format; use the generated .ota file')
        if zlib.crc32(packet[:28]) != header_crc:
            raise ValueError('OTA header CRC mismatch')
        data = packet[32:]
        if size != len(data) or not 256 <= size <= CAPACITY or size % 4:
            raise ValueError('Invalid OTA image length')
        if zlib.crc32(data) != crc:
            raise ValueError('OTA payload CRC mismatch')
        return cls(data, version, crc)

    def begin(self):
        return struct.pack('<B4I', BEGIN, len(self.data), self.crc, self.version, BOARD_ID)


def status_decode(data):
    if len(data) != 20 or data[0] & 0x7F != 1:
        raise ValueError('Unsupported OTA status')
    protocol, command, result, state, offset, size, crc, version = struct.unpack('<4B4I', data)
    return dict(command=command, result=result, state=state, offset=offset,
                size=size, crc=crc, version=version, bootloader=bool(protocol & 0x80))


def frame(payload):
    if not 1 <= len(payload) <= 133:
        raise ValueError('UART payload length is outside 1..133')
    return b'\xa5\x5a' + struct.pack('<H', len(payload)) + payload + struct.pack('<I', zlib.crc32(payload))


def keyboard(modifiers=0, keys=()):
    if not 0 <= modifiers <= 255 or len(keys) > 6 or any(not 0 <= key <= 101 for key in keys):
        raise ValueError('Keyboard requires modifiers 0..255 and at most six usages 0..101')
    return bytes([1, modifiers, 0, *keys, *([0] * (6-len(keys)))])


def mouse(x=0, y=0, buttons=0, wheel=0, pan=0):
    if not -32768 <= x <= 32767 or not -32768 <= y <= 32767 or not 0 <= buttons <= 31:
        raise ValueError('Mouse X/Y or buttons out of range')
    if not -127 <= wheel <= 127 or not -127 <= pan <= 127:
        raise ValueError('Wheel/pan must be in -127..127')
    return struct.pack('<BBhhbb', 2, buttons, x, y, wheel, pan)


def touch(x, y, down):
    if not 0 <= x <= 32767 or not 0 <= y <= 32767 or down not in (0, 1):
        raise ValueError('Touch coordinates must be in 0..32767 and down must be 0 or 1')
    return struct.pack('<BBBHHB', 3, down, 0, x, y, 1)


def gesture_steps(duration):
    if not 40 <= duration <= 10000:
        raise ValueError('Gesture duration must be in 40..10000 ms')
    return max(2, duration // 20)


def pointer_drag(x, y, duration=500):
    mouse(x, y)  # Validate the total relative displacement before sending anything.
    steps = gesture_steps(duration)
    delay = duration / steps / 1000
    reports = [(mouse(buttons=1), delay)]
    previous_x = previous_y = 0
    for step in range(1, steps+1):
        current_x, current_y = x * step // steps, y * step // steps
        reports.append((mouse(current_x-previous_x, current_y-previous_y, buttons=1), delay))
        previous_x, previous_y = current_x, current_y
    reports.append((mouse(), 0.04))
    return reports


def touch_swipe(x0, y0, x1, y1, duration=500):
    touch(x0, y0, 1)
    touch(x1, y1, 0)
    steps = gesture_steps(duration)
    delay = duration / steps / 1000
    reports = [(touch(x0, y0, 1), delay)]
    for step in range(1, steps+1):
        x = x0 + (x1-x0) * step // steps
        y = y0 + (y1-y0) * step // steps
        reports.append((touch(x, y, 1), delay))
    reports.append((touch(x1, y1, 0), 0.04))
    return reports
