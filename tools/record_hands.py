#!/usr/bin/env python3
"""Record the ESP-IDF hand evaluation firmware without reset or commands."""
import argparse
import json
import statistics
import time
from pathlib import Path

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', required=True)
    parser.add_argument('--seconds', type=float, default=15)
    parser.add_argument('--label', default='unlabelled')
    parser.add_argument('--out', required=True, type=Path)
    args = parser.parse_args()
    if args.seconds <= 0:
        parser.error('--seconds must be positive')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    port = serial.Serial(baudrate=115200, timeout=0.2)
    port.dtr = False
    port.rts = False
    port.port = args.port
    frames = []
    port.open()
    started = time.monotonic()
    print(f'Recording {args.label} for {args.seconds:g}s', flush=True)
    try:
        port.reset_input_buffer()
        with args.out.open('x') as out:
            out.write(json.dumps({'label': args.label, 'seconds': args.seconds, 'port': args.port}) + '\n')
            while time.monotonic() - started < args.seconds:
                line = port.readline().decode('utf-8', errors='replace').strip()
                if not line:
                    continue
                out.write(json.dumps({'host_s': time.monotonic() - started, 'log': line}) + '\n')
                if line.startswith('HAND_FRAME '):
                    try:
                        fields = dict(item.split('=', 1) for item in line.split()[1:])
                        values = {key: float(value) for key, value in fields.items()}
                        if not {'seq', 'hands', 'failures', 'infer_ms', 'total_ms', 'age_ms'} <= values.keys():
                            raise ValueError('Incomplete frame')
                        frames.append(values)
                    except ValueError:
                        print(f'Malformed frame log: {line}', flush=True)
                else:
                    print(line, flush=True)
    finally:
        port.close()
    if not frames:
        raise SystemExit('No HAND_FRAME records received; inspect log/port/firmware.')
    summary = {'frames': len(frames), 'frames_with_hand': sum(row['hands'] > 0 for row in frames),
               'capture_failure_counter_first': frames[0]['failures'],
               'capture_failure_counter_last': frames[-1]['failures']}
    for key in ('infer_ms', 'total_ms', 'age_ms'):
        values = sorted(row[key] for row in frames)
        summary[key] = {'median': statistics.median(values), 'max': max(values)}
    print(json.dumps(summary, indent=2))
    print(f'Saved {args.out}; hand presence rate is not gesture accuracy.')


if __name__ == '__main__':
    main()
