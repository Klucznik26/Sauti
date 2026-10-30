#!/usr/bin/env python3
"""Convert full-length audio to Falcon WinRec-style unpacked DVSM/DVS PCM.

Uses FFmpeg for reliable decoding, stereo conversion, resampling and dithering;
writes the 16-byte DVSM header expected by Sauti's Atari Falcon decoder.
Generated files represent Falcon-compatible digital PCM, not an authentic
analog recording of Falcon hardware. No whole-track buffering.
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

RATE = 49170
HEADER = b"DVSM\0\0" + struct.pack(">HHBBI", 16, 7, 0, 1, 0)
CHUNK = 256 * 1024


def convert(source: Path, destination: Path, overwrite: bool) -> None:
    if not source.is_file():
        raise ValueError(f"Brak pliku źródłowego: {source}")
    if source.resolve() == destination.resolve():
        raise ValueError("Plik źródłowy i wynikowy muszą być różne")
    if destination.exists() and not overwrite:
        raise ValueError(f"Plik już istnieje: {destination} (użyj --overwrite)")
    ffmpeg = shutil.which("ffmpeg")
    if not ffmpeg:
        raise ValueError("Brak ffmpeg. Fedora: sudo dnf install ffmpeg-free")
    command = [
        ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin",
        "-i", str(source), "-map", "0:a:0", "-vn", "-sn", "-dn",
        "-ac", "2", "-ar", str(RATE), "-c:a", "pcm_s16be",
        "-f", "s16be", "pipe:1",
    ]
    proc = subprocess.Popen(command, stdout=subprocess.PIPE)
    written = 0
    try:
        assert proc.stdout is not None
        # x+b creates exclusively, preventing accidental destruction of music.
        # --overwrite removes the destination only when explicitly requested.
        if overwrite and destination.exists():
            destination.unlink()
        with destination.open("xb") as output:
            output.write(HEADER)
            while True:
                chunk = proc.stdout.read(CHUNK)
                if not chunk:
                    break
                output.write(chunk)
                written += len(chunk)
        code = proc.wait()
        if code != 0 or written == 0 or written % 4:
            raise RuntimeError(f"FFmpeg zakończył się błędem (kod {code}) lub wygenerował niepełne stereo")
    except BaseException:
        proc.kill()
        proc.wait()
        destination.unlink(missing_ok=True)
        raise
    print(f"Zapisano: {destination}")
    print(f"Falcon DVSM: 16-bit stereo, {RATE} Hz, bez kompresji, {written/4/RATE:.2f} s")


def main() -> int:
    parser = argparse.ArgumentParser(description="WAV/FLAC/MP3 → Atari Falcon DVSM 16-bit stereo")
    parser.add_argument("source", type=Path, help="plik wejściowy (np. .wav, .flac, .mp3)")
    parser.add_argument("destination", type=Path, help="wynikowy plik .dvs")
    parser.add_argument("--overwrite", action="store_true", help="jawnie nadpisz istniejący plik")
    args = parser.parse_args()
    try:
        convert(args.source, args.destination, args.overwrite)
        return 0
    except (ValueError, OSError, RuntimeError) as exc:
        print(f"Błąd: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
