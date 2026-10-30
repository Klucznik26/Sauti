# Sayari Sauti — Stage 5 PipeWire capture (Linux adapter)

## Source modes

- \`--source generator\` (default): pure ASM-generated tones; works without
  PipeWire/physical audio and is used for CI and future AyoOS bring-up.
- \`--source mic\`: system default microphone/audio input through PipeWire.
- \`--source monitor\`: PipeWire capture of sink/output monitor ports.
- \`--target NODE\`: optional explicit PipeWire target for mic/monitor,
  passed as the PipeWire \`target.object\` property. Default graph routing
  otherwise applies. Not all policies will permit a requested target.

Audio modes require optional \`make window-pipewire\` binary. The base
\`make window\` does **not** depend on PipeWire and rejects audio requests
with exit code 2. Capture is not silently replaced by demo/generated audio.

Commands on Fedora (x86-64, session with running PipeWire and Wayland):

    sudo dnf install nasm gcc make SDL2-devel pipewire-devel pkgconf-pkg-config
    cd apps/sayari-sauti
    make test
    make window-pipewire
    SDL_VIDEODRIVER=wayland ./build/sauti_window_pw --source monitor
    SDL_VIDEODRIVER=wayland ./build/sauti_window_pw --source mic
    SDL_VIDEODRIVER=wayland ./build/sauti_window_pw --source mic --target NAME

For CI/headless **build acceptance only**:

    make smoke-headless
    make smoke-headless-pipewire

\`smoke-headless-pipewire\` uses \`--source generator\` to check link/start/
presentation, and is **NOT proof that PipeWire can capture real devices**.
A proper live-audio acceptance requires a running user audio daemon,
connected stream and real source on host equipment.

## Data flow / lock policy

PipeWire negotiates 48kHz stereo float32 interleaved frames and may convert
the physical source in the graph. The callback checks buffer bounds, layout,
stride and alignment and then calls just one ASM operation to downmix and
publish samples into a **single-producer/single-consumer** ring.

The callback never allocates, blocks on a mutex, logs, performs FFT or
invokes SDL. The UI thread reads recently captured mono frames into a
persistent 4096-sample window using ASM and then calls the same ASM
FFT/dB/log/smoothing/render pipeline as generator mode. When no samples
have arrived, the initialized window is silence (there is no fake signal).

Ring: 16384 float32 mono samples (64 KiB + 8-byte indices), x86-64 TSO
single-producer/single-consumer only. Producer drops newest input when
full; consumer on each frame discards stale backlog and keeps the
newest 4096 samples. All indices are 32-bit unsigned and wrap correctly
when occupancy is <= 16384. This is intentionally x86-specific; a future
ARM64 implementation needs acquire/release atomics suitable for AArch64.

The audio source is presently *mono* after averaging two channels.
The UI FFT operates at a fixed N=4096 and cannot yet distinguish stereo
phase or display accurate axis tick labels.

## Limits and follow-up

- CI has no real PipeWire server/monitor; capture must be accepted manually
  on Fedora, including source selection and disconnect/reconnect.
- The generator's fixed per-frame smoothing still affects audio input;
  silence after source disappearance should gain timeout/decay handling.
- Real stream ERROR stops execution rather than silently displaying demo data.
- Real-time callback is intentionally strict: non-stereo/interleaved or
  unsupported stride buffers are dropped; the requested SPA format is F32,
  stereo 48kHz. Resampling occurs in PipeWire graph, not in NASM.
- AyoOS and Windows get **separate audio adapters**, not a clone of PipeWire
  code. Shared DSP and pixel renderer remain operating-system-independent.
- These tests do not measure end-to-end audio latency, capture permission UX,
  CPU utilization or AVX2 performance.

## Fedora diagnostics: output routing and graphics

The `--source monitor` default may rely on session policy routing. If the
active analog sink and a saved default HDMI sink differ, first inspect the
live node via `wpctl inspect <active sink ID>` without changing defaults.
The existing `--target NODE` option passes the PipeWire object name/serial
for explicit routing, when a deliberate live retest is approved.

SDL2 software rendering is now default because the previous accelerated
renderer was correlated with desktop-wide flicker on one Intel/NVIDIA laptop.
The exact cause has **not** been isolated. `--accelerated` is opt-in only;
no GPU driver, compositor policy, kernel or global PipeWire setting is
modified by this change. The CLI prints the actual SDL renderer, plus
PipeWire stream-state transitions from the UI thread (never the audio RT
callback). These diagnostics are not evidence of a successful hardware
capture by themselves.
