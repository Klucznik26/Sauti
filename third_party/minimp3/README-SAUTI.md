# minimp3 vendored in Sauti

Upstream: https://github.com/lieff/minimp3

Files: minimp3.h and minimp3_ex.h from upstream master, copied on 2026-10-24.
Upstream declares CC0-1.0 public-domain dedication in both header files.

The portable adapter lives in ../../src/sauti_decoder_mp3.c.
MINIMP3_NO_STDIO prevents direct filesystem calls; MINIMP3_NO_SIMD keeps
this module free of architecture-specific SIMD requirements. Data buffer
ownership remains with the caller for the decoder's lifetime.

These headers are third-party code. Preserve their upstream notices.

## Local modifications

- minimp3_ex.h, `mp3dec_detect_cb`: `free_format_bytes` and `frame_size` are
  initialised to 0 (upstream leaves them uninitialised). The stack garbage was
  passed to `hdr_frame_bytes` for free-format headers, and a large negative
  value made `mp3d_match_frame` read before the start of the buffer (SEGV in
  `hdr_valid`, found by the TSan CI job on 2026-11-19 once the MaktabaSauti core
  began probing every codec). Regression test: `tests/test_decoder_mp3.c`
  (`probe_with_poisoned_stack`; it fails without the fix in a build without
  ASan, where the stack layout is not altered). Report upstream when possible.
