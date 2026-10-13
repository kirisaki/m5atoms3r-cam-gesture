"""Validate burst framing so partial/corrupt recordings cannot look complete."""
import contextlib
import io
import json
from pathlib import Path
import tempfile
import unittest

from burst_hands import receive


def stream(missing_row=False, end_count=1, box_sequence=1):
    lines = [
        'BURST_BEGIN capacity=40 rotation=270',
        'BURST_CAPTURED frames=1 failures=0 elapsed_ms=100.0',
        'BURST_FRAME seq=1 capture_ms=5.0 hands=1 infer_ms=172.0',
        f'BURST_BOX seq={box_sequence} score=0.5 x1=1 y1=2 x2=3 y2=4',
        'HAND_IMAGE_BEGIN seq=1 width=240 height=320 rotation=270',
    ]
    for y in range(319 if missing_row else 320):
        lines.append(f'HAND_IMAGE_ROW {y} ' + '123456' * 240)
    lines += ['HAND_IMAGE_END seq=1', f'BURST_END frames={end_count}']
    return io.BytesIO(('\n'.join(lines) + '\n').encode())


class BurstReceptionTests(unittest.TestCase):
    def test_exact_pixels_and_metadata(self):
        with tempfile.TemporaryDirectory() as directory, contextlib.redirect_stdout(io.StringIO()):
            out = Path(directory)
            receive(stream(), out)
            self.assertEqual((out / 'frame-001.ppm').read_bytes(), b'P6\n240 320\n255\n' + bytes.fromhex('123456') * (240 * 320))
            frame = json.loads((out / 'manifest.json').read_text())['frames'][0]
            self.assertEqual(frame['capture_ms'], '5.0')
            self.assertEqual(frame['boxes'][0]['score'], '0.5')

    def test_incomplete_or_mismatched_data_never_gets_manifest(self):
        for kwargs in ({'missing_row': True}, {'end_count': 2}, {'box_sequence': 2}):
            with self.subTest(kwargs=kwargs), tempfile.TemporaryDirectory() as directory, contextlib.redirect_stdout(io.StringIO()):
                out = Path(directory)
                with self.assertRaises(RuntimeError):
                    receive(stream(**kwargs), out)
                self.assertFalse((out / 'manifest.json').exists())


if __name__ == '__main__':
    unittest.main()
