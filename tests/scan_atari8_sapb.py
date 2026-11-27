#!/usr/bin/env python3
"""External ASMA compatibility scan; no music is copied into the repo."""
import argparse
from collections import Counter
from concurrent.futures import ThreadPoolExecutor
import time
import csv
from pathlib import Path
import re
import subprocess
import tempfile
import wave


def is_sap(path, kind, without_player=False):
    with path.open("rb") as stream:
        header = stream.read(16386).split(b"\xff\xff", 1)[0]
    lines = header.splitlines()
    return (header.startswith(b"SAP\r\n")
            and ("TYPE " + kind).encode("ascii") in lines
            and (not without_player or not any(line.startswith(b"PLAYER ") for line in lines)))


def subsongs(path):
    """Read only the bounded text header; reject ambiguous SONGS/DEFSONG."""
    with path.open("rb") as stream:
        header = stream.read(16386).split(b"\xff\xff", 1)[0]
    values = {}
    for line in header.splitlines():
        key, _, value = line.partition(b" ")
        if key in (b"SONGS", b"DEFSONG"):
            if key in values or not re.fullmatch(rb"[0-9]{1,3}", value):
                raise ValueError(f"{path}: invalid {key.decode()} tag")
            values[key] = int(value)
    count = values.get(b"SONGS", 1)
    default = values.get(b"DEFSONG", 0)
    if not 1 <= count <= 256 or not 0 <= default < count:
        raise ValueError(f"{path}: invalid subsong count/default")
    return count, default


def declared_times(path, count):
    """One TIME tag per subsong, in header order; absent is unknown."""
    with path.open("rb") as stream:
        header = stream.read(16386).split(b"\xff\xff", 1)[0]
    times = []
    for line in header.splitlines():
        if not line.startswith(b"TIME "):
            continue
        m = re.fullmatch(rb"TIME ([0-9]{1,2}):([0-9]{2})(?:\.([0-9]{1,3}))?( LOOP)?", line)
        if not m or int(m[2]) >= 60:
            times.append((None, "", "invalid TIME"))
            continue
        ms = (int(m[1])*60 + int(m[2]))*1000 + int((m[3] or b"").ljust(3,b"0"))
        times.append((ms, "1" if m[4] else "0", "" if ms else "zero duration"))
    if len(times) > count:
        return [(None, "", "too many TIME tags")]*count
    return times + [(None, "", "duration unknown")]*(count-len(times))


def classify(message):
    match = re.search(r"unsupported I/O (read|write) \$([0-9A-Fa-f]{4})", message)
    if match:
        return "I/O " + match[1] + " $" + match[2]
    match = re.search(r"(INIT/parse|PLAYER) failed: SAP-B status (\d+)(?:, machine status (\d+))?", message)
    if match:
        return " ".join(x for x in match.groups() if x is not None)
    return "other renderer failure"


def inspect_wav(path, seconds, *, frames=None):
    expected = 48000 * seconds if frames is None else frames
    with wave.open(str(path), "rb") as wav:
        if (wav.getframerate() != 48000 or wav.getsampwidth() != 2
                or wav.getnchannels() not in (1, 2)
                or wav.getnframes() != expected):
            return "invalid WAV"
        # A nonzero sample is not evidence of accurate music.
        active = False
        remaining = expected
        width = wav.getnchannels() * 2
        while remaining:
            n = min(remaining, 65536)
            data = wav.readframes(n)
            if len(data) != n * width:
                return "invalid WAV"
            if not active:
                active = any(data)
            remaining -= n
        return "rendered nonzero" if active else "rendered silence"


def inspect_pcm(message, expected):
    matches = re.findall(r"(?m)^PCM-CHECK frames=(\d+) channels=([12]) rate=48000 nonzero=([01])$", message)
    if len(matches) != 1 or int(matches[0][0]) != expected:
        return "invalid PCM report"
    return "rendered nonzero" if matches[0][2] == "1" else "rendered silence"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--renderer", type=Path, default=Path("build/render_atari8_sapb"))
    parser.add_argument("--seconds", type=int, default=1, choices=range(1, 16), metavar="1..15")
    parser.add_argument("--limit", type=int, default=100, help="0 tests all files of selected SAP type")
    parser.add_argument("--report", type=Path, default=Path("build/sapb-scan.csv"))
    parser.add_argument("--retry", type=Path, help="retry only non-success rows of a previous CSV report")
    scope = parser.add_mutually_exclusive_group()
    scope.add_argument("--full-duration", action="store_true",
                       help="render every subsong to its declared TIME (one declared length even for LOOP)")
    scope.add_argument("--other-subsongs", action="store_true",
                        help="test every non-default subsong (default already covered by corpus scan)")
    parser.add_argument("--jobs", type=int, default=1, help="parallel renderer processes (1..32)")
    parser.add_argument("--no-wav", action="store_true", help="validate rendered PCM directly, without WAV I/O")
    parser.add_argument("--resume", action="store_true",
                        help="append missing rows to an existing report; use identical scan options and renderer")
    parser.add_argument("--retry-frame-restart", action="store_true",
                        help="retry B/D PLAYER budget failures with explicit experimental scheduler")
    parser.add_argument("--type", choices=("B", "C", "D", "S"), default="B")
    parser.add_argument("--without-player", action="store_true",
                        help="SAP-D only: scan files without optional PLAYER")
    args = parser.parse_args()
    if not 1 <= args.jobs <= 32:
        parser.error("--jobs must be between 1 and 32")
    if args.retry_frame_restart and args.type not in ("B", "D"):
        parser.error("--retry-frame-restart requires --type B or D")
    if args.limit < 0 or not args.archive.is_dir() or not args.renderer.is_file():
        parser.error("invalid archive, renderer or limit")
    if args.without_player and args.type != "D":
        parser.error("--without-player requires --type D")
    renderer = args.renderer.resolve()
    paths = sorted(p for p in args.archive.rglob("*")
                   if p.is_file() and p.suffix.lower() == ".sap" and is_sap(p, args.type, args.without_player))
    indexed = args.other_subsongs or args.full_duration
    fields = ["file", "result", "diagnostic", "mode"]
    if indexed:
        fields.append("song")
    if args.full_duration:
        fields.extend(["duration_ms", "loop"])
    duration = {}
    def key(path, song):
        name = str(path.relative_to(args.archive))
        return (name, str(song)) if indexed else (name,)
    try:
        if indexed:
            tasks = []
            for path in paths:
                count, default = subsongs(path)
                tasks.extend((path, song) for song in range(count)
                             if args.full_duration or song != default)
                if args.full_duration:
                    duration.update({(path, song): entry for song, entry
                                     in enumerate(declared_times(path, count))})
        else:
            tasks = [(path, -1) for path in paths]
    except (OSError, ValueError) as exc:
        parser.error(str(exc))
    if args.retry:
        if args.retry.resolve() == args.report.resolve():
            parser.error("retry and output reports must be different")
        with args.retry.open(newline="", encoding="utf-8") as previous:
            reader = csv.DictReader(previous)
            if reader.fieldnames != fields:
                parser.error("retry report has incompatible columns")
            retry = {(row["file"], row["song"]) if indexed else (row["file"],)
                     for row in reader if row["result"] != "rendered nonzero"}
        tasks = [(p, song) for p, song in tasks if key(p, song) in retry]
    selected = tasks[:args.limit] if args.limit else tasks
    total = len(selected)
    if not total:
        print("No matching subsongs" if args.other_subsongs else "No matching files", flush=True)
        return 0 if args.other_subsongs else 1
    counts = Counter()
    completed = 0
    append = args.resume and args.report.exists()
    if append:
        expected = [key(p, song) for p, song in selected]
        try:
            with args.report.open("rb") as raw:
                if raw.seek(0, 2) == 0:
                    raise ValueError("empty report")
                raw.seek(-1, 2)
                if raw.read(1) != b"\n":
                    raise ValueError("incomplete last CSV row")
            with args.report.open(newline="", encoding="utf-8") as previous:
                reader = csv.reader(previous, strict=True)
                if next(reader, None) != fields:
                    raise ValueError("unexpected CSV header")
                for row in reader:
                    if (len(row) != len(fields) or completed >= total
                            or ((row[0], row[4]) if indexed else (row[0],)) != expected[completed] or not row[1]
                            or row[3] not in ("standard", "frame-restart")
                            or (row[3] == "frame-restart" and not args.retry_frame_restart)):
                        raise ValueError("report does not match the selected scan prefix")
                    if args.full_duration:
                        ms, loop, _ = duration[selected[completed]]
                        if row[5:7] != [str(ms) if ms is not None else "", loop]:
                            raise ValueError("TIME metadata changed")
                    counts[row[1]] += 1
                    completed += 1
        except (OSError, UnicodeError, ValueError, csv.Error) as exc:
            parser.error(f"cannot resume: {exc}; original report preserved")
        selected = selected[completed:]
        print(f"Resuming: {completed}/{total} already recorded; "
              f"{len(selected)} remaining. Keep the original renderer and scan options.", flush=True)
    print(f"SAP-{args.type} files: {len(paths)}; testing {len(selected)} "
          + ("subsongs at declared TIME" if args.full_duration else
             f"{'non-default subsongs' if args.other_subsongs else 'default subsongs'} at {args.seconds}s each"),
          flush=True)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    def render_task(task):
        index, (path, song) = task
        message = ""
        mode = "standard"
        ms, loop, reason = duration.get((path, song), (None, "", ""))
        if reason:
            return [str(path.relative_to(args.archive)), reason, "", mode,
                    song, "" if ms is None else ms, loop]
        output = Path(tmp) / f"test-{index}.wav"
        cmd = [str(renderer), str(path), str(output),
               str(ms if args.full_duration else args.seconds)]
        if indexed:
            cmd.append(str(song))
        if args.full_duration:
            cmd.append("--milliseconds")
        if args.no_wav:
            cmd.append("--no-wav")
        timeout = max(30, (ms+999)//1000*5) if ms is not None else 30
        frames = ms*48 if ms is not None else args.seconds*48000

        def inspect_result(proc):
            if args.no_wav:
                return inspect_pcm(proc.stdout, frames)
            try:
                return inspect_wav(output, args.seconds, frames=frames)
            except (OSError, EOFError, wave.Error):
                return "invalid WAV"

        try:
            proc = subprocess.run(cmd, capture_output=True, text=True,
                                  errors="replace", timeout=timeout)
            message = (proc.stderr + proc.stdout).strip()
            if proc.returncode:
                result = classify(message)
                if args.type in ("B", "D") and args.retry_frame_restart and result == "PLAYER 3 4":
                    mode = "frame-restart"
                    output.unlink(missing_ok=True)
                    retry_proc = subprocess.run(cmd + ["--frame-restart"],
                        capture_output=True, text=True, errors="replace", timeout=timeout)
                    message += "\nFRAME-RESTART RETRY:\n" + (retry_proc.stderr + retry_proc.stdout).strip()
                    result = (classify(retry_proc.stderr + retry_proc.stdout)
                              if retry_proc.returncode else inspect_result(retry_proc))
                    if result == "rendered silence":
                        result = "frame-restart silence (unverified)"
                    if result == "rendered nonzero":
                        result = "rendered nonzero (frame-restart)"
            else:
                result = inspect_result(proc)
        except subprocess.TimeoutExpired:
            result = "timeout"
        finally:
            output.unlink(missing_ok=True)
        row = [str(path.relative_to(args.archive)), result, message, mode]
        if indexed:
            row.append(song)
        if args.full_duration:
            row.extend([ms, loop])
        return row

    started = time.monotonic()
    print(f"Workers: {args.jobs}; PCM-only: {args.no_wav}", flush=True)
    with tempfile.TemporaryDirectory(prefix="sauti-sapb-") as tmp, args.report.open(
            "a" if append else "w", newline="", encoding="utf-8") as report, \
            ThreadPoolExecutor(max_workers=args.jobs) as pool:
        writer = csv.writer(report)
        if not append:
            writer.writerow(fields)
            report.flush()
        # Bounded batches, ordered writes and one CSV writer preserve the
        # resume prefix. No concurrent access to a single temporary WAV.
        for offset in range(0, len(selected), args.jobs):
            batch = list(enumerate(selected[offset:offset+args.jobs], completed+offset+1))
            for index, row in zip(range(completed+offset+1, completed+offset+1+len(batch)),
                                  pool.map(render_task, batch)):
                counts[row[1]] += 1
                writer.writerow(row)
                report.flush()
                if index % 25 == 0 or index == total:
                    print(f"{index}/{total} completed; {time.monotonic()-started:.1f}s elapsed", flush=True)
    for result, count in counts.most_common():
        print(f"{count:5}  {result}")
    rendered = (counts["rendered nonzero"] + counts["rendered silence"]
                + counts["rendered nonzero (frame-restart)"])
    print(f"Rendered: {rendered}/{total}; not a sound-accuracy test")
    print(f"Report: {args.report}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
