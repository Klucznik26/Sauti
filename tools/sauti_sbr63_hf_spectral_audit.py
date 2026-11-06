#!/usr/bin/env python3
"""SBR-63: frequency-resolved HF residual audit, with a fixed LOW anchor.

Diagnostic only. FFmpeg PCM minus Sauti LOW is NOT an isolated reference
HF stem: it includes noise and any LOW modeling error. No HF phase/gain
fitting, production decoder change, or synthesis change is performed.
"""
import argparse
import math
from sauti_he_aac_compare_pcm import compare, load
from sauti_sbr62_hf_stem_audit import score

FRAME = 2048
RATE = 48000
BAND_EDGES_HZ = (0, 3000, 6000, 9000, 12000, 15000, 18000, 21000, 24000)


def fft(signal):
    """Forward radix-2 FFT, standard library only."""
    n = len(signal)
    if n < 2 or n & (n - 1):
        raise ValueError('FFT requires power-of-two length')
    output = [complex(v) for v in signal]
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j ^= bit
        if i < j:
            output[i], output[j] = output[j], output[i]
    step = 2
    while step <= n:
        angle = -2.0 * math.pi / step
        unit = complex(math.cos(angle), math.sin(angle))
        half = step // 2
        for a in range(0, n, step):
            w = 1.0 + 0.0j
            for k in range(half):
                u = output[a + k]
                v = w * output[a + k + half]
                output[a + k] = u + v
                output[a + k + half] = u - v
                w *= unit
        step *= 2
    return output


def frequency_audit(low, hf, ref, lag, gain, bias, first_frame=3, last_frame=46):
    if len(low) != len(hf) or not low or len(low) % FRAME:
        raise ValueError('bad component geometry')
    if first_frame < 0 or last_frame < first_frame or last_frame >= len(low) // FRAME:
        raise ValueError('invalid frame interval')
    if not isinstance(lag, int) or not (math.isfinite(gain) and math.isfinite(bias)):
        raise ValueError('invalid LOW anchor')
    if not all(math.isfinite(float(v)) for signal in (low, hf, ref) for v in signal):
        raise ValueError('non-finite PCM input')
    window = [0.5 - 0.5 * math.cos(2.0 * math.pi * i / (FRAME - 1))
              for i in range(FRAME)]
    accum = [dict(ref=0.0, hf=0.0, diff=0.0, cross=0.0j)
             for _ in range(len(BAND_EDGES_HZ) - 1)]
    seen = 0
    for frame in range(first_frame, last_frame + 1):
        start = frame * FRAME
        if start + lag < 0 or start + FRAME + lag > len(ref):
            raise ValueError('incomplete shifted reference frame')
        reference_segment = [
            (float(ref[start + lag + i]) - gain * float(low[start + i]) - bias) * window[i]
            for i in range(FRAME)]
        candidate_segment = [gain * float(hf[start + i]) * window[i]
                             for i in range(FRAME)]
        rfft = fft(reference_segment)
        hfft = fft(candidate_segment)
        seen += 1
        for k in range(FRAME // 2 + 1):
            hz = k * RATE / FRAME
            ix = min(len(accum) - 1, int(hz // 3000))
            r, h = rfft[k], hfft[k]
            row = accum[ix]
            row['ref'] += r.real * r.real + r.imag * r.imag
            row['hf'] += h.real * h.real + h.imag * h.imag
            delta = h - r
            row['diff'] += delta.real * delta.real + delta.imag * delta.imag
            row['cross'] += h * r.conjugate()
    total_ref = sum(row['ref'] for row in accum)
    if not seen or total_ref <= 0:
        raise ValueError('silent or unmeasurable reference residual')
    results = []
    for i, row in enumerate(accum):
        power_ref, power_hf = row['ref'], row['hf']
        measurable = power_ref > total_ref * 1e-5 and power_hf > total_ref * 1e-9
        denom = math.sqrt(power_ref * power_hf) if power_ref and power_hf else 0.0
        cross = row['cross']
        results.append(dict(
            start=BAND_EDGES_HZ[i], end=BAND_EDGES_HZ[i + 1],
            fraction_ref=power_ref / total_ref,
            amplitude_ratio=math.sqrt(power_hf / power_ref) if power_ref else None,
            signed_coherence=cross.real / denom if denom else None,
            complex_coherence=abs(cross) / denom if denom else None,
            phase_degrees=math.degrees(math.atan2(cross.imag, cross.real)) if denom else None,
            relative_error=math.sqrt(row['diff'] / power_ref) if power_ref else None,
            measurable=measurable))
    return dict(rows=results, frames=seen)


def self_test():
    count = 10 * FRAME
    low = [0.2 * math.sin(2 * math.pi * 850 * i / RATE) +
           0.1 * math.sin(2 * math.pi * 1500 * i / RATE)
           for i in range(count)]
    a = [0.01 * math.sin(2 * math.pi * 8200 * i / RATE) for i in range(count)]
    b = [0.005 * math.sin(2 * math.pi * 15400 * i / RATE) for i in range(count)]
    hf = [x + y for x, y in zip(a, b)]
    lag, gain, bias = 384, 1.3, 0.002
    ref = [0.0] * lag + [gain * (l + h) + bias for l, h in zip(low, hf)] + [0.0] * FRAME
    good = frequency_audit(low, hf, ref, lag, gain, bias, 2, 8)
    high_a, high_b = good['rows'][2], good['rows'][5]
    assert good['frames'] == 7
    for band in (high_a, high_b):
        assert band['measurable'] and band['signed_coherence'] > 0.999999
        assert band['relative_error'] < 1e-8
    wrong = [x - y for x, y in zip(a, b)]
    bad = frequency_audit(low, wrong, ref, lag, gain, bias, 2, 8)
    assert bad['rows'][2]['signed_coherence'] > 0.999
    assert bad['rows'][5]['signed_coherence'] < -0.999
    assert bad['rows'][5]['relative_error'] > 1.9
    for kwargs in ({'first_frame': 9, 'last_frame': 7},
                   {'first_frame': 0, 'last_frame': 99}):
        try:
            frequency_audit(low, hf, ref, lag, gain, bias, **kwargs)
        except ValueError:
            pass
        else:
            raise AssertionError('bad range accepted')
    print('PASS SBR-63 spectral HF: exact control, inverted high band, invalid frames')


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--self-test', action='store_true')
    p.add_argument('--low')
    p.add_argument('--hf')
    p.add_argument('--noise')
    p.add_argument('--preview')
    p.add_argument('--reference')
    p.add_argument('--first-frame', type=int, default=3)
    p.add_argument('--last-frame', type=int, default=46)
    args = p.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not all((args.low, args.hf, args.noise, args.preview, args.reference)):
        p.error('require --low --hf --noise --preview --reference')
    try:
        low, hf, noise, preview, reference = (
            load(path) for path in (args.low, args.hf, args.noise,
                                   args.preview, args.reference))
        lag, low_corr, _, gain, bias, _, _ = compare(low, reference)
        if not math.isfinite(low_corr) or low_corr < 0.98:
            raise ValueError('LOW reference anchor below 0.98')
        closure = score(low, hf, noise, preview, reference, lag, gain, bias,
                        args.first_frame, args.last_frame)
        result = frequency_audit(low, hf, reference, lag, gain, bias,
                                 args.first_frame, args.last_frame)
    except (OSError, ValueError) as exc:
        p.exit(2, f'SBR-63 refused: {exc}\n')
    print(f'SBR-63 DIAGNOSTIC: frames={result["frames"]}, LOW lag={lag} '
          f'corr={low_corr:.8f}, fixed gain={gain:.8f}, '
          f'closure={closure["closure_rms"]:.8g}')
    print('Frequency Hz | reference energy fraction | HF/ref amplitude | '
          'signed coherence | complex coherence | phase degrees | relative error')
    for row in result['rows']:
        def fmt(x):
            return 'undefined' if x is None else f'{x:.6f}'
        status = '' if row['measurable'] else ' LOW-ENERGY: INCONCLUSIVE'
        print(f'{row["start"]:5d}-{row["end"]:5d}: '
              f'{fmt(row["fraction_ref"])} {fmt(row["amplitude_ratio"])} '
              f'{fmt(row["signed_coherence"])} {fmt(row["complex_coherence"])} '
              f'{fmt(row["phase_degrees"])} {fmt(row["relative_error"])}{status}')
    print('Diagnostics only: reference = FFmpeg PCM minus Sauti LOW; '
          'it also contains reference noise and LOW residual error.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
