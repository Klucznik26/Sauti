# AAC-M1 PS-5a — native HE-AAC v2 CLI + FFmpeg reference diagnostics

Status: committed on `feature/aac-m1-ps-qmf`; the new CLI and
smoke target have NOT yet been executed on Fedora.

## Tools

- `tools/sauti_he_ps_decode.c`: standalone native Sauti decoder for
  strict ADTS SCE+SBR+PS frames. Never invokes FFmpeg and never
  silently substitutes a different decoder. Emits raw little-endian
  f32 stereo L/R interleaved, **one frame delayed**.
- `tools/sauti_ps5_compare.py`: uses Sauti for native PCM and
  independent FFmpeg for reference PCM. Reports best temporal alignment
  in specified lag window, L/R RMS, peaks, RMSE and correlation.
  It is a diagnostic, **not** a conformance verdict.
- `tools/test_sauti_ps5_native.py`: internally generated three-frame
  ADTS+SBR+PS fixture drives the CLI; verifies two 4096-float
  stereo frames and fail-closed truncated stream/output cleanup.
- `tests/test_aac_he_adts_ps.c --emit-ps-smoke OUT.adts` writes
  the previously passing synthetic SCE/SBR+PS ADTS bitstream.

## Run native smoke

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-m1-ps-qmf
make test-aac-ps5-native PKG_CONFIG=/bin/false
make test-aac-he-adts-ps PKG_CONFIG=/bin/false
make test-aac-sanitize PKG_CONFIG=/bin/false
```

## Compare externally encoded HE-AAC v2 ADTS

Prepare a multi-frame **ADTS HE-AAC v2** input from independent
encoder FDK-AAC (or use existing externally generated HE-AAC v2 file).
The current native Sauti driver only supports a small strict subset
and may reject many otherwise valid frames. A failure is a useful
diagnostic, not grounds to fabricate PCM or make FFmpeg a fallback.

```sh
make probe-he-ps PKG_CONFIG=/bin/false
python3 tools/sauti_ps5_compare.py ~/Sauti-fixtures/he_v2_real.aac \
  --native ./build/sauti_he_ps_decode --rate 48000
```

Needs Fedora `python3-numpy` and `ffmpeg`. Adjust --rate for the
real 2x core output sample rate. The comparator DOES NOT inspect
the actual SBR/PS signaling to infer sample rate. Confirm externally
with ffprobe as needed.

The native decoder rejects malformed ADTS and drops the incomplete
output on failure. It keeps one last frame pending at EOF rather than
faking future QMF slots; therefore output contains one 2048-frame
stereo block less than input ADTS frame count. This is intentional,
and no standard-compliant EOS drain is claimed.

## Remaining gaps

1. Real independent-encoder positive acceptance, with true stereo
   source (non-identical L/R) and PS parameter variations.
2. Normative SBR HF delay, LPC/chirp, FIXVAR temporal envelopes and
   hard cases where strict baseline SBR reconstruction rejects input.
3. Reference quality metrics after correct latency alignment;
   stereo imaging, transients, high-frequency spectra, and phase.
4. EOS flush, container integration, and resilience validation.

**No PS-5 quality/compliance PASS until these independent tests succeed.**

## PS-5b — first independent FDK-AAC fixture (Fedora 44, 2026-11-13)

The independent **full** FDK-AAC `fdkaac -p 29 -b 48000 -f 2`
produced a 12-second 48 kHz stereo ADTS stream from distinct
440 Hz left / 880 Hz right tones. This fixture is held locally
at `~/Sauti-fixtures/he_v2_real.aac` (not checked into Git).

The native PS-5a adapter accepted frames #0..2, then rejected
**frame #3 at byte offset 856**, with
`status=-1 reason=4 (SAUTI_AAC_REASON_UNSUPPORTED_FIL)`.
Extended diagnostics establish:

```
FIL diagnostic: result=-1 reason=4 SBR_parser_entered=1 SBR_stage=5
Temporal SBR trial: result=1 reason=0 entered=1 stage=12
SBR grid: class=1 envelopes=4 env_first=0 env_last=17 noise_first=0 noise_last=17
Temporal SBR plan: deferred_QMF_slots=2 current_env_segments=4 deferred_env_segments=1
```

**Interpretation:** this is a legal FIXVAR SBR grid that ends one
SBR time unit past frame boundary (two QMF slots). The strict
within-frame parser rejects it; the existing temporal SBR parser
correctly accepts the side data. This is **not** a PCM pass: audio
generation must honor original envelope/noise ownership across
neighboring frames. Never clip border 17 to 16 or zero the tail.
The current native converter appropriately fails closed and
does not substitute FFmpeg.

### Implementation target PS-5b temporal HF bridge

1. Retain the true decoded SBR payload and AAC core/QMF history
   across the frame boundary; do not commit partially on rejection.
2. Use `sauti_aac_sbr_temporal_plan`,
   `sauti_aac_sbr_temporal_history_push`, and
   `sauti_aac_sbr_temporal_resolve` to determine original
   envelope/noise ownership per QMF slot (SBR-47/49/52).
3. Extend HF adjustment to apply those **owned** parameters
   before publishing the delayed PS stereo frame; preserve
   the existing 6-QMF-slot PS lookahead independently.
4. Verify strict rollback, zero-fill prohibition, boundary
   transitions, non-identical stereo 440/880 Hz, and quality
   versus FFmpeg. Re-run native and ASan/UBSan tests.
5. If the subsequent source grid fails SBR-52 strict ownership,
   diagnose that separately rather than discarding it.

The independent fixture is now a reproducible **known-failing
regression**, not a decoder conformance result.

## PS-5b continued — streaming side-data ownership audit

New stand-alone command `tools/sauti_ps5_temporal_audit.c` provides
a **native, continuous** ADTS SCE/SBR/PS metadata audit. It uses
`sauti_aac_he_adts_mono_temporal_diagnose_ex` (the real SBR parser)
and existing SBR-47/49/52/53a helpers for per-slot envelope/noise/HF
target attribution. It never publishes PCM or silently clamps
borders, and has no FFmpeg decoder fallback.

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-m1-ps-qmf
make test-aac-ps5-temporal PKG_CONFIG=/bin/false
make probe-he-ps-temporal PKG_CONFIG=/bin/false
./build/sauti_ps5_temporal_audit ~/Sauti-fixtures/he_v2_real.aac
```

The positive/negative smoke test uses generated three-frame ADTS,
not independently encoded reference material. The *separate*
full FDK-AAC fixture remains the decisive test. A failed
`ownership` or `HF target ownership` check is a valuable
new regression finding, not a signal to remove validation.

This audit is a precursor to, **not** an implementation of,
temporal HF adjustment, stereo PCM synthesis, or decoder conformance.
The existing strict audio path remains unchanged.
