"""Enable SensairShuttle N16R8 PSRAM through ESP-IDF's configuration server.

Preserves the existing board, ADC workaround, UI, protocol and partition choices.
Run using the ESP-IDF Python environment, from the project root.
"""
import json
from pathlib import Path
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
backup = root / 'custom_assets' / 'psram_fix'
backup.mkdir(exist_ok=True)
for source, name in [('sdkconfig', 'config_before.txt'),
                     ('build/xiaozhi.bin', 'application_before.bin'),
                     ('build/bootloader/bootloader.bin', 'bootloader_before.bin')]:
    dest = backup / name
    if not dest.exists():
        shutil.copy2(root / source, dest)

requests = [
    {'version': 2, 'set': {'SPIRAM': True}},
    {'version': 2, 'set': {'SPIRAM_USE_MALLOC': True,
                           'SPIRAM_MALLOC_ALWAYSINTERNAL': 3072,
                           'SPIRAM_TRY_ALLOCATE_WIFI_LWIP': True,
                           'SPIRAM_SPEED_40M': True,
                           'SPIRAM_MEMTEST': True}},
    {'version': 2, 'save': str(root / 'sdkconfig')},
]
command = [sys.executable, '-m', 'kconfserver',
           '--env-file', str(root / 'build/config.env'),
           '--kconfig', 'C:/esp/v6.1/esp-idf/Kconfig',
           '--sdkconfig-rename', 'C:/esp/v6.1/esp-idf/sdkconfig.rename',
           '--config', str(root / 'sdkconfig'), '--version', '2']
result = subprocess.run(command, input='\n'.join(map(json.dumps, requests)) + '\n',
                        capture_output=True, text=True, cwd=root)
(backup / 'configuration_server.log').write_text(result.stdout + result.stderr,
                                                  encoding='utf-8')
result.check_returncode()
for line in result.stdout.splitlines():
    if line.startswith('{'):
        response = json.loads(line)
        if response.get('error'):
            raise RuntimeError(response['error'])

def values(path):
    return {line.split('=', 1)[0]: line.split('=', 1)[1]
            for line in path.read_text(encoding='utf-8').splitlines()
            if line.startswith('CONFIG_') and '=' in line}

before = values(backup / 'config_before.txt')
after = values(root / 'sdkconfig')
for key in ('CONFIG_IDF_TARGET', 'CONFIG_BOARD_TYPE_ESP_SENSAIRSHUTTLE',
            'CONFIG_USE_DEFAULT_MESSAGE_STYLE', 'CONFIG_PARTITION_TABLE_FILENAME',
            'CONFIG_CODEC_DATA_ADC_SUPPORT'):
    assert before.get(key) == after.get(key), key
assert after['CONFIG_SPIRAM'] == 'y'
assert after['CONFIG_SPIRAM_USE_MALLOC'] == 'y'
assert after['CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL'] == '3072'
changes = {key: [before.get(key), after.get(key)] for key in sorted(before.keys() | after.keys())
           if before.get(key) != after.get(key)}
(backup / 'configuration_changes.json').write_text(json.dumps(changes, indent=2), encoding='utf-8')
print(json.dumps(changes, indent=2))
