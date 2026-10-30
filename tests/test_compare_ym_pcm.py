#!/usr/bin/env python3
"""Self-contained regression for YM PCM comparison metrics and alignment."""
import importlib.util
import math
import pathlib
import struct
import tempfile
import unittest
import wave

SCRIPT = pathlib.Path(__file__).resolve().parents[1] / "tools" / "compare_ym_pcm.py"
spec = importlib.util.spec_from_file_location("compare_ym_pcm", SCRIPT)
cmp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(cmp)


class YmPcmCompareTests(unittest.TestCase):
    def test_identical(self):
        signal = [math.sin(i * .23) for i in range(1024)]
        n, rms, peak, corr = cmp.metrics(signal, signal)
        self.assertEqual(n, 1024)
        self.assertLess(rms, 1e-7)
        self.assertEqual(peak, 0)
        self.assertAlmostEqual(corr, 1)

    def test_inverted(self):
        signal = [math.sin(i * .23) for i in range(1024)]
        _, rms, peak, corr = cmp.metrics(signal, [-v for v in signal])
        self.assertGreater(rms, .9)
        self.assertGreater(peak, 1)
        self.assertAlmostEqual(corr, -1)

    def test_lag(self):
        signal = [math.sin(i * .13) + .25 * math.sin(i * .37) for i in range(8192)]
        lag, score = cmp.best_lag(signal, [0.0] * 17 + signal, 64, 1024)
        self.assertEqual(lag, 17)
        self.assertAlmostEqual(score, 1, places=6)

    def test_envelope_rms_constant(self):
        contour = cmp.envelope_rms([0.25] * 1000, 1000)
        self.assertEqual(len(contour), 100)
        self.assertTrue(all(abs(value - 0.25) < 1e-12 for value in contour))

    def test_envelope_rms_partial_bin(self):
        self.assertEqual(cmp.envelope_rms([0.0, 1.0, 1.0], 200), [math.sqrt(0.5), 1.0])

    def test_reference_filter_silence_and_repeatability(self):
        silence = [0.0] * 2048
        self.assertEqual(cmp.stsound_filter(silence), silence)
        signal = [0.25] * 2048
        self.assertEqual(cmp.stsound_filter(signal), cmp.stsound_filter(signal))

    def test_reference_filter_dc_rejection(self):
        # After the 512-sample DC history has filled, a constant signal
        # produces no steady output. This is a property of the *approximation*.
        filtered = cmp.stsound_filter([0.25] * 2048)
        self.assertTrue(all(value == 0.0 for value in filtered[520:]))

    def test_reference_filter_bounds(self):
        out = cmp.stsound_filter([1.0, -1.0] * 1024)
        self.assertEqual(len(out), 2048)
        self.assertTrue(all(-1.0 <= x <= 1.0 and math.isfinite(x) for x in out))

    def test_gain_fit_exact_scale(self):
        signal = [0.1, -0.2, 0.3, -0.4] * 40
        gain, residual = cmp.gain_fit_metrics(signal, [0.6*x for x in signal])
        self.assertAlmostEqual(gain, 0.6, places=12)
        self.assertLess(residual, 1e-12)

    def test_gain_fit_phase_error_remains(self):
        signal = [1.0, 0.0, -1.0, 0.0] * 40
        shifted = [0.0, 1.0, 0.0, -1.0] * 40
        gain, residual = cmp.gain_fit_metrics(signal, shifted)
        self.assertAlmostEqual(gain, 0.0, places=12)
        self.assertGreater(residual, 0.7)

    def test_gain_fit_silent_candidate(self):
        gain, residual = cmp.gain_fit_metrics([0.0]*64, [0.1]*64)
        self.assertTrue(math.isnan(gain))
        self.assertTrue(math.isnan(residual))

    def test_candidate_gain_half_reduces_rms_error(self):
        candidate = [0.4, -0.2, 0.1, -0.3]
        reference = [x * 0.5 for x in candidate]
        _, raw_error, _, _ = cmp.metrics(candidate, reference)
        _, scaled_error, _, _ = cmp.metrics([x * 0.5 for x in candidate], reference)
        self.assertGreater(raw_error, 0.01)
        self.assertAlmostEqual(scaled_error, 0.0, places=12)

    def test_gain_does_not_change_correlation(self):
        signal = [math.sin(i * 0.13) for i in range(128)]
        reference = [math.sin(i * 0.13 + 0.1) for i in range(128)]
        before = cmp.metrics(signal, reference)[3]
        after = cmp.metrics([x * 0.5 for x in signal], reference)[3]
        self.assertAlmostEqual(before, after, places=12)

    def test_window_diagnostics_local_gain_change(self):
        candidate = [0.5, -0.5] * 10
        reference = [0.25, -0.25] * 5 + [0.5, -0.5] * 5
        rows = cmp.window_diagnostics(candidate, reference, rate=100, window_ms=100)
        self.assertEqual(len(rows), 2)
        self.assertAlmostEqual(rows[0][4], 0.5)
        self.assertAlmostEqual(rows[1][4], 1.0)
        self.assertAlmostEqual(rows[0][2], 1.0)
        self.assertAlmostEqual(rows[1][2], 1.0)

    def test_window_diagnostics_detects_local_phase_error(self):
        candidate = [1.0, 0.0, -1.0, 0.0] * 10
        reference = candidate[:20] + [0.0, 1.0, 0.0, -1.0] * 5
        rows = cmp.window_diagnostics(candidate, reference, rate=100, window_ms=200)
        self.assertEqual(len(rows), 2)
        self.assertAlmostEqual(rows[0][2], 1.0)
        self.assertAlmostEqual(rows[1][2], 0.0)
        self.assertGreater(rows[1][5], 0.7)

    def test_window_diagnostics_partial_last_window(self):
        rows = cmp.window_diagnostics([1.0] * 25, [1.0] * 25, 100, 100)
        self.assertEqual(len(rows), 3)
        self.assertAlmostEqual(rows[-1][1], 0.25)

    def test_ab_metrics_match_single_file_pipeline(self):
        # The four-shape reporter must use the same filter/gain/contour
        # sequence as compare_ym_pcm.py, without automatic lag correction.
        import importlib.util
        ab_script = SCRIPT.parent / "compare_ym_envelope_ab.py"
        # importlib does not add the script's directory to sys.path.
        # Reuse the already imported comparator as the module dependency.
        import sys
        previous = sys.modules.get("compare_ym_pcm")
        sys.modules["compare_ym_pcm"] = cmp
        try:
            spec = importlib.util.spec_from_file_location("compare_ym_envelope_ab", ab_script)
            ab = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(ab)
        finally:
            if previous is None:
                del sys.modules["compare_ym_pcm"]
            else:
                sys.modules["compare_ym_pcm"] = previous
        with tempfile.TemporaryDirectory() as directory:
            candidate_path = pathlib.Path(directory) / "sauti.wav"
            reference_path = pathlib.Path(directory) / "reference.wav"
            samples = [int(6000 * math.sin(i * 0.15)) for i in range(1000)]
            for filename, data in ((candidate_path, samples),
                                   (reference_path, [int(x * 0.5) for x in samples])):
                with wave.open(str(filename), "wb") as out:
                    out.setnchannels(1)
                    out.setsampwidth(2)
                    out.setframerate(1000)
                    out.writeframes(struct.pack("<" + "h"*len(data), *data))
            reported = ab.analyze(candidate_path, reference_path)
            _, source = cmp.read_pcm(candidate_path)
            _, target = cmp.read_pcm(reference_path)
            filtered = [v * 0.5 for v in cmp.stsound_filter(source)]
            self.assertAlmostEqual(reported[0], cmp.metrics(filtered, target)[3])
            self.assertAlmostEqual(reported[1], cmp.metrics(filtered, target)[1])
            self.assertAlmostEqual(reported[3], cmp.gain_fit_metrics(filtered, target)[1])

    def test_controlled_ym_envelope_period_encoding(self):
        import importlib.util
        script = SCRIPT.parent / "generate_ym_reference.py"
        spec = importlib.util.spec_from_file_location("generate_ym_reference", script)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        base = module.create("envelope_0c")
        self.assertEqual(base, module.create("envelope_0c", envelope_period=200))
        for period in (1, 25, 256, 800, 65535):
            data = module.create("envelope_0c", envelope_period=period)
            self.assertEqual(data[4 + 11*module.FRAMES], period & 255)
            self.assertEqual(data[4 + 12*module.FRAMES], period >> 8)
            self.assertEqual(data[4 + 13*module.FRAMES], 0x0c)
        for period in (0, 65536):
            with self.assertRaises(ValueError):
                module.create("envelope_0c", envelope_period=period)

    def test_controlled_ym_fixed_tone_levels(self):
        import importlib.util
        script = SCRIPT.parent / "generate_ym_reference.py"
        spec = importlib.util.spec_from_file_location("generate_ym_reference", script)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        frames = module.FRAMES
        for level in range(16):
            song = module.create("tone_fixed", fixed_volume=level)
            self.assertEqual(song[:4], b"YM3!")
            self.assertEqual(len(song), 4 + 14 * frames)
            self.assertEqual(song[4 + 8*frames], level)
            self.assertEqual(song[4 + 8*frames + frames-1], level)
            self.assertEqual(song[4 + 7*frames], 0x3e)
            self.assertEqual(song[4 + 13*frames], 0xff)
        for level in (-1, 16):
            with self.assertRaises(ValueError):
                module.create("tone_fixed", fixed_volume=level)
        with self.assertRaises(ValueError):
            module.create("tone_fixed")

    def test_ac_rms_constant_and_alternating(self):
        import importlib.util
        import sys
        script = SCRIPT.parent / "compare_ym_dac_levels.py"
        previous_generator = sys.modules.get("generate_ym_reference")
        generator_script = SCRIPT.parent / "generate_ym_reference.py"
        gs = importlib.util.spec_from_file_location("generate_ym_reference", generator_script)
        generator = importlib.util.module_from_spec(gs)
        gs.loader.exec_module(generator)
        sys.modules["generate_ym_reference"] = generator
        previous_cmp = sys.modules.get("compare_ym_pcm")
        sys.modules["compare_ym_pcm"] = cmp
        try:
            spec = importlib.util.spec_from_file_location("compare_ym_dac_levels", script)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
        finally:
            for name, previous in (("compare_ym_pcm", previous_cmp), ("generate_ym_reference", previous_generator)):
                if previous is None:
                    del sys.modules[name]
                else:
                    sys.modules[name] = previous
        self.assertAlmostEqual(module.ac_rms([3.0]*10), 0.0)
        self.assertAlmostEqual(module.ac_rms([1.0,-1.0]*10), 1.0)

    def test_wav_roundtrip(self):
        with tempfile.TemporaryDirectory() as d:
            path = pathlib.Path(d) / "tone.wav"
            with wave.open(str(path), "wb") as out:
                out.setnchannels(1)
                out.setsampwidth(2)
                out.setframerate(44100)
                out.writeframes(struct.pack("<4h", 0, 32767, -32768, 1024))
            rate, samples = cmp.read_pcm(path)
            self.assertEqual(rate, 44100)
            self.assertEqual(len(samples), 4)
            self.assertAlmostEqual(samples[2], -1)
            rate_str, samples_str = cmp.read_pcm(str(path))
            self.assertEqual(rate_str, rate)
            self.assertEqual(samples_str, samples)


if __name__ == "__main__":
    unittest.main()
