# Sauti — assembly-first multiplatform audio visualization

**Stage 6: automatic oscilloscope gain and on-screen Peak/RMS meters in ASM.**
The core remains independent of PipeWire, SDL2, Wayland and operating-system
calls. There is no native AyoOS/Windows audio adapter yet.

## Creative VOC (initial PCM support)

The native decoder accepts Creative Voice File blocks 1/2 (8-bit PCM),
3 (silence), 8 (extended mono/stereo PCM), and 9 (8-bit unsigned or
16-bit signed little-endian PCM). It supports multiple chunks and seeking.
Files with Creative ADPCM, repeat blocks (6/7), unknown audio codecs,
incompatible sample rates, or invalid/truncated headers are rejected
instead of guessed. The Linux audio browser accepts .voc files.
Run `make test-voc` for the bounded parser/PCM regression.

## AAC, Ogg, WebM, ALAC and AIFF

- Ogg Vorbis and Ogg Opus were already supported (Opus requires opusfile).
- Native AIFF / AIFF-C uncompressed signed PCM 8/16/24/32-bit decoder:
  AIFF big-endian and AIFC `sowt` little-endian. Unknown compression
  is rejected.
- Optional *in-process* FFmpeg adapter: ADTS AAC (`.aac`), AAC or ALAC
  in MP4/M4A (`.m4a`, including MP4 renamed to `.alac`), and WebM with
  Opus/Vorbis (`.webm`). Content and codec are checked, not just the suffix.
- Fedora packages: `sudo dnf install ffmpeg-free-devel`, then rebuild
  with `make clean && make test && make window-pipewire`.
- Test via `make test-aiff`, `make test-ffmpeg`, `make test-container-formats`.
  The FFmpeg command-line utility is only used to *generate tests*, not
  to play music.
- The initial FFmpeg decoder buffers PCM at open, up to 512 MiB of float
  samples. Long recordings may not load until streaming support is added.
- Without FFmpeg development files Sauti still compiles; AAC/ALAC/WebM
  adapters remain disabled.

## Features available

- NASM auto-gain (1..64x, visual only), on-screen Peak/RMS dBFS bars.
- NASM oscillator, float32 FFT 64..4096, spectrum, dBFS, log-frequency bands,
  attack/release envelope, oscilloscope and dual-pane ARGB8888 rendering.
- Linux SDL2 window (Wayland backend), resizing, event loop, Esc/close.
- In-process SDL_mixer MP3 on the stereo-bars deck: PLAY/STOP/MUTE, REPEAT,
  SHUFFLE, sequential PREV/NEXT in the loaded track folder, FOLDER browser.
- **Optional PipeWire capture**: mic and playback-monitor modes.
- **OS-independent ASM SPSC ring**: RT stereo-to-mono, sample history/overrun
  handling, no audio thread allocation, SDL or FFT.
- Separate ASM live renderer uses *captured samples*, never fake tones.
- Existing generator mode still runs with SDL2 alone, no audio server.
- Automated FFT/renderer/ring unit tests, CI sanitizer harnesses, SDL dummy
  window start/present smoke tests.

## Fedora x86-64 install/build

    sudo dnf install nasm gcc make SDL2-devel pipewire-devel pkgconf-pkg-config
    git clone https://github.com/Klucznik26/Sauti.git
    cd Sauti
    make test
    make window-pipewire
    SDL_VIDEODRIVER=wayland ./build/sauti_window_pw

The PipeWire build opens the full `stereo-bars` player by default. The old
oscilloscope remains available with `--view scope`; `--source generator`
also selects the diagnostic scope automatically. The non-PipeWire `window`
build continues to use the generator scope by default.

For microphone:

    SDL_VIDEODRIVER=wayland ./build/sauti_window_pw --source mic

You may select an explicit PipeWire node with \`--target NODE\`. For a
generator-only build: \`make window\` and \`make run-wayland\`.

Tests without a display/audio daemon:

    make smoke-headless
    make smoke-headless-pipewire

The second smoke test intentionally uses generator mode: it tests **linkage
and rendering**, not capture or monitor selection.

## Architecture commitment

- At least **90% hand-written ASM** of *production source* is the project
  **goal**, counting adapters but excluding existing Sayari/compositor code,
  linked third-party libraries and test harnesses. It must be measured, not
  implied from compilation or inflated artificially.
- Sauti owns its content UI; Sayari/Nexa owns the outer frame/window policy.
- Linux: SDL2 + optional PipeWire. AyoOS: planned CPL3 Nexa + future audio
  capture API. Windows: future Win32/WASAPI with Microsoft x64 ABI bridge.
- Later CPU architectures get their own DSP/renderer implementations.
- No Ring 0 privilege exception for Sauti.

## Current limitations

Capture has only been **compiled and structurally tested in CI** unless
a real user PipeWire session has been tested separately. CI uses no real
microphone or sink-monitor. Native AyoOS and Windows ports are planned.
There is not yet stereo Lissajous, spectrogram, time-normalized smoothing,
log-frequency axis labels or AVX benchmarking.

Details: [PipeWire capture](docs/PIPEWIRE_CAPTURE.md),
[core/platform architecture](docs/ARCHITECTURE.md).

## Stage 6: low-volume speech visibility

The oscilloscope automatically scales low-amplitude voice/music by up to 64x, aiming at 85% of available half-height. It does NOT amplify, overwrite or otherwise affect the input samples, RMS/Peak measurement, or FFT/dB spectrum. The existing unscaled renderer remains callable. The new P (ruby/Peak) and R (emerald/RMS) meter bars in the upper-left use values normalized over -90..0 dBFS; they represent original input levels regardless of zoom. Unit regression checks the visible height of -32 dBFS test speech-like signals, meter colors, invalid sizes and untouched buffers.

Limitations: automatic gain currently responds immediately to the peak measured in the last 4096 samples; it has no display-gain smoothing or adjustable gain limit. On-screen numbers and calibrated frequency ticks are later UI work.

## Display troubleshooting (Fedora hybrid Intel/NVIDIA)

The SDL2 **software renderer is now the default**. This only affects the
application presentation path; the compositor itself may still use the GPU.
The opt-in `--accelerated` flag restores the previous GPU/vsync SDL2
renderer, but should NOT be used on machines that showed desktop-wide
flicker. `--software` explicitly selects the default safe renderer.

Do not retry live display until the display problem is diagnosed. Offline
`make test` and CI's `SDL_VIDEODRIVER=dummy` exercise the core without
accessing your physical screen. If a later supervised test is appropriate,
try a limited `--frames` run, not an unlimited test.

In PipeWire logs, an unavailable sink-monitor link can cause empty capture.
Inspect `wpctl status` and `wpctl inspect <ID>` (read-only) to find the
actual output sink and its `node.name`/serial. Select the corresponding
PipeWire sink explicitly with `--target NAME`; never assume the configured
HDMI sink is the currently routed analog output. Real capture and desktop
flicker are separate issues until demonstrated otherwise.

## Independent repository

Sauti is developed independently at [Klucznik26/Sauti](https://github.com/Klucznik26/Sauti).
The original, byte-identical source snapshot was extracted from
[Sayari-for-Linux](https://github.com/Klucznik26/Sayari-for-Linux),
branch `feature/sauti-stereo-neon-spectrum`, directory `apps/sayari-sauti/`.
Previous project history remains available on that archived source branch.

