from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
SDK = ROOT / 'vendor/ch592'
REVISION = 'a46e0086f1ffb5e5502703970bff94888e67f4cb'
URL = 'https://github.com/openwch/ch592.git'
DIRECTORIES = [
    'Datasheet', 'EVT/EXAM/SRC', 'EVT/EXAM/BLE/HAL', 'EVT/EXAM/BLE/LIB',
    'EVT/EXAM/BLE/BackupUpgrade_OTA', 'EVT/EXAM/BLE/BackupUpgrade_IAP',
    'EVT/EXAM/BLE/BackupUpgrade_JumpIAP', 'EVT/EXAM/BLE/MultiCentPeri',
    'EVT/EXAM/BLE/HID_Keyboard', 'EVT/EXAM/BLE/HID_Mouse', 'EVT/EXAM/BLE/HID_Touch',
]


def git(*args):
    return subprocess.check_output(['git', *map(str, args)], text=True, encoding='utf-8').strip()


def main():
    if SDK.exists():
        if git('-C', SDK, 'rev-parse', 'HEAD') != REVISION:
            raise SystemExit('Existing SDK has another revision; select a fresh SDK path.')
        if git('-C', SDK, 'status', '--porcelain'):
            raise SystemExit('Existing SDK has local changes.')
        print('Pinned official SDK already present: ' + REVISION)
        return
    SDK.parent.mkdir(exist_ok=True)
    git('clone', '--filter=blob:none', '--no-checkout', URL, SDK)
    git('-C', SDK, 'sparse-checkout', 'set', *DIRECTORIES)
    git('-C', SDK, 'checkout', '--detach', REVISION)
    print('Official SDK fetched: ' + REVISION)


if __name__ == '__main__':
    main()
