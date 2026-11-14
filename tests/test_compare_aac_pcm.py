#!/usr/bin/env python3
"""Deterministic tests of AAC PCM comparison/coverage; no FFmpeg dependency."""
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from compare_aac_pcm import diagnostic, uninterrupted_runs, evaluate, FRAME


def test():
    rows = [(1, "ok")] * 4
    ref = [math.sin(i * 0.015) * 0.25 for i in range(4 * FRAME)]
    native = ref.copy()
    assert uninterrupted_runs(rows) == [(0, 4)]
    exact = diagnostic(native, ref, rows, 0)
    assert exact["count"] == 3 * FRAME
    assert abs(exact["ratio"] - 1) < 1e-12
    assert exact["rmse"] == 0
    assert exact["max_error"] == 0
    assert abs(exact["corr"] - 1) < 1e-12

    scaled = [x * 2 for x in ref]
    m = diagnostic(scaled, ref, rows, 0)
    assert abs(m["ratio"] - 2) < 1e-12
    assert m["rmse"] > 0.01
    assert abs(m["corr"] - 1) < 1e-12
    # Correlation alone cannot distinguish a massive gain error.
    assert evaluate(scaled, ref, rows, 0) == 0

    # A missing frame divides the contiguous runs. The first successful
    # frame after the gap is deliberately excluded as overlap warmup.
    skipped = [(1, "ok"), (-1, "unsupported_fil"), (1, "ok"),
               (1, "ok"), (1, "ok")]
    ref2 = [0.25] * (5 * FRAME)
    nat2 = ref2.copy()
    assert uninterrupted_runs(skipped) == [(0, 1), (2, 5)]
    check = diagnostic(nat2, ref2, skipped, 0)
    assert check["count"] == 2 * FRAME
    assert evaluate(nat2, ref2, skipped, 0) == 0
    assert evaluate(nat2, ref2, [(1, "ok")] * 5, 0) == 0

    # A non-finite PCM value must never be passed off as a good match.
    nat2[3 * FRAME + 50] = float("nan")
    try:
        diagnostic(nat2, ref2, skipped, 0)
    except ValueError:
        pass
    else:
        raise AssertionError("non-finite audio accepted")
    try:
        evaluate(ref2[:-1], ref2, skipped, 0)
    except ValueError:
        pass
    else:
        raise AssertionError("truncated native PCM accepted")
    print("PASS AAC PCM comparator: exact, gain mismatch, RMSE, gaps, non-finite")


if __name__ == "__main__":
    test()
