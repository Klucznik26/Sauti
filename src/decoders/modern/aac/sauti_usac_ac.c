/* USAC arithmetic spectral decoder, see sauti_usac_ac.h.  The algorithm is the normative one of ISO/IEC 23003-3 7.4; the probability tables are in sauti_usac_tables.c. */
#include "sauti_usac_ac.h"
#include "sauti_usac_tables.h"
#include <string.h>

typedef struct { uint32_t low, high, val; } Range;

static void context_map(SautiUsacAcState *s, int reset, unsigned n) {
    if (reset) {
        memset(s->last, 0, sizeof s->last);
        s->last_len = (int)n;
    } else if (s->last_len != (int)n) {
        uint8_t old[512];
        const float ratio = (float)s->last_len / (float)n;
        unsigned i;
        memcpy(old, s->last, sizeof old);
        for (i = 0; i < n / 2u; i++) s->last[i] = old[(unsigned)((float)i * ratio)];
        for (; i < sizeof s->last; i++) s->last[i] = 0;
        s->last_len = (int)n;
    }
    s->cur[3] = s->cur[2] = s->cur[1] = 0;
    s->cur[0] = 1;
    s->state_pre = (uint32_t)s->last[0] << 12;
}
static uint32_t context_get(SautiUsacAcState *s, unsigned i) {
    uint32_t c = s->state_pre >> 8;
    c += (uint32_t)s->last[i + 1u] << 8;
    c <<= 4;
    c += s->cur[1];
    s->state_pre = c & 0xffffu;
    if (i > 3u && (unsigned)(s->cur[3] + s->cur[2] + s->cur[1]) < 5u) return c + 0x10000u;
    return c;
}
static unsigned model_get(uint32_t c) {
    int lo = -1, hi = (int)(sizeof sauti_usac_ac_lookup / sizeof sauti_usac_ac_lookup[0]) - 1;
    while (hi - lo > 1) {
        const int mid = lo + (hi - lo) / 2;
        const uint32_t j = sauti_usac_ac_hash[mid];
        if (c < (j >> 8)) hi = mid;
        else if (c > (j >> 8)) lo = mid;
        else return j & 0xffu;
    }
    return sauti_usac_ac_lookup[hi];
}
static void context_update(SautiUsacAcState *s, unsigned i, unsigned a, unsigned b) {
    unsigned v = a + b + 1u;
    s->cur[0] = (uint8_t)(v > 15u ? 15u : v);
    s->cur[3] = s->cur[2]; s->cur[2] = s->cur[1]; s->cur[1] = s->cur[0];
    s->last[i] = s->cur[0];
}
/* one symbol of the cumulative distribution cdf[0..len) (14-bit scale, decreasing) */
static unsigned symbol(Range *r, SautiUsacBits *b, const uint16_t *cdf, unsigned len) {
    const uint32_t rng = r->high - r->low + 1u;
    const uint32_t c = (((r->val - r->low + 1u) << 14) - 1u);
    unsigned sym = 0;
    while (sym + 1u < len && (uint32_t)cdf[sym] * rng > c) sym++;          /* first symbol whose cumulative bound is not above the value */
    {
        uint32_t high = r->high, low = r->low, val = r->val;
        if (sym) high = low + ((rng * cdf[sym - 1u]) >> 14) - 1u;
        low += (rng * cdf[sym]) >> 14;
        for (;;) {
            if (high < 32768u) { }
            else if (low >= 32768u) { val -= 32768u; low -= 32768u; high -= 32768u; }
            else if (low >= 16384u && high < 49152u) { val -= 16384u; low -= 16384u; high -= 16384u; }
            else break;
            low += low; high += high + 1u; val = (val << 1) | sauti_usac_get(b, 1);
        }
        r->low = low; r->high = high; r->val = val;
    }
    return sym;
}
int sauti_usac_ac_decode(SautiUsacBits *b, SautiUsacAcState *s, int reset, unsigned lg, unsigned n, int32_t *q) {
    Range r;
    SautiUsacBits end;
    unsigned i;
    memset(q, 0, sizeof(int32_t) * n);
    context_map(s, reset, n);
    if (!lg) {
        for (i = 0; i < n / 2u; i++) s->last[i] = 1;
        for (; i < sizeof s->last; i++) s->last[i] = 0;
        return 1;
    }
    r.low = 0; r.high = 65535u; r.val = sauti_usac_get(b, 16);
    for (i = 0; i < lg / 2u; i++) {
        unsigned lvl = 0, esc = 0, m, a, c2;
        const uint32_t c = context_get(s, i);
        for (;;) {
            const unsigned pk = model_get(c + ((uint32_t)esc << 17));
            m = symbol(&r, b, sauti_usac_ac_msb_cdf[pk], 17);
            if (m < 16u) break;
            if (++lvl > 23u) return 0;
            esc = lvl > 7u ? 7u : lvl;
        }
        c2 = m >> 2; a = m & 3u;
        if (!m) {
            if (esc) break;                                                /* end of the arithmetic code */
            a = c2 = 0;
        }
        for (unsigned l = lvl; l > 0; l--) {
            const unsigned idx = !a ? 1u : (!c2 ? 0u : 2u);
            const unsigned rr = symbol(&r, b, sauti_usac_ac_lsb_cdf[idx], 4);
            a = (a << 1) | (rr & 1u);
            c2 = (c2 << 1) | ((rr >> 1) & 1u);
        }
        q[2u * i] = (int32_t)a; q[2u * i + 1u] = (int32_t)c2;
        context_update(s, i, a, c2);
    }
    end = *b;
    if (lg > 1u && end.pos >= 14u) b->pos = end.pos - 14u;
    for (unsigned k = i; k < n / 2u; k++) s->last[k] = 1;                  /* unread tuples count as small values for the next window */
    for (unsigned k = (i > n / 2u ? i : n / 2u); k < sizeof s->last; k++) s->last[k] = 0;
    for (i = 0; i < lg; i++) if (q[i] && !sauti_usac_get(b, 1)) q[i] = -q[i];   /* sign bit 0 = negative */
    return 1;
}
