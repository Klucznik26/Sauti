#!/usr/bin/env python3
"""SBR-71: phase0 / phase+2 per-frame complex Fourier-line audit.

The two preselected PCM frequencies are nominal QMF-band centers (not
complex-QMF captures). The reference is FFmpeg PCM minus Sauti LOW and also
contains reference noise and LOW reconstruction error. Never use this audit
as HE-AAC conformance evidence or to choose a production oscillator sign.
"""
import argparse
import math
import statistics

FRAME = 2048
RATE = 48000
TARGETS = ((18, 296), (42, 680))  # 6937.5 Hz, 15937.5 Hz
ENERGY_FLOOR_FRACTION = 1e-4  # per-tone reference power; excludes weak frames


def _projection(pcm, start, cosines, sines):
    return complex(math.fsum(float(pcm[start+i])*cosines[i]
                             for i in range(FRAME)),
                   -math.fsum(float(pcm[start+i])*sines[i]
                              for i in range(FRAME)))


def analyze(low, reference, baseline, phase0, phase2, lag, gain, bias,
            first_frame=2, last_frame=48):
    if (not low or len(low) % FRAME or
        any(len(x) != len(low) for x in (baseline, phase0, phase2))):
        raise ValueError('all Sauti streams must have identical whole-frame length')
    if not isinstance(lag, int) or not (
            math.isfinite(gain) and math.isfinite(bias)):
        raise ValueError('invalid fixed LOW anchor')
    if first_frame < 0 or last_frame < first_frame or last_frame >= len(low)//FRAME:
        raise ValueError('invalid frame bounds')
    if not all(math.isfinite(float(v))
               for a in (low, reference, baseline, phase0, phase2)
               for v in a):
        raise ValueError('NaN/Inf samples')
    if (math.fsum((float(a)-float(b))**2
                  for a,b in zip(phase0, phase2)) <= 1e-22):
        raise ValueError('no measurable independent phase A/B difference')
    if first_frame*FRAME + lag < 0 or (last_frame+1)*FRAME+lag > len(reference):
        raise ValueError('reference does not cover selected shifted whole frames')

    window = [0.5-0.5*math.cos(2*math.pi*i/(FRAME-1))
              for i in range(FRAME)]
    by_tone = {}
    for qmf, k in TARGETS:
        omega = 2*math.pi*k/FRAME
        cosines = [window[i]*math.cos(omega*i) for i in range(FRAME)]
        sines = [window[i]*math.sin(omega*i) for i in range(FRAME)]
        records = []
        for frame in range(first_frame, last_frame+1):
            start = frame*FRAME
            low_pcm = [float(low[start+i]) for i in range(FRAME)]
            residual = [float(reference[start+lag+i])-gain*low_pcm[i]-bias
                        for i in range(FRAME)]
            a = [gain*(float(phase0[start+i])-low_pcm[i])
                 for i in range(FRAME)]
            b = [gain*(float(phase2[start+i])-low_pcm[i])
                 for i in range(FRAME)]
            r = _projection(residual, 0, cosines, sines)
            p0 = _projection(a, 0, cosines, sines)
            p2 = _projection(b, 0, cosines, sines)
            records.append(dict(frame=frame, reference=r, phase0=p0, phase2=p2))
        max_ref_power = max(abs(row['reference'])**2 for row in records)
        if max_ref_power <= 1e-24:
            raise ValueError(f'k={qmf}: no measurable reference tone at all')
        for row in records:
            r, a, b = row['reference'], row['phase0'], row['phase2']
            row['measurable'] = abs(r)**2 >= max_ref_power * ENERGY_FLOOR_FRACTION
            if not row['measurable']:
                continue
            row['error0'] = abs(a-r)/abs(r)
            row['error2'] = abs(b-r)/abs(r)
            row['amp0'] = abs(a)/abs(r)
            row['amp2'] = abs(b)/abs(r)
            row['angle0'] = ((a*r.conjugate()).real/(abs(a)*abs(r))) if abs(a)>0 else None
            row['angle2'] = ((b*r.conjugate()).real/(abs(b)*abs(r))) if abs(b)>0 else None
            row['delta_error'] = row['error0'] - row['error2']
        by_tone[qmf] = records
    return by_tone


def statistics_for(rows, start=3, stop=46):
    selected = [r for r in rows if start <= r['frame'] <= stop]
    valid = [r for r in selected if r['measurable']]
    if not valid:
        return dict(total=len(selected), usable=0, weak=len(selected),
                    improved=0, worsened=0, tied=0)
    improved = sum(r['delta_error'] > 1e-7 for r in valid)
    worsened = sum(r['delta_error'] < -1e-7 for r in valid)
    tied = len(valid) - improved - worsened
    def median(key):
        values = [r[key] for r in valid if r[key] is not None]
        return statistics.median(values) if values else None
    return dict(total=len(selected), usable=len(valid), weak=len(selected)-len(valid),
                improved=improved, worsened=worsened, tied=tied,
                median_error0=median('error0'), median_error2=median('error2'),
                median_amplitude2=median('amp2'), median_angle2=median('angle2'))


def self_test():
    frames = 9
    n = frames*FRAME
    low = [0.2*math.sin(2*math.pi*750*i/RATE) for i in range(n)]
    tone18 = [0.007*math.sin(2*math.pi*6937.5*i/RATE) for i in range(n)]
    tone42 = [0.004*math.cos(2*math.pi*15937.5*i/RATE) for i in range(n)]
    # One intentionally unmeasurable frame at k42.
    for i in range(4*FRAME, 5*FRAME):
        tone42[i] = 0.0
    tones = [x+y for x,y in zip(tone18,tone42)]
    baseline=low[:]
    phase0 = [x-0.65*t for x,t in zip(low, tones)]
    phase2 = [x+0.65*t for x,t in zip(low, tones)]
    lag,gain,bias=384,1.25,0.003
    reference = [0.0]*lag + [gain*(l+t)+bias for l,t in zip(low,tones)] + [0.0]*FRAME
    result=analyze(low,reference,baseline,phase0,phase2,lag,gain,bias,2,8)
    a=statistics_for(result[18],3,7)
    b=statistics_for(result[42],3,7)
    assert a['total']==5 and a['usable']==5 and a['improved']==5
    assert b['total']==5 and b['usable']==4 and b['weak']==1 and b['improved']==4
    assert abs(a['median_error2']-0.35)<1e-8
    assert abs(b['median_error2']-0.35)<1e-8
    assert a['median_angle2']>0.999999 and b['median_angle2']>0.999999
    assert result[42][4-2]['measurable'] is False
    for bad in ((low[:-1],reference,baseline,phase0,phase2),
                (low,reference,baseline,phase0,phase0),
                (low,reference[:3000],baseline,phase0,phase2)):
        try:
            analyze(*bad,lag,gain,bias,2,8)
        except ValueError:
            pass
        else:
            raise AssertionError('malformed stream accepted')
    corrupt=phase2[:]
    corrupt[33]=math.nan
    try:
        analyze(low,reference,baseline,phase0,corrupt,lag,gain,bias,2,8)
    except ValueError:
        pass
    else:
        raise AssertionError('NaN accepted')
    print('PASS SBR-71: frame-resolved exact tones, weak reference, phase A/B and rollback guards')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--self-test',action='store_true')
    for name in ('low','reference','baseline','phase0','phase2'):
        parser.add_argument('--'+name)
    parser.add_argument('--first-frame',type=int,default=2)
    parser.add_argument('--last-frame',type=int,default=48)
    parser.add_argument('--all-frames',action='store_true')
    args=parser.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not all((args.low,args.reference,args.baseline,args.phase0,args.phase2)):
        parser.error('require --low --reference --baseline --phase0 --phase2')
    from sauti_he_aac_compare_pcm import compare,load
    try:
        low,ref,baseline,phase0,phase2=(load(path) for path in
            (args.low,args.reference,args.baseline,args.phase0,args.phase2))
        lag,corr,_,gain,bias,_,_=compare(low,ref)
        if not math.isfinite(corr) or corr<0.98 or not math.isfinite(gain):
            raise ValueError('LOW reference alignment is weak')
        rows=analyze(low,ref,baseline,phase0,phase2,lag,gain,bias,
                     args.first_frame,args.last_frame)
    except (OSError,ValueError) as e:
        parser.exit(2,f'SBR-71 refused: {e}\n')
    print(f'SBR-71 NON-NORMATIVE per-frame PCM audit: LOW lag={lag} '
          f'corr={corr:.8f} gain={gain:.9g}; frames={args.first_frame}..{args.last_frame}')
    for k,records in rows.items():
        print(f'QMF-center k={k} frequency={(k+0.5)*375:.1f}Hz:')
        stable=statistics_for(records,3,46)
        print(f'  STEADY 3..46: usable={stable["usable"]}/{stable["total"]} '
              f'weak={stable["weak"]} improve={stable["improved"]} '
              f'worsen={stable["worsened"]} ties={stable["tied"]}')
        if stable['usable']:
            angle = stable['median_angle2']
            angle_text = 'undefined' if angle is None else f'{angle:.6f}'
            print(f'  median error phase0={stable["median_error0"]:.6f} '
                  f'phase2={stable["median_error2"]:.6f}; '
                  f'phase2 amplitude={stable["median_amplitude2"]:.6f} '
                  f'signed angle={angle_text}')
        show=records if args.all_frames else [r for r in records if
                r['frame'] in (2,3,4,44,45,46,47,48) or
                (r['measurable'] and r['delta_error']<0)]
        for record in show:
            if not record['measurable']:
                print(f'  frame={record["frame"]:02d} WEAK_REF (excluded)')
            else:
                angle=record['angle2']
                angle_text='undefined' if angle is None else f'{angle:.5f}'
                print(f'  frame={record["frame"]:02d} '
                      f'err0={record["error0"]:.5f} err2={record["error2"]:.5f} '
                      f'improvement={record["delta_error"]:+.5f} '
                      f'phase2_angle={angle_text} '
                      f'phase2_amp={record["amp2"]:.5f}')
    print('Reference residual is FFmpeg PCM minus Sauti LOW (not an independent HF stem). '
          'No per-tone fitting, no production decoder changes.')
    return 0


if __name__=='__main__':
    raise SystemExit(main())
