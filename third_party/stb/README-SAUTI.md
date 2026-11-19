# stb_vorbis vendored in Sauti

Upstream: https://github.com/nothings/stb (`stb_vorbis.c`, v1.22, public domain / MIT-0, dual-licensed by the author). Only `stb_vorbis.c` is used,
with `STB_VORBIS_NO_STDIO` and memory input only (`stb_vorbis_open_memory`).

## Local modifications (all in `third_party/stb/stb_vorbis.c`, each marked `SAUTI` in the source)

Found by the probe audit / fuzzing (`make test-maktabasauti-probe-audit`, regressions in `tests/test_third_party_hardening.c`, `make test-third-party-hardening`);
the unmodified file fails those tests (invalid `free()`, heap overflow, leaks, undefined behaviour).

1. `setup_malloc` / `setup_temp_malloc` take `size_t` and refuse sizes above `INT_MAX - 8` (original: `int`, so `sizeof(x) * count` computed in `size_t` was truncated:
   `comment_list_length = 0x20000001` allocated 8 bytes and the next loop wrote far beyond them = heap overflow on a crafted file; the same pattern is used for
   codebook / floor / residue / mapping tables).
2. `setup_malloc` returns **zeroed** memory (`calloc` / `memset`). `vorbis_deinit()` runs on every setup error and frees members of structure arrays that
   were allocated but not yet filled in; with `malloc` that meant `free()` of garbage pointers (SIGSEGV / invalid free) for a corrupt setup header.
3. Temporary blocks (malloc mode) are kept on a list in the decoder (`temp_blocks`) and freed by `vorbis_deinit()`; `setup_free()` skips pointers that are still live
   temporary blocks (`c->codewords` can point to one when `start_decoder` fails). The original leaked them on every malformed file (and would free them
   twice if the leak were fixed naively).
4. Sanity limits: vendor and comment lengths <= 16 MiB, comment count <= 65536; negative values rejected (original trusted 32-bit values: GiB allocations,
   billions of loop iterations, `len + 1` overflow). `comment_list_length` is reset to 0 when the list allocation fails and `vorbis_deinit()` guards `comment_list == NULL`
   (NULL dereference on that error path).
5. `get32_packet` uses unsigned shifts (`get8_packet` returns `EOP = -1` at the end of a packet; shifting a negative `int` is undefined behaviour).

Behaviour for valid files is unchanged (bit-identical PCM: `make test-maktabasauti-regression`, thread tests, hardening test opens the unmodified fixture).
Upstream status: not reported yet; the patch is in `docs/upstream-patches/stb_vorbis.patch` (diff against the imported v1.22), ready to propose to nothings/stb.
