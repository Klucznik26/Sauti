#!/usr/bin/env python3
"""Compare two mono PCM WAV renders (e.g. Sauti and ST-Sound).

Only Python standard library required. Both inputs must be uncompressed
PCM16, mono, and have identical sample rates. Reports an unaligned comparison
and a lag-aligned comparison; correlation and RMS never imply hardware
accuracy by themselves.
"""
import argparse
import math
import struct
import sys
import wave


def read_pcm(path):
    with wave.open(str(path), "rb") as wav:
        if wav.getcomptype() != "NONE" or wav.getnchannels() != 1 or wav.getsampwidth() != 2:
            raise ValueError(f"{path}: expected mono 16-bit PCM WAV")
        rate = wav.getframerate()
        raw = wav.readframes(wav.getnframes())
    if len(raw) % 2:
        raise ValueError(f"{path}: odd PCM length")
    return rate, [x[0] / 32768.0 for x in struct.iter_unpack("<h", raw)]


def metrics(a, b):
    n = min(len(a), len(b))
    if not n:
        raise ValueError("no overlapping samples")
    a, b = a[:n], b[:n]
    sa = sum(x * x for x in a)
    sb = sum(x * x for x in b)
    cross = sum(x * y for x, y in zip(a, b))
    delta2 = max(0.0, sa + sb - 2.0 * cross)
    rms = math.sqrt(delta2 / n)
    corr = cross / math.sqrt(sa * sb) if sa and sb else float("nan")
    peak = max(abs(x-y) for x, y in zip(a, b))
    return n, rms, peak, corr


def gain_fit_metrics(a, b):
    """Least-squares candidate gain and residual, after sample alignment.

    Fit a scalar only (no DC offset). Keep the reference untouched and avoid
    disguising phase or waveform errors as a gain difference.
    """
    n = min(len(a), len(b))
    if not n:
        raise ValueError("no overlapping samples")
    aa, bb = a[:n], b[:n]
    power = sum(x*x for x in aa)
    if not power:
        return float("nan"), float("nan")
    gain = sum(x*y for x, y in zip(aa, bb)) / power
    residual = math.sqrt(sum((gain*x-y)**2 for x, y in zip(aa, bb)) / n)
    return gain, residual



def window_diagnostics(a, b, rate, window_ms=100):
    """Window-local PCM shape/gain errors after one global lag alignment.

    Report fixed contiguous windows; do not independently shift or fit the
    signals before computing correlation. That would conceal timing errors.
    """
    if not math.isfinite(window_ms) or window_ms <= 0 or rate <= 0:
        raise ValueError("window_ms and sample rate must be positive")
    size = max(1, round(rate * window_ms / 1000))
    length = min(len(a), len(b))
    result = []
    for start in range(0, length, size):
        end = min(start + size, length)
        ca, cb = a[start:end], b[start:end]
        _, rms, _, corr = metrics(ca, cb)
        gain, residual = gain_fit_metrics(ca, cb)
        result.append((start / rate, end / rate, corr, rms, gain, residual))
    return result


def best_lag(a, b, max_lag, window):
    # Offset meaning: positive => reference b starts later than candidate a.
    # Evaluate a fixed-width window of overlapping PCM after an equal guard.
    guard = max_lag
    length = min(window, len(a) - 2 * guard, len(b) - 2 * guard)
    if length <= 0:
        raise ValueError("input too short for requested alignment window")
    anchor_a = a[guard:guard + length]
    energy_a = sum(x*x for x in anchor_a)
    winner = (float("-inf"), 0)
    for lag in range(-max_lag, max_lag + 1):
        segment = b[guard + lag:guard + lag + length]
        numerator = sum(x*y for x, y in zip(anchor_a, segment))
        energy_b = sum(y*y for y in segment)
        score = numerator / math.sqrt(energy_a * energy_b) if energy_a and energy_b else float("-inf")
        if score > winner[0]:
            winner = score, lag
    return winner[1], winner[0]



def envelope_rms(pcm, rate, step_ms=10):
    """Short-time RMS describes loudness evolution, not chip envelope register levels."""
    width = max(1, round(rate * step_ms / 1000))
    return [math.sqrt(sum(x*x for x in pcm[i:i+width]) / len(pcm[i:i+width]))
            for i in range(0, len(pcm), width)]

def stsound_filter(pcm):
    """Offline experimental ST-Sound DC adjuster and integer low-pass filter.

    ST-Sound uses an integer 512-sample moving DC average and a three-tap
    low-pass with integer shifts. Quantize to the reference's 16-bit domain
    for the filtering experiment; clipping is postponed until WAV output.
    """
    history = [0] * 512
    total = 0
    position = 0
    previous2 = previous1 = 0
    result = []
    for value in pcm:
        current = round(value * 32768)
        total += current - history[position]
        history[position] = current
        position = (position + 1) & 511
        # Reference DC adjuster uses integer average; filter uses arithmetic shifts.
        dc = (total // 512) if total >= 0 else -((-total) // 512)
        highpass = current - dc
        lowpass = (previous2 >> 2) + (previous1 >> 1) + (highpass >> 2)
        previous2, previous1 = previous1, highpass
        result.append(max(-32768, min(32767, lowpass)) / 32768.0)
    return result

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sauti", help="Sauti's mono 16-bit PCM WAV")
    parser.add_argument("reference", help="Reference renderer's mono 16-bit PCM WAV")
    parser.add_argument("--max-lag", type=int, default=256, help="alignment search radius in samples (default 256)")
    parser.add_argument("--window", type=int, default=4096, help="alignment analysis window in samples")
    parser.add_argument("--segments", action="store_true", help="report local lag at regular positions")
    parser.add_argument("--segment-step", type=float, default=0.5, help="time between diagnostics in seconds")
    parser.add_argument("--candidate-gain", type=float, default=1.0, help="offline candidate PCM scaling; default 1.0")
    parser.add_argument("--fit-gain", action="store_true", help="report least-squares PCM amplitude gain and residual after alignment")
    parser.add_argument("--stsound-filter", action="store_true", help="apply experimental ST-Sound-style DC and low-pass to candidate")
    parser.add_argument("--local-lag-report", type=float, metavar="MS", help="estimate local PCM lag every MS milliseconds using the fixed alignment window")
    parser.add_argument("--window-report", type=float, metavar="MS", help="report PCM correlation, gain and residual in nonoverlapping windows of MS milliseconds")
    parser.add_argument("--envelope-rms", action="store_true", help="compare 10 ms short-time RMS contours (not actual PSG register levels)")
    parser.add_argument("--transition", type=float, action="append", default=[], help="inspect phase near an exact transition time in seconds; repeatable")
    args = parser.parse_args()
    if args.max_lag < 0 or args.max_lag > 44100 or args.window < 64 or not 0.1 <= args.segment_step <= 30:
        parser.error("max-lag must be 0..44100 and window >= 64")
    rate_a, a = read_pcm(args.sauti)
    rate_b, b = read_pcm(args.reference)
    if rate_a != rate_b:
        raise ValueError(f"sample-rate mismatch: {rate_a} vs {rate_b}; resample externally first")
    if not math.isfinite(args.candidate_gain) or args.candidate_gain < 0:
        parser.error("--candidate-gain must be finite and nonnegative")
    if args.stsound_filter:
        a = stsound_filter(a)
        print("Experimental ST-Sound-style DC/low-pass applied to Sauti candidate")
    if args.candidate_gain != 1.0:
        a = [x * args.candidate_gain for x in a]
        print(f"Offline candidate PCM gain: {args.candidate_gain:.6f} (no production audio change)")
    print(f"PCM16 mono, {rate_a} Hz | samples: Sauti={len(a)}, reference={len(b)}")
    n, rms, peak, corr = metrics(a, b)
    print(f"Raw overlap: {n} | RMS error={rms:.8f}, peak error={peak:.8f}, correlation={corr:.8f}")
    lag, score = best_lag(a, b, args.max_lag, args.window)
    if lag >= 0:
        aligned_a, aligned_b = a, b[lag:]
    else:
        aligned_a, aligned_b = a[-lag:], b
    n, rms, peak, corr = metrics(aligned_a, aligned_b)
    print(f"Aligned lag: {lag:+d} samples ({lag*1000/rate_a:+.3f} ms), window correlation={score:.8f}")
    print(f"Aligned overlap: {n} | RMS error={rms:.8f}, peak error={peak:.8f}, correlation={corr:.8f}")
    if args.fit_gain:
        gain, residual = gain_fit_metrics(aligned_a, aligned_b)
        print(f"Aligned PCM fitted gain={gain:.8f}, residual RMS={residual:.8f} "
              "(diagnostic only; no gain applied to audio)")
    if args.window_report is not None:
        if not math.isfinite(args.window_report) or not 1 <= args.window_report <= 10000:
            parser.error("--window-report must be 1..10000 milliseconds")
        print(f"Local PCM windows ({args.window_report:g} ms, single global lag):")
        for start, end, corr_local, rms_local, gain_local, residual_local in window_diagnostics(
                aligned_a, aligned_b, rate_a, args.window_report):
            print(f"  {start:7.3f}-{end:7.3f}s: corr={corr_local:.6f} "
                  f"RMS={rms_local:.6f} fitted_gain={gain_local:.6f} "
                  f"residual={residual_local:.6f}")
    if args.local_lag_report is not None:
        if not math.isfinite(args.local_lag_report) or not 20 <= args.local_lag_report <= 10000:
            parser.error("--local-lag-report must be 20..10000 milliseconds")
        stride = max(1, round(rate_a * args.local_lag_report / 1000))
        extent = args.window + 2 * args.max_lag
        end_limit = min(len(a), len(b)) - extent
        print(f"Local PCM lag (each {args.local_lag_report:g} ms; "
              f"window={args.window} samples, radius=+/-{args.max_lag}):")
        for start in range(0, end_limit + 1, stride):
            lag_local, corr_local = best_lag(
                a[start:start + extent], b[start:start + extent],
                args.max_lag, args.window)
            print(f"  {start/rate_a:7.3f}s: lag={lag_local:+5d}, corr={corr_local:.6f}")
        print("Note: periodic waveforms permit equivalent lag peaks; "
              "local lag is a diagnostic, not proof of oscillator drift.")
    if args.envelope_rms:
        # Apply whole-track PCM alignment before measuring short-time RMS.
        contours_a = envelope_rms(aligned_a[:n], rate_a)
        contours_b = envelope_rms(aligned_b[:n], rate_a)
        count, rms_env, peak_env, corr_env = metrics(contours_a, contours_b)
        print(f"10ms RMS contour: bins={count}, RMS difference={rms_env:.8f}, "
              f"peak difference={peak_env:.8f}, correlation={corr_env:.8f}")
        # Positive least-squares amplitude gain, in addition to similarity.
        # A close shape correlation does not mean matching absolute loudness.
        energy = sum(x*x for x in contours_a)
        gain_fit = sum(x*y for x, y in zip(contours_a, contours_b)) / energy if energy else float("nan")
        scaled_rms = (math.sqrt(sum((gain_fit*x-y)**2 for x, y in zip(contours_a, contours_b))/count)
                      if math.isfinite(gain_fit) else float("nan"))
        print(f"  Reference/Sauti fitted RMS gain={gain_fit:.6f}, residual RMS={scaled_rms:.8f}")
        for second in range(0, min(3, n // rate_a)):
            j = round(second * rate_a / max(1, round(rate_a * 0.01)))
            if j < count:
                print(f"  {second}s: Sauti RMS={contours_a[j]:.6f}, reference RMS={contours_b[j]:.6f}")
    if args.segments:
        print("Local alignment across track (max correlation; not unambiguous clock drift):")
        extent = args.window + 2 * args.max_lag
        max_start = min(len(a), len(b)) - extent
        step_samples = max(1, round(args.segment_step * rate_a))
        for start in range(0, max_start + 1, step_samples):
            local_lag, local_corr = best_lag(
                a[start:start + extent], b[start:start + extent], args.max_lag, args.window
            )
            print(f"  {start/rate_a:7.3f}s: lag={local_lag:+5d}, corr={local_corr:.6f}")
    for second in args.transition:
        if second < 0 or not math.isfinite(second):
            parser.error("transition time must be finite and nonnegative")
        print(f"Transition at {second:.3f}s (short windows; periodic lags are ambiguous):")
        extent = args.window + 2 * args.max_lag
        for delta in (-0.20, -0.10, 0.02, 0.10, 0.20):
            start = round((second + delta) * rate_a)
            if start < 0 or start + extent > min(len(a), len(b)):
                continue
            loclag, loccorr = best_lag(
                a[start:start + extent], b[start:start + extent], args.max_lag, args.window
            )
            print(f"  {second+delta:7.3f}s: lag={loclag:+5d}, corr={loccorr:.6f}")
    print("Caution: different gains, chip clocks, mixer curves or resamplers change these scores.")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, EOFError, wave.Error) as error:
        print(f"PCM comparison failed: {error}", file=sys.stderr)
        sys.exit(1)
