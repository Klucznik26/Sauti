#!/usr/bin/env python3
"""SBR-54d: same-time low/HF/noise diagnostic ablation against FFmpeg.

No decoder changes, external Python packages, waveform editing, clipping
or normalization. Fit independent linear weights only to QUANTIFY how
much each stem explains of the reference after aligning by LOW.
The fitted weights are NOT codec constants or recommended gain fixes.
"""
import math
import sys

from sauti_he_aac_compare_pcm import find_lag, load, rms_peak


def _solve(matrix, rhs):
    n = len(rhs)
    a = [list(matrix[i]) + [rhs[i]] for i in range(n)]
    for k in range(n):
        pivot = max(range(k, n), key=lambda i: abs(a[i][k]))
        if abs(a[pivot][k]) < 1e-10:
            raise ValueError("dependent or silent stems; cannot fit independently")
        a[k], a[pivot] = a[pivot], a[k]
        f = a[k][k]
        for j in range(k, n + 1):
            a[k][j] /= f
        for i in range(n):
            if i == k:
                continue
            f = a[i][k]
            for j in range(k, n + 1):
                a[i][j] -= f * a[k][j]
    return [a[i][n] for i in range(n)]


def analyze(stems, reference, lag):
    if len(stems) != 3 or not all(len(s) == len(stems[0]) for s in stems):
        raise ValueError("expected three equal-length Sauti stems")
    start = max(0, -lag)
    stop = min(len(stems[0]), len(reference)-lag)
    n = stop-start
    if n < 2048:
        raise ValueError("insufficient overlapping samples")

    signals = [s[start:stop] for s in stems] + [
        reference[start+lag:stop+lag]]
    means = [sum(map(float, v))/n for v in signals]
    centered = [[float(v)-means[j] for v in signal]
                for j, signal in enumerate(signals)]
    scales = [math.sqrt(sum(v*v for v in signal)/n)
              for signal in centered]
    if any(v < 1e-12 or not math.isfinite(v) for v in scales):
        raise ValueError("a stem or reference is effectively silent")
    standardized = [[v/scales[i] for v in centered[i]]
                    for i in range(4)]
    gram = [[sum(a*b for a,b in zip(standardized[i],
                                    standardized[j]))/n
             for j in range(4)] for i in range(4)]
    results = []
    for count in (1,2,3):
        # Tiny ridge penalization only for numerical stability;
        # the output weights remain diagnostic, not decoder gains.
        matrix = [[gram[i][j]+(1e-9 if i==j else 0.0)
                   for j in range(count)] for i in range(count)]
        sol = _solve(matrix,[gram[i][3] for i in range(count)])
        weights = [sol[i]*scales[3]/scales[i]
                   for i in range(count)]
        bias = means[3]-sum(weights[i]*means[i]
                            for i in range(count))
        total = 0.0
        for t in range(n):
            predicted = bias+sum(weights[i]*float(signals[i][t])
                                 for i in range(count))
            delta=predicted-float(signals[3][t])
            total+=delta*delta
        err = math.sqrt(total/n)
        ref_rms = rms_peak(signals[3])[0]
        relative=err/ref_rms if ref_rms>0 else float("inf")
        results.append((weights,bias,err,relative))
    return n,results


def self_test():
    # Aperiodic impulses anchor an unambiguous, OFF-grid lag.
    x = [0.2*math.sin(i*0.021)+0.11*math.sin(i*0.057)+
         (0.7 if i%1301==0 else 0.0) for i in range(14000)]
    h = [0.4*math.sin(i*0.137)+0.12*math.sin(i*0.225)
         for i in range(14000)]
    q = [((i*37+11)%103-51)/83.0 for i in range(14000)]
    lag = 37
    y = [0.0]*lag + [1.4*x[i]+0.15*h[i]-0.05*q[i]+0.003
                     for i in range(len(x))] + [0.0]*100
    estimated,_ = find_lag(x,y)
    assert estimated==lag, (estimated,lag)
    n,fits=analyze([x,h,q],y,lag)
    assert n==len(x) and fits[0][3]>fits[1][3]>fits[2][3]
    # The fit intentionally uses 1e-9 ridge regularization for
    # conditioning; it leaves ~2.33e-10 RMS residual in this exact
    # synthetic case. An absolute 1e-10 threshold falsely rejects
    # the correct solution. 1e-8 is still many orders smaller than
    # the signal RMS and remains a strict near-perfect-fit check.
    assert fits[2][2]<1e-8, fits[2][2]
    for actual,expected in zip(fits[2][0],(1.4,0.15,-0.05)):
        assert abs(actual-expected)<1e-8,(actual,expected)
    print("SBR-54d: synthetic 3-stem +37 lag, known weights and residual PASS")


def main():
    if len(sys.argv)==2 and sys.argv[1]=="--self-test":
        self_test()
        return 0
    if len(sys.argv) not in (5,6):
        print("Usage: python3 tools/sauti_he_aac_stem_ablation.py "
              "LOW.f32le HF.f32le NOISE.f32le REFERENCE.f32le "
              "[FULL_PREVIEW.f32le]",file=sys.stderr)
        return 2
    try:
        low,hf,noise,ref=[load(p) for p in sys.argv[1:5]]
        if not(len(low)==len(hf)==len(noise)):
            raise ValueError("stems have different sample counts")
        lag,corr=find_lag(low,ref)
        print(f"LOW-based common alignment: lag={lag} samples "
              f"({lag/48:.3f} ms), correlation={corr:.6f}")
        if len(sys.argv)==6:
            full=load(sys.argv[5])
            if len(full)!=len(low):
                raise ValueError("full-preview length differs from stems")
            power=0.0;max_abs=0.0
            for i in range(len(full)):
                d=(float(low[i])+float(hf[i])+float(noise[i]))-float(full[i])
                power+=d*d
                max_abs=max(max_abs,abs(d))
            print(f"Stem sum vs SBR-54a PREVIEW: "
                  f"diff RMS={math.sqrt(power/len(full)):.9g}, "
                  f"max={max_abs:.9g}")
        n,results=analyze([low,hf,noise],ref,lag)
        names=["LOW","LOW+HF","LOW+HF+NOISE"]
        for name,(weights,bias,err,relative) in zip(names,results):
            ws=", ".join(f"{w:.9g}" for w in weights)
            print(f"{name}: coefficients=[{ws}], DC={bias:.8g}, "
                  f"error RMS={err:.9g}, relative={relative:.3%}, "
                  f"overlap={n}")
        for i,name in ((1,"HF after LOW"),(2,"NOISE after LOW+HF")):
            improvement=results[i-1][3]-results[i][3]
            print(f"Incremental improvement {name}: "
                  f"{100*improvement:.4f} percentage points")
        print("CAUTION: best-fit weights are NOT decoder constants. "
              "A weak incremental contribution means the current "
              "component does not explain the aligned reference; "
              "it does not identify one normative bug.")
        return 0
    except (OSError,ValueError) as e:
        print(f"SBR-54d error: {e}",file=sys.stderr)
        return 2


if __name__=="__main__":
    raise SystemExit(main())
