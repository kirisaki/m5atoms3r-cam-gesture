#!/usr/bin/env python3
"""Record diagnostic grayscale frames and events; never sends keyboard input."""
import argparse
import json
import time
from pathlib import Path

import serial

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--port', default='/dev/ttyACM1')
parser.add_argument('--seconds', type=float, default=30)
parser.add_argument('--out', type=Path, required=True)
parser.add_argument('--diagnostics', action='store_true',
                    help='Also record timing diagnostics; start with firmware debug off')
args = parser.parse_args()
args.out.parent.mkdir(parents=True, exist_ok=True)
port = serial.Serial(baudrate=115200, timeout=0.2)
# Native USB CDC requires DTR for device-to-host output.
port.dtr = True
port.rts = False
port.port = args.port
port.open()
try:
    # Firmware is freshly booted with image trace and debug off before this tool is used.
    port.reset_input_buffer()
    port.write(b'0bvd' if args.diagnostics else b'0bv')
    print('USB HID disabled for recording; send serial command 1 to re-enable afterwards.', flush=True)
    print('Recording started; keep hand out for 5 seconds, then perform gesture.', flush=True)
    deadline = time.monotonic() + args.seconds
    frames = 0
    events = []
    with args.out.open('w') as output:
        while time.monotonic() < deadline:
            line = port.readline().decode('ascii', errors='replace').strip()
            if not line:
                continue
            if line.startswith('# IMAGE '):
                parts = line.split()
                if len(parts) != 4 or len(parts[3]) != 2400:
                    continue
                try:
                    data = bytes.fromhex(parts[3])
                    stamp = int(parts[2])
                except ValueError:
                    continue
                output.write(json.dumps({'ms': stamp, 'gray': data.hex()}) + '\n')
                frames += 1
            else:
                output.write(json.dumps({'log': line}) + '\n')
                if line in ('ALT_LEFT', 'ALT_RIGHT'):
                    events.append(line)
                    print(line, flush=True)
                elif '# READY' in line:
                    print(line, flush=True)
    print(f'Recorded {frames} frames, events={events}, file={args.out}', flush=True)
finally:
    port.write(b'vd' if args.diagnostics else b'v')
    port.close()
