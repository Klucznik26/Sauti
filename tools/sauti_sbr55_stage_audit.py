#!/usr/bin/env python3
"""SBR-55: compare stage traces from Sauti and disposable FFmpeg QMF analysis.

Inputs contain consecutive 64-float vectors.  The audit is deliberately
limited to gain-fitted real vectors: it reports the best of identity/reverse
order before the transform and the direct post-MDCT coordinate comparison.
It is evidence for locating a discrepancy, not a decoder correction.
"""
import argparse
import array
import math
import sys
from pathlib import Path


def read_vectors(path):
    raw = Path(path).read_bytes()
    if not raw or len(raw) % (64 * 4):
        raise ValueError(f"{path}: expected nonempty whole 64-float vectors")
    values = array.array("f")
    values.frombytes(raw)
    if sys.byteorder != "little":
        values.byteswap()
    if not all(math.isfinite(value) for value in values):
        raise ValueError(f"{path}: nonfinite float")
    return [list(values[index:index + 64]) for index in range(0, len(values), 64)]


def fit(sauti, ffmpeg):
    if len(sauti) != len(ffmpeg) or not sauti:
        raise ValueError("stage vector count mismatch")
    x = [value for row in sauti for value in row]
    y = [value for row in ffmpeg for value in row]
    energy = math.fsum(value * value for value in y)
    output_energy = math.fsum(value * value for value in x)
    if energy <= 0 or output_energy <= 0:
        raise ValueError("zero stage energy")
    gain = math.fsum(a * b for a, b in zip(x, y)) / energy
    residual = math.sqrt(math.fsum((a - gain * b) ** 2 for a, b in zip(x, y)) / output_energy)
    return gain, residual


def pre_candidates(sauti, ffmpeg):
    candidates = []
    for direction, direction_name in ((1, "forward"), (-1, "reverse")):
        for shift in range(64):
            mapper = lambda row, d=direction, s=shift: [row[(d * index + s) % 64]
                                                        for index in range(64)]
            gain, residual = fit(sauti, [mapper(row) for row in ffmpeg])
            candidates.append((residual, f"{direction_name}+{shift}", gain))
    return min(candidates)


def mapper_from_name(name):
    direction_name, shift = name.split("+", 1)
    direction = 1 if direction_name == "forward" else -1
    shift = int(shift)
    return lambda row: [row[(direction * index + shift) % 64] for index in range(64)]


def parity_profile(sauti, ffmpeg, mapper):
    rows = {"even": (sauti[::2], [mapper(row) for row in ffmpeg[::2]]),
            "odd": (sauti[1::2], [mapper(row) for row in ffmpeg[1::2]])}
    return {name: fit(values[0], values[1]) for name, values in rows.items()}


def column_deltas(sauti, ffmpeg, mapper):
    mapped = [mapper(row) for row in ffmpeg]
    return [max(abs(left[column] - right[column])
                for left, right in zip(sauti, mapped))
            for column in range(64)]


def self_test():
    ffmpeg = [[float(slot * 64 + column + 1) for column in range(64)] for slot in range(3)]
    sauti = [[2.0 * value for value in row] for row in ffmpeg]
    residual, name, gain = pre_candidates(sauti, ffmpeg)
    assert name == "forward+0" and residual == 0.0 and gain == 2.0
    reversed_sauti = [list(reversed(row)) for row in sauti]
    residual, name, gain = pre_candidates(reversed_sauti, ffmpeg)
    assert name == "reverse+63" and residual == 0.0 and gain == 2.0
    try:
        fit(sauti, ffmpeg[:-1])
    except ValueError:
        pass
    else:
        raise AssertionError("mismatched vector counts accepted")
    print("PASS SBR-55 stage audit: size model, orientation selection, gain residual")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("sauti_pre")
    parser.add_argument("ffmpeg_pre")
    parser.add_argument("sauti_post")
    parser.add_argument("ffmpeg_post")
    parser.add_argument("--start-slot", type=int, default=64,
                        help="exclude one-context warmup; default matches SBR-54y")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.start_slot < 0:
        parser.error("--start-slot must be >=0")
    try:
        sa_pre, ff_pre = read_vectors(args.sauti_pre), read_vectors(args.ffmpeg_pre)
        sa_post, ff_post = read_vectors(args.sauti_post), read_vectors(args.ffmpeg_post)
        if not (len(sa_pre) == len(ff_pre) == len(sa_post) == len(ff_post)):
            raise ValueError("pre/post vector count mismatch")
        if args.start_slot >= len(sa_pre):
            raise ValueError("start slot leaves no stage vectors")
        sa_pre, ff_pre = sa_pre[args.start_slot:], ff_pre[args.start_slot:]
        sa_post, ff_post = sa_post[args.start_slot:], ff_post[args.start_slot:]
        pre_residual, pre_order, pre_gain = pre_candidates(sa_pre, ff_pre)
        post_gain, post_residual = fit(sa_post, ff_post)
        mapper = mapper_from_name(pre_order)
        pre_parity = parity_profile(sa_pre, ff_pre, mapper)
        post_parity = parity_profile(sa_post, ff_post, lambda row: row)
        deltas = column_deltas(sa_pre, ff_pre, mapper)
    except (OSError, ValueError) as exc:
        parser.exit(2, f"SBR-55 stage audit rejected: {exc}\n")
    print("SBR-55 QMF stage audit: diagnostic only; no decoder correction")
    print(f"slots={len(sa_pre)} start_slot={args.start_slot} pre_best_order={pre_order} "
          f"pre_gain(FF->Sauti)={pre_gain:.9g} pre_residual={pre_residual:.9g}")
    print(f"post_gain(FF->Sauti)={post_gain:.9g} post_residual={post_residual:.9g}")
    print("pre_parity " + " ".join(
        f"{name}:gain={gain:.9g},residual={residual:.9g}"
        for name, (gain, residual) in pre_parity.items()))
    print("post_parity " + " ".join(
        f"{name}:gain={gain:.9g},residual={residual:.9g}"
        for name, (gain, residual) in post_parity.items()))
    print("pre_column_max_abs " + ",".join(
        f"{column}:{delta:.9g}" for column, delta in enumerate(deltas) if delta > 1e-5
    ) or "none")


if __name__ == "__main__":
    main()
