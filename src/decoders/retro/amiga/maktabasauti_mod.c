/* MaktabaSauti, modul maktabasauti_mod: odtwarzacz modulow ProTracker / SoundTracker (4 kanaly, 31 instrumentow).
 *
 * Kod jest nasz: uklad bajtow, zegar Pauli i regulamin taktu wynikaja z publicznego opisu formatu, nie z kodu
 * ProTrackera, libxmp, OpenMPTa, MikModa ani FFmpeg.  Modul nie linkuje sie z zadna z tych bibliotek.
 *
 * Kawalek 1:
 *   - magiki M.K. i FLT4 (do 64 patternow) oraz M!K! i 4CHN (do 256);
 *   - odmiany pliku, nie wycinek: 31 instrumentow (M.K., M&K!, FLT4 do 64 patternow;
 *     M!K! do 256; xCHN 1..9; xxCH i xxCN 10..32; TDZ1..TDZ3; CD81, OCTA, OKTA),
 *     FLT8 (8 kanalow, para patternow 4-kanalowych) i 15 instrumentow bez sygnatury;
 *   - nieznana czteroznakowa sygnatura jest odrzucana i nie jest czytana jako 15 instrumentow;
 *   - uciety pattern, probka za koncem pliku, petla poza probka i zla dlugosc utworu: blad, nic nie gra;
 *   - komorka gra okresem, ktory jest w niej zapisany; finetune jest tylko zachowany
 *     (arpeggio szuka poltonow w tablicy finetune 0, nie stroi nuty bazowej);
 *   - dzialaja: 0xy arpeggio (000 zostaje brakiem efektu), 1xx/2xx porta, 3xx tone porta,
 *     4xy vibrato (sinus wyliczony, nie tablica cudzego playera), 5xy tone porta ze slizgiem glosnosci,
 *     6xy vibrato ze slizgiem glosnosci, 9xx offset probki przy uderzeniu nuty, Axy slizg glosnosci,
 *     Cxx (0..64) i Fxx (1..31 taktow na wiersz, 32..255 BPM, F00 nic);
 *   - 1xx/2xx stoja na okresie 113..856.  Slizg, vibrato i arpeggio ida od taktu 1; takt 0 gra bazowy okres.
 *     Parametr 0 powtarza ostatni niezerowy parametr tego efektu.  Bxx, Dxx i Exx nadal nic nie zmieniaja;
 *   - utwor idzie raz przez liste order i staje; bajt restart nie zapetla;
 *   - wyjscie to splot stereo S16.  Panorama powtarza pary Amigi: 4k i 4k+3 na lewo, 4k+1 i 4k+2 na prawo;
 *   - miks w int32, nasycenie do int16.  Zegar PAL Pauli 3546895 Hz.  NTSC nie jest w tym kawalku.
 *
 * Czas taktu: 2,5 / BPM sekundy (przy 125 BPM i 44100 Hz wychodzi 882 klatki).  Fxx z predkoscia 1..31
 * obowiazuje juz ten wiersz.  Fxx z tempem BPM odmierza biezacy takt starym tempem, a nastepne nowym.
 * Reszta z dzielenia przechodzi na kolejny takt, zeby srednia byla dokladnie 2,5/BPM.
 *
 * Faza probki: po klatce i (od zera) indeks = floor(i * 3546895 / (okres * czestotliwosc)).
 * Petla: poczatek probki gra raz, potem odcinek petli.  Dlugosc petli 0 albo 1 slowo = brak petli.
 * Nowy okres zeruje faze.  Sam numer instrumentu bez okresu nie zeruje fazy.
 */
#include "sauti_decoder_mod.h"
#include <string.h>

#define MOD_INST 31u
#define MOD_HEADER_31 1084u
#define MOD_HEADER_15 600u
#define MOD_CH_MAX 32u
#define PAULA_PAL 3546895u

typedef struct {
    const uint8_t *data;
    uint32_t length;
    uint32_t loop_start;
    uint32_t loop_len;
    uint8_t volume;
    int8_t finetune;
    char name[23];
} inst_t;

typedef struct {
    const uint8_t *sample;
    uint32_t length;
    uint32_t loop_start;
    uint32_t loop_len;
    uint32_t idx;
    uint64_t frac;
    uint16_t period;
    uint16_t play_period;
    uint16_t target;
    uint8_t volume;
    uint8_t effect;
    uint8_t param;
    uint8_t inst;
    uint8_t porta_up, porta_down, tone_speed;
    uint8_t vib_pos, vib_speed, vib_depth;
    uint8_t vol_slide;
    uint8_t offset;
} voice_t;

/* Okresy PAL, finetune 0, C-1..B-3.  Kolejne oktawy to polowa poprzedniej, zaokraglona. */
static const uint16_t k_period[36] = {
    856, 808, 762, 720, 678, 640, 604, 570, 538, 508, 480, 453,
    428, 404, 381, 360, 339, 320, 302, 285, 269, 254, 240, 226,
    214, 202, 190, 180, 170, 160, 151, 143, 135, 127, 120, 113
};

/* sin(i * 2π / 64) * 255, zaokraglone.  Delta vibrato = wartosc * glebokosc / 128. */
static const int16_t k_vibrato[64] = {
    0, 25, 50, 74, 98, 120, 142, 162, 180, 197, 212, 225, 236, 244, 250, 254,
    255, 254, 250, 244, 236, 225, 212, 197, 180, 162, 142, 120, 98, 74, 50, 25,
    0, -25, -50, -74, -98, -120, -142, -162, -180, -197, -212, -225, -236, -244, -250, -254,
    -255, -254, -250, -244, -236, -225, -212, -197, -180, -162, -142, -120, -98, -74, -50, -25
};

typedef struct {
    const maktabasauti_host_t *host;
    const uint8_t *file;
    char title[21];
    char magic[5];
    inst_t inst[MOD_INST];
    uint8_t orders[128];
    const uint8_t *patterns;
    uint8_t song_length;
    uint8_t restart;
    uint16_t pattern_count;
    uint8_t channels;
    uint8_t instruments;
    uint8_t flt8;
    uint32_t rate;
    uint32_t order_pos;
    uint32_t row;
    uint32_t row_tick;
    uint32_t speed;
    uint32_t tempo;
    uint32_t tick_rem;
    uint32_t tick_left;
    uint32_t ticks_done;
    uint32_t tick_cap;
    uint64_t frame_pos;
    uint64_t total_frames;
    int have_tick;
    int ended;
    const char *err;
    voice_t voice[MOD_CH_MAX];
} mod_t;

typedef struct {
    uint8_t channels;
    uint8_t instruments;
    uint8_t flt8;
    uint16_t pattern_limit;
    uint16_t header;
    char magic[5];
} ident_t;

static uint16_t be16(const uint8_t *p) {
    return (uint16_t)(((unsigned)p[0] << 8) | p[1]);
}

static int tag_bytes(const uint8_t *m) {
    unsigned i;
    for (i = 0; i < 4u; i++) {
        unsigned c = m[i];
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '!' || c == '&') continue;
        return 0;
    }
    return 1;
}

/* Sygnatury z publicznego opisu MOD.  xxCH/xxCN stoja na 10..32, bo tyle kanalow ma ta rodzina. */
static int ident_magic(const uint8_t *m, ident_t *id) {
    unsigned ch;
    memset(id, 0, sizeof *id);
    id->instruments = 31;
    id->header = MOD_HEADER_31;
    id->pattern_limit = 256;
    memcpy(id->magic, m, 4u);
    id->magic[4] = 0;
    if (memcmp(m, "M.K.", 4) == 0 || memcmp(m, "M&K!", 4) == 0 || memcmp(m, "FLT4", 4) == 0) {
        id->channels = 4;
        id->pattern_limit = 64;
        return 1;
    }
    if (memcmp(m, "M!K!", 4) == 0) {
        id->channels = 4;
        return 1;
    }
    if (memcmp(m, "FLT8", 4) == 0) {
        id->channels = 8;
        id->flt8 = 1;
        id->pattern_limit = 128;
        return 1;
    }
    if (memcmp(m, "CD81", 4) == 0 || memcmp(m, "OCTA", 4) == 0 || memcmp(m, "OKTA", 4) == 0) {
        id->channels = 8;
        return 1;
    }
    if (m[0] == 'T' && m[1] == 'D' && m[2] == 'Z' && m[3] >= '1' && m[3] <= '3') {
        id->channels = (uint8_t)(m[3] - '0');
        return 1;
    }
    if (m[0] >= '1' && m[0] <= '9' && m[1] == 'C' && m[2] == 'H' && m[3] == 'N') {
        id->channels = (uint8_t)(m[0] - '0');
        return 1;
    }
    if (m[0] >= '0' && m[0] <= '9' && m[1] >= '0' && m[1] <= '9' && m[2] == 'C' && (m[3] == 'H' || m[3] == 'N')) {
        ch = (unsigned)(m[0] - '0') * 10u + (unsigned)(m[1] - '0');
        if (ch < 10u || ch > MOD_CH_MAX) return 0;
        id->channels = (uint8_t)ch;
        return 1;
    }
    return 0;
}

static void ident_15(ident_t *id) {
    memset(id, 0, sizeof *id);
    id->channels = 4;
    id->instruments = 15;
    id->pattern_limit = 64;
    id->header = MOD_HEADER_15;
    memcpy(id->magic, "15ST", 5u);
}

static int recognize(const uint8_t *p, size_t n, ident_t *id) {
    if (n >= MOD_HEADER_31 && ident_magic(p + 1080, id)) return 1;
    if (n >= MOD_HEADER_31 && tag_bytes(p + 1080)) return 0;
    if (n < MOD_HEADER_15 || p[470] < 1u || p[470] > 128u) return 0;
    ident_15(id);
    return 1;
}

static const uint8_t *cell_at(const mod_t *m, unsigned pat, unsigned row, unsigned ch) {
    if (m->flt8) {
        unsigned stored = pat + (ch >> 2);
        return m->patterns + ((size_t)stored * 64u + row) * 16u + (size_t)(ch & 3u) * 4u;
    }
    return m->patterns + ((size_t)pat * 64u + row) * (size_t)m->channels * 4u + (size_t)ch * 4u;
}

static int16_t sat16(int32_t x) {
    if (x > 32767) return 32767;
    if (x < -32768) return -32768;
    return (int16_t)x;
}

static int32_t s8(uint8_t b) {
    return b < 128u ? (int32_t)b : (int32_t)b - 256;
}

static void playback_reset(mod_t *m) {
    uint32_t i;
    for (i = 0; i < MOD_CH_MAX; i++) memset(&m->voice[i], 0, sizeof m->voice[i]);
    m->order_pos = 0;
    m->row = 0;
    m->row_tick = 0;
    m->speed = 6u;
    m->tempo = 125u;
    m->tick_rem = 0;
    m->tick_left = 0;
    m->ticks_done = 0;
    m->frame_pos = 0;
    m->have_tick = 0;
    m->ended = 0;
    m->err = NULL;
}

static uint32_t tick_frames(mod_t *m) {
    uint64_t acc = (uint64_t)m->tick_rem + (uint64_t)m->rate * 5u;
    uint32_t den = m->tempo * 2u;
    m->tick_rem = (uint32_t)(acc % den);
    return (uint32_t)(acc / den);
}

static void voice_wrap(voice_t *v) {
    uint32_t end;
    if (!v->loop_len) return;
    end = v->loop_start + v->loop_len;
    if (v->idx < end) return;
    v->idx = v->loop_start + (v->idx - v->loop_start) % v->loop_len;
}

static void use_period(voice_t *v, uint16_t period) {
    if (v->play_period && period && v->play_period != period)
        v->frac = v->frac * period / v->play_period;
    v->play_period = period;
}

static int nearest_note(unsigned period) {
    unsigned i, best = 0, gap = 65535u;
    for (i = 0; i < 36u; i++) {
        unsigned d = period > k_period[i] ? period - k_period[i] : k_period[i] - period;
        if (d < gap) { gap = d; best = i; }
    }
    return (int)best;
}

static uint16_t arpeggio_period(unsigned base, unsigned semis) {
    int note = nearest_note(base) + (int)semis;
    if (note < 0) note = 0;
    if (note > 35) note = 35;
    return k_period[note];
}

static void voice_advance(voice_t *v, uint32_t rate) {
    uint64_t den, idx;
    if (!v->play_period || !rate) return;
    den = (uint64_t)v->play_period * rate;
    v->frac += PAULA_PAL;
    idx = (uint64_t)v->idx + v->frac / den;
    v->frac %= den;
    if (v->loop_len) {
        uint64_t end = (uint64_t)v->loop_start + v->loop_len;
        if (idx >= end) idx = (uint64_t)v->loop_start + (idx - v->loop_start) % v->loop_len;
        v->idx = (uint32_t)idx;
        return;
    }
    if (!v->sample || idx >= v->length) {
        v->idx = v->length;
        v->frac = 0;
        return;
    }
    v->idx = (uint32_t)idx;
}

static void apply_row(mod_t *m, uint32_t *tempo_after) {
    unsigned ch;
    for (ch = 0; ch < m->channels; ch++) {
        const uint8_t *c = cell_at(m, m->orders[m->order_pos], m->row, ch);
        unsigned sample = (c[0] & 0xF0u) | (c[2] >> 4);
        unsigned period = ((unsigned)(c[0] & 0x0Fu) << 8) | c[1];
        unsigned effect = c[2] & 0x0Fu;
        unsigned param = c[3];
        voice_t *v = &m->voice[ch];
        v->effect = (uint8_t)effect;
        v->param = (uint8_t)param;
        if (effect == 0x1u && param) v->porta_up = (uint8_t)param;
        if (effect == 0x2u && param) v->porta_down = (uint8_t)param;
        if (effect == 0x3u && param) v->tone_speed = (uint8_t)param;
        if (effect == 0x4u) {
            if (param >> 4) v->vib_speed = (uint8_t)(param >> 4);
            if (param & 0x0Fu) v->vib_depth = (uint8_t)(param & 0x0Fu);
        }
        if ((effect == 0x5u || effect == 0x6u || effect == 0xAu) && param) v->vol_slide = (uint8_t)param;
        if (effect == 0x9u && param) v->offset = (uint8_t)param;
        if (sample >= 1u && sample <= m->instruments) {
            const inst_t *in = &m->inst[sample - 1u];
            v->sample = in->data;
            v->length = in->length;
            v->loop_start = in->loop_start;
            v->loop_len = in->loop_len;
            v->volume = in->volume;
            v->inst = (uint8_t)sample;
        }
        if (period) {
            int porta = effect == 0x3u || effect == 0x5u;
            if (porta && v->period) v->target = (uint16_t)period;
            else {
                uint32_t at = 0;
                v->period = (uint16_t)period;
                v->target = (uint16_t)period;
                v->frac = 0;
                v->play_period = 0;
                if (effect == 0x9u) at = (uint32_t)v->offset * 256u;
                if (!v->loop_len && at >= v->length) v->idx = v->length;
                else v->idx = at;
            }
        }
        if (effect == 0x0Cu) v->volume = param > 64u ? 64u : (uint8_t)param;
        if (effect == 0x0Fu && param) {
            if (param < 32u) m->speed = param;
            else *tempo_after = param;
        }
    }
}

static void clamp_volume(voice_t *v, int delta) {
    int vol = (int)v->volume + delta;
    if (vol < 0) vol = 0;
    if (vol > 64) vol = 64;
    v->volume = (uint8_t)vol;
}

static void slide_volume(voice_t *v) {
    unsigned up = v->vol_slide >> 4, down = v->vol_slide & 0x0Fu;
    if (!v->vol_slide) return;
    if (up) clamp_volume(v, (int)up);
    else clamp_volume(v, -(int)down);
}

static void porta_up(voice_t *v) {
    unsigned p = v->period, step = v->porta_up;
    if (!step || p <= 113u) return;
    v->period = (uint16_t)(p - 113u > step ? p - step : 113u);
}

static void porta_down(voice_t *v) {
    unsigned p = v->period, step = v->porta_down;
    if (!step || !p || p >= 856u) return;
    v->period = (uint16_t)(856u - p > step ? p + step : 856u);
}

static void tone_porta(voice_t *v) {
    unsigned p = v->period, goal = v->target, step = v->tone_speed;
    if (!step || !p || !goal || p == goal) return;
    if (p < goal) v->period = (uint16_t)(goal - p > step ? p + step : goal);
    else v->period = (uint16_t)(p - goal > step ? p - step : goal);
}

static void shape_tick(mod_t *m) {
    unsigned ch;
    for (ch = 0; ch < m->channels; ch++) {
        voice_t *v = &m->voice[ch];
        unsigned fx = v->effect, pa = v->param;
        if (m->row_tick > 0u) {
            if (fx == 0x1u) porta_up(v);
            else if (fx == 0x2u) porta_down(v);
            else if (fx == 0x3u || fx == 0x5u) tone_porta(v);
            if (fx == 0x5u || fx == 0x6u || fx == 0xAu) slide_volume(v);
        }
        if ((fx == 0x4u || fx == 0x6u) && m->row_tick > 0u && v->period) {
            int32_t waved = (int32_t)k_vibrato[v->vib_pos] * (int32_t)v->vib_depth / 128;
            int32_t sounded = (int32_t)v->period + waved;
            if (sounded < 1) sounded = 1;
            use_period(v, (uint16_t)sounded);
            v->vib_pos = (uint8_t)((v->vib_pos + v->vib_speed) & 63u);
        } else if (fx == 0u && pa && v->period) {
            unsigned semis = 0, part = m->row_tick % 3u;
            if (part == 1u) semis = pa >> 4;
            else if (part == 2u) semis = pa & 0x0Fu;
            use_period(v, semis ? arpeggio_period(v->period, semis) : v->period);
        } else use_period(v, v->period);
    }
}

static int begin_tick(mod_t *m) {
    uint32_t tempo_after;
    if (m->ended) return 0;
    if (m->ticks_done >= m->tick_cap) {
        m->ended = 1;
        m->err = "przekroczony limit taktow";
        return 0;
    }
    m->ticks_done++;
    tempo_after = m->tempo;
    if (m->row_tick == 0u) apply_row(m, &tempo_after);
    shape_tick(m);
    m->tick_left = tick_frames(m);
    m->tempo = tempo_after;
    m->have_tick = 1;
    return 1;
}

static void end_tick(mod_t *m) {
    m->have_tick = 0;
    m->tick_left = 0;
    m->row_tick++;
    if (m->row_tick < m->speed) return;
    m->row_tick = 0;
    m->row++;
    if (m->row < 64u) return;
    m->row = 0;
    m->order_pos++;
    if (m->order_pos >= m->song_length) m->ended = 1;
}

static int count_frames(mod_t *m, uint64_t *out) {
    uint64_t n = 0;
    playback_reset(m);
    for (;;) {
        if (!begin_tick(m)) break;
        n += m->tick_left;
        end_tick(m);
        if (m->err) return 0;
    }
    if (m->err) return 0;
    *out = n;
    playback_reset(m);
    return 1;
}

static void mix_one(mod_t *m, int16_t *dst) {
    int32_t left = 0, right = 0;
    unsigned ch;
    for (ch = 0; ch < m->channels; ch++) {
        voice_t *v = &m->voice[ch];
        int32_t s = 0;
        voice_wrap(v);
        if (v->play_period && v->sample && v->idx < v->length) s = s8(v->sample[v->idx]) * (int32_t)v->volume;
        if ((ch & 3u) == 0u || (ch & 3u) == 3u) left += s;
        else right += s;
        voice_advance(v, m->rate);
    }
    dst[0] = sat16(left);
    dst[1] = sat16(right);
}

static maktabasauti_result_t render(mod_t *m, int16_t *dst, size_t dst_frames, size_t *frames) {
    size_t wrote = 0;
    if (!frames) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    if (dst_frames && !dst) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    if (!dst_frames) { *frames = 0; return MAKTABASAUTI_OK; }
    while (wrote < dst_frames) {
        uint32_t chunk, i;
        if (!m->have_tick || m->tick_left == 0u) {
            if (m->have_tick) end_tick(m);
            if (!begin_tick(m)) break;
            if (m->tick_left == 0u) continue;
        }
        chunk = m->tick_left;
        if ((size_t)chunk > dst_frames - wrote) chunk = (uint32_t)(dst_frames - wrote);
        for (i = 0; i < chunk; i++) mix_one(m, dst + (wrote + i) * 2u);
        m->tick_left -= chunk;
        m->frame_pos += chunk;
        wrote += chunk;
    }
    *frames = wrote;
    if (!wrote) return MAKTABASAUTI_EOF;
    m->err = NULL;
    return MAKTABASAUTI_OK;
}

static int parse_rate(const maktabasauti_host_t *h, uint32_t *rate) {
    const char *s = NULL;
    uint32_t v = 0;
    if (h->option) {
        s = h->option(h->user, "sample_rate");
        if (!s) s = h->option(h->user, "mod.sample_rate");
    }
    if (!s || !s[0]) { *rate = 44100u; return 1; }
    for (; *s; s++) {
        uint32_t d;
        if (*s < '0' || *s > '9') return 0;
        d = (uint32_t)(*s - '0');
        if (v > (4294967295u - d) / 10u) return 0;
        v = v * 10u + d;
    }
    if (v == 0u || v > 384000u) return 0;
    *rate = v;
    return 1;
}

static void copy_name(char *dst, size_t cap, const uint8_t *src, size_t n) {
    size_t i;
    if (n >= cap) n = cap - 1u;
    for (i = 0; i < n; i++) dst[i] = (char)src[i];
    dst[n] = 0;
}

static maktabasauti_result_t parse_module(mod_t *m, const uint8_t *p, size_t n, const ident_t *id) {
    unsigned max_order = 0, span;
    size_t info_at, off, cell;
    unsigned i;
    if (n < id->header) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    m->channels = id->channels;
    m->instruments = id->instruments;
    m->flt8 = id->flt8;
    memcpy(m->magic, id->magic, 5u);
    info_at = 20u + (size_t)id->instruments * 30u;
    m->song_length = p[info_at];
    m->restart = p[info_at + 1u];
    if (m->song_length < 1u || m->song_length > 128u) {
        m->err = "dlugosc utworu poza 1..128";
        return MAKTABASAUTI_E_CORRUPT_DATA;
    }
    memcpy(m->orders, p + info_at + 2u, 128u);
    for (i = 0; i < 128u; i++) if (m->orders[i] > max_order) max_order = m->orders[i];
    /* FLT8: pozycja P potrzebuje patternow P i P+1, wiec zapisanych jest max+2. */
    m->pattern_count = (uint16_t)(max_order + 1u + (id->flt8 ? 1u : 0u));
    if ((unsigned)m->pattern_count > id->pattern_limit) {
        m->err = "za duzo patternow dla tego magika";
        return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
    }
    span = id->flt8 ? 4u : id->channels;
    off = (size_t)id->header + (size_t)m->pattern_count * 64u * span * 4u;
    if (off > n) {
        m->err = "pattern urywa sie przed koncem pliku";
        return MAKTABASAUTI_E_TRUNCATED;
    }
    m->patterns = p + id->header;
    copy_name(m->title, sizeof m->title, p, 20u);
    for (i = 0; i < id->instruments; i++) {
        const uint8_t *ip = p + 20u + (size_t)i * 30u;
        uint16_t words = be16(ip + 22);
        uint16_t loop_w = be16(ip + 26);
        uint16_t loop_l = be16(ip + 28);
        uint32_t bytes = (uint32_t)words * 2u;
        uint8_t nib = (uint8_t)(ip[24] & 0x0Fu);
        inst_t *in = &m->inst[i];
        copy_name(in->name, sizeof in->name, ip, 22u);
        in->finetune = nib < 8u ? (int8_t)nib : (int8_t)((int)nib - 16);
        in->volume = ip[25] > 64u ? 64u : ip[25];
        in->length = bytes;
        in->data = NULL;
        in->loop_start = 0;
        in->loop_len = 0;
        if (bytes) {
            if (off > n || bytes > n - off) {
                m->err = "probka urywa sie przed koncem pliku";
                return MAKTABASAUTI_E_TRUNCATED;
            }
            in->data = p + off;
            off += bytes;
        }
        if (loop_l > 1u) {
            uint32_t ls = (uint32_t)loop_w * 2u;
            uint32_t ll = (uint32_t)loop_l * 2u;
            if (ls > bytes || ll > bytes - ls) {
                m->err = "petla probki wychodzi poza jej koniec";
                return MAKTABASAUTI_E_CORRUPT_DATA;
            }
            in->loop_start = ls;
            in->loop_len = ll;
        }
    }
    for (cell = 0; cell < (size_t)m->pattern_count * 64u * span; cell++) {
        const uint8_t *c = m->patterns + cell * 4u;
        unsigned sample = (c[0] & 0xF0u) | (c[2] >> 4);
        if (sample > id->instruments) {
            m->err = "numer probki spoza zakresu instrumentow";
            return MAKTABASAUTI_E_CORRUPT_DATA;
        }
    }
    m->tick_cap = (uint32_t)m->song_length * 64u * 31u;
    m->file = p;
    return MAKTABASAUTI_OK;
}

static int32_t MAKTABASAUTI_CALL mod_probe(const maktabasauti_codec_t *self, const uint8_t *p, size_t n, const char *hint) {
    ident_t id;
    (void)self;
    (void)hint;
    if (!p || !recognize(p, n, &id)) return 0;
    return id.header == MOD_HEADER_15 ? 60 : 70;
}

static maktabasauti_result_t MAKTABASAUTI_CALL mod_open(const maktabasauti_codec_t *self, const maktabasauti_host_t *h,
                                                       maktabasauti_reader_t *rd, maktabasauti_stream_info_t *info, void **state) {
    const uint8_t *data = NULL;
    size_t n = 0;
    mod_t *m;
    ident_t id;
    maktabasauti_result_t r;
    (void)self;
    if (!h || !info || !state || !h->alloc || !h->free || !h->map) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    *state = NULL;
    r = h->map(h->user, rd, &data, &n);
    if (r != MAKTABASAUTI_OK) return r;
    if (!data || !recognize(data, n, &id)) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    m = (mod_t *)h->alloc(h->user, sizeof *m);
    if (!m) return MAKTABASAUTI_E_NO_MEMORY;
    memset(m, 0, sizeof *m);
    m->host = h;
    r = parse_module(m, data, n, &id);
    if (r != MAKTABASAUTI_OK) { h->free(h->user, m); return r; }
    if (!parse_rate(h, &m->rate)) {
        h->free(h->user, m);
        return MAKTABASAUTI_E_INVALID_ARGUMENT;
    }
    if (!count_frames(m, &m->total_frames)) {
        r = m->err ? MAKTABASAUTI_E_CORRUPT_DATA : MAKTABASAUTI_E_INTERNAL;
        h->free(h->user, m);
        return r;
    }
    memset(info, 0, sizeof *info);
    info->struct_size = sizeof *info;
    info->sample_rate = m->rate;
    info->channels = 2u;
    info->flags = MAKTABASAUTI_STREAM_SEEKABLE;
    info->channel_mask = MAKTABASAUTI_LAYOUT_STEREO;
    info->native_format = MAKTABASAUTI_FORMAT_S16;
    info->bits_per_sample = 16u;
    info->total_frames = m->total_frames;
    memcpy(info->codec_name, "mod", 4u);
    memcpy(info->codec_profile, m->magic, 4u);
    memcpy(info->container_name, "mod", 4u);
    *state = m;
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t MAKTABASAUTI_CALL mod_read(void *state, void *dst, size_t dst_frames, size_t *frames) {
    if (!state || !frames) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    return render((mod_t *)state, (int16_t *)dst, dst_frames, frames);
}

static maktabasauti_result_t MAKTABASAUTI_CALL mod_seek(void *state, uint64_t frame, uint32_t flags, uint64_t *actual) {
    mod_t *m = (mod_t *)state;
    int16_t junk[128 * 2];
    (void)flags;
    if (!m || !actual) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    if (frame > m->total_frames) {
        m->err = "pozycja poza koncem utworu";
        return MAKTABASAUTI_E_OUT_OF_RANGE;
    }
    if (frame < m->frame_pos) playback_reset(m);
    while (m->frame_pos < frame) {
        size_t want = (size_t)(frame - m->frame_pos);
        size_t got = 0;
        maktabasauti_result_t r;
        if (want > 128u) want = 128u;
        r = render(m, junk, want, &got);
        if (r < 0) return r;
        if (!got) {
            m->err = "seek zatrzymal sie przed celem";
            return MAKTABASAUTI_E_INTERNAL;
        }
    }
    *actual = m->frame_pos;
    m->err = NULL;
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t MAKTABASAUTI_CALL mod_reset(void *state) {
    if (!state) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    playback_reset((mod_t *)state);
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t MAKTABASAUTI_CALL mod_flush(void *state) {
    if (!state) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    ((mod_t *)state)->err = NULL;
    return MAKTABASAUTI_OK;
}

static void put_i32(char *dst, int v) {
    char rev[8];
    unsigned u;
    int n = 0, i = 0;
    if (v < 0) { dst[i++] = '-'; v = -v; }
    u = (unsigned)v;
    do { rev[n++] = (char)('0' + (u % 10u)); u /= 10u; } while (u);
    while (n) dst[i++] = rev[--n];
    dst[i] = 0;
}

static maktabasauti_result_t write_tag(char *buf, size_t cap, size_t *needed, const char *s) {
    size_t n = strlen(s) + 1u;
    if (needed) *needed = n;
    if (!buf || cap < n) return MAKTABASAUTI_E_BUFFER_TOO_SMALL;
    memcpy(buf, s, n);
    return MAKTABASAUTI_OK;
}

static maktabasauti_result_t MAKTABASAUTI_CALL mod_get_tag(const void *state, const char *key, char *buf, size_t cap, size_t *needed) {
    const mod_t *m = (const mod_t *)state;
    char num[8];
    unsigned idx;
    const char *p;
    if (!m || !key) return MAKTABASAUTI_E_INVALID_ARGUMENT;
    if (strcmp(key, "title") == 0) return write_tag(buf, cap, needed, m->title);
    if (strcmp(key, "magic") == 0) return write_tag(buf, cap, needed, m->magic);
    if (strcmp(key, "restart") == 0) {
        put_i32(num, (int)m->restart);
        return write_tag(buf, cap, needed, num);
    }
    if (strncmp(key, "finetune.", 9) != 0) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
    idx = 0;
    for (p = key + 9; *p; p++) {
        if (*p < '0' || *p > '9') return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
        idx = idx * 10u + (unsigned)(*p - '0');
        if (idx > MOD_INST) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
    }
    if (idx < 1u || idx > m->instruments) return MAKTABASAUTI_E_UNSUPPORTED_FEATURE;
    put_i32(num, m->inst[idx - 1u].finetune);
    return write_tag(buf, cap, needed, num);
}

static const char *MAKTABASAUTI_CALL mod_last_error(const void *state) {
    return state ? ((const mod_t *)state)->err : NULL;
}

static void MAKTABASAUTI_CALL mod_close(void *state) {
    mod_t *m = (mod_t *)state;
    const maktabasauti_host_t *h;
    if (!m) return;
    h = m->host;
    h->free(h->user, m);
}

const maktabasauti_codec_t maktabasauti_mod_desc = {
    sizeof(maktabasauti_codec_t), MAKTABASAUTI_ABI_VERSION, "mod",
    "ProTracker / SoundTracker module (15- and 31-instrument)", "maktabasauti_mod",
    MAKTABASAUTI_CODEC_FLAG_NEEDS_FULL_BUFFER | MAKTABASAUTI_CODEC_FLAG_HAS_SEEK | MAKTABASAUTI_CODEC_FLAG_HAS_TAGS | MAKTABASAUTI_CODEC_FLAG_EXPERIMENTAL,
    0u, NULL,
    mod_probe, mod_open, mod_read, mod_seek, mod_reset, mod_flush, mod_get_tag, mod_last_error, NULL, mod_close,
    { NULL }, { NULL }
};
