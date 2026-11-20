/* Cel 2: otwarcie, odczyt, seek, flush, reset przez PELNE API (rejestr domyslny, rozpoznawanie, czytnik, kontenery, dekodery).  Pierwszy bajt wybiera sposob dostarczania wejscia:
 * bit0 = przewijalne, bit1 = rozmiar znany, bity 2-3 = limit porcji odczytu (0 = bez, 1 = 1 B, 2 = 7 B, 3 = 300 B), bit4 = losowe WOULD_BLOCK po otwarciu (zasilane nastepnymi bajtami). */
#include "fuzz_common.h"
typedef struct { const uint8_t *d; size_t n, pos, chunk; int seekable, size_known, blocking, armed; uint32_t rng; int closed; } In;
static int64_t MAKTABASAUTI_CALL in_read(void *u, void *b, size_t n) {
    In *s = (In *)u; size_t g = s->n - s->pos;
    if (s->blocking && s->armed) { s->rng ^= s->rng << 13; s->rng ^= s->rng >> 17; s->rng ^= s->rng << 5; if ((s->rng & 3u) == 0) return MAKTABASAUTI_E_WOULD_BLOCK; }
    if (g > n) g = n;
    if (s->chunk && g > s->chunk) g = s->chunk;
    if (g) memcpy(b, s->d + s->pos, g);
    s->pos += g; return (int64_t)g;
}
static int64_t MAKTABASAUTI_CALL in_seek(void *u, int64_t off, int wh) {
    In *s = (In *)u; int64_t base = wh == 0 ? 0 : wh == 1 ? (int64_t)s->pos : (int64_t)s->n;
    if (wh < 0 || wh > 2 || (off > 0 && base > INT64_MAX - off) || base + off < 0 || (uint64_t)(base + off) > (uint64_t)s->n) return MAKTABASAUTI_E_OUT_OF_RANGE;
    s->pos = (size_t)(base + off); return base + off;
}
static int64_t MAKTABASAUTI_CALL in_size(void *u) { return (int64_t)((In *)u)->n; }
static void MAKTABASAUTI_CALL in_close(void *u) { ((In *)u)->closed++; }
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    In s; FzMem m; maktabasauti_allocator_t a; maktabasauti_open_params_t p; maktabasauti_decoder_t *d = NULL; maktabasauti_result_t r; unsigned tries;
    static const size_t chunks[4] = { 0, 1, 7, 300 };
    if (size < 2) return 0;
    memset(&s, 0, sizeof s); memset(&m, 0, sizeof m);
    s.seekable = data[0] & 1; s.size_known = (data[0] >> 1) & 1; s.chunk = chunks[(data[0] >> 2) & 3]; s.blocking = (data[0] >> 4) & 1; s.rng = 0x9E3779B9u ^ data[1];
    s.d = data + 2; s.n = size - 2;
    a = fz_allocator(&m);
    memset(&p, 0, sizeof p); p.struct_size = sizeof p; p.abi_version = MAKTABASAUTI_ABI_VERSION; p.allocator = &a;
    p.io.struct_size = sizeof p.io; p.io.user = &s; p.io.read = in_read; p.io.close = in_close; p.io.seek = s.seekable ? in_seek : NULL; p.io.size = s.size_known ? in_size : NULL;
    for (tries = 0; tries < 4; tries++) { r = maktabasauti_decoder_open(&p, &d); if (r != MAKTABASAUTI_E_WOULD_BLOCK) break; if (!s.seekable) break; }
    if (r == MAKTABASAUTI_OK) { s.armed = 1; fz_exercise(d); maktabasauti_decoder_close(d); if (s.closed != 1) abort(); }
    else if (s.closed != 0) abort();                                      /* nieudane otwarcie nie zamyka wejscia */
    fz_check_mem(&m);
    return 0;
}
