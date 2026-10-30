# YM decoder performance and PCM regression

The decoder currently supports YM2, YM3, YM3b, YM5 and YM6. Its replay
checkpoints are **8 bounded decoder-state snapshots**, refreshed every five
seconds during regular playback and after explicit seeks. The oldest saved
snapshot may be evicted. This is a cache, not a complete seek index.

## Reproduce on Fedora / x86-64

From `apps/sayari-sauti`:

```sh
make build/test_decoder_ym build/sauti_window_pw
./build/test_decoder_ym
make bench-ym
```

The benchmark generates a **synthetic 120-second YM3 recording** in memory,
requiring no copyrighted tracks or SDL/PipeWire. It measures CPU time
(`clock()`) for a cold seek to 110s, a warm seek to the same position,
a backward seek, 30s of streaming and a seek back into streamed audio.
A PCM checksum prevents a trivial no-op benchmark. It does not establish
hardware-accurate playback, wall-clock latency or end-to-end UI performance.
Measurements will vary with CPU, compiler optimization and OS load; do not
enforce absolute timing thresholds in correctness tests.

## Correctness gate

The regression suite checks sample-by-sample identity for:
- multi-buffer YM3 playback with automatic checkpoints and zig-zag seeks;
- longer YM6 playback with SID and sync-buzzer transitions;
- exact MFP timer tick parity and boundary crossing;
- rewind/replay and previous tone/noise/envelope/digi-drum cases.

A test reporting PASS is not a listening test. For reference comparison, render
the same YM to PCM with ST-Sound (matching sample rate, timing, mixing, and
loop settings) and compare both waveforms and audible behavior. Effects remain
experimental until reference validation.

## Remaining optimization

Cold forward seeks still decode from time zero if no suitable checkpoint exists.
For long songs, a bounded sparse index or background-free precomputation may
provide better worst-case latency, but should be benchmarked before adoption.

## Extended parser robustness suite

Run `make test-ym-extended` to compile and execute the standard YM decoder
regression, the malformed-input/mutation corpus, and YM archive tests. The
malformed suite builds a valid in-memory YM6 fixture, tries every truncated
length, modifies each byte to all 256 possibilities, exercises explicit
corrupted metadata/timer fields, and feeds deterministic pseudorandom data.
Successful opens are decoded briefly and rewound to exercise cleanup paths.

A passing mutation suite proves only these finite cases did not trigger a
visible failure. It does **not** replace a memory-sanitizer run or a security
review. For a stronger diagnostic build on Fedora, optionally use:

```sh
make build/test_ym_malformed CFLAGS='-O1 -g -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer'
ASAN_OPTIONS=detect_leaks=1 ./build/test_ym_malformed
```

The optional sanitizer build links the existing NASM object and is intended
for diagnostic use; restore the regular binary with `make -B build/test_ym_malformed`
when you want normal benchmark comparisons. Avoid interpreting benchmark
numbers from sanitizer builds as production performance.

## Dedicated sanitizer command

The preferred diagnostic command is now:

```sh
make test-ym-sanitize
```

It builds `build/test_decoder_ym_san` and `build/test_ym_malformed_san`
using AddressSanitizer and UndefinedBehaviorSanitizer and executes both with
leak detection and immediate failure enabled. These targets never overwrite the
optimized benchmark or normal test binaries. A sanitizer build needs GCC/Clang
with sanitizer runtime packages installed. NASM routines are not instrumented
by C sanitizers, so sanitizer success must not be mistaken for complete memory
safety verification of assembly code. Run `make test-ym-extended` as a separate
correctness gate and `make bench-ym` only for performance measurements.

## Reference PCM comparison (experimental)

The renderer `make build/render_decoder_ym` converts a raw or LHA-packed YM
file to mono PCM16 WAV, at 44.1 kHz. Render the **same song** with a trusted
ST-Sound implementation as mono PCM16 at the **same sample rate**. The external
renderer is **not bundled**, and no reference WAV is supplied.

```sh
make build/render_decoder_ym
./build/render_decoder_ym song.ym sauti.wav
python3 tools/compare_ym_pcm.py sauti.wav reference.wav
make test-ym-reference-tools
```

The comparison script prints sample counts, unaligned RMS and peak errors,
uncentered waveform correlation, and a maximum-correlation lag search within
256 samples by default. This helps identify start offsets and significant
waveform differences. Silence produces undefined correlation (NaN); gain,
chip-clock, mixer curves, frame timing and resampling choices must be aligned
before treating waveform differences as decoder defects. For longer offsets use
`--max-lag` (up to 44100). The input is strictly uncompressed mono PCM16 WAV.

This is a **diagnostic comparison**, not yet evidence of ST-Sound parity.
