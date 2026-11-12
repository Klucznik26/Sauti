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

## PS-5c1 — first real DSP join (2026-11-13)

`sauti_aac_sbr_qmf_frame_export_raw` is a new **transactional**
32x64 raw complex QMF export. It consumes exactly 1024 samples of
genuine native AAC-LC PCM per frame; reuses SBR-27 analysis and
SBR-12 patch transposition with persistent analysis and low-band
history, without invoking the SBR-24 HF ROM adjustment. No arbitrary
clipping of time borders, no synthesized future samples and no PCM
output. Existing adjusted-QMF/PS APIs are unchanged.

`sauti_ps5_temporal_audit` now executes this raw DSP for **every
frame** and validates that real raw high-bin complex coefficients
can be paired with the **original owner's** SBR-53a energy/noise
targets, including the two inherited slots at FDK frames #3/#4.
The output is still diagnostic only: the temporal-owned HF limiter,
smoothing, noise, sine generation, and final stereo synthesis are
**not implemented** by this interface.

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-m1-ps-qmf
make test-aac-ps5-temporal PKG_CONFIG=/bin/false
./build/sauti_ps5_temporal_audit ~/Sauti-fixtures/he_v2_real.aac
make test-aac-he-adts-ps PKG_CONFIG=/bin/false
```

This step requires actual Fedora validation. In particular, a failure
at `raw QMF stage=2` (history) or `stage=4` (transpose) is a
useful blocker for the next phase, not a reason to bypass validation.
Next PS-5c2: temporal-owner-aware HF adjustment with independent
SBR/PS delay and phase continuity and reference PCM measurements.

## PS-5c2 — temporally owned HF gain diagnostics (not PCM)

New `sauti_aac_ps5_hf_gain_analyze` accepts each **original-owner**
SBR-53a energy/noise/harmonic target and real pre-adjusted raw
complex QMF. It produces a bounded **diagnostic gain estimate**
for every HF bin of every 32-slot frame and records missing
source energy instead of inventing signal. It stages all results
and does not mutate the raw QMF or output audible PCM.

It intentionally is NOT the normative SBR algorithm: it uses
**per-slot** measured raw power rather than time-averaged SBR
envelope powers and does not yet apply frequency limiter groups,
ROM noise, harmonics, smoothing, chirp or phase management. The
computed gains are a diagnostic step for testing data flow, **not**
authorized coefficients to use as decoded PCM.

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-m1-ps-qmf
make test-aac-ps5-hf-gain PKG_CONFIG=/bin/false
make test-aac-ps5-temporal PKG_CONFIG=/bin/false
./build/sauti_ps5_temporal_audit ~/Sauti-fixtures/he_v2_real.aac
make test-aac-he-adts-ps PKG_CONFIG=/bin/false
```

The real FDK fixture should still report 285 frames and
FIXVAR/VARFIX at #3/#4 and now additionally a separate
`PASS PS-5c2 diagnostic` containing inherited slot count,
missing raw source count and target/raw power. Any newly failing
test must be investigated; do not extrapolate PCM compliance.

Next: envelope-averaged HF limiter and source-aligned cross-frame
noise/harmonic state; only then integrate stereo rendering.

## PS-5c3 — envelope-mean and noise budget preflight

A new independent `sauti_aac_ps5_hf_envelope_analyze` groups
raw complex QMF **by original side-data owner (previous/current),
original envelope index and QMF bin**. Unlike the former PS-5c2
per-slot diagnostic, this measures mean raw power over each
represented envelope group. Two inherited QMF slots therefore
retain the previous owner's target; they are not reassigned to
the incoming VARFIX envelope.

It partitions each target energy provisionally into signal
`E/(1+Q)` and noise `E - E/(1+Q)`, counts harmonic marks and
zero-source cases, and reports finite bounded provisional gain
estimates. All data are validated before committing the report.
This **is not the final normative SBR adjustment**: the complete
energy support of a cross-frame envelope, spectral band limiter,
noise ROM, harmonic injection, smoothing and predictor state are
not yet included. The output is *not* fed into audible stereo
PCM and must not be treated as decoded samples.

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-m1-ps-qmf
make test-aac-ps5-hf-envelope PKG_CONFIG=/bin/false
make test-aac-ps5-temporal PKG_CONFIG=/bin/false
./build/sauti_ps5_temporal_audit ~/Sauti-fixtures/he_v2_real.aac
make test-aac-he-adts-ps PKG_CONFIG=/bin/false
```

Expected on independent FDK input: the existing PS-5b and PS-5c2
audits continue to pass over 285 frames, followed by a PS-5c3
budget diagnostic. Results need a Fedora test run; no claim
of audio conformance is made.

Next: actual cross-frame envelope power window (not only its
current-frame support), normative limiter/noise/sine and output
QMF integration with independently validated PS history.

## PS-5c4a — genuine adjacent-frame envelope window

The new `sauti_aac_ps5_cross_envelope_analyze` is a **diagnostic**
real-QMF mean-power calculation for the complete FIXVAR final
envelope (its support may extend from previous frame into the
current VARFIX frame). Uses original previous-frame envelope
start/end, the carried SBR-52/SBR-53a energy/noise ownership,
and both adjacent raw QMF buffers. The function checks inherited
slot indices, original target consistency, finite complex QMF,
and does not change caller output on any invalid input.

For the independent FDK-AAC fixture, frame #3 FIXVAR ends at
QMF slot 34 and frame #4 VARFIX begins at slot 2: the audit
should now emit `CROSS frame=4 ... inherited=2 ...`, and a
PS-5c4a pass summary.

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-m1-ps-qmf
make test-aac-ps5-cross PKG_CONFIG=/bin/false
make test-aac-ps5-temporal PKG_CONFIG=/bin/false
./build/sauti_ps5_temporal_audit ~/Sauti-fixtures/he_v2_real.aac
make test-aac-he-adts-ps PKG_CONFIG=/bin/false
```

**Important remaining fidelity gap:** raw high-frequency
transposition is still generated using the **current** SBR patch
mapping for the new frame. Full normative cross-frame rendering
must validate and, where appropriate, retain the prior owner's
HF generation geometry, LPC/chirp, limiter, smoothing history,
ROM noise index, sine phase and PS delay. A passing temporal
mean-power audit is **not** proof of these stages or audible PCM.

## PS-5 geometry bridge — first original-owner DSP reconstruction

The original native QMF driver has now gained
`sauti_aac_sbr_qmf_frame_export_raw_owned`. Unlike the earlier
metadata-only ownership audit, this function performs **actual
pre-adjustment HF transposition using the previous frame's SBR patch
geometry** on the inherited current-frame time slots (the FDK
FIXVAR->VARFIX handoff is two QMF slots). Remaining current-frame
slots retain the current patch map. The method preserves every
low-frequency analysis source bin until both maps have been
applied; analysis/history updates are transactional and performed
exactly once per frame. The original raw export API is retained
as a zero-inheritance wrapper.

The real-stream diagnostic now calls the owned-geometry export,
rather than applying only the new frame's patch map to its
inherited time slots. A new synthetic regression uses **two
different, independently valid SBR maps** and checks each complex
QMF bin against a separate analysis reference, as well as preserving
output/state on invalid ownership, a missing previous map and NaNs.

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-m1-ps-qmf
make test-aac-ps-owned-geometry PKG_CONFIG=/bin/false
make test-aac-ps5-temporal PKG_CONFIG=/bin/false
./build/sauti_ps5_temporal_audit ~/Sauti-fixtures/he_v2_real.aac
make test-aac-he-adts-ps PKG_CONFIG=/bin/false
```

**Important:** this is a substantial data-path DSP correction,
not completed HE-AAC v2 PCM. Full SBR HF regeneration still
requires temporal-owner-aware LPC/chirp, limiter, ROM noise,
harmonic injection, smoothing, synthesis and PS histories.
No artificial QMF tail, skipped side data, or FFmpeg dependency
was introduced. The real FDK test requires a Fedora rerun.

## Native experimental stereo preview — first full real-stream PCM route

The previously audit-only executable `sauti_ps5_temporal_audit`
now supports an **explicit opt-in experimental stereo output**:

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-m1-ps-qmf
make test-aac-ps5-temporal PKG_CONFIG=/bin/false
./build/sauti_ps5_temporal_audit \
    ~/Sauti-fixtures/he_v2_real.aac \
    --experimental-pcm /tmp/sauti_ps_preview.f32le
```

It reads the *real* stream through the temporal SBR parser,
constructs owner-aware 32x64 raw QMF, and for each HF slot
uses the PS-5c3 provisional envelope gain and decoded SBR-53a
noise target with the native SBR-23 complex noise ROM. It then
passes the complex QMF into the existing six-slot-lookahead
**native stereo PS bridge**. No FFmpeg component is called.
The first input frame is buffered and the final input frame
remains pending at EOS; on any error the incomplete output
is removed. The default one-argument mode remains audit-only.

**WARNING: This is a non-conformant listening/diagnostic
preview, not a finished AAC decoder.** Noise amplitudes are
not the normative SBR limiter output. This bridge does NOT
implement full temporal-owner limiter groups, coefficient
smoothing, SBR harmonic sine injection or LPC/chirp predictor,
and its provisional gains may be audibly incorrect. **Do not
ship or substitute it for the strict decoder.** The existing
`sauti_he_ps_decode` production/strict experiment remains
unchanged.

Expected output for FDK input of 285 valid ADTS frames:
284 delayed stereo blocks, each 4096 float32 interleaved values
(2 channels x 2048 PCM samples), totaling 4,653,056 bytes.
This is a test expectation, not an observed pass.
Compare the resulting audio with FFmpeg by inspection and
numerical quality diagnostics before any promotion.
