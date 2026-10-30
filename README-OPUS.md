# Opus in Sayari Sauti

The portable audio decoder interface now includes an **optional** Opusfile backend
(`src/sauti_decoder_opus.c`). This is an Ogg Opus decoder, not an OS audio
API. Its output is 48 kHz interleaved float PCM, downmixed to stereo.

- Linux: `pkg-config opusfile` is checked by the Makefile. If found, Opus
  decoding is compiled into the regular Sauti window with
  `-DSAUTI_HAVE_OPUSFILE`. If absent, the application still builds and the
  pre-existing SDL2_mixer path remains the fallback.
- AyoOS / Windows: port or vendor `libogg`, `libopus`, and `opusfile` and
  enable `SAUTI_HAVE_OPUSFILE`. The decoder code itself uses no POSIX, SDL,
  PipeWire, or OS file I/O.
- The `OpusHead` signature is checked before the Vorbis probe, to avoid
  mistaking an Ogg Opus stream for Vorbis.

Test the decoder with a real Ogg Opus file:
```sh
make build/test_decoder_opus
./build/test_decoder_opus path/to/song.opus
```
A real-file test requires `opusfile` development headers/libs available at
build time. With that optional library missing, the no-argument test checks
only decoder API validity and rejection of malformed input. It does **not**
demonstrate that Opus decoding works.

Outstanding work: bundle the portable library stack for OS-independent
builds, validate real-world Opus files, improve streaming file reads, and test
seeking, chained streams, and multichannel downmix.
