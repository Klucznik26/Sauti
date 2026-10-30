# YM decoder — accepted baseline (2026-10-27)

## Decision

Close the *basic YM playback implementation* phase. Do not conflate this
with cycle-accurate ST-Sound parity. Further YM changes require a concrete
regression, a real affected file, or verifiable reference discrepancy.

## Supported paths

- YM2, YM3, YM3b, YM5 and YM6 PSG register dumps via the portable decoder.
- Tone, noise, envelope, seek and the tested YM5/6 effects.
- YM supplied inside supported LHA archives (unpacked in memory).
- Optional YM2 Mad Max external unsigned PCM8 samples. The Linux SDL player
  activates the bank only for YM2 songs with nonzero-timer R10-bit7 triggers.
  Caller-owned PCM buffers are retained until decoder close.

Linux SDL bank convention: set `SAUTI_YM2_BANK_DIR` to a directory of
`0.raw` through `127.raw` files (unsigned 8-bit headerless PCM); individual
files are limited to 1 MiB. No sample data is distributed.

## Verification performed on Fedora

User-reported successful runs of:

- `make test-ym-extended`: main PSG regressions, malformed files, YM LHA
  archive tests and Mad Max PCM regression including voice C masking.
- `make test-ym-sanitize`: same relevant decoder tests under ASan/UBSan,
  with ASan leak detection enabled.
- `make window-pipewire`: Linux SDL/PipeWire player compiles with `-Werror`.
- Live ANCOOL1.YM playback: unpacked LHA YM2, 92 s, mono 44100 Hz.
- Live synthetic Mad Max fixture: three bank files loaded and output audible.
  A user reported hearing sound; the three events were not separately
  verified by ear.

Expected negative archive test message:
`Sauti LHA: cannot open archive: Unrecognized archive format`.

## Explicit limitations

- No claim of bit-exact or hardware-cycle-accurate rendering.
- Historical ST-Sound/Mad Max samples are not bundled; a user-supplied bank
  is required for full intended percussion in affected YM2 songs.
- The external PCM8 bank layout is Sauti-specific; native ST-Sound bank
  compatibility and historical sample identities are not established.
- Precise historical sample timing, DAC mixing and rendition compared with
  ST-Sound remain unverified.
- New SDL bank loader has compiled and run in the user's live session,
  but its invalid-bank error paths have not been independently exercised
  under sanitizers.

## Future work is conditional, not a release blocker

Only reopen YM with reproducible evidence of a genuine compatibility fault.
Keep tests as regression gates. Prioritize other decoders instead of extending
YM indefinitely.
