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
