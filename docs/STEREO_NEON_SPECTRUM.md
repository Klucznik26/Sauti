# Sauti — true-stereo segmented neon spectrum (implementation contract)

Status: **approved design; implementation pending**. This document is a contract, not a claim that the existing renderer already supports stereo bars.

## User-approved visual design

- Two *independent* frequency spectra, 12 logarithmic bands for **LEFT** and 12 for **RIGHT**, with a visible 12–16 px central gutter at a 1024 px window, scaled for narrower windows.
- Each vertical bar has **20 discrete rectangular segments** (numbered bottom-up 0..19).
- Segments: 0 amber (`#FFBF40`); 1–8 emerald (`#20C997`, Ayo eye palette); 9–14 sapphire (`#327DF4`); 15–19 ruby (`#E73D65`).
- Unlit: muted violet graphite (`#272331`), optional border (`#363145`); background `#101016`.
- Active segments retain crisp, readable cores and carry a **subtle neon halo in their own color**. Independent per-segment pulsing should affect *halo intensity*, not the core, avoiding synchronized flashing. Each band may hold a color-matched glowing peak marker.
- Pulse phase is based on elapsed time, not frame count. Frequency and brightness must be bounded, no visible full-screen flicker. Pulse must stop completely for unlit segments and for silence.

## Data integrity — no fake stereo

The current `sauti_ring_push_stereo_f32` averages L+R into one mono sample. It **cannot** feed a true stereo analyzer. Do not feed the same mono spectrum to both halves.

Plan:
1. Introduce a *new* stereo SPSC ring ABI (interleaved L/R or two channel arrays) rather than silently changing the existing mono ABI. Add unit tests for L-only, R-only, antiphase L/R, overrun and wraparound. Preserve real-time constraints: no allocations/locks/FFT in PipeWire callbacks.
2. Maintain independent 4096-sample histories for L and R; compute separate FFT → dBFS → logarithmic bands → attack/release, reusing existing ASM DSP functions. Keep persistent smoothing and peak state per channel.
3. Downsample the existing 256 log bands to 12 **per channel** using a defined frequency mapping. Do not assign the first 12 elements of the 256-band array to the entire display.
4. Add a dedicated OS-independent NASM ARGB8888 renderer for a dual 12-band / 20-segment canvas. Draw clipped pixel rectangles with bounded halos; prefer precomputed palette/phase tables to costly per-pixel blur. Keep oscilloscope as a separate option.
5. Add `--view scope|stereo-bars` (default retains existing scope); test generator and PipeWire paths without requiring a physical screen.

## Safety and validation

- Preserve current Linux software SDL renderer default. Previous hybrid Intel/NVIDIA desktop flicker is not cleared: **do not run on the real Wayland screen automatically**.
- Run `make test` and headless `SDL_VIDEODRIVER=dummy` smoke tests after integration; add deterministic pixel tests for segment positions, colors, gutter, pulse boundedness, clipping and silence.
- Test synthetic L-only/R-only and antiphase input to prove channel independence; show a silent channel as unlit graphite rather than manufactured activity.
- No changes to AyoOS ring policy; DSP remains operating-system independent, adapters stay in platform directories, normal user-space privileges only.
- In `--view scope` mode, preserve the present waveform, measured Peak/RMS, and original spectrum without regressions.

## Acceptance criteria

- L-only signal illuminates **only L**; R-only illuminates **only R**; phase-canceling stereo remains visible on both sides.
- The central gutter remains empty and clearly separates channels.
- Segment colors/counts match the palette; active halos gently pulse, inactive ones never glow.
- Continuous audio shows stable peak hold and frame-rate-independent attenuation; silence decays to off.
- Builds, unit tests and dummy SDL smoke tests pass before offering live graphics testing.
