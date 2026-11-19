/* MaktabaSauti: konwersja formatow probek PCM (U8, S16, S24, S32, F32, F64), z opcjonalnym ditherem TPDF dla wyjscia calkowitoliczbowego.
 * Rozne formaty wymagaja rozlacznych buforow; ten sam format moze sie pokrywac (memmove).
 * Konwencje: calkowitoliczbowe -> float: x / 2^(bity-1) (U8: (x-128)/128); float -> calkowitoliczbowe: zaokraglenie do najblizszej (polowki w gore) z nasyceniem do zakresu.
 * Probki w kolejnosci bajtow platformy; S24 = 3 bajty spakowane, tez w kolejnosci platformy.  Stan ditheru jest w wywolujacym (uchwyt dekodera), nie globalny. */
#include "mks_internal.h"
#include <math.h>
#include <string.h>

void mks_dither_init(mks_dither_t *d, uint32_t enabled) { d->state = 0x9E3779B9u; d->enabled = enabled; }

static double next_uniform(mks_dither_t *d) {            /* xorshift32 -> [0,1) */
    uint32_t x = d->state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    d->state = x ? x : 0x9E3779B9u;
    return (double)(d->state >> 8) / 16777216.0;
}

static int is_little_endian(void) { const uint32_t one = 1u; return *(const unsigned char *)&one == 1u; }

static double load_sample(const unsigned char *p, maktabasauti_sample_format_t f) {
    switch (f) {
    case MAKTABASAUTI_FORMAT_U8: return ((double)p[0] - 128.0) / 128.0;
    case MAKTABASAUTI_FORMAT_S16: { int16_t v; memcpy(&v, p, 2); return (double)v / 32768.0; }
    case MAKTABASAUTI_FORMAT_S24: {
        int32_t v = is_little_endian() ? (int32_t)((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16))
                                       : (int32_t)((uint32_t)p[2] | ((uint32_t)p[1] << 8) | ((uint32_t)p[0] << 16));
        if (v & 0x800000) v -= 0x1000000;
        return (double)v / 8388608.0;
    }
    case MAKTABASAUTI_FORMAT_S32: { int32_t v; memcpy(&v, p, 4); return (double)v / 2147483648.0; }
    case MAKTABASAUTI_FORMAT_F32: { float v; memcpy(&v, p, 4); return (double)v; }
    case MAKTABASAUTI_FORMAT_F64: { double v; memcpy(&v, p, 8); return v; }
    default: return 0.0;
    }
}

static double quantise(double x, double scale, double lo, double hi, mks_dither_t *dither) {
    double v = x * scale;
    if (dither && dither->enabled) v += next_uniform(dither) - next_uniform(dither);   /* TPDF o amplitudzie +-1 LSB */
    v = floor(v + 0.5);
    if (!(v >= lo)) v = lo;                                /* takze NaN -> lo */
    if (v > hi) v = hi;
    return v;
}

static void store_sample(unsigned char *p, maktabasauti_sample_format_t f, double x, mks_dither_t *dither) {
    switch (f) {
    case MAKTABASAUTI_FORMAT_U8: p[0] = (unsigned char)(quantise(x, 128.0, -128.0, 127.0, dither) + 128.0); break;
    case MAKTABASAUTI_FORMAT_S16: { int16_t v = (int16_t)quantise(x, 32768.0, -32768.0, 32767.0, dither); memcpy(p, &v, 2); break; }
    case MAKTABASAUTI_FORMAT_S24: {
        int32_t v = (int32_t)quantise(x, 8388608.0, -8388608.0, 8388607.0, dither);
        uint32_t u = (uint32_t)v & 0xFFFFFFu;
        if (is_little_endian()) { p[0] = (unsigned char)(u & 0xFF); p[1] = (unsigned char)((u >> 8) & 0xFF); p[2] = (unsigned char)((u >> 16) & 0xFF); }
        else { p[2] = (unsigned char)(u & 0xFF); p[1] = (unsigned char)((u >> 8) & 0xFF); p[0] = (unsigned char)((u >> 16) & 0xFF); }
        break;
    }
    case MAKTABASAUTI_FORMAT_S32: { int32_t v = (int32_t)quantise(x, 2147483648.0, -2147483648.0, 2147483647.0, dither); memcpy(p, &v, 4); break; }
    case MAKTABASAUTI_FORMAT_F32: { float v = (float)x; memcpy(p, &v, 4); break; }
    case MAKTABASAUTI_FORMAT_F64: memcpy(p, &x, 8); break;
    default: break;
    }
}

static int valid_format(maktabasauti_sample_format_t f) { return f >= MAKTABASAUTI_FORMAT_U8 && f <= MAKTABASAUTI_FORMAT_F64; }

maktabasauti_result_t mks_pcm_convert(const void *src, maktabasauti_sample_format_t sf, void *dst, maktabasauti_sample_format_t df, size_t samples, mks_dither_t *dither) {
    size_t i, sb, db;
    const unsigned char *s = (const unsigned char *)src;
    unsigned char *d = (unsigned char *)dst;
    if ((sf & MAKTABASAUTI_FORMAT_PLANAR_FLAG) || (df & MAKTABASAUTI_FORMAT_PLANAR_FLAG)) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
    if (!valid_format(sf) || !valid_format(df) || (!src && samples) || (!dst && samples)) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    sb = maktabasauti_format_bytes(sf); db = maktabasauti_format_bytes(df);
    if (sf == df) { if (samples && s != d) memmove(d, s, samples * sb); return MAKTABASAUTI_OK; }
    /* rozne formaty: bufory nie moga sie nakladac */
    if (samples && s < d + samples * db && d < s + samples * sb) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    for (i = 0; i < samples; i++) store_sample(d + i * db, df, load_sample(s + i * sb, sf), dither);
    return MAKTABASAUTI_OK;
}

maktabasauti_result_t MAKTABASAUTI_CALL maktabasauti_pcm_convert(const void *src, maktabasauti_sample_format_t src_format, void *dst, maktabasauti_sample_format_t dst_format,
                                                                 size_t samples, uint32_t dither) {
    mks_dither_t d;
    if (dither > MAKTABASAUTI_DITHER_TPDF) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    mks_dither_init(&d, dither == MAKTABASAUTI_DITHER_TPDF);   /* bezstanowo: stale ziarno przy kazdym wywolaniu */
    return mks_pcm_convert(src, src_format, dst, dst_format, samples, &d);
}
