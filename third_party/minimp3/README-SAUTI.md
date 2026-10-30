# minimp3 vendored in Sauti

Upstream: https://github.com/lieff/minimp3

Files: minimp3.h and minimp3_ex.h from upstream master, copied on 2026-10-24.
Upstream declares CC0-1.0 public-domain dedication in both header files.

The portable adapter lives in ../../src/sauti_decoder_mp3.c.
MINIMP3_NO_STDIO prevents direct filesystem calls; MINIMP3_NO_SIMD keeps
this module free of architecture-specific SIMD requirements. Data buffer
ownership remains with the caller for the decoder's lifetime.

These headers are third-party code. Preserve their upstream notices.
