# SBR-74: guarded QMF SSE2 / AVX2 optimization

**Branch:** `feature/sbr74-qmf-avx2-dispatch`. This is an experimental performance branch, not a new AAC/HE-AAC correctness milestone.

## Changes

- Retain the working x86-64 System V SSE2 synthesis kernels. The modulation kernel can fold loads from 32-byte-aligned, constant lookup tables into `mulps`. The polyphase window kernel **must not** do so: its caller-supplied coefficient pointer may be unaligned, and legacy SSE `mulps xmm, m128` requires 16-byte alignment. The window uses `movups` followed by register-register `mulps`.
- Add a 256-bit AVX2 implementation of the 640-tap polyphase synthesis window (32 output samples per chunk, ten taps) and complex modulation (eight floats per vector; mirrored outputs).
- An experimental fully-unrolled AVX2 window was tested and rejected: on GitHub Actions its median was 28.1 ns/slot versus 28.4 ns/slot for the simpler AVX2 loop (about 1% difference, below a credible cross-CPU improvement). The unrolled object was removed rather than expanding the binary without a justified benefit.
- The two modulator variants share the same precomputed trigonometric tables. AVX2 uses **no FMA**, deliberately keeping multiplication and addition separate to limit rounding differences. Modulation may still differ by a few float ulps because of the reduction tree.
- The public C wrappers check finite inputs, bounds, output overlap and rollback before/after invoking ASM, as before.
- Use `__builtin_cpu_supports("avx2")` on GNU/Clang System V x86-64: this checks processor and OS extended register support. Never execute AVX2 before the check. CPUID is evaluated once per call to the frame wrapper, outside the slot loop. On older x86-64 use SSE2.
- With `-DSAUTI_QMF_FORCE_SSE2`, always choose SSE2 on x86-64. With `-DSAUTI_QMF_DISABLE_ASM`, select the scalar C11 reference code. On Windows x64 the current **System V/ELF NASM kernels are deliberately disabled** in the C wrappers; use the portable C implementation pending a real Windows object/ABI port.

## Differential and regression tests

```sh
make test-aac-qmf-simd
make test-aac-qmf-variants  # C11 and forced SSE2 public-API tests
make test-aac-sbr-qmf-synth-mod test-aac-sbr-qmf-synth-window
make test-aac-ps-pcm-stereo test-aac-he-ps-bridge
make test-aac-native
make bench-aac-qmf  # optional per-slot median microbenchmark
make bench-aac-qmf-frame  # 40-slot full synthesis incl. validation/history
make test-aac-qmf-frame-parity  # 2560 PCM values auto vs forced SSE2
```

`test-aac-qmf-simd` runs real native SSE2 and AVX2 window with input variation, deliberately offset/unaligned input and output buffers, and output canaries. The 640-tap window should be bit-identical; modulation is compared with an explicit rounding tolerance. On hosts without AVX2 the differential target prints SKIP; the SSE2 public tests still run. Existing public tests cover the C wrapper's transactional behavior and mathematical reference.

Run a **fresh rebuild** to force SSE2 before comparing output or measuring the per-frame cost:

```sh
make -B test-aac-sbr-qmf-synth-mod test-aac-sbr-qmf-synth-window \
  CFLAGS='-O2 -std=c11 -Wall -Wextra -Werror -DSAUTI_QMF_FORCE_SSE2'
```

For standalone C11 reference checks on a non-ELF platform, compile the two `.c` files without NASM and define `SAUTI_QMF_DISABLE_ASM`. The repository-wide Makefile still builds other NASM x86-64 codec modules and is not a general Windows build system.

## Frame-level validation (PR #6)

The new `tools/sauti_qmf_frame_bench.c` runs the public
`sauti_aac_sbr_qmf_synth_64()` function in two separately built executables:
the normal runtime-dispatched build and the build with
`SAUTI_QMF_FORCE_SSE2`. Each renders 40 QMF slots (2560 real PCM samples).
Seven batches of 300 frames are timed after a 32-frame warmup; the median and
maximum batch-normalized frame times are printed. This includes finite checks,
output staging, the 10-slot history ring, and two kernel calls per slot.

The parity target dumps raw float32 PCM from each executable and compares all
2560 samples with a per-sample bound of `3e-6 * max(1, abs(auto), abs(sse))`.
It rejects nonfinite results and reports RMS/max absolute differences. This is
a **regression tolerance**, not proof of bit-identical output or correctness
against FFmpeg. It also does not measure demuxing, AAC-LC, SBR parameter decode
or PS.

Run `make bench-aac-qmf-frame` on the target Ryzen 5 7600X before drawing
conclusions about hardware-specific performance. GitHub Actions numbers are
only measurements on the runner CPU.

## Acceptance criteria

Do **not** claim a speedup from source inspection alone. Collect median and tail times per 40-slot frame on the actual CPU (plus full HE-AAC playback under load), verify the intended CPU branch is used, compare pre-PS PCM on real fixtures, and run sanitizer tests. FMA or a DCT/FFT rewrite would be a separate, numerically revalidated change. This branch does not change the SBR envelope/noise/PS reconstruction.
