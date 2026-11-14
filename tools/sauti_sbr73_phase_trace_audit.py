#!/usr/bin/env python3
"""SBR-73: strict clock/geometry audit for independent FFmpeg and Sauti logs.

No automatic FFmpeg-call or slot alignment is inferred. Supplying offsets
explicitly compares phases; such a comparison is valid only if the selected
audio channel and matched QMF time origin are independently verified.
"""
import argparse
import csv
import math
from collections import defaultdict
from pathlib import Path

FF_HEADER = ["call", "env", "slot", "indexsine", "kx", "mmax",
             "sine18", "sine42"]
SA_FIELDS = 11
SA_PREFIX = "SBR73_SAUTI"


def load_sauti(path):
    frames = defaultdict(list)
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        if not line.startswith(SA_PREFIX + ","):
            continue  # Diagnostic stderr includes unrelated lines.
        parts = line.split(",")
        if len(parts) != SA_FIELDS + 1:
            raise ValueError("Sauti line has wrong column count")
        values = [int(v) for v in parts[1:]]
        (frame, slot, p0, p2, kx, k2, source, envelope,
         flag18, flag42, onset) = values
        if not (0 <= slot < 32 and 0 <= p0 < 4 and
                p2 == (p0 + 2) % 4 and 1 <= kx <= 32 and
                kx < k2 <= 64 and source in (0, 1) and
                flag18 in (0, 1) and flag42 in (0, 1) and
                onset in (0, 1)):
            raise ValueError(f"invalid Sauti slot frame={frame} slot={slot}")
        frames[frame].append(dict(frame=frame, slot=slot, phase0=p0,
                                  phase2=p2, kx=kx, k2=k2, source=source,
                                  envelope=envelope, flag18=flag18,
                                  flag42=flag42, onset=onset))
    if not frames:
        raise ValueError("no SBR73_SAUTI lines in log")
    for frame, entries in frames.items():
        if len(entries) != 32 or [v["slot"] for v in entries] != list(range(32)):
            raise ValueError(f"missing or duplicate Sauti slots at frame {frame}")
        for r in entries:
            if r["phase0"] != (entries[0]["phase0"] + r["slot"]) % 4:
                raise ValueError("Sauti oscillator does not advance per slot")
    frame_ids = sorted(frames)
    if frame_ids != list(range(frame_ids[0], frame_ids[-1] + 1)):
        raise ValueError("missing Sauti emitted frame in trace")
    return frames


def load_ffmpeg(path):
    calls = defaultdict(list)
    with Path(path).open(encoding="utf-8", newline="") as f:
        reader = csv.DictReader(f)
        if reader.fieldnames != FF_HEADER:
            raise ValueError("unsupported FFmpeg SBR-73 CSV header")
        for item in reader:
            if not item or None in item or any(v is None for v in item.values()):
                raise ValueError("malformed FFmpeg CSV record")
            call, env, slot, phase, kx, mmax = (
                int(item[key]) for key in FF_HEADER[:6])
            sine18, sine42 = (float(item["sine18"]), float(item["sine42"]))
            if not (call >= 0 and env >= 0 and slot >= 0 and 0 <= phase < 4
                    and 1 <= kx <= 32 and 1 <= mmax <= 64-kx
                    and math.isfinite(sine18) and math.isfinite(sine42)):
                raise ValueError("invalid FFmpeg HF slot values")
            calls[call].append(dict(call=call, env=env, slot=slot,
                                    phase=phase, kx=kx, mmax=mmax,
                                    sine18=sine18, sine42=sine42))
    if not calls:
        raise ValueError("empty FFmpeg HF assembly trace")
    call_ids = sorted(calls)
    if call_ids != list(range(call_ids[0], call_ids[-1] + 1)):
        raise ValueError("missing FFmpeg HF assembly call")
    for call, rows in calls.items():
        previous = None
        for row in rows:
            if previous is not None:
                step = row["slot"] - previous["slot"]
                if step <= 0 or row["phase"] != (previous["phase"] + step) % 4:
                    raise ValueError(f"FFmpeg indexsine clock jump at call {call}")
            previous = row
    return calls


def compare_explicit(sauti, ffmpeg, call_offset, slot_offset):
    # This intentionally does NOT infer offsets from reference data.
    matches = {0: 0, 2: 0}
    matched = active18 = active42 = 0
    unmatched = 0
    for frame, slots in sauti.items():
        ref = {v["slot"]: v for v in ffmpeg.get(frame + call_offset, ())}
        for sa in slots:
            ff = ref.get(sa["slot"] + slot_offset)
            if ff is None:
                unmatched += 1
                continue
            matched += 1
            matches[0] += (ff["phase"] == sa["phase0"])
            matches[2] += (ff["phase"] == sa["phase2"])
            active18 += (ff["sine18"] != 0.0)
            active42 += (ff["sine42"] != 0.0)
    if not matched:
        raise ValueError("no matched slots at these user-specified offsets")
    return dict(matched=matched, unmatched=unmatched,
                phase0_equal=matches[0], phase2_equal=matches[2],
                ffmpeg_sine18_slots=active18,
                ffmpeg_sine42_slots=active42)


def self_test():
    from tempfile import TemporaryDirectory
    with TemporaryDirectory() as tmp:
        sa = Path(tmp) / "sauti.log"
        ff = Path(tmp) / "ff.csv"
        sa.write_text(
            "unrelated diagnostic line\n" +
            "".join(f"SBR73_SAUTI,{fr},{slot},{slot%4},{(slot+2)%4},"
                    f"16,44,0,0,1,1,1\n"
                    for fr in range(3) for slot in range(32)),
            encoding="utf-8")
        ff.write_text(
            ",".join(FF_HEADER) + "\n" +
            "".join(f"{fr+1},0,{slot+2},{(slot+2)%4},16,28,0.3,0.4\n"
                    for fr in range(3) for slot in range(32)),
            encoding="utf-8")
        s, f = load_sauti(sa), load_ffmpeg(ff)
        measured = compare_explicit(s, f, 1, 2)
        assert measured["matched"] == 96
        assert measured["phase0_equal"] == 0
        assert measured["phase2_equal"] == 96
        assert measured["ffmpeg_sine18_slots"] == 96
        assert measured["ffmpeg_sine42_slots"] == 96
        try:
            compare_explicit(s, f, 100, 0)
        except ValueError:
            pass
        else:
            raise AssertionError("unmatched offset accepted")
        broken = sa.read_text().replace("SBR73_SAUTI,0,31,", "SBR73_SAUTI,0,30,", 1)
        sa.write_text(broken, encoding="utf-8")
        try:
            load_sauti(sa)
        except ValueError:
            pass
        else:
            raise AssertionError("duplicate slot accepted")
        ff.write_text(",".join(FF_HEADER)+"\n"+"0,0,0,0,16,28,nan,0\n",
                      encoding="utf-8")
        try:
            load_ffmpeg(ff)
        except ValueError:
            pass
        else:
            raise AssertionError("nonfinite FFmpeg trace accepted")
    print("PASS SBR-73: strict per-slot oscillator CSV, explicit origins and fail-closed inputs")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--self-test", action="store_true")
    p.add_argument("--sauti")
    p.add_argument("--ffmpeg")
    p.add_argument("--ffmpeg-call-offset", type=int)
    p.add_argument("--ffmpeg-slot-offset", type=int)
    args = p.parse_args()
    if args.self_test:
        self_test()
        return 0
    if not args.sauti or not args.ffmpeg:
        p.error("--sauti and --ffmpeg required")
    if (args.ffmpeg_call_offset is None) != (args.ffmpeg_slot_offset is None):
        p.error("provide BOTH fixed call and slot offsets, or neither")
    try:
        sa = load_sauti(args.sauti)
        ff = load_ffmpeg(args.ffmpeg)
        print(f"SBR-73 TRACE: Sauti emitted frames={len(sa)}, "
              f"FFmpeg HF assembly calls={len(ff)}")
        print("Sauti first-frame start phase=", sa[min(sa)][0]["phase0"])
        print("FFmpeg first-call first-slot/phase=",
              ff[min(ff)][0]["slot"], ff[min(ff)][0]["phase"])
        print("FFmpeg first-call active sine slots: k18=",
              sum(r["sine18"] != 0 for r in ff[min(ff)]),
              "k42=", sum(r["sine42"] != 0 for r in ff[min(ff)]))
        if args.ffmpeg_call_offset is not None:
            r = compare_explicit(sa, ff, args.ffmpeg_call_offset,
                                 args.ffmpeg_slot_offset)
            print(f"EXPLICIT alignment only: call offset={args.ffmpeg_call_offset}, "
                  f"slot offset={args.ffmpeg_slot_offset}; "
                  f"matched={r['matched']} missing={r['unmatched']}")
            print(f"Index agreement: phase0={r['phase0_equal']}/{r['matched']} "
                  f"phase+2={r['phase2_equal']}/{r['matched']}")
            print(f"FFmpeg sine-present matched slots: "
                  f"k18={r['ffmpeg_sine18_slots']} k42={r['ffmpeg_sine42_slots']}")
    except (OSError, ValueError) as e:
        p.exit(2, f"SBR-73 trace rejected: {e}\n")
    print("No automatically inferred time origin or independent QMF amplitude "
          "equivalence. Match channel, emitted frame and QMF slot before "
          "calling any phase offset normative.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
