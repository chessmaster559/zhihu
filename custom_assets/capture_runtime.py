"""Capture C5 display, ADC and memory diagnostics; reconnect after USB reset."""
import argparse
import time
from pathlib import Path

import serial
from esptool.reset import HardReset

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--seconds', type=int, default=50)
parser.add_argument('--port', default='COM10')
parser.add_argument('--reset', action='store_true')
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
deadline = time.monotonic() + args.seconds
port = None
reset_pending = args.reset
pending = bytearray()
keywords = ('Assets:', 'LvglGif:', 'GIF:', 'LcdDisplay:', 'StateMachine:',
            'SystemInfo:', 'ESP-ROM:', 'rst:', 'panic', 'Guru', 'assert',
            'abort()', 'CORRUPT', 'failed', 'malloc', 'App version:',
            'psram', 'heap_init:', 'AdcPdmAudioCodec:', 'AudioCodec:', 'AudioService:',
            'Adev_Codec:', 'adc_data:', 'memory test', 'Pet presentation:', 'Pet motion:',
            'Bmi270Pet:', 'StickmanPet:', 'ELF file SHA256')
with args.output.open('wb') as log:
    try:
        while time.monotonic() < deadline:
            try:
                if port is None:
                    port = serial.Serial()
                    port.port = args.port
                    port.baudrate = 115200
                    port.timeout = 0.3
                    port.dtr = False
                    port.rts = False
                    port.open()
                    print('Serial connected:', args.port, flush=True)
                    if reset_pending:
                        reset_pending = False
                        HardReset(port, uses_usb=True)()
                data = port.read(2048)
                if not data:
                    continue
                log.write(data)
                log.flush()
                pending.extend(data)
                while b'\n' in pending:
                    line, _, remainder = pending.partition(b'\n')
                    pending = bytearray(remainder)
                    text = line.decode('utf-8', 'replace').rstrip()
                    if any(key.lower() in text.lower() for key in keywords):
                        print(text, flush=True)
            except (serial.SerialException, OSError) as exc:
                print('Serial reconnect:', type(exc).__name__, flush=True)
                if port is not None:
                    port.close()
                    port = None
                time.sleep(0.5)
    finally:
        if port is not None:
            port.close()
print('Capture finished:', args.output, flush=True)
