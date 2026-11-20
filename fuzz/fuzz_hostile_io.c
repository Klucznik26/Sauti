/* Cel 4: wejscie lamiace kontrakt.  Dane = skrypt zachowania callbackow (co wywolanie: ile bajtow zwrocic, w tym WIECEJ niz zazadano, wartosci ujemne i skrajne, deklarowany rozmiar,
 * wyniki seek) + material bazowy (wbudowany minimalny WAV).  Rdzen nie moze przekroczyc bufora, zawiesic sie ani zwrocic OK z niezainicjowanymi wynikami. */
#include "fuzz_common.h"
typedef struct { const uint8_t *script; size_t slen, spos; uint8_t base[256]; size_t bn, pos; int64_t size_claim; int has_seek; int closed; } H;
static uint8_t sbyte(H *h) { return h->slen ? h->script[h->spos++ % h->slen] : 0; }
static int64_t MAKTABASAUTI_CALL h_read(void *u, void *b, size_t n) {
    H *h = (H *)u; uint8_t c = sbyte(h); size_t g = h->bn - h->pos;
    if (g > n) g = n;
    if (g) memcpy(b, h->base + h->pos, g);
    h->pos += g;
    switch (c & 7u) { case 0: return (int64_t)n + 1;                  /* wiecej niz zazadano */
    case 1: return INT64_MAX; case 2: return INT64_MIN; case 3: return MAKTABASAUTI_E_WOULD_BLOCK; case 4: return -(int64_t)(c >> 3) - 1; case 5: return 0;
    default: return (int64_t)g; }
}
static int64_t MAKTABASAUTI_CALL h_seek(void *u, int64_t off, int wh) { H *h = (H *)u; uint8_t c = sbyte(h); (void)off; (void)wh; if (c & 1u) return MAKTABASAUTI_E_NOT_SEEKABLE; if (c & 2u) return INT64_MAX; h->pos = 0; return 0; }
static int64_t MAKTABASAUTI_CALL h_size(void *u) { H *h = (H *)u; uint8_t c = sbyte(h); return c & 1u ? h->size_claim : (c & 2u ? -1 : (int64_t)h->bn); }
static void MAKTABASAUTI_CALL h_close(void *u) { ((H *)u)->closed++; }
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    static const uint8_t wav[] = { 'R','I','F','F', 0x24,0,0,0, 'W','A','V','E', 'f','m','t',' ', 16,0,0,0, 1,0, 1,0, 0x40,0x1f,0,0, 0x80,0x3e,0,0, 2,0, 16,0, 'd','a','t','a', 0x00,0x01,0,0 };
    H h; FzMem m; maktabasauti_allocator_t a; maktabasauti_open_params_t p; maktabasauti_decoder_t *d = NULL; maktabasauti_result_t r; maktabasauti_registry_t *reg;
    if (size < 9) return 0;
    memset(&h, 0, sizeof h); memset(&m, 0, sizeof m);
    memcpy(&h.size_claim, data, 8); h.script = data + 8; h.slen = size - 8; h.has_seek = 1;
    memcpy(h.base, wav, sizeof wav); h.bn = sizeof h.base;
    a = fz_allocator(&m);
    memset(&p, 0, sizeof p); p.struct_size = sizeof p; p.abi_version = MAKTABASAUTI_ABI_VERSION; p.allocator = &a;
    reg = fz_pure_registry(); p.registry = reg;
    p.io.struct_size = sizeof p.io; p.io.user = &h; p.io.read = h_read; p.io.seek = h_seek; p.io.size = h_size; p.io.close = h_close;
    r = maktabasauti_decoder_open(&p, &d);
    if (r == MAKTABASAUTI_OK) { fz_exercise(d); maktabasauti_decoder_close(d); } else if (h.closed) abort();
    maktabasauti_registry_destroy(reg);
    fz_check_mem(&m);
    return 0;
}
