#!/usr/bin/env python3
"""Record optical flow after a display cue; requires current camera firmware."""
import argparse
import json
import time
from pathlib import Path

import serial


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', default='/dev/ttyACM0')
    parser.add_argument('--seconds', type=float, default=25)
    parser.add_argument('--sequence', action='store_true', help='Require sequence experiment; report segment/complete/cancel events')
    parser.add_argument('--label', required=True, help='Intended action, not ground truth')
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    if args.seconds <= 0:
        parser.error('--seconds must be positive')
    args.out.parent.mkdir(parents=True, exist_ok=True)
    # Exclusive creation prevents accidental destruction of earlier trials.
    with args.out.open('x') as output:
        def save(row):
            output.write(json.dumps(row) + '\n')
            output.flush()

        save({'trial': args.label, 'seconds': args.seconds, 'cue': 'start'})
        port = serial.Serial(baudrate=115200, timeout=0.2)
        port.dtr, port.rts = True, False
        port.port = args.port
        port.open()
        try:
            port.reset_input_buffer()
            port.write(b'r')
            started = time.monotonic()
            ready = disabled = cue_sent = False
            deadline = None
            frames = 0
            events = []
            sequences = []
            print('Keep still: calibrating, then START will signal the start.', flush=True)
            while deadline is None or time.monotonic() < deadline:
                now = time.monotonic()
                if deadline is None and now - started > 20:
                    raise RuntimeError('No confirmed display cue within 20 seconds')
                if ready and disabled and not cue_sent and now - started >= 5:
                    port.write(b'c')
                    cue_sent = True
                line = port.readline().decode('ascii', errors='replace').strip()
                if not line:
                    continue
                if line.startswith('# IMAGE '):
                    parts = line.split()
                    if len(parts) != 4 or len(parts[3]) != 2400:
                        raise RuntimeError('Incomplete image frame')
                    gray = bytes.fromhex(parts[3])
                    save({'ms': int(parts[2]), 'gray': gray.hex()})
                    frames += 1
                elif line.startswith('# PRESENCE '):
                    fields = line.split()[2:]
                    if len(fields) != 7:
                        raise RuntimeError('Incomplete background presence record')
                    ms, state, changed, largest, fit, gain, offset = fields
                    save({'presence': {'ms': int(ms), 'state': int(state), 'changed': int(changed),
                                       'largest': int(largest), 'fit_pixels': int(fit),
                                       'gain': float(gain), 'offset': float(offset)}})
                elif line.startswith('# SEQ_STOP_FLOW '):
                    fields = line.split()[2:]
                    if len(fields) != 12:
                        raise RuntimeError('Incomplete sequence flow record')
                    ms, dt, x, y, inliers, moving, cx, cy, edges, idle, glob, processing = fields
                    save({'sequence_flow': {'ms': int(ms), 'dt_ms': int(dt),
                                           'x': float(x), 'y': float(y),
                                           'inliers': int(inliers), 'moving': int(moving),
                                           'center_x': float(cx), 'center_y': float(cy),
                                           'edge_votes': int(edges), 'idle_ms': int(idle),
                                           'global': bool(int(glob)), 'processing_ms': int(processing)}})
                elif line.startswith('# FLOW '):
                    fields = line.split()[2:]
                    if len(fields) != 9:
                        raise RuntimeError('Incomplete flow record')
                    ms, dt, along, across, inliers, moving, glob, travel, state = fields
                    dt = int(dt)
                    save({'flow': {'ms': int(ms), 'dt_ms': dt,
                                  'along': float(along), 'across': float(across),
                                  'speed': abs(float(along)) * 1000 / dt if dt else 0,
                                  'inliers': int(inliers), 'moving': int(moving),
                                  'global': bool(int(glob)), 'travel': float(travel),
                                  'state': int(state)}})
                else:
                    save({'log': line})
                    if line == '# hid_enabled=0':
                        disabled = True
                    if line.startswith('# READY'):
                        if args.sequence and 'sequence experiment' not in line:
                            raise RuntimeError('Sequence experiment firmware is required')
                        ready = True
                    if line.startswith('# SEQUENCE '):
                        sequences.append(line)
                        print(line, flush=True)
                    if line.startswith('# TRIAL_CUE'):
                        if not cue_sent or not line.endswith('ack=1'):
                            raise RuntimeError(f'Display cue failed: {line}')
                        deadline = time.monotonic() + args.seconds
                        print(f'START: {args.label}, {args.seconds:g} seconds. {line}', flush=True)
                    if line in ('ALT_LEFT', 'ALT_RIGHT'):
                        events.append(line)
                        print(line, flush=True)
                    if 'SENT' in line and line.startswith('# HID '):
                        raise RuntimeError('Unexpected HID output during measurement')
            save({'complete': True, 'frames': frames, 'events': events, 'sequences': sequences})
            print(f'DONE: {frames} frames, events={events}, sequence_events={len(sequences)}, file={args.out}', flush=True)
        finally:
            try:
                port.write(b'0si')
                port.flush()
            finally:
                port.close()


if __name__ == '__main__':
    main()
