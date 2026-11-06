#!/usr/bin/env python3
"""SBR-72: distinguish a phase-only HF oscillator change from a HF time offset.

Two-QMF-slot (=128 PCM samples) time shifts also invert sinusoids at the
nominal 64-QMF band centers (k+1/2)*375 Hz. Try predeclared +/-128 and
+/-384 sample HF-only shifts WITHOUT refitting LOW alignment or gain.
Test non-harmonic control bands too. A shifted HF diagnostic is NOT a
valid decoded waveform and MUST NOT be used for production HE-AAC.
"""
import argparse
import math

from sauti_he_aac_compare_pcm import compare, load
from sauti_sbr66_tonal_ab_audit import FRAME, RATE, evaluate
from sauti_sbr70_qmf_phase_ab import check_paths_and_streams

SLOT = 64  # full-rate 64-QMF synthesis emits 64 real PCM samples/slot
SHIFTS = (-384, -128, 128, 384)
CONTROL_BANDS = ((8625, 9000), (12000, 15000))
TARGET_HZ = (6937.5, 15937.5)


def oscillator_lag_hypothesis(lag):
    if not isinstance(lag, int):
        raise ValueError("LOW alignment lag must be integer samples")
    if lag % SLOT:
        return None
    slots = lag // SLOT
    phase_mod4 = slots % 4
    # If applying a time shift to a nominal QMF-band-center pure tone:
    # cycles = (k + 0.5) * 375 Hz * lag / 48,000 Hz.
    predicted = [complex(math.cos(2 * math.pi * f * lag / RATE),
                         math.sin(2 * math.pi * f * lag / RATE))
                 for f in TARGET_HZ]
    return {"lag": lag, "slots": slots, "phase_mod4": phase_mod4,
            "nominal_tone_rotation": predicted}


def shift_hf_only(low, phase0, samples, first_frame, last_frame):
    if (not isinstance(samples, int) or samples not in SHIFTS or
        not low or len(low) != len(phase0) or len(low) % FRAME):
        raise ValueError("invalid HF shift or stream geometry")
    if (first_frame < 0 or last_frame < first_frame or
        (last_frame + 1) * FRAME > len(low)):
        raise ValueError("bad selected frames")
    if (first_frame * FRAME + samples < 0 or
        (last_frame + 1) * FRAME + samples > len(low)):
        raise ValueError("HF time-shifted window exceeds recording")
    if not all(math.isfinite(float(v)) for signal in (low, phase0) for v in signal):
        raise ValueError("nonfinite PCM")
    # Outside requested measurement range, copy phase0 unchanged, not
    # zero-padding. Only the selected frames are analyzed.
    preview = [float(v) for v in phase0]
    for i in range(first_frame * FRAME, (last_frame + 1) * FRAME):
        src = i + samples
        preview[i] = float(low[i]) + float(phase0[src]) - float(low[src])
    if not all(math.isfinite(v) for v in preview):
        raise ValueError("time-shifted preview overflow")
    return preview


def analyze(low, ref, baseline, phase0, phase2, lag, gain, bias,
            first_frame=3, last_frame=46):
    check_paths_and_streams(low, baseline, phase0, phase2)
    if not math.isfinite(gain) or not math.isfinite(bias):
        raise ValueError("nonfinite LOW anchor")
    variants = {"baseline": baseline, "phase0": phase0, "phase2": phase2}
    for shift in SHIFTS:
        variants[f"HF_shift_{shift:+d}"] = shift_hf_only(
            low, phase0, shift, first_frame, last_frame)
    return evaluate(low, ref, variants, lag, gain, bias,
                    first_frame, last_frame)["variants"]


def self_test():
    assert oscillator_lag_hypothesis(384)["phase_mod4"] == 2
    assert oscillator_lag_hypothesis(-384)["phase_mod4"] == 2
    assert oscillator_lag_hypothesis(256)["phase_mod4"] == 0
    assert oscillator_lag_hypothesis(385) is None
    for z in oscillator_lag_hypothesis(384)["nominal_tone_rotation"]:
        assert abs(z.real + 1) < 1e-12 and abs(z.imag) < 1e-12

    n = 9 * FRAME
    low = [0.2 * math.sin(2*math.pi*950*i/RATE) for i in range(n)]
    # Synthetic tones match the previously observed two HF centers.
    tones = [0.012 * math.sin(2*math.pi*6937.5*i/RATE) +
             0.009 * math.cos(2*math.pi*15937.5*i/RATE)
             for i in range(n)]
    # Independent, deliberately non-QMF-center signal checks broad-band
    # damage from time-shifting the entire HF rather than only a tone.
    control = [0.008 * math.sin(2*math.pi*8789*i/RATE) +
               0.007 * math.cos(2*math.pi*12800*i/RATE)
               for i in range(n)]
    baseline = [l+c for l,c in zip(low,control)]
    phase0 = [l+c-t for l,c,t in zip(low,control,tones)]
    phase2 = [l+c+t for l,c,t in zip(low,control,tones)]
    lag,gain,bias = 384,1.25,0.002
    ref = [0.0]*lag + [
        gain*(l+c+t)+bias for l,c,t in zip(low,control,tones)
    ] + [0.0]*FRAME
    result = analyze(low,ref,baseline,phase0,phase2,lag,gain,bias,2,7)
    for j in (0,1):
        p0=result["phase0"]["peaks"][j]
        p2=result["phase2"]["peaks"][j]
        assert p0["signed"] < -0.9999
        assert p2["signed"] > 0.9999 and p2["relative_error"] < 1e-8
    # A whole-HF time shift can rotate the oscillator yet changes the
    # otherwise correct nonharmonic control tones.
    for key in ("HF_shift_+128","HF_shift_-128"):
        for band_idx in (1,2):
            assert result[key]["bands"][band_idx]["relative_error"] > (
                result["phase2"]["bands"][band_idx]["relative_error"]+0.1)
    for invalid in (-5000, 1):
        try:
            shift_hf_only(low, phase0, invalid, 2, 7)
        except ValueError:
            pass
        else:
            raise AssertionError("unsupported lag accepted")
    try:
        shift_hf_only(low,phase0,384,2,8)
    except ValueError:
        pass
    else:
        raise AssertionError("shifted window out of bounds accepted")
    bad=phase0[:]
    bad[0]=float("nan")
    try:
        shift_hf_only(low,bad,128,2,7)
    except ValueError:
        pass
    else:
        raise AssertionError("NaN accepted")
    print("PASS SBR-72: 6-slot/4-phase hypothesis, tone-vs-timing controls, invalid inputs")


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--self-test",action="store_true")
    for key in ("low","reference","baseline","phase0","phase2"):
        p.add_argument("--"+key)
    p.add_argument("--first-frame",type=int,default=3)
    p.add_argument("--last-frame",type=int,default=46)
    args=p.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not all((args.low,args.reference,args.baseline,args.phase0,args.phase2)):
        p.error("require --low --reference --baseline --phase0 --phase2")
    try:
        low,ref,baseline,phase0,phase2=(load(path) for path in (
            args.low,args.reference,args.baseline,args.phase0,args.phase2))
        lag,corr,_,gain,bias,_,_=compare(low,ref)
        if not math.isfinite(corr) or corr<0.98:
            raise ValueError("LOW reference anchor below 0.98")
        result=analyze(low,ref,baseline,phase0,phase2,lag,gain,bias,
                       args.first_frame,args.last_frame)
    except (OSError,ValueError) as exc:
        p.exit(2,f"SBR-72 refused: {exc}\n")
    hypothesis=oscillator_lag_hypothesis(lag)
    print(f"SBR-72 NONNORMATIVE HF timing control: LOW lag={lag}, "
          f"corr={corr:.8f}, gain={gain:.9g}, fixed over all cases")
    if hypothesis is None:
        print("LOW lag not integral 64-QMF slots; no 4-phase timing hypothesis")
    else:
        print(f"LOW alignment offset={hypothesis['slots']} output QMF slots, "
              f"offset mod 4={hypothesis['phase_mod4']}")
        for f,z in zip(TARGET_HZ,hypothesis["nominal_tone_rotation"]):
            print(f"  Pure {f:.1f} Hz sinusoid under {lag}-sample time "
                  f"shift rotates by (Re={z.real:+.6f}, Im={z.imag:+.6f})")
    print("PREDECLARED HF-only time shifts in PCM samples: -384,-128,+128,+384.")
    print("Columns: nominal center error k18 / k42; "
          "working control-band relative errors 8625-9000 / 12000-15000 Hz")
    for name,metrics in result.items():
        errors=[r["relative_error"] for r in metrics["peaks"]]
        controls=[r["relative_error"] for r in metrics["bands"][1:3]]
        if any(x is None for x in errors+controls):
            print(name+": INCONCLUSIVE (missing spectral power)")
            continue
        print(f"{name:20s}  tones={errors[0]:.6f}/{errors[1]:.6f}"
              f"  controls={controls[0]:.6f}/{controls[1]:.6f}")
    print("A PCM HF shift is only a diagnostic counterexample: unlike a QMF "
          "oscillator phase change it also shifts all nonharmonic HF. "
          "Source timing and normative FFmpeg QMF phase remain unverified.")
    return 0


if __name__=="__main__":
    raise SystemExit(main())
