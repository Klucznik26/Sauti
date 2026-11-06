#!/usr/bin/env python3
"""Select whole, regularly spaced records from an SBR-57 binary trace."""
import argparse
import tempfile
from pathlib import Path


def select(source, destination, floats_per_record, start, stride):
    if floats_per_record <= 0 or start < 0 or stride <= 0:
        raise ValueError("record geometry, start, and stride must be positive")
    raw = Path(source).read_bytes()
    record_size = floats_per_record * 4
    if not raw or len(raw) % record_size:
        raise ValueError("input is not a nonempty integral f32le record stream")
    records = len(raw) // record_size
    if start >= records:
        raise ValueError("start is outside the input record stream")
    selected = b"".join(raw[index * record_size:(index + 1) * record_size]
                        for index in range(start, records, stride))
    if not selected:
        raise ValueError("selection is empty")
    Path(destination).write_bytes(selected)
    return records, len(selected) // record_size


def self_test():
    with tempfile.TemporaryDirectory(prefix="sbr57-select-") as tmp:
        root = Path(tmp)
        source, destination = root / "input.f32le", root / "output.f32le"
        source.write_bytes(b"".join(bytes([index]) * 8 for index in range(5)))
        total, selected = select(source, destination, 2, 1, 2)
        assert (total, selected) == (5, 2)
        assert destination.read_bytes() == bytes([1]) * 8 + bytes([3]) * 8
        try:
            select(source, destination, 2, 5, 2)
        except ValueError:
            pass
        else:
            raise AssertionError("out-of-range trace selection accepted")
    print("PASS SBR-57 trace selector: full records and channel stride")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", nargs="?")
    parser.add_argument("destination", nargs="?")
    parser.add_argument("--floats-per-record", type=int)
    parser.add_argument("--start", type=int, default=0)
    parser.add_argument("--stride", type=int, default=1)
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if not args.source or not args.destination or args.floats_per_record is None:
        parser.error("source, destination, and --floats-per-record are required")
    try:
        total, selected = select(args.source, args.destination, args.floats_per_record,
                                 args.start, args.stride)
    except (OSError, ValueError) as exc:
        parser.exit(2, f"SBR-57 trace selection rejected: {exc}\n")
    print(f"SBR-57 selected {selected}/{total} full records start={args.start} stride={args.stride}")


if __name__ == "__main__":
    main()
