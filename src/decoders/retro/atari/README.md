# Atari ST / STE / TT / Falcon audio

## AVR (Audio Visual Research) — first implementation

- `sauti_decoder_avr.asm`: **NASM x86-64** for parsing and PCM sample conversion.
- `sauti_decoder_avr.c`: small portable `SautiDecoderOps` adapter for lifetime, read/seek state and allocation.
- `include/sauti_decoder_avr.h`: decoder registry symbol.
- `tests/test_decoder_avr.c`: deterministic synthetic regression fixtures; `make test_decoder_avr` is not a top-level target, use `make build/test_decoder_avr && ./build/test_decoder_avr` or `make test`.

Scope: 128-byte `2BIT` header, big-endian fields, 8/16-bit uncompressed PCM, signed and unsigned, mono and interleaved stereo. Interpret header length as individual sample units (bytes at 8-bit and words at 16-bit), inclusive of both channels. Legacy speed selector occupies the high byte; low 24 bits specify sample Hz.

Current limitations: reserved compression modes rejected; loop metadata not acted upon; mixed-resolution 12/14-bit variants not yet handled; AVR is now registered in the Linux SDL playback decoder list and recognized as a browser candidate by the .avr extension. End-to-end playback of a real Atari file has not yet been verified; rebuild and regression tests are required after integration.

Sources: Atari Wiki AVR / 1991 Stereo Master appendix, https://www.atari-wiki.com/index.php?title=AVR

Future: DVSM and YM/SNDH need dedicated implementations; YM/SNDH additionally require a replay/emulation engine rather than raw PCM parsing.

## Falcon030 / WinRec DVSM — phase 1

- `sauti_decoder_dvsm.asm`: original NASM conversion of big-endian unpacked PCM to float32.
- `sauti_decoder_dvsm.c`: strict minimal header parser, sample-rate mapping, state and `SautiDecoderOps` bridge.
- `tests/test_decoder_dvsm.c`: mono/stereo, 8/16-bit, rate-index, invalid-header regression tests.
- `make build/test_decoder_dvsm && ./build/test_decoder_dvsm` runs the standalone test.

**Integrated into the interactive player** as `.dvs` using the decoder registry; the full application rebuild and playback on a real WinRec recording remain unverified. Pack 0 uncompressed PCM and pack 2 mono/stereo Delta are implemented. pack 5 ADPCM, PACK extension overrides, metadata extension semantics and authentic historical file verification remain pending. Unknown encodings are deliberately rejected rather than decoded as noise.

Reference for header and modes: https://old-file-viewer.bobpritchett.com/docs/dvsm-format/ (format research and independent converter comparison). Historical WinRec information: https://temlib.org/AtariForumWiki/index.php/WinRec

Historical DVSM references: [Atarimania 525 converter documentation](https://www.atarimania.com/games/atari-st-utilities-525-five-to-five-44240), [Old File Viewer DVSM reference](https://old-file-viewer.bobpritchett.com/docs/dvsm-format/). The 8 Falcon CODEC sample-rate values vary slightly across historical software; real-file comparisons are needed before declaring complete compatibility.

### Pack-2 Delta mono (experimental)

NASM implements the 16-bit seed and saturating signed-delta accumulation. The 256-entry exponential table is built by the C adapter (1.084618362 exponent, rounded to integer); compressed frames are decoded once to a seekable float32 buffer. Stereo packed blocks use the header's big-endian block length. Every block resets both independent L/R predictors with two 16-bit big-endian seeds, followed by interleaved delta index pairs. Truncated, odd-sized, and malformed blocks are rejected. No authentic Falcon DVSM Delta file has been validated with the current implementation; synthetic tests are the initial acceptance gate.

### Stereo Delta verification

New NASM block decoder is implemented and exercised by synthetic two-block cases testing independent L/R accumulation, clipping, restart at each block, seeking and truncated blocks. **Build/test and historic reference comparison still pending after this commit.**

### WinRec Pro Voice pack 4 (experimental)

The NASM decoder now supports signed 16-bit predictor seeds and packed 4-bit deltas with the 16-step signed lookup table. In mono each byte yields two samples (high nibble before low nibble); in stereo each block holds 4 seed bytes followed by one byte per interleaved LR frame. Predictor sums saturate to the signed 16-bit range. Header validation intentionally requires 16-bit mode for packed formats; no real WinRec Pro file has been verified yet. This implementation must pass its new unit tests and a Fedora rebuild before it is considered tested. Reference: https://old-file-viewer.bobpritchett.com/docs/dvsm-format/.

### Authentic WinRec data: digital.dvs prefix regression

The DVSM test now embeds the 16 compressed bytes at payload offset 16 from `digital.dvs` (Sembiance DVSM sample archive; user-inspected hex dump) and checks all 15 resulting signed-16-bit samples against a separately calculated exponential-delta reference. It also verifies CODEC index 5 (24,585 Hz), the mono block-length field 0x00007d01, and seeking near the end of this prefix. This is **only a genuine-data prefix regression**, not full-file bit-exact comparison against a reference decoder or an auditory confirmation; the latter are still pending. The real full-file playback smoke log confirmed initial PCM non-silence and SDL submission, not accuracy of the entire waveform.

### Full-file independent Delta mono check

With an authentic unmodified `digital.dvs` in `$HOME/Pulpit/MUZYKA/Testy/`:

```sh
make build/dump_decoder_dvsm
./build/dump_decoder_dvsm "$HOME/Pulpit/MUZYKA/Testy/digital.dvs" /tmp/sauti-digital-dvsm.raw
python3 tests/compare_dvsm_reference.py "$HOME/Pulpit/MUZYKA/Testy/digital.dvs" /tmp/sauti-digital-dvsm.raw
```

The production decoder (NASM/C) dumps signed 16-bit little-endian PCM, then a completely separate Python implementation of the published pack-2 mono formula compares every sample and reports first mismatches, frame counts and total differences. Intentionally restricted to `header_len=16, pack=2, mode=3`; extended PACK tables need explicit support before comparison. This is **not** yet evidence of identical output to original WinRec or its independently built vintage converter. The tool has not been exercised on the user's machine after addition; do not mark full-file audit as passed until results arrive.

Specification reference: https://old-file-viewer.bobpritchett.com/docs/dvsm-format/.

### Delta stereo / Voice full-file audit

The independent Python full-stream comparator now supports the 16-byte-header variants: pack 2 mono/stereo and pack 4 mono/stereo (16-bit). It validates the stereo block length and compares interleaved L/R samples with the production NASM decoder.

Use for any downloaded `.dvs` sample with header length 16:

```sh
make build/dump_decoder_dvsm
./build/dump_decoder_dvsm "/absolute/path/to/sample.dvs" /tmp/sauti-dvsm-audit.raw
python3 tests/compare_dvsm_reference.py "/absolute/path/to/sample.dvs" /tmp/sauti-dvsm-audit.raw
```

Inspect which format a sample contains before the comparison:

```sh
xxd -l 16 "/absolute/path/to/sample.dvs"
```

Header byte 10 is the pack identifier (`02` Delta, `04` Voice), byte 11 is mode (`01` stereo 16-bit, `03` mono 16-bit). This parser explicitly refuses extended headers (`header_len != 16`) because the PACK extension can alter the delta table; it does not silently claim such streams match.

Evidence status: a real complete `digital.dvs` mono Delta recording passed the previous full-file 129,161-frame independent algorithm comparison. No authentic stereo Delta or Voice recording has been independently compared yet. Some publicly available archives contain DVSM samples, but their compression modes are not established solely from filenames. The reference remains based on format documentation, not original WinRec's executable output.

## YM5/YM6 — first Atari ST YM2149 tone-only milestone

- `sauti_decoder_ym.asm`: original NASM x86-64 kernel for three square-tone generators, with per-voice phase and volume.
- `sauti_decoder_ym.c`: minimal strict YM5/YM6 `LeOnArD!` register-dump parser; validates frame count, clock, metadata strings, interleaved or sequential register layout and `End!` marker.
- `include/sauti_decoder_ym.h`, `tests/test_decoder_ym.c`: decoder API and synthetic NASM test.
- Build: `make build/test_decoder_ym && ./build/test_decoder_ym`. Also included in `make test`.

**Explicitly experimental and NOT yet registered in the music-player decoder array.** The engine outputs 44,100 Hz mono square waves and shared 17-bit LFSR noise and supports `seek(0)` only. It deliberately refuses digidrums, nonzero YM6 special-effect registers, envelope-enabled channels, and unrecognized effects. It does **not** yet emulate YM2149 envelope, analogue volume curve, SID/digidrum/sync buzzer, LHA packed `.ym` or other formats YM2/YM3/SNDH. These must be implemented and separately tested before authentic full playback is claimed. No Fedora build/runtime pass has yet been observed for this newly added module.

Format references: https://github.com/FlorentFlament/ym2149-sndh/blob/master/ym-file-format.txt ; https://hed0rah.github.io/audio_files_anatomy/ym-anatomy.html . YM2149 silicon overview: https://bitsavers.trailing-edge.com/components/yamaha/YM2149_199209.pdf

### YM2149 noise generator — experimental second milestone

The ASM PSG core now includes a shared 17-bit LFSR, with feedback `bit0 XOR bit3` inserted at bit16 (as independently described by MAME). Noise is clocked using the PSG master clock divided by 16, with 5-bit period from R6, and gated independently per channel via mixer R7 bits 3–5. Each channel's tone/noise gates combine according to R7. Reset with `seek(0)` restores the LFSR seed and noise phase.

New tests verify 256 raw LFSR transitions against an independently written C recurrence, noise-only channel variation, and deterministic reset. They have **not yet run on Fedora**. The emulator remains a baseline, not cycle-accurate: sample-step clock approximation, provisional bipolar amplitude and no hardware envelope, digi-drum or special effects. It is still deliberately **not registered in the interactive music player**.

References: [MAME AY8910 sound core](https://sources.debian.org/src/mame/0.289+dfsg1-1/src/devices/sound/ay8910.h), [YM2149 implementation notes](https://docs.rs/crate/ym2149/latest/source/ARCHITECTURE.md).

### YM2149 32-step envelope — experimental third milestone

The NASM core now includes a shared 32-level hardware envelope counter driven by period registers R11/R12, shape R13 and the envelope-select bit 4 in volumes R8–R10. It implements continue/attack/alternate/hold logic across the 16 shape codes. The YM5/YM6 baseline no longer rejects an envelope-enabled voice. A new unit test models all 16 shapes independently and checks acceptance of envelope-enabled register data.

Caveats: The YM register-dump is a periodic snapshot, not a CPU bus-write trace. This initial version restarts the envelope when the sampled R13 **value changes**, not on every actual hardware write to R13 with the same value. It uses a simplified linear level mapping and sample-based timing; no YM analogue DAC characterization or authentic-file match is asserted. The decoder remains isolated from the main SDL player until verified.

Run `make build/test_decoder_ym && ./build/test_decoder_ym && make test`. This change has not yet been compiled/tested on Fedora.

Reference: [Yamaha YM2149 Manual](https://www.temlib.org/AtariForumWiki/index.php/Yamaha_YM2149_Manual).

### YM PSG mixer audit — envelope volume regression

A NASM flags bug in the tone/noise mixer has been fixed: after selecting an envelope volume with a `MOV` instruction, the old conditional branch could incorrectly use stale flags from `TEST volume_register,16` and mute envelope-controlled channels. The mixer now explicitly tests the selected volume. A direct NASM mixer test checks nonzero envelope level, zero envelope level, fixed volume independence and mute. Run `make build/test_decoder_ym && ./build/test_decoder_ym && make test` on Fedora. **Not yet independently run on the user's Fedora system.**

Remaining fidelity work: clock-divider validation against a reference emulator, cycle-accurate writes/retriggers, 32-step DAC lookup, period/phase edge cases and authentic YM5/YM6 samples. Passing this regression does not establish YM2149 silicon accuracy.

### Clock-divider audit (YM2149)

Against the Yamaha manual and MAME reference, the previous C adapter fed the tone and envelope phases with master-clock/16. Since the NASM tone counts a **half-wave** per period, this produced half the intended pitch. It now uses master-clock/8 for tone *half-wave steps* (full output period master/(16×TP)); the envelope's 32 positions likewise use master/8 (full envelope waveform master/(256×EP)). The noise retains master/16 ticks (master/(16×NP)). A new direct NASM test checks ~1,000 polarity transitions at 500 Hz in one second for a 2 MHz clock and tone period 250.

This is a divider correction, **not** a claim of cycle-accurate YM2149 emulation: 44.1 kHz sample-step quantization, /SEL configuration, the nonlinear DAC curve, write timing and authentic-reference comparisons remain outstanding. At this point, Fedora build/tests have **not yet been performed** for the divider patch.

### Nonlinear YM2149 DAC baseline

The NASM mixer now uses a 32-entry nonlinear YM2149 DAC approximation from Mitsutaka Okazaki's `emu2149` (YM2149-specific `voltbl[0]`, released with `emu2149.c`), rather than `level/45`. Fixed R8–R10 volumes 1–15 are mapped to odd DAC slots `2*level+1`; volume zero is silent in this AC-style bipolar baseline. Envelope mode uses all 32 slots (0–31), preserving 5-bit resolution. Per-channel amplitudes are divided by 765, keeping the maximum three-channel sum near ±1.

The new unit test checks all 16 fixed-volume levels, all 32 envelope levels and three-voice headroom using independently encoded expected values. The source is https://github.com/emestee/bk-emulator/blob/master/emu2149.c . The YM2149 manual describes fixed 4-bit versus envelope 5-bit DAC selection: https://map.grauw.nl/resources/sound/yamaha_ym2149.pdf .

**This is an approximate amplitude lookup, not a full measured analogue circuit model**: it does not reproduce DC bias, external resistor loading, nonlinear interactions among multiple channels or hardware output filtering. It has not yet been tested on the user's Fedora machine; run `make build/test_decoder_ym && ./build/test_decoder_ym && make test`. Integration with Sauti's main playback UI remains deferred until authentic YM test files and playback comparison.

### Real YM5/YM6 frame semantics: R13 sentinel (2026-10)

In the original ST-Sound YM register-dump convention, `R13=0xFF` is a **no-write** sentinel: the envelope must continue unchanged. By contrast, each explicit R13 value 0–15 **restarts** the hardware envelope at that VBL, even if the same shape was already active. The adapter now evaluates this event once per decoded YM frame boundary, not on every output sample; previous code only restarted on a changed shape. A dedicated two-frame regression compares R13=FF with an explicit same-shape retrigger and requires different second-frame output. The YM parser remains intentionally isolated from the main player.

Authentic-format sources:
- ST-Sound author's sample corpus: https://github.com/arnaud-carre/StSound/tree/master/YmSampleFiles
- R13 no-write semantics and LHA container caveat: https://hed0rah.github.io/audio_files_anatomy/ym-anatomy.html
- ST-Sound document: https://github.com/FlorentFlament/ym2149-sndh/blob/master/ym-file-format.txt

Samples often arrive as LHA/LZH archives, not unpacked YM streams. Sauti's decoder currently requires raw YM5/YM6 bytes. Do not report authentic playback verified until testing samples locally and comparing with ST-Sound.

### First real Atari ST YM listening test: isolated WAV render

Build with `make build/render_decoder_ym`. The standalone binary `./build/render_decoder_ym unpacked-song.ym /tmp/sauti-ym.wav` writes PCM16 RIFF/WAV at 44.1 kHz, mono, using the **native experimental NASM YM2149** decoder; it does not use ST-Sound and is not linked into the Sauti GUI.

One source of original YM samples is the ST-Sound author's repository:
https://github.com/arnaud-carre/StSound/tree/master/YmSampleFiles

For example, try their documented `ANCOOL1.YM` file. First inspect `xxd -l 20 ANCOOL1.YM`. Raw `YM5!LeOnArD!` / `YM6!LeOnArD!` can be rendered directly. Many distributable `.YM` files are **LHA archives**, beginning with an LHA header rather than YM5/YM6; extract one member with `7z x -o/tmp/sauti-ym-extracted ANCOOL1.YM` (install a suitable LHA-capable extractor if necessary), then point `render_decoder_ym` at the extracted *raw* YM member. An archive containing other YM versions, unsupported DigiDrums/effects, or malformed data is intentionally rejected. Do not rename a compressed file to fool the signature check.

The WAV is for subjective listening and subsequent external reference comparison, **not** proof of sample-exact fidelity to ST-Sound or YM2149 silicon. There is no integrated `.ym` GUI playback, LHA decompressor, or validated authentic YM corpus in this change.

### YM2/YM3 50 Hz register dump and real ANCOOL1 experiment

The experimental YM2149 decoder now recognizes raw `YM2!` and `YM3!` as well as YM5/YM6. The older files are simply 4-byte magic plus **14 interleaved register planes**; `frames = (filesize-4)/14`, with Atari ST default **2 MHz clock** and **50 Hz VBL**. The exact unpacked `ANCOOL1` size reported from the original ST-Sound sample (64,404 bytes) equals **4600 frames = 92 seconds**. This format has no `LeOnArD!` header and no `End!` trailer. The assembler tone/noise/envelope/DAC implementation is shared with YM5/6. Malformed lengths and unknown magic are rejected. R13=FF retains no-write semantics.

The YM2 Mad Max player may reference **player-resident digital drum samples that are not stored in the YM2 file**, so even successful rendering is not a guarantee of matching the original; authentic auditory comparison is still required. Not implementing YM3b loop metadata or LHA decompression in this step.

On Fedora, with the original unpacked file already extracted by 7z:
```bash
cd "/home/marek/AYO Projekty/LAS"
git pull --ff-only origin feature/sauti-stereo-neon-spectrum
cd apps/sayari-sauti
make build/test_decoder_ym build/render_decoder_ym
./build/test_decoder_ym
make test
./build/render_decoder_ym "$HOME/Pulpit/MUZYKA/Testy/YM/rozpakowane/ANCOOL1" /tmp/sauti-ancool1.wav
ffplay -autoexit -nodisp /tmp/sauti-ancool1.wav
```
This generates a **44100 Hz / mono / 16-bit WAV** from the Sauti native assembler synth, not ST-Sound. The sample has **not yet been played or compared** against hardware/independent emulator, and running the new tests on Fedora is still required.

Format source: https://hed0rah.github.io/audio_files_anatomy/ym-anatomy.html .

### GUI player: experimental raw YM support

The Linux SDL and PipeWire window targets now link the YM decoder and expose `.ym` in the file browser. The portable PCM SDL adapter probes raw YM2/YM3/YM5/YM6 headers; unsupported data fails to open. **Packed LHA/LZH .ym files must be extracted first**, since decompression remains unsupported. The original ANCOOL1 archive extracts to a file without an extension: give a *copy* the `.ym` suffix before opening it in the GUI:

```sh
cd "/home/marek/AYO Projekty/LAS"
git pull --ff-only origin feature/sauti-stereo-neon-spectrum
cd apps/sayari-sauti
make build/test_decoder_ym window-pipewire
./build/test_decoder_ym
make test
cp "$HOME/Pulpit/MUZYKA/Testy/YM/rozpakowane/ANCOOL1" "$HOME/Pulpit/MUZYKA/Testy/YM/rozpakowane/ANCOOL1.ym"
SDL_VIDEODRIVER=wayland ./build/sauti_window_pw
```

Select `ANCOOL1.ym` in the Sauti browser. Opening, playing, stopping and restarting are supported by the YM decoder. **Non-zero sample-accurate seek remains unsupported**; do not interpret seeking via the window's timeline as implemented. This integration has not been run on Fedora yet and is intentionally experimental.

### YM2/YM3/YM5/YM6 seek and envelope write correctness

The SDL playback timeline can now seek to nonzero sample positions for YM. This initial implementation reconstructs all NASM PSG state (three tone phases, 17-bit noise LFSR and YM envelope) by rendering and discarding samples from the start, in fixed 2048-sample chunks. It uses no additional persistent memory. The seek result must match uninterrupted playback sample for sample. Seeking far into long YM songs is O(position) and can briefly interrupt the UI; use per-frame PSG checkpoints for future responsiveness.

Also fixed R13 write handling to occur exactly once on **actual frame transition**, not on the first two samples of each frame as previously happened at 50 Hz. Every explicit shape write restarts the envelope, while FF leaves its state untouched. Exact PCM before and after seeking is checked at multiple offsets spanning a VBL transition, including end-of-stream. This may change envelope rendering compared with the previous bugged implementation.

Validation on Fedora:
```sh
cd "/home/marek/AYO Projekty/LAS"
git pull --ff-only origin feature/sauti-stereo-neon-spectrum
cd apps/sayari-sauti
make build/test_decoder_ym window-pipewire
./build/test_decoder_ym
make test
```

### Automatic LHA/LH5 YM archive loading (experimental)

On Linux SDL/PipeWire builds with `pkg-config --exists libarchive`, Sauti accepts LHA-wrapped `.ym` files (including the original compressed `ANCOOL1.YM`) directly. The bounded reader uses the ISC/BSD-style permissively licensed system `libarchive` API; the file is decompressed into memory and passed to the **unchanged native NASM YM2/3/5/6 decoder**. It never runs a shell, extracts archive paths, creates files, or changes other decoders. Raw YM files still work.

Safety checks: LHA magic, at most 16 MiB compressed input, one regular-file member, positive declared uncompressed size at most 32 MiB, exact member length, end of archive and raw YM magic. Unsupported, corrupt, multi-member or excessive archives are rejected. This is limited **LHA** support, not universal LZH decompression.

Fedora setup and verification:
```sh
sudo dnf install libarchive-devel
cd "/home/marek/AYO Projekty/LAS"
git pull --ff-only origin feature/sauti-stereo-neon-spectrum
cd apps/sayari-sauti
make build/test_ym_archive window-pipewire
./build/test_ym_archive
make test
SDL_VIDEODRIVER=wayland ./build/sauti_window_pw --source monitor --view stereo-bars --software
```
Open **original compressed** `$HOME/Pulpit/MUZYKA/Testy/YM/ANCOOL1.YM` in the file browser, not just the previously extracted `ANCOOL1.ym`. An absent `libarchive-devel` causes a build without LHA support and a clear rejection rather than fallback to unsafe external extraction. A successful test requires confirming in the terminal `Sauti: unpacked LHA YM in memory` and hearing the original song in the GUI. This has not yet been tested on Fedora with actual ANCOOL1.
