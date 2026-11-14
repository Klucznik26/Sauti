#!/usr/bin/env python3
"""Offline AAC-29 strict gate tests (no FFmpeg, NumPy or compiled decoder)."""
import array
import math
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "tools"))
from verify_aac_stereo_strict import assess

FRAME = 1024


class StrictStereoTests(unittest.TestCase):
    def setUp(self):
        self.rows = [(1, "ok")] * 4
        self.native = array.array("f")
        for n in range(4 * FRAME):
            self.native.append(math.sin(n * 0.021) * 0.45)
            self.native.append(math.sin(n * 0.037) * 0.36)
        self.reference = array.array("f", self.native)

    def test_perfect_stereo(self):
        result = assess(self.native, self.reference, self.rows)
        self.assertTrue(result["ok"])
        self.assertEqual(set(result["channels"]), {"LEFT", "RIGHT"})

    def test_missing_frame_is_failure(self):
        self.rows[1] = (-1, "unsupported_intensity")
        result = assess(self.native, self.reference, self.rows)
        self.assertFalse(result["ok"])
        self.assertEqual(result["decoded"], 3)

    def test_bad_left_gain(self):
        for i in range(0, len(self.native), 2):
            self.native[i] *= 2.0
        result = assess(self.native, self.reference, self.rows)
        self.assertFalse(result["ok"])
        self.assertFalse(result["channels"]["LEFT"]["ok"])
        self.assertTrue(result["channels"]["RIGHT"]["ok"])

    def test_bad_right_phase(self):
        for i in range(1, len(self.native), 2):
            self.native[i] *= -1.0
        result = assess(self.native, self.reference, self.rows)
        self.assertFalse(result["ok"])
        self.assertTrue(result["channels"]["LEFT"]["ok"])
        self.assertFalse(result["channels"]["RIGHT"]["ok"])

    def test_nonfinite(self):
        self.native[2049] = float("nan")
        with self.assertRaises(ValueError):
            assess(self.native, self.reference, self.rows)

    def test_size_mismatch(self):
        with self.assertRaises(ValueError):
            assess(self.native[:-2], self.reference, self.rows)


if __name__ == "__main__":
    unittest.main()
