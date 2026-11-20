/* C odpowiednik sauti_decoder_avr.asm (patrz sauti_portable.h). */
#include "sauti_portable.h"
typedef struct { uint32_t rate, channels, bits, is_signed; uint64_t frames; const uint8_t *audio; } PortAvrMeta;
int sauti_avr_parse_asm(const uint8_t *data, size_t len, void *meta_out) {
    PortAvrMeta *m = (PortAvrMeta *)meta_out; uint32_t channels, bits, is_signed, rate, units; uint64_t need;
    if (!data || !m || len < 128u || memcmp(data, "2BIT", 4) != 0) return 0;
    { const unsigned c = sp_be16(data + 12); if (c == 0) channels = 1; else if (c == 0xffffu) channels = 2; else return 0; }
    bits = sp_be16(data + 14);
    if (bits != 8u && bits != 16u) return 0;
    { const unsigned s = sp_be16(data + 16); if (s == 0) is_signed = 0; else if (s == 0xffffu) is_signed = 1; else return 0; }
    if (data[40] != 0 || data[41] != 0) return 0;                                          /* zarezerwowana kompresja = 0 */
    rate = sp_be32(data + 22) & 0x00ffffffu;
    if (!rate) return 0;
    units = sp_be32(data + 26);
    if (!units || units % channels) return 0;                                              /* pelne ramki */
    need = (uint64_t)units * (bits >> 3);
    if (need > len - 128u) return 0;
    m->rate = rate; m->channels = channels; m->bits = bits; m->is_signed = is_signed; m->frames = units / channels; m->audio = data + 128;
    return 1;
}
void sauti_avr_pcm_to_f32_asm(const uint8_t *src, float *dst, size_t samples, uint32_t bits, uint32_t is_signed) {
    size_t i;
    for (i = 0; i < samples; i++) {
        if (bits == 8u) { const int32_t v = is_signed ? (int32_t)(int8_t)src[i] : (int32_t)src[i] - 128; dst[i] = (float)v * sp_u2f(0x3c000000u); }
        else { const uint16_t w = sp_be16(src + 2 * i); const int32_t v = is_signed ? (int32_t)(int16_t)w : (int32_t)w - 32768; dst[i] = (float)v * sp_u2f(0x38000000u); }
    }
}
