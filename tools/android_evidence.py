import argparse
from pathlib import Path
import re
import subprocess
import xml.etree.ElementTree as ET


def main():
    parser = argparse.ArgumentParser(description='Capture Android evidence without injecting input')
    parser.add_argument('--serial', required=True)
    parser.add_argument('--output', type=Path, default=Path('build/android-validation'))
    parser.add_argument('action', choices=['capture', 'events', 'ui'])
    parser.add_argument('name', nargs='?', default='snapshot')
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_-]+', args.name):
        parser.error('name must contain only letters, digits, underscore or hyphen')
    args.output.mkdir(parents=True, exist_ok=True)
    adb = ['adb', '-s', args.serial]

    def run(*command):
        return subprocess.check_output(adb + list(command), timeout=30)

    if args.action == 'events':
        path = args.output / (args.name + '-events.txt')
        with path.open('wb') as stream:
            process = subprocess.Popen(adb + ['shell', 'getevent', '-lt'], stdout=stream,
                                       stderr=subprocess.STDOUT)
            print('Recording native events: ' + str(path), flush=True)
            try:
                process.wait()
            except KeyboardInterrupt:
                process.terminate()
                process.wait(timeout=5)
        return

    run('shell', 'uiautomator', 'dump', '/sdcard/ch592-window.xml')
    xml = run('exec-out', 'cat', '/sdcard/ch592-window.xml')
    root = ET.fromstring(xml)
    for node in root.iter('node'):
        text = node.get('text') or node.get('content-desc')
        if text:
            print(node.get('bounds'), text)
    if args.action == 'capture':
        (args.output / (args.name + '.xml')).write_bytes(xml)
        (args.output / (args.name + '.png')).write_bytes(run('exec-out', 'screencap', '-p'))
        for name, command in [('input', ['shell', 'dumpsys', 'input']),
                              ('devices', ['shell', 'getevent', '-lp'])]:
            data = run(*command).decode('utf-8', errors='replace')
            (args.output / (args.name + '-' + name + '.txt')).write_text(data, encoding='utf-8')
        print('Evidence saved: ' + str(args.output / args.name))


if __name__ == '__main__':
    main()
