# Sayari Sauti — architecture, Stage 5

The Sayari Sauti application is assembly-first and designed for multiple
operating systems, not merely Linux/AyoOS. Its DSP, FFT, statistics,
spectrum, sliding sample history and content renderer are independent of
audio APIs, window systems, allocator and kernel calls. It uses
x86-64 System V calling conventions at present. Windows x64 will require a
Microsoft ABI bridge; non-x86-64 targets require new architecture cores.

## Responsibility boundaries

- **Common NASM core:** tone generator, microphone/monitor sample ring,
  4096-sample rolling window, FFT, dBFS, logarithmic bands, smoothing,
  oscilloscope and spectrum ARGB8888 rasterization.
- **Linux SDL2 adapter:** creates the window, manages resize, input events,
  frame pacing and ARGB8888 texture presentation.
- **Optional PipeWire adapter:** creates a capture stream, negotiates
  stereo F32 at 48 kHz (PipeWire performs graph conversion), bounds-checks
  incoming chunks and calls NASM SPSC downmix.
- **Future AyoOS adapter:** CPL3 Nexa surface, generator first; provide
  audio capture only when an audited Ring 3 source exists.
- **Future Windows adapter:** Win32/WASAPI and Microsoft x64 ABI thunks.
- **Other OS/CPU:** analogous adapters preserving functional UI and DSP
  contracts.

## Live input versus demo

\`sauti_render_demo_frame\` generates two synthetic tones and then uses the
FFT/spectrum pipeline. \`sauti_render_audio_frame\` skips tone generation
and consumes a caller-filled \`scratch->samples\` array instead; its
FFT/dB/log/smoothing and actual renderers are the same. The Linux main loop
chooses between the two based on an explicit source option.

\`sauti_ring_push_stereo_f32\` and \`sauti_ring_drain_window_f32\` are
nonallocating x86-64 SPSC operations on caller-owned data. The producer
only writes the ring, the UI only drains it. Memory ordering relies on
x86 TSO and naturally aligned 32-bit indices. The 16384-frame ring drops
newest samples when full; UI keeps newest 4096 available frames.
Stereo is deliberately reduced to mono (L+R)/2 for the current views.

A \`SautiFrameScratch\` structure contains fixed arrays for input,
FFT, dB, bands and long-lived phases/smoothing (see \`sauti.h\`).
A separate \`SautiAudioRing\` is allocated by the platform adapter.
Neither allocation belongs in a real-time audio callback.

## Acceptance

\`make test\`: numerical host regression, canary checks, ring full/wrap/
invalid inputs, preserving arbitrary user input, live frame without sine
generation. \`make smoke-headless\` verifies generator SDL dummy path.
\`make smoke-headless-pipewire\` validates that the PipeWire-linked binary
starts and renders with generator mode without the server. GitHub Actions
compiles and runs both; it does *not* confirm actual microphone capture,
graph targeting, reconnect/recovery or timing under nested Sayari.

No assumptions of user-visible 60 fps, verified PipeWire target selection,
complete stereo visualization, stable capture API for AyoOS or overall
application completion should be inferred from an automated build.

The design goal remains >=90% NASM of production Sauti source, excluding
Sayari itself, linked external libraries and standalone tests. Because
PipeWire uses a C adapter, the ratio may temporarily fall below target.
Do not inflate ASM merely to make the ratio green.

See docs/PIPEWIRE_CAPTURE.md for usage and limitations, and the parent's
docs/NEXA_APPLICATION_HOSTING_CONTRACT.md for compositor parity.

## Stage 6: audio observability

The independent NASM `sauti_render_wave_scaled_argb32` draws the same caller-owned float32 sample window with a requested display gain clamped to 1..64x. The parent frame renderer calculates *unscaled* peak and RMS with NASM and sets visual gain = clamp(0.85/max(peak,0.01328125), 1,64). It paints two compact P/R level meters with `sauti_draw_levels_argb32`; the bars are based on raw peak/RMS normalized by the existing -90..0 dBFS routine. This ensures a weak TV voice waveform is readable without altering the signal or its spectrum. Both meter bars and visual magnification are independent of SDL2/PipeWire. A regression test checks ordinary/faint amplitude spans, sample immutability, meter positions, boundaries and source mode.
