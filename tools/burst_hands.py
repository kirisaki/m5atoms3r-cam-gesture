#!/usr/bin/env python3
"""Capture a RAM-buffered burst and save its exact inference inputs locally."""
import argparse
import json
import time
from pathlib import Path

def fields(line):
    return dict(item.split('=', 1) for item in line.split()[1:])


def receive(port, out):
    deadline = time.monotonic() + 180
    frames = []
    current = None
    rows = None
    captured = None
    with (out / 'events.jsonl').open('x') as log:
        while time.monotonic() < deadline:
            line = port.readline().decode('ascii', errors='replace').strip()
            if not line:
                continue
            if line.startswith('HAND_IMAGE_ROW '):
                if rows is None:
                    raise RuntimeError('Image row without header')
                _, row, pixels = line.split()
                y = int(row)
                data = bytes.fromhex(pixels)
                if not 0 <= y < 320 or y in rows or len(data) != 720:
                    raise RuntimeError('Invalid image row')
                rows[y] = data
                continue
            log.write(json.dumps({'host_s': time.monotonic(), 'log': line}) + '\n')
            log.flush()
            if line.startswith('BURST_ERROR '):
                raise RuntimeError(line)
            if line.startswith('BURST_BEGIN '):
                print(line, flush=True)
            elif line.startswith('BURST_CAPTURED '):
                captured = fields(line)
                print(line, flush=True)
            elif line.startswith('BURST_FRAME '):
                if current is not None:
                    raise RuntimeError('Previous frame incomplete')
                current = fields(line)
                if int(current['seq']) != len(frames) + 1:
                    raise RuntimeError('Frame sequence mismatch')
                current['boxes'] = []
            elif line.startswith('BURST_BOX '):
                box = fields(line)
                if current is None or box['seq'] != current['seq']:
                    raise RuntimeError('Box sequence mismatch')
                current['boxes'].append(box)
            elif line.startswith('HAND_IMAGE_BEGIN '):
                info = fields(line)
                if (current is None or info['seq'] != current['seq'] or rows is not None or
                        (int(info['width']), int(info['height'])) != (240, 320)):
                    raise RuntimeError('Invalid image header')
                current.update(info)
                rows = {}
            elif line.startswith('HAND_IMAGE_END '):
                if (current is None or rows is None or len(rows) != 320 or
                        fields(line)['seq'] != current['seq'] or
                        len(current['boxes']) != int(current['hands'])):
                    raise RuntimeError('Incomplete image or detections')
                filename = f"frame-{int(current['seq']):03d}.ppm"
                with (out / filename).open('xb') as image:
                    image.write(b'P6\n240 320\n255\n')
                    for y in range(320):
                        image.write(rows[y])
                current['file'] = filename
                frames.append(current)
                print(f"Saved {filename} at {current['capture_ms']}ms; hands={current['hands']}", flush=True)
                current = rows = None
            elif line.startswith('BURST_END '):
                count = int(fields(line)['frames'])
                if captured is None or count != int(captured['frames']) or count != len(frames) or current is not None:
                    raise RuntimeError('Burst count mismatch')
                if not frames:
                    raise RuntimeError('No burst frames captured')
                (out / 'manifest.json').write_text(json.dumps({'capture': captured, 'frames': frames}, indent=2) + '\n')
                print(f'Completed {len(frames)} frames in {out}', flush=True)
                return
        raise RuntimeError(f'Burst timed out after {len(frames)} images')


def main():
    import serial

    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--trigger-file', type=Path, help='Wait for this new file after ARMED (up to 5 minutes)')
    args = parser.parse_args()
    if args.out.exists():
        parser.error('--out must be a new directory')
    if args.trigger_file and args.trigger_file.exists():
        parser.error('--trigger-file must not exist yet')
    args.out.mkdir(parents=True)
    port = serial.Serial(baudrate=115200, timeout=0.5)
    port.dtr = port.rts = False
    port.port = args.port
    port.open()
    try:
        started = time.monotonic()
        while time.monotonic() - started < 25:
            if port.readline().startswith(b'HAND_FRAME ') and time.monotonic() - started >= 5:
                break
        else:
            raise RuntimeError('Camera did not become ready')
        print('ARMED: camera ready', flush=True)
        if args.trigger_file:
            deadline = time.monotonic() + 300
            while not args.trigger_file.exists():
                if time.monotonic() >= deadline:
                    raise RuntimeError('Trigger timed out')
                port.readline()  # Keep draining live logs while waiting.
        port.reset_input_buffer()
        port.write(b'b')
        port.flush()
        receive(port, args.out)
    finally:
        port.close()


if __name__ == '__main__':
    main()
