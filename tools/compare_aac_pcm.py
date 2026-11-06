#!/usr/bin/env python3
"""Native AAC-LC versus FFmpeg float32 diagnostic, without NumPy.

Evaluation is restricted to uninterrupted successful frame runs. Do NOT use
samples from a run after missing history. Phase-sensitive RMSE/max error
are evaluated at the selected alignment; correlation alone is insufficient.
"""
import argparse
import array
import math
import sys

FRAME = 1024


def load_pcm(path):
    data = array.array("f")
    with open(path, "rb") as stream:
        payload = stream.read()
    if len(payload) % 4:
        raise ValueError("PCM byte count is not divisible by four")
    data.frombytes(payload)
    if sys.byteorder != "little":
        data.byteswap()
    return data


def load_mask(path):
    rows = []
    with open(path, encoding="utf-8") as stream:
        for line in stream:
            fields = line.split()
            if len(fields) != 3:
                raise ValueError("invalid frame mask row")
            index, status = int(fields[0]), int(fields[1])
            if index != len(rows) or status not in (-1, 0, 1):
                raise ValueError("non-contiguous index or invalid status")
            rows.append((status, fields[2]))
    return rows


def uninterrupted_runs(rows):
    runs = []
    start = None
    for index, (status, _) in enumerate(rows):
        if status == 1 and start is None:
            start = index
        if status != 1 and start is not None:
            runs.append((start, index))
            start = None
    if start is not None:
        runs.append((start, len(rows)))
    return runs


def diagnostic(native, reference, rows, lag, stride=1):
    """Compare exact phase and gain, with no fitted amplitude correction."""
    sum_n = sum_r = sum_diff = cross = 0.0
    max_error = 0.0
    count = 0
    for start, stop in uninterrupted_runs(rows):
        # Discard first 1024 samples of each run as overlap warmup, but
        # retain all remaining samples. A skipped frame is never bridged.
        for i in range(start + 1, stop):
            for sample in range(i * FRAME, (i + 1) * FRAME, stride):
                q = sample + lag
                if q < 0 or q >= len(reference):
                    continue
                a, b = float(native[sample]), float(reference[q])
                if not (math.isfinite(a) and math.isfinite(b)):
                    raise ValueError("non-finite PCM")
                delta = a - b
                sum_n += a * a
                sum_r += b * b
                sum_diff += delta * delta
                cross += a * b
                max_error = max(max_error, abs(delta))
                count += 1
    if not count:
        return dict(count=0, corr=float("nan"), ratio=float("nan"),
                    rmse=float("nan"), max_error=float("nan"), ref_rms=float("nan"),
                    nrmse=float("nan"))
    ref_rms = math.sqrt(sum_r / count)
    native_rms = math.sqrt(sum_n / count)
    rmse = math.sqrt(sum_diff / count)
    return dict(
        count=count,
        corr=(cross / math.sqrt(sum_n * sum_r) if sum_n and sum_r else float("nan")),
        ratio=(native_rms / ref_rms if ref_rms else float("nan")),
        rmse=rmse,
        max_error=max_error,
        ref_rms=ref_rms,
        nrmse=(rmse / ref_rms if ref_rms else float("nan")),
    )


def evaluate(native, reference, rows, lag):
    if len(native) != len(rows) * FRAME:
        raise ValueError("native PCM does not match full frame mask")
    if not reference:
        raise ValueError("empty reference PCM")
    runs = uninterrupted_runs(rows)
    decoded = sum(status == 1 for status, _ in rows)
    rejected = [(index, reason) for index, (status, reason)
                in enumerate(rows) if status != 1]
    valid_runs = [(start, stop) for start, stop in runs if stop - start >= 2]
    print("AAC PCM coverage: frames=%d decoded=%d unsupported=%d invalid=%d" %
          (len(rows), decoded, sum(status == -1 for status, _ in rows),
           sum(status == 0 for status, _ in rows)))
    print("AAC PCM continuity: successful_runs=%d comparable_runs=%d "
          "longest=%d frames excluded=%d" %
          (len(runs), len(valid_runs),
           max((end - start for start, end in runs), default=0),
           len(rows) - decoded))
    for index, reason in rejected[:20]:
        print("  rejected frame %d: %s" % (index, reason))
    if len(rejected) > 20:
        print("  ... %d more rejected frames" % (len(rejected) - 20))
    if not valid_runs:
        print("INCONCLUSIVE: no uninterrupted run with at least two decoded frames")
        return 0

    # Sparse lag search is diagnostic only. Perform sample-accurate
    # evaluation at the requested lag; never silently fit gain/offset.
    if lag is None:
        candidates = range(-2048, 2049, 16)
        estimate = max(
            ((shift, diagnostic(native, reference, rows, shift, stride=16))
             for shift in candidates),
            key=lambda item: abs(item[1]["corr"])
            if math.isfinite(item[1]["corr"]) else -1.0,
        )
        best = max(
            ((shift, diagnostic(native, reference, rows, shift, stride=4))
             for shift in range(estimate[0]-16, estimate[0]+17)),
            key=lambda item: abs(item[1]["corr"])
            if math.isfinite(item[1]["corr"]) else -1.0,
        )
        lag = best[0]

    result = diagnostic(native, reference, rows, lag)
    print("AAC PCM alignment: lag=%d samples; observations=%d" %
          (lag, result["count"]))
    print("AAC PCM metrics: correlation=%.9f native/ref_RMS=%.9g "
          "RMSE=%.9g max_abs_error=%.9g reference_RMS=%.9g normalized_RMSE=%.9g" %
          (result["corr"], result["ratio"], result["rmse"],
           result["max_error"], result["ref_rms"], result["nrmse"]))
    enough = result["count"] >= 1024
    finite = all(math.isfinite(result[k]) for k in
                 ("corr", "ratio", "rmse", "max_error", "nrmse"))
    if enough and finite and result["corr"] >= 0.999 and             0.99 <= result["ratio"] <= 1.01 and result["nrmse"] <= 0.03:
        print("CANDIDATE MATCH: correlation, RMS and normalized RMSE pass "
              "(diagnostic, not conformance)")
    else:
        print("NOT VERIFIED: PCM differs or comparison coverage is insufficient")
    return 0


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("native")
    parser.add_argument("reference")
    parser.add_argument("mask")
    parser.add_argument("--lag", type=int, default=None,
                        help="fixed sample alignment; omit to search within ±2048")
    opts = parser.parse_args(argv)
    try:
        return evaluate(load_pcm(opts.native), load_pcm(opts.reference),
                        load_mask(opts.mask), opts.lag)
    except (ValueError, OSError, OverflowError) as exc:
        print("FAIL AAC diagnostic: %s" % exc, file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
