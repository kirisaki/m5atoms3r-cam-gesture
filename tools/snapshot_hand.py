#!/usr/bin/env python3
"""Save one rotated RGB inference input from the hand evaluation firmware as PPM."""
import argparse
import time
from pathlib import Path

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--out', required=True, type=Path)
    parser.add_argument('--delay', type=float, default=5, help='Wait after first frame before requesting image')
    args = parser.parse_args()
    if args.delay < 0:
        parser.error('--delay must be nonnegative')
    if args.out.exists():
        parser.error('--out must be a new file')
    port = serial.Serial(baudrate=115200, timeout=0.5)
    port.dtr = False
    port.rts = False
    port.port = args.port
    port.open()
    try:
        started = time.monotonic()
        deadline = started + 20
        while time.monotonic() < deadline:
            if port.readline().startswith(b'HAND_FRAME ') and time.monotonic() - started >= 5:
                break
        else:
            raise RuntimeError('No camera frames received')
        print(f'Camera running; snapshot in {args.delay:g}s', flush=True)
        time.sleep(args.delay)
        port.reset_input_buffer()
        port.write(b's')
        print('Snapshot requested', flush=True)
        width = height = sequence = None
        rows = {}
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            line = port.readline().decode('ascii', errors='replace').strip()
            if line.startswith('HAND_IMAGE_BEGIN '):
                fields = dict(item.split('=', 1) for item in line.split()[1:])
                width, height, sequence = int(fields['width']), int(fields['height']), int(fields['seq'])
                if (width, height) != (240, 320):
                    raise RuntimeError('Unexpected image dimensions')
                rows.clear()
                print(f'Receiving frame {sequence}', flush=True)
            elif line.startswith('HAND_IMAGE_ROW ') and width:
                _, y, pixels = line.split()
                data = bytes.fromhex(pixels)
                if len(data) != width * 3 or not 0 <= int(y) < height or int(y) in rows:
                    raise RuntimeError('Invalid/duplicate image row')
                rows[int(y)] = data
            elif line == f'HAND_IMAGE_END seq={sequence}':
                if len(rows) != height:
                    raise RuntimeError(f'Incomplete image: {len(rows)} rows')
                args.out.parent.mkdir(parents=True, exist_ok=True)
                with args.out.open('xb') as out:
                    out.write(f'P6\n{width} {height}\n255\n'.encode())
                    for y in range(height):
                        out.write(rows[y])
                print(f'Saved frame {sequence}: {args.out}', flush=True)
                return
        raise RuntimeError(f'Snapshot timed out: received {len(rows)} image rows')
    finally:
        port.close()


if __name__ == '__main__':
    main()
