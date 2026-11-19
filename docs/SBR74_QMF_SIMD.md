# SBR-74: guarded QMF SSE2 / AVX2 optimization

**Branch:** `feature/sbr74-qmf-avx2-dispatch`. This is an experimental performance branch, not a new AAC/HE-AAC correctness milestone.

## Changes

- Retain the working x86-64 System V SSE2 synthesis kernels. The modulation kernel can fold loads from 32-byte-aligned, constant lookup tables into `mulps`. The polyphase window kernel **must not** do so: its caller-supplied coefficient pointer may be unaligned, and legacy SSE `mulps xmm, m128` requires 16-byte alignment. The window uses `movups` followed by register-register `mulps`.
- Add a 256-bit AVX2 implementation of the 640-tap polyphase synthesis window (32 output samples per chunk, ten taps) and complex modulation (eight floats per vector; mirrored outputs).
- An experimental fully-unrolled AVX2 window was tested and rejected: on GitHub Actions its median was 28.1 ns/slot versus 28.4 ns/slot for the simpler AVX2 loop (about 1% difference, below a credible cross-CPU improvement). The unrolled object was removed rather than expanding the binary without a justified benefit.
- The two modulator variants share the same precomputed trigonometric tables. AVX2 uses **no FMA**, deliberately keeping multiplication and addition separate to limit rounding differences. Modulation may still differ by a few float ulps because of the reduction tree.
- The public C wrappers check finite inputs, bounds, output overlap and rollback before/after invoking ASM, as before.
- Use `__builtin_cpu_supports("avx2")` on GNU/Clang System V x86-64: this checks processor and OS extended register support. Never execute AVX2 before the check. CPUID is evaluated once per call to the QMF batch wrapper, outside the slot loop. On older x86-64 use SSE2.
- **Intel hybrid P/E dispatch (experimental):** when CPUID leaf 7 EDX bit 15 indicates a hybrid CPU, query the native core type from CPUID leaf 0x1A, EAX bits 31–24, on the *current logical processor*. On type 0x40 (P-core) choose AVX2; on type 0x20 (E-core), or an unknown hybrid type, choose SSE2. The policy reflects local measurements from an Intel Core i7-12700H: AVX2 was 25.6 vs SSE2 31.6 microseconds per 40-slot QMF frame on CPU 4 (P), but AVX2 was 55.9 vs SSE2 51.1 microseconds on CPU 12 (E). This is **not** a universal performance fact about other Intel hybrid models. On homogeneous or other vendors, retain the previous AVX2-if-available behavior. Linux may migrate a thread after the CPUID check, so the selection is a best-effort **performance decision**, never a safety or correctness assumption.
- `-DSAUTI_QMF_FORCE_AVX2` bypasses the P/E preference **only when AVX2 is supported**; `-DSAUTI_QMF_FORCE_SSE2` always selects SSE2. Never enable both flags simultaneously.
- With `-DSAUTI_QMF_FORCE_SSE2`, always choose SSE2 on x86-64. With `-DSAUTI_QMF_DISABLE_ASM`, select the scalar C11 reference code. On Windows x64 the current **System V/ELF NASM kernels are deliberately disabled** in the C wrappers; use the portable C implementation pending a real Windows object/ABI port.

## Differential and regression tests

```sh
make test-aac-qmf-dispatch  # pure P/E policy + live CPU checks
make test-aac-qmf-migration  # 16 frames; automatically alternates available P/E cores
make probe-aac-qmf  # report selected implementation on this CPU
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

## Verify on the i7-12700H laptop

After `git pull --ff-only` on the PR branch, run these read-only probes on
specific Linux logical CPUs from the observed topology:

```sh
taskset -c 4 make probe-aac-qmf
taskset -c 12 make probe-aac-qmf
taskset -c 4 make bench-aac-qmf-frame
taskset -c 12 make bench-aac-qmf-frame
make test-aac-qmf-dispatch test-aac-qmf-frame-parity
```

Expected diagnostics on that machine: CPU 4 reports **P-core → AVX2**;
CPU 12 reports **E-core → SSE2**. If the operating system or hypervisor masks
the hybrid CPUID data, an unknown type is deliberately treated as SSE2. This
probe does not pin threads by itself; `taskset` is for the measurement only.
The normal player does not change thread affinity. Both wrappers recheck the
current core at each 40-slot batch; migration *during* the batch can affect
performance but not the semantics of the selected kernel.

## Multi-frame P/E migration continuity

`make test-aac-qmf-migration` creates two independently compiled
executables, automatic dispatch and forced SSE2. The Linux test inspects the
**currently allowed** CPU affinity mask and searches for one P-core and one
E-core by CPUID (never assumes that CPU numbers 4 or 12 are universal).
When both are accessible, it alternates those cores on consecutive 40-slot
frames for 16 frames, verifying the dispatch preference each time, and
**restores the original affinity mask** before exiting. It does not alter
thread affinity in the real decoder. On homogeneous CI runners or restricted
CPU sets it reports SKIP for migration, but still generates both PCM streams.

The existing PCM comparator now accepts a whole number of 40-slot frames,
rejects different file lengths, and checks **all 40,960 samples** for
finite values and agreement within the established numerical tolerance.
It continues to support the original one-frame test. This covers state
continuity across P/E transitions without claiming identical-bit AVX2
modulation output. The benchmark remains separate from this correctness test.

## Acceptance criteria

Do **not** claim a speedup from source inspection alone. Collect median and tail times per 40-slot frame on the actual CPU (plus full HE-AAC playback under load), verify the intended CPU branch is used, compare pre-PS PCM on real fixtures, and run sanitizer tests. FMA or a DCT/FFT rewrite would be a separate, numerically revalidated change. This branch does not change the SBR envelope/noise/PS reconstruction.

## Bit-identical AVX2 modulation (2026-11-19)

The first AVX2 modulation kernel summed the 64 products per output in 8-lane groups, which changed the low bits relative to SSE2 (the old test allowed 3e-6 relative error). On hybrid Intel CPUs the dispatch is evaluated per batch and picks AVX2 on P-cores and SSE2 on E-cores, so a thread migrating between core types changed individual decoded samples. `test_aac_ps_pcm_stereo` and `test_aac_he_ps_bridge` (which compare calls bit for bit) failed in about 30 % of runs on an i7-12700H when unpinned, and never when pinned to one core, with the SSE2-forced build, or with the C11 build.

The AVX2 kernel now keeps the SSE2 summation tree exactly: `ymm0=[acc0|acc1]`, `ymm1=[acc2|acc3]` (and two more for the imaginary part) so lane j of accumulator a still sums k = 16m+4a+j for m = 0..3 in order, and the reduction repeats (acc0+acc1)+(acc2+acc3), lanes 2,3 into 0,1, lane 1 into lane 0. The window kernel was already bit-identical. `test_aac_qmf_simd` now requires `memcmp == 0` between the SSE2 and AVX2 modulation outputs; the QMF PCM parity gate reports RMS error 0. Result: 0/200 failures of both PS tests unpinned (was ~30 %), full `make test-aac-native` PASS.
