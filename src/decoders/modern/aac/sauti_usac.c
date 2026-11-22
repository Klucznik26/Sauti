/* USAC (xHE-AAC) UsacConfig parser and frequency-domain core decoder, see sauti_usac.h.  Syntax per ISO/IEC 23003-3 (UsacConfig, UsacFrame, fd_channel_stream, ics_info, tns_data); the transform
 * chain reuses the AAC building blocks (scale-factor Huffman code, TNS data, IMDCT, windows); the spectrum is arithmetic coded (sauti_usac_ac.c). */
#include "sauti_usac.h"
#include "sauti_usac_lpd.h"
#include "sauti_aac_lc_multi.h"
#include "sauti_usac_sbr.h"
#include "sauti_usac_tables.h"
#include "sauti_aac_bands.h"
#include "sauti_aac_huffman.h"
#include "sauti_aac_imdct.h"
#include "sauti_aac_tables.h"
#include "sauti_aac_tns.h"
#include "sauti_aac_window.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

#ifndef SAUTI_USAC_OUT_SCALE
#define SAUTI_USAC_OUT_SCALE (1.0f / 65536.0f)               /* the IMDCT is normalised 2/n: full scale 1.0 = 2^16 in the dequantised domain (as for AAC-LC) */
#endif

/* ---- UsacConfig ---------------------------------------------------------------------------------------------------------------------------------------------------------------------- */
static unsigned rate_index_of(unsigned rate) {                              /* the sampling_frequency_index whose band tables serve `rate`: the nominal rate each interval around it belongs to */
    static const unsigned limit[11] = { 92017, 75132, 55426, 46009, 37566, 27713, 23004, 18783, 13856, 11502, 9391 };
    unsigned i;
    for (i = 0; i < 11u; i++) if (rate >= limit[i]) return i;
    return 11u;
}
static void parse_sbr_config(SautiUsacBits *b, SautiUsacElement *e) {
    unsigned x1, x2;
    e->sbr_harmonic = sauti_usac_get(b, 1);
    e->sbr_intertes = sauti_usac_get(b, 1);
    e->sbr_pvc = sauti_usac_get(b, 1);
    e->sbr_start_freq = sauti_usac_get(b, 4);
    e->sbr_stop_freq = sauti_usac_get(b, 4);
    x1 = sauti_usac_get(b, 1); x2 = sauti_usac_get(b, 1);
    e->sbr_freq_scale = 2; e->sbr_alter_scale = 1; e->sbr_noise_bands = 2;
    if (x1) { e->sbr_freq_scale = sauti_usac_get(b, 2); e->sbr_alter_scale = sauti_usac_get(b, 1); e->sbr_noise_bands = sauti_usac_get(b, 2); }
    e->sbr_limiter_bands = 2; e->sbr_limiter_gains = 2; e->sbr_interpol_freq = 1; e->sbr_smoothing_mode = 1;
    if (x2) { e->sbr_limiter_bands = sauti_usac_get(b, 2); e->sbr_limiter_gains = sauti_usac_get(b, 2); e->sbr_interpol_freq = sauti_usac_get(b, 1); e->sbr_smoothing_mode = sauti_usac_get(b, 1); }
}
static int parse_mps212_config(SautiUsacBits *b, SautiUsacElement *e) {
    static const unsigned bands[8] = { 0, 28, 20, 14, 10, 7, 5, 4 }, phase[8] = { 0, 10, 10, 7, 5, 3, 2, 2 };
    e->mps_freq_res = sauti_usac_get(b, 3);
    if (!e->mps_freq_res) return 0;                                         /* reserved */
    e->mps_fixed_gain = sauti_usac_get(b, 3);
    e->mps_temp_shape = sauti_usac_get(b, 2);
    e->mps_decorr = sauti_usac_get(b, 2);
    e->mps_high_rate = sauti_usac_get(b, 1);
    e->mps_phase_coding = sauti_usac_get(b, 1);
    e->mps_otts_bands_phase = phase[e->mps_freq_res];
    if (sauti_usac_get(b, 1)) { e->mps_otts_bands_phase = sauti_usac_get(b, 5); if (e->mps_otts_bands_phase > bands[e->mps_freq_res]) return 0; }
    e->mps_residual_coding = e->stereo_config_index >= 2u;
    if (e->mps_residual_coding) {
        e->mps_residual_bands = sauti_usac_get(b, 5);
        if (e->mps_residual_bands > bands[e->mps_freq_res]) return 0;
        if (e->mps_otts_bands_phase < e->mps_residual_bands) e->mps_otts_bands_phase = e->mps_residual_bands;
        e->mps_pseudo_lr = sauti_usac_get(b, 1);
    }
    if (e->mps_temp_shape == 2u) e->mps_env_quant = sauti_usac_get(b, 1);
    return 1;
}
int sauti_usac_config_parse(const uint8_t *data, size_t size, size_t bit_offset, SautiUsacConfig *out) {
    static const unsigned chans[16] = { 0, 1, 2, 3, 4, 5, 6, 8, 0, 0, 0, 7, 8, 24, 8, 0 };
    SautiUsacBits b;
    SautiUsacConfig c;
    unsigned i, mult, div, total = 0;
    int unsup = 0;
    if (!data || !out || size > SIZE_MAX / 8u || bit_offset > size * 8u) return 0;
    b.data = data; b.nbits = size * 8u; b.pos = bit_offset;
    memset(&c, 0, sizeof c);
    c.sampling_index = sauti_usac_get(&b, 5);
    if (c.sampling_index == 0x1fu) c.sample_rate = sauti_usac_get(&b, 24);
    else { const int r = sauti_usac_samplerate[c.sampling_index]; c.sample_rate = r > 0 ? (unsigned)r : 0u; }
    if (!c.sample_rate) return 0;
    c.core_sbr_frame_len_idx = sauti_usac_get(&b, 3);
    if (c.core_sbr_frame_len_idx > 4u) return 0;
    c.core_frame_len = (c.core_sbr_frame_len_idx == 0 || c.core_sbr_frame_len_idx == 2) ? 768u : 1024u;
    c.sbr_mode = c.core_sbr_frame_len_idx == 2 ? 2u : c.core_sbr_frame_len_idx == 3 ? 1u : c.core_sbr_frame_len_idx == 4 ? 3u : 0u;
    mult = c.sbr_mode == 2u ? 8u : c.sbr_mode == 1u ? 2u : c.sbr_mode == 3u ? 4u : 1u;
    div = c.sbr_mode == 2u ? 3u : 1u;
    c.core_rate = (unsigned)((uint64_t)c.sample_rate * div / mult);
    c.out_frame_len = c.core_frame_len * mult / div;
    c.aac_rate_index = rate_index_of(c.sbr_mode == 2u ? c.core_rate * 4u / 3u : c.core_rate);   /* 8:3: a 768-line core at core_rate resolves the spectrum like a 1024-line core at 4/3 of it; the band tables follow that rate (libxaac streams, SBR-109) */
    c.channel_config = sauti_usac_get(&b, 5);
    if (c.channel_config) {
        if (c.channel_config > 15u || !chans[c.channel_config]) return 0;
        c.out_channels = chans[c.channel_config];
    } else {
        const unsigned n = sauti_usac_escaped(&b, 5, 8, 16);
        if (n < 1u || n > 64u) return 0;
        for (i = 0; i < n; i++) (void)sauti_usac_get(&b, 5);
        c.out_channels = n; unsup = 1;                                      /* explicit channel positions: not mapped to an output order yet */
    }
    c.num_elements = sauti_usac_escaped(&b, 4, 8, 16) + 1u;
    if (c.num_elements > SAUTI_USAC_MAX_ELEMENTS) return 0;
    for (i = 0; i < c.num_elements; i++) {
        SautiUsacElement *e = &c.e[i];
        e->type = sauti_usac_get(&b, 2);
        switch (e->type) {
        case SAUTI_USAC_SCE:
            e->tw_mdct = sauti_usac_get(&b, 1); e->noise_fill = sauti_usac_get(&b, 1);
            if (c.sbr_mode) parse_sbr_config(&b, e);
            total += 1u; break;
        case SAUTI_USAC_CPE:
            e->tw_mdct = sauti_usac_get(&b, 1); e->noise_fill = sauti_usac_get(&b, 1);
            if (c.sbr_mode) { parse_sbr_config(&b, e); e->stereo_config_index = sauti_usac_get(&b, 2); }
            if (e->stereo_config_index && !parse_mps212_config(&b, e)) return 0;
            total += 2u; break;
        case SAUTI_USAC_LFE:
            total += 1u; break;
        default: {
            unsigned cfg_len;
            e->ext_type = sauti_usac_escaped(&b, 4, 8, 16);
            cfg_len = sauti_usac_escaped(&b, 4, 8, 16);
            if (sauti_usac_get(&b, 1)) e->ext_default_len = sauti_usac_escaped(&b, 8, 16, 0) + 1u;
            e->ext_payload_frag = sauti_usac_get(&b, 1);
            if (cfg_len > (b.nbits - (b.pos < b.nbits ? b.pos : b.nbits)) / 8u) return 0;
            b.pos += (size_t)cfg_len * 8u;
            break; }
        }
        if (b.pos > b.nbits) return 0;
    }
    if (c.channel_config && total != c.out_channels) return 0;
    if (c.channel_config > 7u) unsup = 1;                                   /* 6.1 / 7.1 variants / 22.2: the output order is not mapped yet */
    if (sauti_usac_get(&b, 1)) {                                            /* usacConfigExtensionPresent: loudness, stream id, fill - nothing the decoder needs */
        const unsigned n = sauti_usac_escaped(&b, 2, 4, 8) + 1u;
        for (i = 0; i < n; i++) {
            unsigned len;
            (void)sauti_usac_escaped(&b, 4, 8, 16);
            len = sauti_usac_escaped(&b, 4, 8, 16);
            if (b.pos > b.nbits || len > (b.nbits - b.pos) / 8u) return 0;
            b.pos += (size_t)len * 8u;
        }
    }
    if (b.pos > b.nbits) return 0;
    c.bits_used = b.pos - bit_offset;
    *out = c;
    return unsup ? -1 : 1;
}

int sauti_usac_asc_parse_at(const uint8_t *data, size_t size, size_t bit_offset, SautiUsacConfig *out, size_t *asc_bits) {
    SautiUsacBits b;
    unsigned aot;
    int r;
    if (!data || !out || size < 3u || size > SIZE_MAX / 8u || bit_offset > size * 8u) return 0;
    b.data = data; b.nbits = size * 8u; b.pos = bit_offset;
    aot = sauti_usac_get(&b, 5);
    if (aot == 31u) aot = 32u + sauti_usac_get(&b, 6);
    if (aot != 42u) return 0;
    if (sauti_usac_get(&b, 4) == 15u) (void)sauti_usac_get(&b, 24);
    (void)sauti_usac_get(&b, 4);
    if (b.pos > b.nbits) return 0;
    r = sauti_usac_config_parse(data, size, b.pos, out);
    if (r != 0 && asc_bits) *asc_bits = (b.pos - bit_offset) + out->bits_used;
    return r;
}
int sauti_usac_asc_parse(const uint8_t *asc, size_t size, SautiUsacConfig *out) { return sauti_usac_asc_parse_at(asc, size, 0, out, NULL); }

void sauti_usac_state_init(SautiUsacState *s, const SautiUsacConfig *cfg) {
    unsigned i, ch = 0;
    memset(s, 0, sizeof *s);
    for (i = 0; i < SAUTI_USAC_MAX_CHANNELS; i++) { s->ch[i].prev_shape = 0xffu; sauti_usac_lpd_state_init(&s->ch[i].lpd, cfg->core_rate); }
    s->channels = cfg->out_channels < SAUTI_USAC_MAX_CHANNELS ? cfg->out_channels : SAUTI_USAC_MAX_CHANNELS;
    for (i = 0; i < SAUTI_USAC_MAX_CHANNELS; i++) { (void)sauti_aac_sbr_channel_init(&s->sbr_ch[i]); if (cfg->sbr_mode == 2u) s->sbr_ch[i].analysis_bands = 24u; else if (cfg->sbr_mode == 3u) s->sbr_ch[i].analysis_bands = 16u; }
    for (i = 0; i < cfg->num_elements; i++) {                              /* the noise generator starts at 0x3039 (first channel) / 0x10932 (second channel of a pair) */
        const SautiUsacElement *e = &cfg->e[i];
        if (e->type == SAUTI_USAC_EXT) continue;
        if (ch < SAUTI_USAC_MAX_CHANNELS) s->ch[ch].noise_seed = 0x3039u;
        if (e->type == SAUTI_USAC_CPE && ch + 1u < SAUTI_USAC_MAX_CHANNELS) s->ch[ch + 1u].noise_seed = 0x10932u;
        ch += e->type == SAUTI_USAC_CPE ? 2u : 1u;
    }
}

/* ---- frequency-domain channel ---------------------------------------------------------------------------------------------------------------------------------------------------- */
typedef struct {
    unsigned seq, shape, max_sfb, grouping, ngroups, glen[8], nwin, nswb, tns_max, wlen, stride;
    const uint16_t *swb;
    int16_t sfo[8][64];
    float coef[1024];
    unsigned noise_level, noise_offset, tns_data_present, tns_present;
    SautiAacTnsData tns;
    SautiUsacLpdFac fac;                                                    /* fac_data() of the frame (after an LPD frame) */
} Fd;

static int fd_setup(const SautiUsacConfig *cfg, Fd *fd) {
    const unsigned rate = cfg->aac_rate_index, is_short = fd->seq == 2u, short768 = cfg->core_frame_len == 768u;
    unsigned count = 0, j;
    if (rate > 12u) return 0;
    if (is_short) {
        if (short768) { fd->swb = sauti_usac_swb96[rate]; count = sauti_usac_num_swb96[rate]; }
        else if (!sauti_aac_band_offsets(rate, 1, &fd->swb, &count)) return 0;
        fd->tns_max = sauti_usac_tns_max_bands_128[rate];
        fd->wlen = cfg->core_frame_len / 8u; fd->stride = 128u; fd->nwin = 8u;
        fd->ngroups = 0;
        for (j = 0; j < 8u; j++) fd->glen[j] = 0;
        fd->glen[0] = 1;
        for (j = 0; j < 7u; j++) {
            if (fd->grouping & (1u << (6u - j))) fd->glen[fd->ngroups]++;
            else { fd->ngroups++; fd->glen[fd->ngroups] = 1; }
        }
        fd->ngroups++;
    } else {
        if (short768) { fd->swb = sauti_usac_swb768[rate]; count = sauti_usac_num_swb768[rate]; }
        else if (!sauti_aac_band_offsets(rate, 0, &fd->swb, &count)) return 0;
        fd->tns_max = sauti_usac_tns_max_bands_1024[rate];
        fd->wlen = cfg->core_frame_len; fd->stride = cfg->core_frame_len; fd->nwin = 1u;
        fd->ngroups = 1u; fd->glen[0] = 1u;
    }
    fd->nswb = count;
    return fd->max_sfb <= count;
}
static int ics_info(SautiUsacBits *b, Fd *fd) {
    fd->seq = sauti_usac_get(b, 2);
    fd->shape = sauti_usac_get(b, 1);
    if (fd->seq == 2u) { fd->max_sfb = sauti_usac_get(b, 4); fd->grouping = sauti_usac_get(b, 7); }
    else { fd->max_sfb = sauti_usac_get(b, 6); fd->grouping = 0; }
    return 1;
}
static int parse_tns(SautiUsacBits *b, Fd *fd) {                            /* tns_data(): as in AAC but a long filter has a 4-bit order (the AAC syntax has 5) */
    const unsigned is_short = fd->seq == 2u, countbits = is_short ? 1u : 2u, lengthbits = is_short ? 4u : 6u, orderbits = is_short ? 3u : 4u;
    unsigned w, f, i;
    memset(&fd->tns, 0, sizeof fd->tns);
    fd->tns_present = 1;
    for (w = 0; w < fd->nwin; w++) {
        const unsigned count = sauti_usac_get(b, countbits);
        unsigned resolution = 0;
        if (count > SAUTI_AAC_TNS_MAX_FILTERS) return 0;
        fd->tns.filter_count[w] = (uint8_t)count;
        if (count) resolution = sauti_usac_get(b, 1);
        for (f = 0; f < count; f++) {
            SautiAacTnsFilter *filter = &fd->tns.filter[w][f];
            unsigned width, direction = 0, compress = 0;
            int32_t sign;
            filter->length = (uint8_t)sauti_usac_get(b, lengthbits);
            filter->order = (uint8_t)sauti_usac_get(b, orderbits);
            filter->resolution = (uint8_t)(resolution ? 4u : 3u);
            if (!filter->order) continue;
            direction = sauti_usac_get(b, 1); compress = sauti_usac_get(b, 1);
            filter->direction = (uint8_t)direction; filter->compress = (uint8_t)compress;
            width = filter->resolution - compress;
            sign = (int32_t)1 << (width - 1u);
            for (i = 0; i < filter->order; i++) filter->coefficients[i] = (int8_t)((int32_t)(sauti_usac_get(b, width) ^ (uint32_t)sign) - sign);
        }
    }
    return b->pos <= b->nbits;
}
static int parse_scalefactors(SautiUsacBits *b, Fd *fd, unsigned global_gain) {
    SautiAacHuffNode nodes[SAUTI_AAC_HUFF_MAX_NODES];
    const size_t count = sauti_aac_standard_tree_build(0, nodes, SAUTI_AAC_HUFF_MAX_NODES);
    int off = (int)global_gain;
    unsigned g, sfb;
    if (!count) return 0;
    for (g = 0; g < fd->ngroups; g++)
        for (sfb = 0; sfb < fd->max_sfb; sfb++) {
            if (g || sfb) {
                int delta = 0;
                if (!sauti_aac_scalefactor_symbol_decode(b->data, (b->nbits + 7u) / 8u, &b->pos, nodes, count, &delta)) return 0;
                off += delta;
            }
            if (off < 0 || off > 255) return 0;
            fd->sfo[g][sfb] = (int16_t)(off - 100);
        }
    return 1;
}
static float noise_sign(uint32_t *seed) {
    *seed = *seed * 69069u + 5u;
    return (*seed & 0x10000u) ? -1.0f : 1.0f;
}
static void noise_fill(const SautiUsacConfig *cfg, Fd *fd, uint32_t *seed) {
    const float noise_val = fd->noise_level ? powf(2.0f, ((float)fd->noise_level - 14.0f) / 3.0f) : 0.0f;   /* level 0 = no noise, but the generator still runs (FDK, the reference decoder: the random sequence advances and noise_offset applies to empty bands) */
    const int noise_offset = (int)fd->noise_offset - 16;
    const unsigned band_off = sauti_usac_noise_fill_start[(cfg->core_frame_len == 768u ? 2u : 0u) + (fd->seq == 2u ? 1u : 0u)];
    unsigned g, sfb, win = 0, w, z;
    for (g = 0; g < fd->ngroups; g++) {
        for (sfb = 0; sfb < fd->max_sfb; sfb++) {
            const unsigned lo = fd->swb[sfb], len = fd->swb[sfb + 1u] - lo;
            int zero = 1;
            if (lo < band_off) continue;
            for (w = 0; w < fd->glen[g]; w++) {
                float *cb = fd->coef + (win + w) * fd->stride + lo;
                for (z = 0; z < len; z++) { if (cb[z] == 0.0f) cb[z] = noise_sign(seed) * noise_val; else zero = 0; }
            }
            if (zero) { int v = fd->sfo[g][sfb] + noise_offset; fd->sfo[g][sfb] = (int16_t)(v < -200 ? -200 : v); }
        }
        win += fd->glen[g];
    }
}
static void scale_spectrum(Fd *fd) {
    unsigned g, sfb, win = 0, w, z;
    for (g = 0; g < fd->ngroups; g++) {
        for (sfb = 0; sfb < fd->max_sfb; sfb++) {
            const unsigned lo = fd->swb[sfb], len = fd->swb[sfb + 1u] - lo;
            const float gain = (float)exp2((double)fd->sfo[g][sfb] * 0.25);
            for (w = 0; w < fd->glen[g]; w++) {
                float *cb = fd->coef + (win + w) * fd->stride + lo;
                for (z = 0; z < len; z++) cb[z] *= gain;
            }
        }
        win += fd->glen[g];
    }
}
static int tns_apply(Fd *fd) {                                              /* inverse TNS of every window, the filters run from the top band downwards (tns_data of ISO/IEC 14496-3) */
    const double pi = 3.14159265358979323846264338327950288;
    unsigned w, j, k, i, m;
    const unsigned cap = fd->tns_max < fd->max_sfb ? fd->tns_max : fd->max_sfb;
    for (w = 0; w < fd->nwin; w++) {
        unsigned bottom = fd->nswb;
        if (fd->tns.filter_count[w] > (fd->seq == 2u ? 1u : 3u)) return 0;
        for (j = 0; j < fd->tns.filter_count[w]; j++) {
            const SautiAacTnsFilter *f = &fd->tns.filter[w][j];
            const unsigned top = bottom;
            unsigned low, high, start, stop;
            float lpc[SAUTI_AAC_TNS_MAX_ORDER] = { 0 }, temp[SAUTI_AAC_TNS_MAX_ORDER] = { 0 };
            double iqpos, iqneg;
            int dir, index;
            bottom = f->length > top ? 0u : top - f->length;
            if (!f->order) continue;
            iqpos = ((1 << (f->resolution - 1)) - 0.5) / (pi / 2.0);
            iqneg = ((1 << (f->resolution - 1)) + 0.5) / (pi / 2.0);
            for (k = 0; k < f->order; k++) {
                const int q = f->coefficients[k];
                const float refl = (float)sin((double)q / (q >= 0 ? iqpos : iqneg));
                for (i = 0; i < k; i++) temp[i] = lpc[i] + refl * lpc[k - 1u - i];
                temp[k] = refl;
                for (i = 0; i <= k; i++) lpc[i] = temp[i];
            }
            low = bottom < cap ? bottom : cap; high = top < cap ? top : cap;
            start = fd->swb[low]; stop = fd->swb[high];
            if (start >= stop) continue;
            dir = f->direction ? -1 : 1;
            index = (int)(w * fd->stride + (dir < 0 ? stop - 1u : start));
            for (m = 0; m < stop - start; m++, index += dir) {
                float v = fd->coef[index];
                const unsigned past = m < f->order ? m : f->order;
                for (k = 1; k <= past; k++) v -= lpc[k - 1u] * fd->coef[index - (int)k * dir];
                if (!isfinite(v)) return 0;
                fd->coef[index] = v;
            }
        }
    }
    return 1;
}
static int synthesize(const SautiUsacConfig *cfg, Fd *fd, SautiUsacChannel *ch, float *pcm) {
    float time[2048], next[1024];
    const unsigned n = cfg->core_frame_len;
    unsigned w, i;
    if (fd->seq == 2u) { for (w = 0; w < 8u; w++) if (!sauti_aac_imdct(fd->coef + w * fd->stride, fd->wlen, time + w * 2u * fd->wlen)) return 0; }
    else if (!sauti_aac_imdct(fd->coef, n, time)) return 0;
    if (ch->lpd.prev_lpd && fd->seq == 2u) {                                /* eight short windows after an LPD frame: the first window starts with the transition slope (sine, 128 samples) over the LPD overlap, the FAC fills [512, 640) */
        float ovl[1024], fs[128], prev[1024], o[1024], x0[1024];
        const float sc = 1.0f / (SAUTI_USAC_OUT_SCALE * 32768.0f);
        int r = sauti_usac_lpd_fd_short_begin(&ch->lpd, n, &fd->fac, ovl, fs);
        if (r != 1) return r;
        memset(prev, 0, sizeof prev);
        for (i = 0; i < 448u; i++) prev[i] = ovl[i] * sc;
        if (fd->fac.present) { for (i = 0; i < 64u; i++) prev[448u + i] = ovl[448u + i] * sc; memset(time, 0, 64u * sizeof(float)); }
        else for (i = 0; i < 128u; i++) prev[448u + i] = ovl[448u + i] * sc * (float)sin(3.14159265358979323846 * (127.0 - (double)i + 0.5) / 256.0);
        (void)x0;
        if (!sauti_aac_window_synth_n(n, 2u, 0u, fd->shape, time, prev, pcm, next)) return 0;
        for (i = 0; i < n; i++) o[i] = pcm[i] * (SAUTI_USAC_OUT_SCALE * 32768.0f);
        if (fd->fac.present) for (i = 0; i < 128u; i++) o[512u + i] += fs[i];
        r = sauti_usac_lpd_fd_finish(&ch->lpd, 1, o);
        if (r != 1) return r;
        memcpy(ch->overlap, next, n * sizeof(float));
        for (i = 0; i < n; i++) pcm[i] = o[i] * (1.0f / 32768.0f);
        for (i = 0; i < n; i++) ch->fd_tail[i] = next[i] * (SAUTI_USAC_OUT_SCALE * 32768.0f);
        for (i = 0; i < fd->wlen; i++) ch->fd_tail[(n - fd->wlen) / 2u + i] = time[7u * 2u * fd->wlen + fd->wlen + i] * (SAUTI_USAC_OUT_SCALE * 32768.0f);
        ch->fd_short = 1;
        ch->prev_shape = fd->shape;
        ch->fd_decoded = 1;
        return 1;
    }
    if (!sauti_aac_window_synth_n(n, fd->seq, ch->prev_shape == 0xffu ? fd->shape : ch->prev_shape, fd->shape, time, ch->overlap, pcm, next)) return 0;
    memcpy(ch->overlap, next, n * sizeof(float));
    for (i = 0; i < n; i++) pcm[i] *= SAUTI_USAC_OUT_SCALE;
    if (ch->lpd.prev_lpd) {                                                 /* the frame follows an LPD frame: its start is the look-ahead of the LPD synthesis plus the aliasing cancellation (stop window with the slope of ccfl / 4) */
        float x[1024], o[1024];
        int r;
        for (i = 0; i < n; i++) x[i] = time[i] * (SAUTI_USAC_OUT_SCALE * 32768.0f);
        r = sauti_usac_lpd_fd_frame(&ch->lpd, n, fd->seq, x, &fd->fac, o);
        if (r != 1) return r;
        for (i = 0; i < n; i++) pcm[i] = o[i] * (1.0f / 32768.0f);
    }
    for (i = 0; i < n; i++) ch->fd_tail[i] = (fd->seq == 2u ? next[i] : time[n + i]) * (SAUTI_USAC_OUT_SCALE * 32768.0f);   /* the tail an LPD frame takes over: unwindowed after a long frame; after eight short windows the overlap of the first seven (windowed) and the RAW second half of the last window */
    if (fd->seq == 2u) for (i = 0; i < fd->wlen; i++) ch->fd_tail[(n - fd->wlen) / 2u + i] = time[7u * 2u * fd->wlen + fd->wlen + i] * (SAUTI_USAC_OUT_SCALE * 32768.0f);
    ch->fd_short = fd->seq == 2u;
    ch->prev_shape = fd->shape;
    ch->fd_decoded = 1;
    return 1;
}

/* fd_channel_stream() after the stereo info: global_gain, noise filling data, ics_info (unless common), scale factors, TNS, arithmetic spectrum.  0 = malformed, -1 = unsupported. */
static int fd_channel(const SautiUsacConfig *cfg, const SautiUsacElement *e, SautiUsacBits *b, SautiUsacChannel *ch, Fd *fd, int common_window, int common_tw, int indep, unsigned *reason) {
    unsigned global_gain, w;
    int32_t q[1024];
    global_gain = sauti_usac_get(b, 8);
    fd->noise_level = fd->noise_offset = 0;
    if (e->noise_fill) { fd->noise_level = sauti_usac_get(b, 3); fd->noise_offset = sauti_usac_get(b, 5); }
    if (!common_window) { ics_info(b, fd); if (!fd_setup(cfg, fd) || (e->type == SAUTI_USAC_LFE && fd->seq != 0u)) { *reason = SAUTI_AAC_REASON_INVALID_SCE; return 0; } }   /* an LFE has long windows only */
    if (e->tw_mdct && !common_tw) {
        if (sauti_usac_get(b, 1)) { *reason = SAUTI_AAC_REASON_UNSUPPORTED_ELEMENT; return -1; }   /* tw_data: time-warped MDCT */
    }
    if (!parse_scalefactors(b, fd, global_gain)) { *reason = SAUTI_AAC_REASON_INVALID_SCALEFACTORS; return 0; }
    if (fd->tns_data_present && !parse_tns(b, fd)) { *reason = SAUTI_AAC_REASON_TNS; return 0; }
    {
        const int reset = indep ? 1 : (int)sauti_usac_get(b, 1);              /* arith_reset_flag */
        const unsigned lg = fd->swb[fd->max_sfb];
        memset(fd->coef, 0, sizeof fd->coef);
        for (w = 0; w < fd->nwin; w++) {
            unsigned i;
            if (!sauti_usac_ac_decode(b, &ch->ac, reset && w == 0, lg, fd->wlen, q)) { *reason = SAUTI_AAC_REASON_INVALID_SPECTRAL; return 0; }
            for (i = 0; i < lg; i++) {
                const int32_t v = q[i];
                const int32_t a = v < 0 ? -v : v;
                if (a) { const float m = (float)((double)a * cbrt((double)a)); fd->coef[w * fd->stride + i] = v < 0 ? -m : m; }
            }
        }
    }
    if (sauti_usac_get(b, 1)) {                                                 /* fac_data_present: the forward aliasing cancellation of a frame that follows an LPD frame */
        if (!sauti_usac_lpd_fac_parse(b, fd->seq == 2u ? cfg->core_frame_len / 16u : cfg->core_frame_len / 8u, 1, &fd->fac)) { *reason = SAUTI_AAC_REASON_INVALID_SCE; return 0; }
    }
    return 1;
}

/* ---- complex stereo prediction (ISO/IEC 23003-3 7.7, cplx_pred_data) -------------------------------------------------------------------------------------------------------- */
#define mdst_prev sauti_usac_mdst_filt_prev                                /* filters of the previous frame's downmix: derived from the definition, see tools/gen_usac_mdst.py */
typedef struct { uint8_t used[8][64]; int16_t re[8][64], im[8][64]; unsigned pred_dir, complex_coef, use_prev; } Cplx;

static int cplx_parse(SautiUsacBits *b, const Fd *f0, unsigned mss, int indep, SautiUsacChannel *c0, Cplx *cp) {
    SautiAacHuffNode nodes[SAUTI_AAC_HUFF_MAX_NODES];
    const size_t count = sauti_aac_standard_tree_build(0, nodes, SAUTI_AAC_HUFF_MAX_NODES);
    unsigned g, sfb, delta_code_time = 0, i;
    if (!count) return 0;
    memset(cp, 0, sizeof *cp);
    if ((f0->seq == 2u && c0->last_seq != 2u) || (c0->last_seq == 2u && f0->seq != 2u)) { memset(c0->alpha_prev_re, 0, sizeof c0->alpha_prev_re); memset(c0->alpha_prev_im, 0, sizeof c0->alpha_prev_im); }
    if (!sauti_usac_get(b, 1)) {                                            /* cplx_pred_all = 0: one flag per pair of bands */
        for (g = 0; g < f0->ngroups; g++) for (sfb = 0; sfb < mss; sfb += 2u) { const uint8_t v = (uint8_t)sauti_usac_get(b, 1); cp->used[g][sfb] = v; if (sfb + 1u < mss) cp->used[g][sfb + 1u] = v; }
    } else for (g = 0; g < f0->ngroups; g++) for (sfb = 0; sfb < mss; sfb++) cp->used[g][sfb] = 1;
    cp->pred_dir = sauti_usac_get(b, 1);
    cp->complex_coef = sauti_usac_get(b, 1);
    if (cp->complex_coef && !indep) cp->use_prev = sauti_usac_get(b, 1);
    if (!indep) delta_code_time = sauti_usac_get(b, 1);
    for (g = 0; g < f0->ngroups; g++) {
        for (sfb = 0; sfb < mss; sfb += 2u) {
            int last_re = 0, last_im = 0;
            if (delta_code_time) {
                if (g) { last_re = cp->re[g - 1u][sfb]; last_im = cp->im[g - 1u][sfb]; }
                else if (f0->seq == 2u && c0->last_seq == 2u) {
                    if (!c0->prev_ngroups) return 0;
                    last_re = c0->alpha_prev_re[c0->prev_ngroups - 1u][sfb]; last_im = c0->alpha_prev_im[c0->prev_ngroups - 1u][sfb];
                } else { last_re = c0->alpha_prev_re[g][sfb]; last_im = c0->alpha_prev_im[g][sfb]; }
            } else if (sfb) { last_re = cp->re[g][sfb - 1u]; last_im = cp->im[g][sfb - 1u]; }
            if (cp->used[g][sfb]) {
                int32_t d = 0;
                if (!sauti_aac_scalefactor_symbol_decode(b->data, (b->nbits + 7u) / 8u, &b->pos, nodes, count, &d)) return 0;
                cp->re[g][sfb] = (int16_t)(last_re - d);
                if (cp->complex_coef) {
                    if (!sauti_aac_scalefactor_symbol_decode(b->data, (b->nbits + 7u) / 8u, &b->pos, nodes, count, &d)) return 0;
                    cp->im[g][sfb] = (int16_t)(last_im - d);
                }
            }
            if (sfb + 1u < mss) { cp->re[g][sfb + 1u] = cp->re[g][sfb]; cp->im[g][sfb + 1u] = cp->im[g][sfb]; }
        }
    }
    memset(c0->alpha_prev_re, 0, sizeof c0->alpha_prev_re); memset(c0->alpha_prev_im, 0, sizeof c0->alpha_prev_im);
    for (g = 0; g < f0->ngroups; g++) for (i = 0; i < mss; i += 2u) { c0->alpha_prev_re[g][i] = cp->re[g][i]; c0->alpha_prev_im[g][i] = cp->im[g][i]; }
    return b->pos <= b->nbits;
}
/* out[j] += sign(j) * sum_k tap(k) * in[j + k]; the input is mirrored at both ends (x[-1-k] = x[k], x[n+k] = x[n-1-k]) */
static float mirrored(const float *in, int n, int i) { if (i < 0) i = -1 - i; if (i >= n) i = 2 * n - 1 - i; return in[i]; }
static void mdst_cur(const float *in, int n, const float h[7], float *out) {                   /* antisymmetric: taps +c0 +c1 +c2 0 -c2 -c1 -c0 at offsets +3..-3 */
    int j, i;
    for (j = 0; j < n; j++) { float s = 0.0f; for (i = 0; i < 3; i++) s += h[i] * (mirrored(in, n, j + 3 - i) - mirrored(in, n, j - 3 + i)); out[j] += s; }
}
static void mdst_add_prev(const float *in, int n, const float p[4], float *out) {                  /* symmetric taps, the sign alternates with the line (even: -, odd: +) */
    int j, i;
    for (j = 0; j < n; j++) {
        float s = p[3] * mirrored(in, n, j);
        for (i = 0; i < 3; i++) s += p[i] * (mirrored(in, n, j - 3 + i) + mirrored(in, n, j + 3 - i));
        out[j] += (j & 1) ? s : -s;
    }
}
static void cplx_apply(const SautiUsacConfig *cfg, Fd *f0, Fd *f1, unsigned mss, const Cplx *cp, const SautiUsacChannel *c0, const SautiUsacChannel *c1) {
    const unsigned wlen = f0->wlen, stride = f0->stride;
    const float dir = cp->pred_dir ? -1.0f : 1.0f;
    const unsigned cur_shape = f0->shape, prev_shape = c0->last_shape;
    const unsigned shape_pair = (prev_shape == 0u && cur_shape == 0u) ? 0u : (prev_shape == 1u && cur_shape == 1u) ? 1u : (prev_shape == 0u) ? 2u : 3u;
    const int after_short = c0->last_seq == 2u || c0->last_seq == 1u;                          /* a stop-start sequence follows an eight-short or a start sequence */
    unsigned cur_win = f0->seq == 1u ? (after_short ? 3u : 1u) : f0->seq == 3u ? 2u : 0u;
    unsigned prev_row = (f0->seq == 3u || (f0->seq == 1u && after_short)) ? 2u + prev_shape : prev_shape;
    float dmx_re[1024], dmx_im[1024], dmx_prev[1024];
    unsigned g, w = 0, k, sfb;
    (void)cfg;
    memset(dmx_prev, 0, sizeof dmx_prev);
    if (cp->use_prev) {                                                    /* downmix of the previous frame: its last window when this frame has eight short windows */
        const unsigned off = f0->seq == 2u ? 7u * stride : 0u;
        for (k = 0; k < wlen; k++) dmx_prev[k] = 0.5f * (c0->prev_coef[off + k] + dir * c1->prev_coef[off + k]);
    }
    for (g = 0; g < f0->ngroups; g++) {
        for (k = 0; k < f0->glen[g]; k++, w++) {
            float *l = f0->coef + w * stride, *r = f1->coef + w * stride, *re = dmx_re + w * stride, *im = dmx_im + w * stride;
            unsigned top = f0->swb[mss];
            for (sfb = 0; sfb < mss; sfb++) {
                unsigned i;
                for (i = f0->swb[sfb]; i < f0->swb[sfb + 1u]; i++) re[i] = cp->used[g][sfb] ? l[i] : 0.5f * (l[i] + dir * r[i]);
            }
            for (; top < wlen; top++) re[top] = 0.0f;
            memset(im, 0, wlen * sizeof(float));
            if (cp->complex_coef) {
                if (f0->seq == 2u && w != 0) {
                    mdst_cur(re, (int)wlen, sauti_usac_mdst_filt_cur[0][cur_shape == 1u ? 1u : 0u], im);
                    mdst_add_prev(dmx_re + (w - 1u) * stride, (int)wlen, mdst_prev[cur_shape], im);
                } else {
                    mdst_cur(re, (int)wlen, sauti_usac_mdst_filt_cur[f0->seq == 2u ? 0u : cur_win][shape_pair], im);
                    if (cp->use_prev) mdst_add_prev(dmx_prev, (int)wlen, mdst_prev[f0->seq == 2u ? prev_shape : prev_row], im);
                }
            }
            for (sfb = 0; sfb < mss; sfb++) {
                const float a_re = 0.1f * (float)cp->re[g][sfb], a_im = 0.1f * (float)cp->im[g][sfb];
                unsigned i;
                if (!cp->used[g][sfb]) continue;
                for (i = f0->swb[sfb]; i < f0->swb[sfb + 1u]; i++) {
                    const float side = r[i] - a_re * re[i] - a_im * im[i];
                    const float left = l[i] + side, right = (l[i] - side) * dir;
                    l[i] = left; r[i] = right;
                }
            }
        }
    }
}

/* An SCE / CPE whose channels are all LPD. */
static int lpd_element(const SautiUsacConfig *cfg, const SautiUsacElement *e, SautiUsacBits *b, SautiUsacChannel *chs, SautiUsacSbrState *sbrst, SautiAacSbrChannel *sbrch, int indep, float out[2][4096], unsigned *reason) {
    const unsigned nch = e->type == SAUTI_USAC_CPE ? 2u : 1u;
    SautiUsacLpdFrame *lf = (SautiUsacLpdFrame *)malloc(2u * sizeof *lf);
    SautiAacSbrPayloadFrame sbr_pl[2];
    SautiAacSbrComplex *grid = NULL;
    float core[2][1024];
    unsigned c, i, sbr_cpl = 0;
    int r = 1;
    if (!lf) { *reason = SAUTI_AAC_REASON_INVALID_SYNTH; return 0; }
    if (e->stereo_config_index) { *reason = SAUTI_AAC_REASON_UNSUPPORTED_ELEMENT; r = -1; goto done; }
    for (c = 0; c < nch; c++)
        if (!sauti_usac_lpd_parse(b, cfg->core_frame_len, indep, &chs[c].ac, &lf[c])) { *reason = SAUTI_AAC_REASON_INVALID_SCE; r = 0; goto done; }
    if (cfg->sbr_mode) {                                                    /* usac_sbr_data() follows the channel streams */
        sbrst->header.slot_scale = cfg->sbr_mode == 3u ? 4u : 2u;
        r = sauti_usac_sbr_parse(e, nch, b, indep, cfg->sample_rate, sbrst, sbr_pl, &sbr_cpl);
        if (r != 1) { *reason = r < 0 ? SAUTI_AAC_REASON_UNSUPPORTED_ELEMENT : SAUTI_AAC_REASON_INVALID_SCE; goto done; }
    }
    for (c = 0; c < nch; c++) {
        float pcm[1024];
        const int d = sauti_usac_lpd_decode(&chs[c].lpd, &lf[c], cfg->core_frame_len, &chs[c].noise_seed, chs[c].fd_decoded ? chs[c].fd_tail : NULL, (int)chs[c].fd_short, pcm);
        if (d != 1) { *reason = d < 0 ? SAUTI_AAC_REASON_UNSUPPORTED_ELEMENT : SAUTI_AAC_REASON_INVALID_SYNTH; r = d < 0 ? -1 : 0; goto done; }
        for (i = 0; i < cfg->core_frame_len; i++) core[c][i] = pcm[i] * (1.0f / 32768.0f);
        if (!cfg->sbr_mode) memcpy(out[c], core[c], cfg->core_frame_len * sizeof(float));
    }
    if (cfg->sbr_mode) {
        grid = (SautiAacSbrComplex *)malloc(sizeof(SautiAacSbrComplex) * 2u * SAUTI_AAC_SBR_CHANNEL_GRID);
        if (!grid) { *reason = SAUTI_AAC_REASON_INVALID_SYNTH; r = 0; goto done; }
        for (c = 0; c < nch; c++)
            if (!sauti_aac_sbr_channel_frame(&sbrch[c], core[c], &sbr_pl[c], grid) || !sauti_aac_sbr_channel_synthesize(&sbrch[c], grid, out[c])) { *reason = SAUTI_AAC_REASON_INVALID_SYNTH; r = 0; goto done; }
    }
    r = 1;
done:
    free(grid); free(lf);
    return r;
}

/* One SCE / LFE / CPE.  out[c] = PCM of channel c.  1 ok, 0 malformed, -1 unsupported. */
static int element(const SautiUsacConfig *cfg, const SautiUsacElement *e, SautiUsacBits *b, SautiUsacChannel *chs, SautiUsacSbrState *sbrst, SautiAacSbrChannel *sbrch, int indep, float out[2][4096], unsigned *reason) {
    const unsigned nch = (e->type == SAUTI_USAC_CPE && e->stereo_config_index != 1u) ? 2u : 1u;     /* stereoConfigIndex 1: one core channel (the downmix), 2 / 3: the downmix and a residual */
    Fd fdv[2];
    Fd *fd = fdv;
    unsigned c, g, sfb, w, z;
    int common_window = 0, common_tw = 0, tns_on_lr = 0, ms_mode = 0, r;
    uint8_t ms_mask[8][64];
    unsigned max_sfb_ste = 0;
    Cplx cp;
    SautiAacSbrPayloadFrame sbr_pl[2];
    unsigned sbr_cpl = 0;
    float core[2][1024];
    SautiUsacMpsConfig mc;
    SautiUsacMpsFrame mf;
    int mps_on = 0;
    memset(&mc, 0, sizeof mc);
    memset(&cp, 0, sizeof cp);
    memset(fdv, 0, sizeof fdv);
    memset(ms_mask, 0, sizeof ms_mask);
    if (e->type == SAUTI_USAC_LFE) {                                       /* UsacLfeElement: core_mode = 0 is implicit, fd_channel_stream(0, 0, 0, 0, indep): no tns_data_present, no noise filling */
        if (cfg->sbr_mode) { *reason = SAUTI_AAC_REASON_UNSUPPORTED_ELEMENT; return -1; }   /* an LFE has no eSBR data: the reference upsamples it through the QMF, not implemented */
    } else {
        unsigned core_mode[2] = { 0, 0 };
        for (c = 0; c < nch; c++) core_mode[c] = sauti_usac_get(b, 1);
        if (core_mode[0] || core_mode[1]) {                                 /* core_mode 1: LPD (ACELP / TCX) */
            if (core_mode[0] != core_mode[nch - 1u] || e->tw_mdct) { *reason = SAUTI_AAC_REASON_UNSUPPORTED_ELEMENT; return -1; }   /* a pair of one FD and one LPD channel, time-warped MDCT */
            return lpd_element(cfg, e, b, chs, sbrst, sbrch, indep, out, reason);
        }
    }
    if (nch == 1u) {
        if (e->type != SAUTI_USAC_LFE) fd[0].tns_data_present = sauti_usac_get(b, 1);
    } else {
        const unsigned tns_active = sauti_usac_get(b, 1);
        common_window = (int)sauti_usac_get(b, 1);
        if (common_window) {
            ics_info(b, &fd[0]);
            fd[1].seq = fd[0].seq; fd[1].shape = fd[0].shape; fd[1].grouping = fd[0].grouping;
            fd[1].max_sfb = fd[0].max_sfb;
            if (!sauti_usac_get(b, 1)) fd[1].max_sfb = sauti_usac_get(b, fd[1].seq == 2u ? 4u : 6u);   /* common_max_sfb == 0 */
            if (!fd_setup(cfg, &fd[0]) || !fd_setup(cfg, &fd[1])) { *reason = SAUTI_AAC_REASON_INVALID_SCE; return 0; }
            max_sfb_ste = fd[0].max_sfb > fd[1].max_sfb ? fd[0].max_sfb : fd[1].max_sfb;
            ms_mode = (int)sauti_usac_get(b, 2);
            if (ms_mode == 1) {
                for (g = 0; g < fd[0].ngroups; g++) for (sfb = 0; sfb < max_sfb_ste; sfb++) ms_mask[g][sfb] = (uint8_t)sauti_usac_get(b, 1);
            } else if (ms_mode == 2) {
                memset(ms_mask, 1, sizeof ms_mask);
            } else if (ms_mode == 3) {                                      /* complex stereo prediction (plain stereo only: with MPS212 the field is reserved) */
                if (e->stereo_config_index) { *reason = SAUTI_AAC_REASON_MID_SIDE; return -1; }
                if (!cplx_parse(b, &fd[0], max_sfb_ste, indep, &chs[0], &cp)) { *reason = SAUTI_AAC_REASON_MID_SIDE; return 0; }
            }
        }
        if (e->tw_mdct) { (void)sauti_usac_get(b, 1); *reason = SAUTI_AAC_REASON_UNSUPPORTED_ELEMENT; return -1; }
        if (tns_active) {
            int common_tns = 0;
            if (common_window) common_tns = (int)sauti_usac_get(b, 1);
            tns_on_lr = (int)sauti_usac_get(b, 1);
            if (common_tns) {
                if (!parse_tns(b, &fd[0])) { *reason = SAUTI_AAC_REASON_TNS; return 0; }
                fd[1].tns = fd[0].tns; fd[1].tns_present = 1;
            } else if (sauti_usac_get(b, 1)) { fd[0].tns_data_present = fd[1].tns_data_present = 1; }
            else { fd[1].tns_data_present = sauti_usac_get(b, 1); fd[0].tns_data_present = !fd[1].tns_data_present; }
        }
    }
    for (c = 0; c < nch; c++) {
        r = fd_channel(cfg, e, b, &chs[c], &fd[c], common_window, common_tw, indep, reason);
        if (r != 1) return r;
    }
    if (e->stereo_config_index) {                                           /* MPS212: eSBR data of ONE channel (the downmix), then Mps212Data() */
        unsigned slots = cfg->sbr_mode == 3u ? 64u : 32u;
        mps_on = 1;
        if (!cfg->sbr_mode || e->stereo_config_index == 3u) { *reason = SAUTI_AAC_REASON_UNSUPPORTED_ELEMENT; return -1; }
        sbrst->header.slot_scale = cfg->sbr_mode == 3u ? 4u : 2u;
        r = sauti_usac_sbr_parse(e, 1u, b, indep, cfg->sample_rate, sbrst, sbr_pl, &sbr_cpl);
        if (r != 1) { *reason = r < 0 ? SAUTI_AAC_REASON_UNSUPPORTED_ELEMENT : SAUTI_AAC_REASON_INVALID_SCE; return r; }
        mc.freq_res = e->mps_freq_res; mc.high_rate = e->mps_high_rate; mc.phase_coding = e->mps_phase_coding; mc.bands_phase = e->mps_otts_bands_phase; mc.temp_shape = e->mps_temp_shape; mc.env_quant = e->mps_env_quant; mc.decorr = e->mps_decorr; mc.fixed_gain = e->mps_fixed_gain; mc.residual = e->stereo_config_index >= 2u; mc.residual_bands = e->mps_residual_bands;
        r = sauti_usac_mps212_parse(&mc, slots, b, indep, &sbrst->mps, &mf);
        if (r != 1) { *reason = r < 0 ? SAUTI_AAC_REASON_UNSUPPORTED_ELEMENT : SAUTI_AAC_REASON_INVALID_SCE; return r; }
    } else
    if (cfg->sbr_mode) {                                                    /* usac_sbr_data() follows the channel streams */
        sbrst->header.slot_scale = cfg->sbr_mode == 3u ? 4u : 2u;
        r = sauti_usac_sbr_parse(e, nch, b, indep, cfg->sample_rate, sbrst, sbr_pl, &sbr_cpl);
        if (r != 1) { *reason = r < 0 ? SAUTI_AAC_REASON_UNSUPPORTED_ELEMENT : SAUTI_AAC_REASON_INVALID_SCE; return r; }
    }
    for (c = 0; c < nch; c++) {
        if (e->noise_fill) noise_fill(cfg, &fd[c], &chs[c].noise_seed);
        scale_spectrum(&fd[c]);
    }
    if (nch == 2u && common_window) {
        for (c = 0; c < 2u; c++) if (fd[c].tns_present && !tns_on_lr && !tns_apply(&fd[c])) { *reason = SAUTI_AAC_REASON_TNS; return 0; }
        if (ms_mode == 3) cplx_apply(cfg, &fd[0], &fd[1], max_sfb_ste, &cp, &chs[0], &chs[1]);
        else if (ms_mode) {
            unsigned win = 0;
            for (g = 0; g < fd[0].ngroups; g++) {
                for (sfb = 0; sfb < max_sfb_ste; sfb++) {
                    const unsigned lo = fd[0].swb[sfb], len = fd[0].swb[sfb + 1u] - lo;
                    if (!ms_mask[g][sfb]) continue;
                    for (w = 0; w < fd[0].glen[g]; w++) {
                        float *l = fd[0].coef + (win + w) * fd[0].stride + lo, *rr = fd[1].coef + (win + w) * fd[1].stride + lo;
                        for (z = 0; z < len; z++) { const float m = l[z], s = rr[z]; l[z] = m + s; rr[z] = m - s; }
                    }
                }
                win += fd[0].glen[g];
            }
        }
    }
    for (c = 0; c < nch; c++) {
        if (fd[c].tns_present && (nch == 1u || tns_on_lr || !common_window) && !tns_apply(&fd[c])) { *reason = SAUTI_AAC_REASON_TNS; return 0; }
        if (nch == 2u) memcpy(chs[c].prev_coef, fd[c].coef, sizeof chs[c].prev_coef);       /* the spectrum that goes into the IMDCT: the previous frame for complex prediction */
        { const int sr = synthesize(cfg, &fd[c], &chs[c], cfg->sbr_mode ? core[c] : out[c]); if (sr != 1) { *reason = sr < 0 ? SAUTI_AAC_REASON_UNSUPPORTED_ELEMENT : SAUTI_AAC_REASON_INVALID_SYNTH; return sr < 0 ? -1 : 0; } }
    }
    if (nch == 2u && e->stereo_config_index >= 2u && e->mps_pseudo_lr && cfg->sbr_mode) {   /* bsPseudoLr: the two core channels are an M / S pair rotated by 45 degrees, undone before eSBR and the upmix */
        const float k = 0.7071067812f;
        unsigned i;
        for (i = 0; i < cfg->core_frame_len; i++) { const float a = core[0][i], d = core[1][i]; core[0][i] = (a + d) * k; core[1][i] = (a - d) * k; }
    }
    if (cfg->sbr_mode) {                                                    /* the core output goes through the QMF / HF generation / adjustment chain of HE-AAC */
        SautiAacSbrComplex *grid = (SautiAacSbrComplex *)malloc(sizeof(SautiAacSbrComplex) * 6u * SAUTI_AAC_SBR_CHANNEL_GRID);
        if (!grid) { *reason = SAUTI_AAC_REASON_INVALID_SYNTH; return 0; }
        if (mps_on) {                                                       /* MPS212: the downmix goes through the eSBR chain, then the upmix makes two QMF grids that get their own synthesis */
            const unsigned slots = cfg->sbr_mode == 3u ? 64u : 32u;
            SautiAacSbrComplex *g1 = grid + SAUTI_AAC_SBR_CHANNEL_GRID * 2u, *g2 = g1 + SAUTI_AAC_SBR_CHANNEL_GRID * 2u;
            sbrch[0].mps_lf_ahead = 1u;
            if (!sauti_aac_sbr_channel_frame(&sbrch[0], core[0], &sbr_pl[0], grid)) { free(grid); *reason = SAUTI_AAC_REASON_INVALID_SYNTH; return 0; }
            SautiAacSbrComplex *rgrid = NULL;
            if (mc.residual_bands) {                                        /* the residual (the second core channel) goes through the same QMF analysis as the downmix (32 bands at eSBR 2:1, 16 at 4:1) */
                SautiAacSbrComplex *low = (SautiAacSbrComplex *)calloc((size_t)slots * 32u, sizeof(SautiAacSbrComplex));
                rgrid = (SautiAacSbrComplex *)calloc((size_t)slots * 64u, sizeof(SautiAacSbrComplex));
                if (!low || !rgrid || !(cfg->sbr_mode == 3u ? sauti_aac_sbr_qmf_analyze_16(core[1], 1024u, slots, low, (size_t)slots * 32u, &sbrst->mpsdec.res_analysis) : cfg->sbr_mode == 2u ? sauti_aac_sbr_qmf_analyze_24(core[1], 768u, slots, low, (size_t)slots * 32u, &sbrst->mpsdec.res_analysis) : sauti_aac_sbr_qmf_analyze_32(core[1], 1024u, slots, 32u, low, (size_t)slots * 32u, &sbrst->mpsdec.res_analysis))) { free(low); free(rgrid); free(grid); *reason = SAUTI_AAC_REASON_INVALID_SYNTH; return 0; }
                sauti_usac_mps212_residual_grid(&sbrst->mpsdec, low, slots, cfg->sbr_mode == 3u ? 6u : 0u, cfg->sbr_mode == 3u ? 12u : 6u, rgrid);
                free(low);
            }
            if (
                !sauti_usac_mps212_apply(&sbrst->mpsdec, &mc, &mf, sbr_pl[0].tables.kx + sbr_pl[0].tables.m, slots, grid, rgrid, cfg->sbr_mode == 3u ? 16u : cfg->sbr_mode == 2u ? 24u : 32u, g1, g2) ||
                !sauti_aac_sbr_channel_synthesize(&sbrch[0], g1, out[0]) || !sauti_aac_sbr_channel_synthesize(&sbrch[1], g2, out[1])) { free(rgrid); free(grid); *reason = SAUTI_AAC_REASON_INVALID_SYNTH; return 0; }
            free(rgrid);
        } else
        for (c = 0; c < nch; c++)
            if (!sauti_aac_sbr_channel_frame(&sbrch[c], core[c], &sbr_pl[c], grid) || !sauti_aac_sbr_channel_synthesize(&sbrch[c], grid, out[c])) { free(grid); *reason = SAUTI_AAC_REASON_INVALID_SYNTH; return 0; }
        free(grid);
    }
    if (nch == 2u) {
        if (ms_mode != 3) { memset(chs[0].alpha_prev_re, 0, sizeof chs[0].alpha_prev_re); memset(chs[0].alpha_prev_im, 0, sizeof chs[0].alpha_prev_im); }
        if (common_window) chs[0].prev_ngroups = fd[0].ngroups;
        chs[0].last_seq = fd[0].seq; chs[0].last_shape = fd[0].shape;
    }
    return 1;
}

#define SAUTI_USAC_EXT_AUDIOPREROLL 3u                                      /* usacExtElementType ID_EXT_ELE_AUDIOPREROLL */
#define SAUTI_USAC_MAX_PREROLL 4u

/* The access unit decoded INTO `next` (a scratch copy of the state: the caller commits or drops it).  preroll_ok = 0 for the access units carried inside a pre-roll payload. */
static int decode_core(const SautiUsacConfig *cfg, SautiUsacState *next, const uint8_t *au, size_t length, float *pcm, SautiAacReason *reason, int preroll_ok) {
    unsigned why = 0, ch = 0, el, n, c, i;
    int indep, r;
    SautiUsacBits b;
    float stage[SAUTI_USAC_MAX_CHANNELS][4096];
    b.data = au; b.nbits = length * 8u; b.pos = 0;
    indep = (int)sauti_usac_get(&b, 1);
    n = cfg->out_frame_len;
    for (el = 0; el < cfg->num_elements; el++) {
        const SautiUsacElement *e = &cfg->e[el];
        float out[2][4096];
        if (e->type == SAUTI_USAC_EXT) {
            unsigned len;
            if (!sauti_usac_get(&b, 1)) continue;                           /* usacExtElementPresent */
            if (sauti_usac_get(&b, 1)) len = e->ext_default_len;
            else { len = sauti_usac_get(&b, 8); if (len == 255u) len += sauti_usac_get(&b, 16) - 2u; }
            if (!len) continue;
            if (e->ext_payload_frag) (void)sauti_usac_get(&b, 2);
            if (b.pos > b.nbits || (size_t)len * 8u > b.nbits - b.pos) { *reason = SAUTI_AAC_REASON_INVALID_TRAILING; r = 0; goto fail; }
            if (e->ext_type == SAUTI_USAC_EXT_AUDIOPREROLL && preroll_ok && !next->primed && el == 0u) {   /* AudioPreRoll(): the access units that precede this one, decoded to prime the state, their PCM dropped */
                SautiUsacBits p; unsigned cfg_len, num, k; uint8_t *buf = NULL; float *scratch = NULL; int ok = 0, inner = 0; SautiAacReason pwhy = SAUTI_AAC_REASON_INVALID_SCE;
                p.data = au; p.nbits = b.pos + (size_t)len * 8u; p.pos = b.pos;
                cfg_len = sauti_usac_escaped(&p, 4, 4, 8);                  /* a UsacConfig repeated in the payload: the configuration of a stream does not change here, skipped */
                if ((size_t)cfg_len * 8u > p.nbits - (p.pos < p.nbits ? p.pos : p.nbits)) { *reason = SAUTI_AAC_REASON_INVALID_TRAILING; r = 0; goto fail; }
                p.pos += (size_t)cfg_len * 8u;
                (void)sauti_usac_get(&p, 1); (void)sauti_usac_get(&p, 1);     /* applyCrossfade, reserved */
                num = sauti_usac_escaped(&p, 2, 4, 0);
                if (p.pos > p.nbits || num > SAUTI_USAC_MAX_PREROLL) { *reason = SAUTI_AAC_REASON_INVALID_TRAILING; r = 0; goto fail; }
                scratch = (float *)malloc((size_t)SAUTI_USAC_MAX_CHANNELS * 4096u * sizeof(float));
                if (!scratch) { *reason = SAUTI_AAC_REASON_INVALID_SYNTH; r = 0; goto fail; }
                for (k = 0; k < num; k++) {
                    const unsigned al = sauti_usac_escaped(&p, 16, 16, 0); size_t j;
                    if (!al || p.pos > p.nbits || (size_t)al * 8u > p.nbits - p.pos) goto preroll_end;
                    free(buf); buf = (uint8_t *)calloc(al, 1u);
                    if (!buf) goto preroll_end;
                    for (j = 0; j < (size_t)al * 8u; j++) { const size_t at = p.pos + j; if ((au[at >> 3] >> (7u - (at & 7u))) & 1u) buf[j >> 3] |= (uint8_t)(0x80u >> (j & 7u)); }
                    if (k == 0u && !(buf[0] & 0x80u)) goto preroll_end;                 /* the first pre-roll access unit must be independent */
                    inner = decode_core(cfg, next, buf, al, scratch, &pwhy, 0);
                    if (inner != 1) goto preroll_end;
                    p.pos += (size_t)al * 8u;
                }
                ok = 1;
preroll_end:
                free(buf); free(scratch);
                if (!ok) { *reason = inner == -1 ? pwhy : SAUTI_AAC_REASON_INVALID_SCE; r = inner == -1 ? -1 : 0; goto fail; }   /* a pre-roll frame the decoder cannot decode (LPD ...) makes the access unit unsupported, not malformed */
            }
            b.pos += (size_t)len * 8u;                                      /* fill, DRC gain, ...: not decoded */
            continue;
        }
        if (ch + (e->type == SAUTI_USAC_CPE ? 2u : 1u) > cfg->out_channels) { *reason = SAUTI_AAC_REASON_UNSUPPORTED_ELEMENT; r = -1; goto fail; }
        r = element(cfg, e, &b, &next->ch[ch], &next->sbr[el], &next->sbr_ch[ch], indep, out, &why);
        *reason = (SautiAacReason)why;
        if (r != 1) goto fail;
        if (b.pos > b.nbits) { *reason = SAUTI_AAC_REASON_INVALID_TRAILING; r = 0; goto fail; }
        for (c = 0; c < (e->type == SAUTI_USAC_CPE ? 2u : 1u); c++) memcpy(stage[ch + c], out[c], n * sizeof(float));
        ch += e->type == SAUTI_USAC_CPE ? 2u : 1u;
    }
    if (b.pos > b.nbits || ch != cfg->out_channels) { *reason = SAUTI_AAC_REASON_INVALID_TRAILING; r = 0; goto fail; }
    {                                                                       /* stream order (C, L R, Ls Rs, LFE ...) -> WAVE / FFmpeg order, the table of the multichannel AAC decoder */
        uint8_t map[SAUTI_AAC_LC_MULTI_MAX];
        if (!sauti_aac_lc_multi_output_map(cfg->channel_config, map)) { *reason = SAUTI_AAC_REASON_UNSUPPORTED_ELEMENT; r = -1; goto fail; }
        for (c = 0; c < ch; c++) for (i = 0; i < n; i++) pcm[(size_t)i * ch + c] = stage[map[c]][i];
    }
    next->primed = 1u;
    *reason = SAUTI_AAC_REASON_OK;
    return 1;
fail:
    return r;
}

int sauti_usac_decode(const SautiUsacConfig *cfg, SautiUsacState *state, const uint8_t *au, size_t length, float *pcm, SautiAacReason *reason) {
    SautiAacReason local;
    SautiUsacState *next;                                                  /* a copy, committed only on success; no shared state: callers may decode on several threads */
    int r;
    if (!reason) reason = &local;
    *reason = SAUTI_AAC_REASON_INVALID_SCE;
    if (!cfg || !state || !au || !pcm || length > SIZE_MAX / 8u || cfg->out_channels < 1u || cfg->out_channels > SAUTI_USAC_MAX_CHANNELS || cfg->channel_config == 0 || cfg->channel_config > 7u || state->channels != cfg->out_channels) return 0;
    next = (SautiUsacState *)malloc(sizeof *next);
    if (!next) return 0;
    memcpy(next, state, sizeof *next);
    r = decode_core(cfg, next, au, length, pcm, reason, 1);
    if (r == 1) memcpy(state, next, sizeof *state);
    free(next);
    return r;
}
