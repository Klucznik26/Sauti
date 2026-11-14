#!/usr/bin/env python3
"""SBR-68: source-blind two-bin projection of experimental harmonic delta.

No comparison with FFmpeg is used to generate A/B variants. The exact bins
(k=296,680 for 2048-point PCM at 48 kHz) were preselected from SBR-65.
This is a PCM Fourier-line diagnostic, NOT a clean SBR harmonic/noise split,
not complex QMF, and never a normative decoder phase correction.
"""
import argparse
import math

FRAME = 2048
RATE = 48000
BINS = (296, 680)  # centers of QMF k=18 and k=42: 6937.5, 15937.5 Hz


def split_lines(baseline, sine):
    if not baseline or len(baseline) != len(sine) or len(baseline) % FRAME:
        raise ValueError('need equal, whole-frame baseline/sine PCM')
    if not all(math.isfinite(float(v)) for a in (baseline, sine) for v in a):
        raise ValueError('nonfinite PCM')
    lookup = []
    for k in BINS:
        if k <= 0 or k >= FRAME // 2:
            raise ValueError('invalid diagnostic FFT line')
        angle = 2.0 * math.pi * k / FRAME
        lookup.append((
            [math.cos(angle * j) for j in range(FRAME)],
            [math.sin(angle * j) for j in range(FRAME)]))
    line = [0.0] * len(baseline)
    full_delta_sq = line_sq = residual_sq = 0.0
    for frame in range(len(baseline) // FRAME):
        start = frame * FRAME
        delta = [float(sine[start + j]) - float(baseline[start + j])
                 for j in range(FRAME)]
        if not all(math.isfinite(x) for x in delta):
            raise ValueError('delta overflow')
        projection = [0.0] * FRAME
        for cosines, sines in lookup:
            a = (2.0 / FRAME) * math.fsum(d * c for d, c in zip(delta, cosines))
            b = (2.0 / FRAME) * math.fsum(d * q for d, q in zip(delta, sines))
            for j in range(FRAME):
                projection[j] += a * cosines[j] + b * sines[j]
        for j, (d, t) in enumerate(zip(delta, projection)):
            line[start + j] = t
            full_delta_sq += d*d
            line_sq += t*t
            residual_sq += (d-t)*(d-t)
    if not all(math.isfinite(v) for v in (full_delta_sq, line_sq, residual_sq)):
        raise ValueError('nonfinite energy')
    if full_delta_sq <= 1e-22:
        raise ValueError('SBR-54h difference contains no measurable signal')
    orthogonality_error = abs(full_delta_sq-line_sq-residual_sq) / full_delta_sq
    if orthogonality_error > 1e-7:
        raise ValueError(f'Fourier line decomposition lost closure: {orthogonality_error}')
    variants = {
        'baseline': [float(x) for x in baseline],
        'sine': [float(x) for x in sine],
        # Keep all other differences (including noise suppression) unchanged.
        'flip_two_lines_keep_remainder': [float(s)-2.0*t for s,t in zip(sine,line)],
        # Remove all non-line modifications. These two lines are not necessarily
        # pure oscillator output: a reference-noise projection can share bins.
        'invert_two_lines_only': [float(b)-t for b,t in zip(baseline,line)],
        'remainder_only': [float(s)-t for s,t in zip(sine,line)],
        'inverse_whole_delta': [2.0*float(b)-float(s) for b,s in zip(baseline,sine)],
    }
    if not all(math.isfinite(x) for a in variants.values() for x in a):
        raise ValueError('A/B candidate overflow')
    return variants, dict(frames=len(baseline)//FRAME,
                          projected_fraction=line_sq/full_delta_sq,
                          remainder_fraction=residual_sq/full_delta_sq,
                          orthogonality_error=orthogonality_error,
                          projection_rms=math.sqrt(line_sq/len(baseline)))


def self_test():
    n = 8*FRAME
    baseline = [0.2*math.sin(2*math.pi*36*j/FRAME) for j in range(n)]
    reference_tone = [0.012*math.sin(2*math.pi*BINS[0]*j/FRAME) +
                      0.007*math.cos(2*math.pi*BINS[1]*j/FRAME)
                      for j in range(n)]
    other = [0.003*math.sin(2*math.pi*448*j/FRAME) for j in range(n)]
    # Deliberately anti-phase tonal difference plus an unrelated change.
    sine = [b - 0.7*t + o for b,t,o in zip(baseline,reference_tone,other)]
    variants, report = split_lines(baseline,sine)
    assert report['frames']==8 and report['orthogonality_error'] < 1e-10
    for j in (0,1,17,1523,FRAME+5,5*FRAME+77):
        inverted = variants['flip_two_lines_keep_remainder'][j]
        isolated = variants['invert_two_lines_only'][j]
        remaining = variants['remainder_only'][j]
        assert abs(inverted-(baseline[j]+0.7*reference_tone[j]+other[j])) < 1e-11
        assert abs(isolated-(baseline[j]+0.7*reference_tone[j])) < 1e-11
        assert abs(remaining-(baseline[j]+other[j])) < 1e-11
    for a,b in ((baseline,sine[:-1]),(baseline[:-1],sine)):
        try:
            split_lines(a,b)
        except ValueError:
            pass
        else:
            raise AssertionError('partial PCM accepted')
    for a,b in ((baseline,baseline),(baseline,[float('nan')]+sine[1:])):
        try:
            split_lines(a,b)
        except ValueError:
            pass
        else:
            raise AssertionError('zero delta / NaN accepted')
    print('PASS SBR-68: exact two-bin isolation, anti-phase controls, closure and invalid PCM')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test',action='store_true')
    parser.add_argument('--low')
    parser.add_argument('--reference')
    parser.add_argument('--baseline')
    parser.add_argument('--sine')
    parser.add_argument('--first-frame',type=int,default=3)
    parser.add_argument('--last-frame',type=int,default=46)
    args=parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not all((args.low,args.reference,args.baseline,args.sine)):
        parser.error('require --low --reference --baseline --sine')
    from sauti_he_aac_compare_pcm import compare, load
    from sauti_sbr66_tonal_ab_audit import evaluate
    try:
        low, ref, baseline, sine = (
            load(p) for p in (args.low,args.reference,args.baseline,args.sine))
        if len(low)!=len(baseline):
            raise ValueError('LOW and baseline geometry mismatch')
        lag,corr,_,gain,bias,_,_=compare(low,ref)
        if not math.isfinite(corr) or corr < 0.98 or not math.isfinite(gain):
            raise ValueError('LOW anchor below 0.98 correlation')
        variants, diagnostic=split_lines(baseline,sine)
        result=evaluate(low,ref,variants,lag,gain,bias,
                        args.first_frame,args.last_frame)
    except (OSError,ValueError) as error:
        parser.exit(2,f'SBR-68 refused: {error}\n')
    print(f'SBR-68 diagnostic frames={result["frames"]}, LOW lag={lag}, '
          f'corr={corr:.8f}, gain={gain:.9g}')
    print(f'Experimental delta: two-line energy fraction={diagnostic["projected_fraction"]:.6f}, '
          f'remainder={diagnostic["remainder_fraction"]:.6f}, '
          f'orthogonality_err={diagnostic["orthogonality_error"]:.3g}')
    for name,row in result['variants'].items():
        print(f'[{name}]')
        for p in row['peaks']:
            def fmt(x):
                return 'undefined' if x is None else f'{x:.6f}'
            print(f'  k={p["band"]} {p["hz"]:.1f}Hz: '
                  f'amplitude={fmt(p["amplitude"])} coherence={fmt(p["signed"])} '
                  f'relative_error={fmt(p["relative_error"])}')
        for band in row['bands']:
            print(f'  {band["start"]}-{band["end"]}Hz: '
                  f'corr={fmt(band["signed"])} relative_error={fmt(band["relative_error"])}')
    print('Non-normative: exact PCM Fourier-bin lines are NOT isolated complex QMF '
          'oscillator output; no reference-based optimization or decoder changes.')
    return 0


if __name__=='__main__':
    raise SystemExit(main())
