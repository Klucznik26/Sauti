# Sauti HE-AAC v2 / PS-5 — checkpoint, 2026-11-13

**Status: paused intentionally; no codec change required to resume.**
Repository: `Klucznik26/Sauti`. Working branch: `feature/aac-m1-ps-qmf`.
Last code/test commit before this checkpoint: `b36accc5e9d313fbbee1c21ee4c27bf438582af9`.

## What is implemented and verified

- Native AAC-LC + SBR + PS experimental stereo/PCM diagnostics with temporal ownership of previous/current SBR envelopes, including FIXVAR cross-frame cases.
- Owner-aware limiter bands (`f_tablelim`) and a *single* energy restoration for signal + noise (`energy_balanced`); the earlier stacked gain boost was removed.
- SBR 512-vector ROM noise generator; measured rather than assumed noise power on the Bogini fixture.
- **Experimental** `tonal` mode: owner-resolved SBR harmonic flags at high-band midpoints, a four-phase sinusoid, muted ROM noise at active tonal bins, and tri-component (signal/noise/sine) energy reservation *before* restoration. Still not normative SBR.
- Regression suites PASS on user's Fedora, including `make test-aac-ps5-temporal PKG_CONFIG=/bin/false`. The latter includes a self-test of the new narrowband `tools/sauti_ps_tonal_spectrum.py` audit.
- No decoder implementation was changed in the latest narrowband-audit step.

## Latest real-file measurements (user's Fedora)

Source: `~/Sauti-tools/Bogini_HEv2.aac`; PCM files in `/tmp/sauti_bogini`; reference FFmpeg at 48 kHz, stereo; diagnostic scripts use reference offset `--lag 384` unless overridden.

- Audited `tonal` decode: **3550** ADTS frames, **3549** published PS frames; last pending frame expected. **17,842** active tonal QMF bins over **591** frames; `sine_power=51.5905974`.
- `tonal` HF-power: `signal_power=310707.262`, `rom_noise_power=180622.025`, `peak_gain=4.83671018`.
- Earlier ROM audit on `energy_balanced`: expected noise power `180093.953`; actual `180673.811`; ratio **1.00321976**; energy-identity residual `4.7206413e-08`. No global ROM rescaling justified from this sample.
- Broad 40 ms envelope, median absolute energy error: both `energy_balanced` and `tonal` **1.07 dB at 6–12 kHz**, **1.10 dB at 12–24 kHz**. At 12–24 kHz `tonal` P90 **3.92 dB**, `energy_balanced` P90 **3.89 dB**.
- New 80 ms narrowband FFT audit (FFmpeg-*reference-derived* prominent peaks; NOT SBR-signaled tone locations):
  - 6–12 kHz, **790** peaks per mode: both median frequency error **0 Hz**, P90 **62.5 Hz**, median absolute peak level error **1.58 dB**, median absolute prominence error **1.79 dB**, prominence hit rate **0.606**, adjacent-window peak drift **12.5 Hz**.
  - 12–24 kHz, **858** peaks per mode: both median frequency error **0 Hz**, P90 **62.5 Hz**, median absolute level error **1.49 dB**, median absolute prominence error **2.06 dB**, adjacent drift **12.5 Hz**. Prominence hit rate **0.576** for `energy_balanced`, **0.577** for `tonal`.
- **Interpretation:** tri-component energy accounting behaves coherently, but no convincing measurable spectral improvement over `energy_balanced` yet. Do not claim HE-AAC v2 normative conformance.

## Where to resume — next work session

**Primary task: bitstream-guided harmonic audit**, not another global gain/noise tweak.

1. Export a per-QMF-slot and bin time map of *active* SBR `harmonic` flags from `sauti_aac_sbr_temporal_hf_targets` / `slot->harmonic[k]`, including previous/current owner, envelope index, transient-onset semantics and cross-frame continuity.
2. Check the mapping from QMF slots/bins to output PCM time/frequency (PS delay, 48 kHz sample domain, existing FFmpeg alignment). Avoid assuming an FFT reference peak is necessarily an SBR harmonic.
3. Measure the local PCM spectral energy/prominence of `tonal` vs `energy_balanced` vs FFmpeg **only around bitstream-signaled active harmonic bins/times**, with matched control regions where no harmonic is signaled. Report coverage, errors and peak stability.
4. Specifically audit four-phase oscillator index/continuity at transitions and frame boundaries. Current `tonal` uses `phase=t&3` in the preview; verify alignment rather than assuming correctness.
5. Only after evidence, refine normative harmonic/noise/gain allocation and smoothing, independently validate on several additional clips. Preserve working `energy_balanced` mode as regression reference.

## Resume commands

```bash
cd ~/Sauti
git pull --ff-only origin feature/aac-m1-ps-qmf
make test-aac-ps5-temporal PKG_CONFIG=/bin/false

python3 tools/sauti_ps_tonal_spectrum.py \
  ~/Sauti-tools/Bogini_HEv2.aac \
  --pcm-dir /tmp/sauti_bogini

python3 tools/sauti_ps_music_hf_envelope.py \
  ~/Sauti-tools/Bogini_HEv2.aac \
  --pcm-dir /tmp/sauti_bogini
```

If temporary PCM files are gone, regenerate (the input fixture stays under `~/Sauti-tools`):

```bash
mkdir -p /tmp/sauti_bogini
./build/sauti_ps5_temporal_audit ~/Sauti-tools/Bogini_HEv2.aac \
  --experimental-pcm-energy-balanced /tmp/sauti_bogini/energy_balanced.f32le \
  2>/tmp/sauti_energy_balanced.log
./build/sauti_ps5_temporal_audit ~/Sauti-tools/Bogini_HEv2.aac \
  --experimental-pcm-tonal /tmp/sauti_bogini/tonal.f32le \
  2>/tmp/sauti_tonal.log
```

**Do not** mark the HE-AAC v2 decoder finished. The latest PS-5 work is an experimental, testable HF reconstruction preview; normative smoothing, harmonic phase/temporal correctness, and multi-fixture validation remain open.
