# TinySoundFont `tml.h` vendored in Sauti

Upstream: https://github.com/schellingb/TinySoundFont (`tml.h`, public domain / MIT). Only the MIDI parser (`tml.h`) is used, with `TML_NO_STDIO` and memory input.
The file keeps its CRLF line endings: edit it in binary-safe mode.

## Local modifications (`third_party/tml/tml.h`)

- `tml_stream_memory_read`: no `memcpy` when `ptr` is NULL or `size` is 0 (UBSan: "null pointer passed as argument 1, which is declared to never be null").
- MTrk header length is assembled with unsigned shifts (`track_header[4] << 24` with a byte >= 128 is signed overflow); values >= 2^31 become negative and are
  rejected by the existing `track_length < 0` check.
Upstream status: not reported yet; patch in `docs/upstream-patches/tml.patch`.
