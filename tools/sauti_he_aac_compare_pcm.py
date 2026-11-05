#!/usr/bin/env python3
"""SBR-54b: compare exported PREVIEW f32le mono/48k with an independent decoder.

No decoding libraries are imported. This is a developer measurement utility;
the comparison is not proof of MPEG-4 HE-AAC conformance.
"""
import array
import math
import os
import sys

WINDOW = 2048
MAX_LAG = 8192


def load(path):
    count = os.path.getsize(path)
    if count == 0 or count % 4:
        raise ValueError(f"{path}: expected nonempty float32 raw PCM")
    samples = array.array("f")
    with open(path, "rb") as f:
        samples.frombytes(f.read())
    if sys.byteorder != "little":
        samples.byteswap()
    if not all(math.isfinite(float(s)) for s in samples):
        raise ValueError(f"{path}: NaN or infinity")
    return samples


def rms_peak(samples):
    if not samples:
        return 0.0, 0.0
    square = sum(float(s) * float(s) for s in samples)
    return math.sqrt(square / len(samples)), max(abs(float(s)) for s in samples)


def corr_at(x, y, lag, stride):
    """Positive lag: reference y occurs later than preview x."""
    lo = max(0, -lag) + 1024
    hi = min(len(x), len(y) - lag) - 1024
    if hi - lo < 2048:
        return None
    n = 0
    sx = sy = sxx = syy = sxy = 0.0
    for ix in range(lo, hi, stride):
        a = float(x[ix])
        b = float(y[ix + lag])
        sx += a
        sy += b
        sxx += a*a
        syy += b*b
        sxy += a*b
        n += 1
    xx = sxx - sx*sx/n
    yy = syy - sy*sy/n
    xy = sxy - sx*sy/n
    if xx <= 0 or yy <= 0:
        return None
    return xy / math.sqrt(xx*yy)


def find_lag(x, y):
    # Sample-exact alignment needs a sufficiently fine initial scan.
    # The old 64-sample grid was anchored at -maxlag, not at zero:
    # for maxlag=4128 it skipped lag=384 altogether and locked onto
    # a distant periodic side-lobe (-3685). An 8-sample, ZERO-anchored
    # grid captures sharp transients and both offset signs reliably.
    maxlag = min(MAX_LAG, max(len(x), len(y)) // 4)
    coarse_step = 8
    coarse = []
    for lag in range(-(maxlag // coarse_step) * coarse_step,
                     maxlag + 1, coarse_step):
        c = corr_at(x, y, lag, 16)
        if c is not None:
            coarse.append((abs(c), lag))
    if not coarse:
        raise ValueError("insufficient overlapping non-silent samples")
    coarse.sort(reverse=True)
    # Refine several peaks at native sample offsets, rather than
    # accepting periodic false matches from one wide coarse grid.
    candidates = {}
    for _, center in coarse[:8]:
        for lag in range(max(-maxlag, center-16),
                         min(maxlag, center+16) + 1, 2):
            c = corr_at(x, y, lag, 8)
            if c is not None:
                candidates[lag] = c
    ranked = sorted(candidates, key=lambda lag: abs(candidates[lag]),
                    reverse=True)
    best = None
    for center in ranked[:6]:
        for lag in range(max(-maxlag, center-2),
                         min(maxlag, center+2) + 1):
            c = corr_at(x, y, lag, 1)
            if c is not None and (best is None or abs(c) > abs(best[1])):
                best = (lag, c)
    if best is None:
        raise ValueError("cannot align samples")
    return best


def compare(x, y):
    lag, corr = find_lag(x, y)
    start = max(0, -lag)
    stop = min(len(x), len(y)-lag)
    n = stop-start
    sx = sy = sxx = sxy = syy = 0.0
    for i in range(start, stop):
        a = float(x[i])
        b = float(y[i+lag])
        sx += a
        sy += b
        sxx += a*a
        sxy += a*b
        syy += b*b
    mx, my = sx/n, sy/n
    xx = sxx-sx*sx/n
    xy = sxy-sx*sy/n
    gain = xy/xx if xx else float("nan")
    bias = my-gain*mx
    squared = 0.0
    for i in range(start, stop):
        e = (float(x[i])*gain+bias)-float(y[i+lag])
        squared += e*e
    ref_rms = math.sqrt(syy/n)
    error_rms = math.sqrt(squared/n)
    return lag,corr,n,gain,bias,error_rms,(error_rms/ref_rms if ref_rms else float("nan"))


def synthetic_test():
    x = array.array("f", [
        0.2*math.sin(i*0.061) + 0.07*math.sin(i*0.0193) +
        (0.3 if i%1109 < 5 else 0.0)
        for i in range(16000)])
    # Exact-grid, non-grid, positive and negative offsets; also
    # reversed polarity. These catch the old -3685 false peak.
    for shift, scale in ((384, 0.125), (385, 0.125),
                         (471, 0.125), (-423, -0.125)):
        if shift >= 0:
            y = array.array("f", [0.0]*shift +
                            [float(v)*scale for v in x] + [0.0]*128)
        else:
            y = array.array("f",
                            [float(v)*scale for v in x[-shift:]] +
                            [0.0]*128)
        lag,corr,n,gain,_,err,_ = compare(x,y)
        assert lag == shift, (lag,shift)
        expected_sign = 1.0 if scale > 0 else -1.0
        assert abs(corr-expected_sign)<1e-6 and n>10000, (corr,n)
        assert abs(gain-scale)<1e-6 and err<1e-6, (gain,err)
        print(f"SBR-54b: synthetic shift={shift}, gain={scale} PASS")
    print("SBR-54b: pure-Python sample lag/scale comparison self-test PASS")


def main():
    if len(sys.argv)==2 and sys.argv[1]=="--self-test":
        synthetic_test()
        return 0
    if len(sys.argv)!=3:
        print("Usage: python3 tools/sauti_he_aac_compare_pcm.py "
              "Sauti-preview.f32le FFmpeg-reference.f32le",file=sys.stderr)
        return 2
    try:
        x,y = load(sys.argv[1]),load(sys.argv[2])
        a,b = rms_peak(x),rms_peak(y)
        print(f"PREVIEW: {len(x)} samples, RMS={a[0]:.9g}, peak={a[1]:.9g}")
        print(f"REFERENCE: {len(y)} samples, RMS={b[0]:.9g}, peak={b[1]:.9g}")
        if a[1]>1.0:
            print("WARNING: preview exceeds normalized f32 PCM [-1,1]; "
                  "do NOT normalize/clamp before diagnosis")
        lag,corr,n,gain,bias,error,relative=compare(x,y)
        print(f"Best alignment: lag={lag} samples ({lag/48:.3f} ms at 48 kHz); "
              f"overlap={n} samples")
        print(f"Correlation: {corr:.6f}; optimal signed scale={gain:.8g}; "
              f"DC offset={bias:.8g}")
        print(f"After best scale/offset: error RMS={error:.8g}; "
              f"relative error={relative:.3%}")
        if abs(corr)<0.85:
            print("WARNING: weak waveform correlation even after alignment; "
                  "do NOT interpret the scale as a decoder fix")
        print("Developer comparison only; normative HE-AAC delay, "
              "harmonic/sine, chirp and output equivalence NOT verified.")
        return 0
    except (OSError,ValueError) as e:
        print(f"SBR-54b comparison error: {e}",file=sys.stderr)
        return 2


if __name__=="__main__":
    raise SystemExit(main())
