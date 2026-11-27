#!/usr/bin/env python3
"""External ASMA compatibility scan; no music is copied into the repo."""
import argparse
from collections import Counter
import csv
from pathlib import Path
import re
import subprocess
import tempfile
import wave


def is_sap(path, kind):
    with path.open("rb") as stream:
        header = stream.read(16386).split(b"\xff\xff", 1)[0]
    return header.startswith(b"SAP\r\n") and ("TYPE " + kind).encode("ascii") in header.splitlines()


def classify(message):
    match = re.search(r"unsupported I/O (read|write) \$([0-9A-Fa-f]{4})", message)
    if match:
        return "I/O " + match[1] + " $" + match[2]
    match = re.search(r"(INIT/parse|PLAYER) failed: SAP-B status (\d+)(?:, machine status (\d+))?", message)
    if match:
        return " ".join(x for x in match.groups() if x is not None)
    return "other renderer failure"


def inspect_wav(path, seconds):
    with wave.open(str(path), "rb") as wav:
        if (wav.getframerate() != 48000 or wav.getsampwidth() != 2
                or wav.getnchannels() not in (1, 2)
                or wav.getnframes() != 48000 * seconds):
            return "invalid WAV"
        # A nonzero sample is not evidence of accurate music.
        data = wav.readframes(wav.getnframes())
        if len(data) != wav.getnframes() * wav.getnchannels() * 2:
            return "invalid WAV"
        return "rendered nonzero" if any(data) else "rendered silence"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--renderer", type=Path, default=Path("build/render_atari8_sapb"))
    parser.add_argument("--seconds", type=int, default=1, choices=range(1, 16), metavar="1..15")
    parser.add_argument("--limit", type=int, default=100, help="0 tests all SAP-B files")
    parser.add_argument("--report", type=Path, default=Path("build/sapb-scan.csv"))
    parser.add_argument("--retry", type=Path, help="retry only non-success rows of a previous CSV report")
    parser.add_argument("--retry-frame-restart", action="store_true",
                        help="retry PLAYER budget failures with explicit legacy scheduler")
    parser.add_argument("--type", choices=("B", "C"), default="B")
    args = parser.parse_args()
    if args.limit < 0 or not args.archive.is_dir() or not args.renderer.is_file():
        parser.error("invalid archive, renderer or limit")
    renderer = args.renderer.resolve()
    paths = sorted(p for p in args.archive.rglob("*")
                   if p.is_file() and p.suffix.lower() == ".sap" and is_sap(p, args.type))
    if args.retry:
        if args.retry.resolve() == args.report.resolve():
            parser.error("retry and output reports must be different")
        with args.retry.open(newline="", encoding="utf-8") as previous:
            retry = {row["file"] for row in csv.DictReader(previous)
                     if row["result"] != "rendered nonzero"}
        paths = [p for p in paths if str(p.relative_to(args.archive)) in retry]
    selected = paths[:args.limit] if args.limit else paths
    print(f"SAP-{args.type} files: {len(paths)}; testing {len(selected)} at {args.seconds}s each", flush=True)
    if not selected:
        return 1
    counts = Counter()
    args.report.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="sauti-sapb-") as tmp, args.report.open(
            "w", newline="", encoding="utf-8") as report:
        writer = csv.writer(report)
        writer.writerow(["file", "result", "diagnostic", "mode"])
        output = Path(tmp) / "test.wav"
        for index, path in enumerate(selected, 1):
            output.unlink(missing_ok=True)
            message = ""
            mode = "standard"
            try:
                proc = subprocess.run(
                    [str(renderer), str(path), str(output), str(args.seconds)],
                    capture_output=True, text=True, errors="replace", timeout=30)
                message = (proc.stderr + proc.stdout).strip()
                if proc.returncode:
                    result = classify(message)
                    if args.type == "B" and args.retry_frame_restart and result == "PLAYER 3 4":
                        mode = "frame-restart"
                        output.unlink(missing_ok=True)
                        retry_proc = subprocess.run(
                            [str(renderer), str(path), str(output),
                             str(args.seconds), "--frame-restart"],
                            capture_output=True, text=True, errors="replace", timeout=30)
                        message += "\nFRAME-RESTART RETRY:\n" + (retry_proc.stderr + retry_proc.stdout).strip()
                        if retry_proc.returncode:
                            result = classify(retry_proc.stderr + retry_proc.stdout)
                        else:
                            try:
                                result = inspect_wav(output, args.seconds)
                            except (OSError, EOFError, wave.Error):
                                result = "invalid WAV"
                        # A silent retry does not establish recovered sound.
                        if result == "rendered silence":
                            result = "frame-restart silence (unverified)"
                        if result == "rendered nonzero":
                            result = "rendered nonzero (frame-restart)"
                else:
                    try:
                        result = inspect_wav(output, args.seconds)
                    except (OSError, EOFError, wave.Error):
                        result = "invalid WAV"
            except subprocess.TimeoutExpired:
                result = "timeout"
            counts[result] += 1
            writer.writerow([str(path.relative_to(args.archive)), result, message, mode])
            report.flush()
            if index % 25 == 0 or index == len(selected):
                print(f"{index}/{len(selected)} completed", flush=True)
    for result, count in counts.most_common():
        print(f"{count:5}  {result}")
    rendered = (counts["rendered nonzero"] + counts["rendered silence"]
                + counts["rendered nonzero (frame-restart)"])
    print(f"Rendered: {rendered}/{len(selected)}; not a sound-accuracy test")
    print(f"Report: {args.report}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
