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

## Listen-test triage: real PCM contains audible noise

The experimental preview successfully emitted 284 stereo blocks,
but the user's listening test reports **strange noise**, not
clearly isolated 440 Hz left / 880 Hz right tones. Output presence
must NOT be confused with audio fidelity.

`tools/sauti_ps5_compare.py` now accepts a pre-existing
`--pcm` file and calculates two independent kinds of diagnostics:
(1) reference-aligned RMS, peak, RMSE and channel correlations;
(2) 440/880 Hz tone fractions and channel isolation for the
independently generated dual-tone FDK test only.

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-m1-ps-qmf
python3 tools/sauti_ps5_compare.py \
  ~/Sauti-fixtures/he_v2_real.aac \
  --pcm /tmp/sauti_ps_preview.f32le --rate 48000
```

Requires python3-numpy and a working FFmpeg decoder as **reference
only**. This command does not alter the native PCM file or
invoke the native decoder again. Poor correlations or poor tone
separation are a diagnostic FAIL in sound quality, irrespective
of successful parser/ROM/QMF/PS smoke tests. Next work should
locate which stage dominates the noise rather than inflate the
number of substage PASS markers.

## Audio fault isolation: separate ROM noise, HF amplification and PS

The independently decoded reference is clean in **both** the native
FFmpeg AAC decoder and Fraunhofer libfdk_aac. The native experimental
preview is noisy; the existing diagnostic measured RMS ~0.548/0.546
versus reference ~0.087/0.088, global correlation ~0.039,
and unexpected 880 Hz right-tone isolation of -46.4 dB.
These are **fidelity failures**, not syntax failures.

The temporal audit now has three **opt-in PCM isolation modes**:
- `--experimental-pcm`: previous experimental HF signal gain + ROM noise.
- `--experimental-pcm-no-noise`: same provisional HF gains, NO ROM
  contribution. ROM index is still advanced to avoid phase-history
  differences unrelated to the noise toggle.
- `--experimental-pcm-low-only`: keep the native lower QMF bands,
  zero ALL high-band samples; SAME PS stereo bridge and timing.

Run the tests and all three previews with the reference comparison:

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-m1-ps-qmf
make test-aac-ps5-temporal PKG_CONFIG=/bin/false
python3 tools/sauti_ps_isolate.py ~/Sauti-fixtures/he_v2_real.aac
```

Files are saved as `/tmp/sauti_ps_isolation/full.f32le`,
`no_noise.f32le` and `low_only.f32le` and are compared against
FFmpeg using tone fractions, channel isolation, peak and RMS.
The user can listen to any with `ffplay -hide_banner -f f32le
-ar 48000 -ch_layout stereo /tmp/sauti_ps_isolation/low_only.f32le`.

Interpretation:
- Low-only is much cleaner than no-noise => HF generated signal
  gain or transposition already corrupts audio.
- No-noise much cleaner than full => added SBR ROM noise dominates.
- Even low-only has wrong 440/880 channel dominance => investigate
  QMF/PS timing and stereo parameter rendering independent of HF.
- All modes compared against the same clean FDK fixture; NO mode is
  ISO-compliant SBR or an audio-fidelity PASS.

## Confirmed PS matrix channel-wiring defect — native ASM mixer repair

The independent FDK frame #4 has both extreme positive IID
(+15) and extreme negative IID (-15) in different bands.
Sauti matrix coefficients at negative IID include a large
`h12` but a very small `h21` (e.g., bin 6 h12 ~= 1.4142,
h21 ~= 0.00447). The previous NASM PS mixer used the WRONG
coefficient axis:

- **BUG:** left = h11*direct + h12*decorrelated;
  right = h21*direct + h22*decorrelated.
- **FIX:** left = h11*direct + h21*decorrelated;
  right = h12*direct + h22*decorrelated.

This matches the mixing layout in reference PS implementations;
both real and complex NASM kernels are corrected. A deliberately
asymmetric {1,2,3,4} regression now asserts this mapping, because
a symmetric test matrix could hide the channel swap. The source
of the near-silent right channel in the former low-only preview
is now specifically explained by incorrect use of small h21
instead of large h12 on extreme negative-IID bands.

Run with a clean rebuild to ensure the modified assembly object
is actually reassembled:

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-m1-ps-qmf
make test-aac-ps-mix test-aac-he-adts-ps PKG_CONFIG=/bin/false
make test-aac-ps5-temporal PKG_CONFIG=/bin/false
python3 tools/sauti_ps_isolate.py ~/Sauti-fixtures/he_v2_real.aac
```

Check whether right-channel 880 Hz isolation changes SIGN and
the low-only right-channel RMS rises from ~0.00086 toward the
reference ~0.088. No claim of passing audio quality until the
independent Fedora run; full SBR HF generation is still a
separate unresolved fidelity problem.

## Follow-up after PS wiring correction: separate HF transpose from amplification

Fedora result after correcting PS mixer (`f261468`) on the real
FDK stereo sine fixture:

- `low_only` reproduces reference with **global correlation 0.999994**
  and RMSE ~2.1e-5 per channel; 440/880 stereo separation is correct.
- `no_noise` drops to correlation ~0.118 with RMS ~0.45 per channel.
- `full` drops further to ~0.076, RMS ~0.54–0.55.
- This is evidence of an **HF rendering** problem, not an
  outstanding stereo matrix/channel-routing failure.

Before wiring up the existing SBR limiter, isolate the raw HF
transpose itself. The production limiter API consumes whole
in-frame envelope boundaries, whereas the real ADTS FIXVAR
cross-frame envelope can straddle a frame boundary. Applying
the limiter on a truncated envelope would be incorrect.

New `raw_hf` mode outputs transposed high-QMF samples without
any provisional gain or noise. The existing `low_only` mode
remains the clean baseline, while `no_noise` isolates gain.

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-m1-ps-qmf
make test-aac-ps5-temporal PKG_CONFIG=/bin/false
python3 tools/sauti_ps_isolate.py ~/Sauti-fixtures/he_v2_real.aac
```

The comparison now prints **four** variants (full/no_noise/raw_hf/low_only)
and `HF-POWER` raw, signal and ROM-noise QMF powers plus maximum
applied gain. Interpret:
- `raw_hf` close to reference but `no_noise` poor => the
  provisional energy/gain model blows up HF.
- `raw_hf` poor => transposition, HF geometry/QMF synthesis or
  implicit frequency scaling needs investigation before limiter.
- `low_only` remains clean => do not change the fixed PS
  mixing until new independent evidence justifies it.

This is a fault-isolation tool, NOT a claim that SBR synthesis is
complete or that these preview modes are standard-compliant.

## SBR output spectrum and QMF-unit hypothesis (real FDK 440/880 fixture)

Fedora spectrum audit after fixing PS:
- `low_only`: correct LF 440/880; correlation **0.999994**.
- `raw_hf`: 6–12 kHz excess +66.8/+78.0 dB versus the
  extremely low reference energy of the pure-tone fixture.
- `no_noise`: 6–12 kHz +84.1/+86.7 dB; 12–24 kHz +89.6/+89.4 dB.
- `full`: 12–24 kHz +90.8/+90.8 dB.
- In provisional `no_noise`, HF signal energy is amplified
  **74.34x**, with a peak gain of **100000**; ROM is NOT its
  only cause. Tone percentages previously shown were confined to
  the 200–2000 Hz window and could not expose this HF failure.

Reference code inspection: FFmpeg's floating-point SBR QMF path
scales AAC core PCM to an internal 16-bit amplitude convention,
then scales the synthesized PCM back down. Sauti's floating QMF
path instead directly consumes normalized PCM. Both projects use
the `exp2(q*alpha+6)` form for uncoupled envelope dequantization.
Therefore the 32768 amplitude-domain mismatch is a **testable
hypothesis**, not a verified complete repair.

The optional `--experimental-pcm-unit-scale` preview scales
provisional envelope gain and ROM amplitude by 1/32768 during
QMF synthesis; it does NOT modify normative dequantization,
production decoder, or existing `low_only` behavior. Its
5-way comparison includes full / no_noise / unit_scale /
raw_hf / low_only:

```sh
cd ~/Sauti
git pull --ff-only origin feature/aac-m1-ps-qmf
make test-aac-ps5-temporal PKG_CONFIG=/bin/false
python3 tools/sauti_ps_isolate.py ~/Sauti-fixtures/he_v2_real.aac
```

**Interpretation requires caution:** the clean pure-tone reference
has virtually no legitimate HF content; simply reducing all
HF will make this fixture look better and does not establish
correct SBR on speech, music or HF-rich samples. A normative
limiter/temporal envelope integration and at least one broadband
test fixture are still required before claiming HE-AAC v2 fidelity.

References for unit convention / dequant formula:
- FFmpeg `libavcodec/aacsbr_template.c` (`ff_aac_sbr_ctx_alloc_init` QMF scale)
- FFmpeg `libavcodec/aacsbr.c` (`sbr_dequant`).
