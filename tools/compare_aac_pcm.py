#!/usr/bin/env python3
"""AAC-15/17: compare native mono f32 frames against FFmpeg decoded f32.
Diagnostic by design: no claim of conformance until coverage and measured
correlation/scale meet explicit thresholds. No NumPy dependency.
"""
import array
import math
import sys

def load(path):
    a = array.array("f")
    with open(path, "rb") as file:
        a.frombytes(file.read())
    if sys.byteorder != "little":
        a.byteswap()
    return a

def main():
    if len(sys.argv) != 4:
        print("usage: compare_aac_pcm.py native.f32 reference.f32 mask.txt")
        return 2
    native, reference = load(sys.argv[1]), load(sys.argv[2])
    rows = []
    with open(sys.argv[3], encoding="utf-8") as file:
        for line in file:
            index, status, reason = line.split()
            rows.append((int(index), int(status), reason))
    if len(native) != 1024 * len(rows) or not reference:
        print("FAIL: invalid PCM lengths or frame mask")
        return 1
    good = [i for i, result, _ in rows if result == 1]
    bad = [(i, reason) for i, result, reason in rows if result != 1]
    print("AAC comparison: frames=%d, decoded=%d, skipped=%d" %
          (len(rows), len(good), len(bad)))
    for index, reason in bad:
        print("  skipped frame %d: %s" % (index, reason))
    if not good:
        print("INCONCLUSIVE: no comparable native frames")
        return 0

    # Align reference globally using sparse correlation on decoded frames
    # (skip the first 1024 native samples affected by encoder priming).
    def metrics(lag):
        sum_a = sum_b = cross = err = 0.0
        count = 0
        for i in good:
            if i == 0:
                continue
            lo = i * 1024
            for p in range(lo, lo + 1024, 8):
                q = p + lag
                if 0 <= q < len(reference):
                    a, b = float(native[p]), float(reference[q])
                    if not (math.isfinite(a) and math.isfinite(b)):
                        continue
                    sum_a += a * a
                    sum_b += b * b
                    cross += a * b
                    count += 1
        correlation = cross / math.sqrt(sum_a * sum_b) if sum_a and sum_b else 0
        amplitude_ratio = math.sqrt(sum_a / sum_b) if sum_b else float("nan")
        return correlation, amplitude_ratio, count
    lag, corr, ratio, count = max(
        ((shift, *metrics(shift)) for shift in range(-2048, 2049, 16)),
        key=lambda x: abs(x[1])
    )
    print("AAC PCM diagnostics: lag=%d samples, correlation=%.6f, native/ref RMS=%.6g, observations=%d" %
          (lag, corr, ratio, count))
    if count < 128 or abs(corr) < 0.98 or not (0.8 <= ratio <= 1.25):
        print("NOT VERIFIED: native PCM does not yet match FFmpeg reference")
    else:
        print("CANDIDATE MATCH: correlation/RMS threshold met; inspect waveform and skipped frames")
    # Intentionally diagnostic: a candidate match isn't an AAC conformance proof.
    return 0

if __name__ == "__main__":
    sys.exit(main())
