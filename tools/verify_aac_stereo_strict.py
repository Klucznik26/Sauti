#!/usr/bin/env python3
"""AAC-29 strict per-channel PCM and coverage gate.

Unlike the earlier exploratory comparator, returns a nonzero code when
coverage/continuity/quality fail. No gain fitting or sample alignment fitting.
"""
import argparse
import json
import math
import sys

from compare_aac_pcm import FRAME, diagnostic, load_mask, load_pcm, uninterrupted_runs

MIN_CORRELATION = 0.999
RMS_RATIO_MIN = 0.99
RMS_RATIO_MAX = 1.01
MAX_NORMALIZED_RMSE = 0.03


def assess(native, reference, rows):
    if not rows:
        raise ValueError("empty frame mask")
    if len(native) != len(rows) * 2 * FRAME:
        raise ValueError("native PCM length differs from full frame coverage")
    if len(reference) != len(native):
        raise ValueError("reference PCM length differs from native PCM")
    if any(status != 1 for status, _ in rows):
        rejections = [(i, reason) for i, (status, reason) in enumerate(rows)
                      if status != 1]
        return dict(ok=False, frames=len(rows), decoded=len(rows)-len(rejections),
                    reason="rejected frames: " + repr(rejections[:8]))
    if uninterrupted_runs(rows) != [(0, len(rows))] or len(rows) < 3:
        return dict(ok=False, frames=len(rows), decoded=len(rows),
                    reason="insufficient uninterrupted decoded frames")
    result = dict(ok=True, frames=len(rows), decoded=len(rows), channels={})
    for ch, label in enumerate(("LEFT", "RIGHT")):
        n = native[ch::2]
        r = reference[ch::2]
        score = diagnostic(n, r, rows, lag=0)
        finite = all(math.isfinite(score[k]) for k in (
            "corr", "ratio", "rmse", "nrmse", "max_error"))
        passed = (finite and score["count"] >= FRAME and
                  score["corr"] >= MIN_CORRELATION and
                  RMS_RATIO_MIN <= score["ratio"] <= RMS_RATIO_MAX and
                  score["nrmse"] <= MAX_NORMALIZED_RMSE)
        result["channels"][label] = dict(score, ok=bool(passed))
        result["ok"] = result["ok"] and passed
    return result


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("native")
    ap.add_argument("reference")
    ap.add_argument("mask")
    ap.add_argument("--json", action="store_true")
    args = ap.parse_args(argv)
    try:
        summary = assess(load_pcm(args.native), load_pcm(args.reference),
                         load_mask(args.mask))
    except (OSError, ValueError, OverflowError) as error:
        print("FAIL AAC-29: " + str(error), file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps(summary, sort_keys=True))
    else:
        print("AAC-29 coverage: %d/%d frames" %
              (summary["decoded"], summary["frames"]))
        for name, data in summary.get("channels", {}).items():
            print("%s: %s corr=%.9f RMS=%.7f nRMSE=%.6f max_error=%.7g" %
                  (name, "PASS" if data["ok"] else "FAIL", data["corr"],
                   data["ratio"], data["nrmse"], data["max_error"]))
        if "reason" in summary:
            print("FAIL: " + summary["reason"])
        print("AAC-29 verdict:", "PASS" if summary["ok"] else "FAIL")
    return 0 if summary["ok"] else 1


if __name__ == "__main__":
    sys.exit(main())
