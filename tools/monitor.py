import argparse
from datetime import datetime, timezone
from pathlib import Path
import sys
import time

import serial
from serial.tools import list_ports


def main():
    parser = argparse.ArgumentParser(description='Read-only PB12/PB13 UART1 startup log capture')
    parser.add_argument('--list', action='store_true', help='List serial ports and exit')
    parser.add_argument('--port', help='Confirmed UART adapter port, e.g. COM8')
    parser.add_argument('--seconds', type=float, default=30, help='Capture duration; 0 runs until Ctrl+C')
    parser.add_argument('--output', type=Path, help='New UTF-8 log file; existing files are preserved')
    args = parser.parse_args()
    if args.list:
        for port in list_ports.comports():
            print(port.device, port.description, port.hwid)
        return
    if not args.port:
        parser.error('--port is required; use --list to inspect available ports')
    if args.seconds < 0:
        parser.error('--seconds cannot be negative')
    destination = args.output or (Path(__file__).resolve().parents[1] / 'build' /
                                  ('uart-' + datetime.now(timezone.utc).strftime('%Y%m%d-%H%M%S') + '.log'))
    destination.parent.mkdir(parents=True, exist_ok=True)
    # Set control lines before opening; this tool sends no protocol bytes.
    port = serial.Serial(port=None, baudrate=115200, timeout=0.2)
    port.dtr = False
    port.rts = False
    port.port = args.port
    received = 0
    with destination.open('x', encoding='utf-8', newline='') as log:
        log.write(f'UART1 {args.port} 115200 8N1; UTC {datetime.now(timezone.utc).isoformat()}\n')
        try:
            port.open()
            print(f'Capturing {args.port}; reset the target to capture all boot stages. Log: {destination}')
            started = time.monotonic()
            while not args.seconds or time.monotonic()-started < args.seconds:
                data = port.read(max(1, port.in_waiting))
                if data:
                    received += len(data)
                    text = data.decode('ascii', errors='backslashreplace')
                    log.write(text)
                    log.flush()
                    sys.stdout.write(text)
                    sys.stdout.flush()
        except KeyboardInterrupt:
            pass
        finally:
            port.close()
    print(f'\nCapture finished: {received} bytes, {destination}')
    if not received:
        print('No UART bytes received; this does not identify a firmware failure stage.')


if __name__ == '__main__':
    main()
