/* MaktabaSauti, modul maktabasauti_g7111: STRUMIENIOWY dekoder ITU-T G.711.1 (szerokopasmowe rozszerzenie G.711, 5 ms ramki), napisany od zera w czystym C.
 *
 * Zrodlo wiedzy: tekst Rekomendacji ITU-T G.711.1 (09/2012) i obserwacje zachowania programu referencyjnego jako czarnej skrzynki (wspolny wzorzec: scripts/test_g7111_vs_itu.py).
 * Nic z kodu ITU nie zostalo skopiowane, takze we fragmentach; arytmetyka jest w podwojnej precyzji ze zrownaniem do liczb calkowitych tam, gdzie norma je wymaga (probki dolnego pasma,
 * wspolczynniki filtra w Q12), wiec wynik jest rownowazny z wzorcem (jak alternatywa zmiennoprzecinkowa z Aneksu A normy), nie zawsze bit w bit.
 *
 * Tryby (jak w normie): R1 = rdzen G.711 (8 kHz, 64 kbit/s, 40 B / ramke), R2a = rdzen + warstwa 1 (8 kHz, 80 kbit/s, 50 B), R2b = rdzen + warstwa 2 (16 kHz, 80 kbit/s, 50 B),
 * R3 = wszystkie trzy warstwy (16 kHz, 96 kbit/s, 60 B).  Prawo G.711 rdzenia (A / mu) i tryb nie sa w strumieniu (sygnalizacja zewnetrzna), wiec opcje g7111.law (a | u) i g7111.mode.
 * Kolejnosc warstw w ramce: warstwa 0 (40 bajtow G.711), potem warstwa 1 (10 bajtow, bity upakowane od najstarszego), potem warstwa 2 (10 bajtow).
 *
 * Stan jest zalezny od historii (filtr percepcyjny dolnego pasma, bramka szumow, nakladanie okien gornego pasma), wiec przewijanie = dekodowanie od poczatku z odrzuceniem. */
#include "maktabasauti/maktabasauti_codec.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define LB 40u                        /* probek dolnego pasma w ramce 5 ms (8 kHz) */
#define HIST 80u                      /* historia dla analizy LP: dwie ramki */
#define ORD 4u                        /* rzad filtra percepcyjnego */
#define HB_SCALE 0.70710678118654752440     /* 1 / sqrt(2): okno nakladania sqrt(2) sin (wynik potwierdzony na wzorcu) */

typedef int32_t i32;
typedef struct { int a[5]; int rc[4]; } lp_state_t;
typedef struct {
    int alaw, mode;                   /* mode: 1 R1, 2 R2a, 3 R2b, 4 R3 */
    double past[HIST];                /* dekodowany rdzen (bez przesuniecia prawa A) z dwoch ostatnich ramek */
    lp_state_t lp;                    /* poprzednie A(z) i wspolczynniki odbicia */
    int shape[ORD];                   /* ostatnie wyjscia filtru ksztaltujacego warstwy 1 */
    int have_shape;
    double ng_gain;                   /* bramka szumow: biezace wzmocnienie (Q15) */
    i32 e_prev;                       /* energia poprzedniej ramki w bramce szumow */
    int lb_delay[LB];                 /* wyjscie dolnego pasma jest opozniona o jedna ramke */
    double hb_prev[LB];               /* druga polowa poprzedniego IMDCT gornego pasma (nakladanie okien) */
    int hb_muted;                     /* pierwsza ramka po resecie: wyjscie gornego pasma = 0 */
    int qmf_sum[16], qmf_dif[16]; int qmf_hist_n;     /* pamiec syntezy QMF: ostatnie 15 probek sumy i roznicy */
    int qmf_ov_pre;                   /* czy poprzednia ramka miala ryzyko przepelnienia (skala 1/2) */
    int primed;
} g7111_t;

/* ---------------------------------------------------------------- dolne pasmo ---------------------------------------------------------------- */
static int lround_i(double v) { return (int)floor(v + 0.5); }

/* rdzen G.711 -> probka z przesunieciem prawa A (zachowane), wykladnik i znak */
static int core_decode(int alaw, uint8_t code, int *e, int *sgn) {
    int y, m, val;
    *sgn = code & 0x80;
    if (alaw) {
        y = (code ^ 0x55) & 0x7f; *e = y >> 4; m = y & 0x0f;
        val = (m << 4) + 8;
        if (*e > 0) val = (val + 256) << (*e - 1);
        return *sgn ? val : -val;
    }
    y = (code ^ 0x7f) & 0x7f; *e = y >> 4; m = y & 0x0f;
    val = (((m << 3) + 132) << *e) - 132;
    return *sgn ? val : -val;
}

/* tablica przydzialu bitow warstwy 1: 80 bitow na 40 probek, wiecej dla wiekszych wykladnikow */
static void bit_allocation(const int *e, int *ba) {
    int rows[10][3 * LB], cnt[10] = { 0 }, n, i, budget = 80, j;
    memset(ba, 0, LB * sizeof *ba);
    for (n = 0; n < (int)LB; n++) for (i = 0; i < 3; i++) { const int r = e[n] + i; rows[r][cnt[r]++] = n; }
    for (j = 9; j >= 0 && budget > 0; j--) {
        const int q = cnt[j] < budget ? cnt[j] : budget;
        for (i = 0; i < q; i++) ba[rows[j][i]]++;
        budget -= q;
    }
}

/* ulamek bitow warstwy 1 -> poprawka probki; `bits` to liczba przydzielonych bitow, `code` ich wartosc */
static int enhancement(int alaw, int code, int e, int sgn, int bits) {
    int ext = (code << (4 - bits)) - (8 - (8 >> bits));
    if (alaw) { if (e - 1 > 0) ext *= 1 << (e - 1); }
    else ext = e >= 1 ? ext * (1 << (e - 1)) : (ext >> 1);
    return sgn ? ext : -ext;
}

static double lp_window(unsigned n) {                       /* okno asymetryczne 60 + 20 probek analizy LP */
    const double pi = 3.14159265358979323846;
    if (n == 0) return 0.0;
    if (n < 60u) { const double t = pi * ((double)n + 0.5) / 120.0; return 0.5 * sin(t) + 0.5 * sin(t) * sin(t); }
    { const double t = pi * ((double)n - 60.0 + 0.5) / 40.0; return 0.5 * cos(t) + 0.5 * cos(t) * cos(t); }
}

/* ---- arytmetyka stalopozycyjna 16 / 32 bity z nasyceniem (definicje podstawowych operatorow ITU-T G.191, uzywane w normie) ---- */
static i32 sat32(long long v) { return v > 2147483647LL ? 2147483647 : v < -2147483648LL ? (i32)-2147483647 - 1 : (i32)v; }
static int sat16(long long v) { return v > 32767 ? 32767 : v < -32768 ? -32768 : (int)v; }
static i32 l_mult(int a, int b) { return (a == -32768 && b == -32768) ? 2147483647 : (i32)(2 * a * b); }
static i32 l_add(i32 a, i32 b) { return sat32((long long)a + b); }
static i32 l_sub(i32 a, i32 b) { return sat32((long long)a - b); }
static i32 l_mac(i32 acc, int a, int b) { return l_add(acc, l_mult(a, b)); }
static i32 l_shl(i32 v, int n) { if (n <= 0) return n < -31 ? (v < 0 ? -1 : 0) : (v >> -n); return n > 31 ? (v == 0 ? 0 : v > 0 ? 2147483647 : (i32)-2147483647 - 1) : sat32((long long)v * (1LL << n)); }
static int norm_l(i32 v) { int n = 0; if (v == 0) return 0; if (v < 0) v = ~v; while (v < 0x40000000) { v <<= 1; n++; } return n; }
static int mult16(int a, int b) { return sat16(((long long)a * b) >> 15); }
static int mult_r(int a, int b) { return sat16((((long long)a * b) + 16384) >> 15); }
static int shl16(int a, int n) { return n >= 0 ? sat16((long long)a * (1LL << (n > 20 ? 20 : n))) : (a >> (-n > 15 ? 15 : -n)); }
static int shr16(int a, int n) { return shl16(a, -n); }
static int div_s(int num, int den) { if (num == den) return 32767; return (int)(((long long)num * 32768) / den); }
static i32 l_abs(i32 v) { return v == (i32)-2147483647 - 1 ? 2147483647 : v < 0 ? -v : v; }
static i32 l_neg(i32 v) { return v == (i32)-2147483647 - 1 ? 2147483647 : -v; }
/* format "podwojnej precyzji": liczba Q31 jako hi (Q15) + lo (nizsze 15 bitow) */
static void split32(i32 v, int *hi, int *lo) { *hi = v >> 16; *lo = (v >> 1) - *hi * 32768; }
static i32 join32(int hi, int lo) { return sat32((long long)hi * 65536 + 2LL * lo); }
static i32 mpy32(int h1, int l1, int h2, int l2) { i32 r = l_mult(h1, h2); r = l_mac(r, mult16(h1, l2), 1); r = l_mac(r, mult16(l1, h2), 1); return r; }
static i32 mpy32_16(int h, int l, int n) { i32 r = l_mult(h, n); r = l_mac(r, mult16(l, n), 1); return r; }
static i32 div32(i32 num, int dh, int dl) {                  /* num / (dh,dl): Newton 1/x w arytmetyce 32 bitowej */
    int appr = div_s(0x3fff, dh), hi, lo, nh, nl; i32 t;
    t = mpy32_16(dh, dl, appr); t = l_sub(0x7fffffff, t);
    split32(t, &hi, &lo); t = mpy32_16(hi, lo, appr);
    split32(t, &hi, &lo); split32(num, &nh, &nl);
    t = mpy32(nh, nl, hi, lo);
    return l_shl(t, 2);
}

/* pierwiastek 32 bitowej liczby (tablica 49 punktow sqrt(x) dla x = 0,25 .. 1 w Q15 z interpolacja liniowa); wynik = sqrt(wejscie) * 2^15,5 */
static i32 sqrt31(i32 v) {
    static int tab[49]; static int ready;
    int ex, ex2, idx, frac, diff, i;
    i32 acc;
    if (!ready) { for (i = 0; i < 49; i++) { const double q = floor(sqrt((double)(i + 16) / 64.0) * 32768.0 + 0.5); tab[i] = q > 32767.0 ? 32767 : (int)q; } ready = 1; }
    if (v <= 0) return 0;
    ex = norm_l(v); ex2 = ex >> 1; ex = ex2 << 1;
    acc = l_shl(v, ex) >> 9;
    idx = acc >> 16;
    frac = (int)((l_shl(acc - (i32)(idx * 65536), 15)) >> 16);
    diff = tab[idx - 16] - tab[idx + 1 - 16];
    acc = (i32)((long long)tab[idx - 16] * 65536);
    acc = l_sub(acc, l_mult(frac, diff));
    return acc >> ex2;
}

/* stan filtra percepcyjnego (lp_state_t): poprzednie (juz wazone) A(z) i wspolczynniki odbicia; przy niestabilnej analizie zostaje stara wartosc */

/* analiza LP rzedu 4 dolnego pasma z 80 ostatnich probek rdzenia; wynik: a[1..4] w Q12 z wagami / tlumieniem; arytmetyka jak w normie (16 / 32 bity, hi+lo) */
static void perceptual_filter(lp_state_t *st, const double *xd, int *c) {
    static int win[HIST], lag_h[ORD], lag_l[ORD]; static int ready;
    const double pi = 3.14159265358979323846;
    int x[HIST], y[HIST], zcross, alpha, sshift = 0, norm, i, j, rh[ORD + 1], rl[ORD + 1], hi, lo, stable_fail = 0;
    i32 sum, t0, t1, t2;
    if (!ready) {
        for (i = 0; i < (int)HIST; i++) win[i] = lround_i(lp_window((unsigned)i) * 32768.0);
        for (i = 0; i < (int)ORD; i++) { const double w = 2.0 * pi * 120.0 * (double)(i + 1) / 8000.0; split32((i32)floor(0.9999 * exp(-0.5 * w * w) * 2147483648.0 + 0.5), &lag_h[i], &lag_l[i]); }
        ready = 1;
    }
    for (i = 0; i < (int)HIST; i++) x[i] = (int)xd[i];
    zcross = (int)HIST - 1;
    for (i = 1; i < (int)HIST; i++) if ((x[i - 1] ^ x[i]) < 0) zcross--;
    zcross = 12543 + zcross * 256;
    y[0] = 0;
    for (i = 1; i < (int)HIST; i++) y[i] = mult_r(win[i], sat16((long long)x[i] - mult_r(zcross, x[i - 1])));
    alpha = 100; sum = l_mult(alpha, 100);
    for (i = 1; i < (int)HIST; i++) sum = l_mac(sum, y[i], y[i]);
    if (sum == 2147483647) {                                                                  /* przepelnienie: skala 1/4 i podloga 25 */
        sshift = 2; alpha = 25; sum = l_mult(alpha, 25);
        for (i = 1; i < (int)HIST; i++) { y[i] = shr16(y[i], 2); sum = l_mac(sum, y[i], y[i]); }
    }
    alpha = mult16(alpha, 31130);
    norm = norm_l(sum);
    sum = l_shl(sum, norm); split32(sum, &rh[0], &rl[0]);
    for (i = 1; i <= (int)ORD; i++) {
        sum = l_mult(alpha, shr16(100, sshift));
        alpha = mult16(alpha, 31130);
        for (j = 1; j < (int)HIST - i; j++) sum = l_mac(sum, y[j], y[j + i]);
        sum = l_shl(sum, norm); split32(sum, &rh[i], &rl[i]);
    }
    for (i = 1; i <= (int)ORD; i++) { t0 = mpy32(rh[i], rl[i], lag_h[i - 1], lag_l[i - 1]); split32(t0, &rh[i], &rl[i]); }   /* okno opoznien */
    {   /* Levinson-Durbin w formacie hi + lo */
        int ah[ORD + 1] = { 0 }, al[ORD + 1] = { 0 }, nh[ORD + 1], nl[ORD + 1], kh, kl, alh, all, alexp, rctmp[ORD], ord = (int)ORD;
        t1 = join32(rh[1], rl[1]); t2 = l_abs(t1);
        t0 = div32(t2, rh[0], rl[0]);
        if (t1 > 0) t0 = l_neg(t0);
        split32(t0, &kh, &kl); rctmp[0] = kh;
        t0 = l_shl(t0, -4); split32(t0, &ah[1], &al[1]);
        t0 = mpy32(kh, kl, kh, kl); t0 = l_abs(t0); t0 = l_sub(0x7fffffff, t0); split32(t0, &hi, &lo);
        t0 = mpy32(rh[0], rl[0], hi, lo);
        alexp = norm_l(t0); t0 = l_shl(t0, alexp); split32(t0, &alh, &all);
        for (i = 2; i <= ord && !stable_fail; i++) {
            t0 = mpy32(rh[1], rl[1], ah[i - 1], al[i - 1]);
            for (j = 2; j < i; j++) t0 = l_add(t0, mpy32(rh[j], rl[j], ah[i - j], al[i - j]));
            t0 = l_shl(t0, 4); t1 = join32(rh[i], rl[i]); t0 = l_add(t0, t1);
            t1 = l_abs(t0); t2 = div32(t1, alh, all);
            if (t0 > 0) t2 = l_neg(t2);
            t2 = l_shl(t2, alexp); split32(t2, &kh, &kl); rctmp[i - 1] = kh;
            if ((kh < 0 ? -kh : kh) > 32750) { stable_fail = 1; break; }                      /* niestabilny: zostaje poprzednie A(z) */
            for (j = 1; j < i; j++) { t0 = mpy32(kh, kl, ah[i - j], al[i - j]); t0 = l_add(t0, join32(ah[j], al[j])); split32(t0, &nh[j], &nl[j]); }
            t2 = l_shl(t2, -4); split32(t2, &nh[i], &nl[i]);
            t0 = mpy32(kh, kl, kh, kl); t0 = l_abs(t0); t0 = l_sub(0x7fffffff, t0); split32(t0, &hi, &lo);
            t0 = mpy32(alh, all, hi, lo);
            j = norm_l(t0); t0 = l_shl(t0, j); split32(t0, &alh, &all); alexp += j;
            for (j = 1; j <= i; j++) { ah[j] = nh[j]; al[j] = nl[j]; }
        }
        if (!stable_fail) {
            st->a[0] = 4096;
            for (i = 1; i <= ord; i++) st->a[i] = sat16(((long long)l_shl(join32(ah[i], al[i]), 1) + 0x8000) >> 16);
            for (i = 0; i < ord; i++) st->rc[i] = rctmp[i];
        }
    }
    if (norm >= 16) {                                                                         /* sygnal bardzo cichy: a_i * 2^-(i + norm - 16) */
        for (i = 1; i <= (int)ORD; i++) st->a[i] = shr16(st->a[i], i + norm - 16);
    } else {
        int sal = -st->rc[0], fac, gam = 30147;                                               /* gamma = 0,92 w Q15; sal = r1 / r0 */
        if (sal < -32256) {                                                                   /* r1 / r0 < -0,984375: tlumienie dla widma przy 4 kHz */
            sal = sat16((long long)sal + 32767); sal = sat16((long long)sal + 1536); sal = shl16(sal, 4);
            gam = mult_r(30147, sal);
        }
        fac = gam;
        for (i = 1; i < (int)ORD; i++) { st->a[i] = mult_r(st->a[i], fac); fac = mult_r(fac, gam); }
        st->a[ORD] = mult_r(st->a[ORD], fac);
    }
    for (i = 1; i <= (int)ORD; i++) c[i - 1] = st->a[i];
}

/* jedna ramka dolnego pasma; l1 = NULL gdy brak warstwy 1; out = 40 probek calkowitych (bez opoznienia), *gt = docelowe wzmocnienie bramki szumow (0 .. 1) */
static void lb_frame(g7111_t *d, const uint8_t *l0, const uint8_t *l1, int *out, double *gt) {
    int sig[LB], e[LB], sg[LB], ba[LB], c[ORD], n, i;
    const int off = d->alaw ? 8 : 0;
    perceptual_filter(&d->lp, d->past, c);
    for (n = 0; n < (int)LB; n++) sig[n] = core_decode(d->alaw, l0[n], &e[n], &sg[n]) - off;
    memmove(d->past, d->past + LB, LB * sizeof d->past[0]);
    for (n = 0; n < (int)LB; n++) d->past[LB + (unsigned)n] = (double)sig[n];
    if (l1) {                                                 /* warstwa 1 sklada sie z samych zer = brak warstwy */
        int zero = 1;
        for (i = 0; i < 10; i++) if (l1[i]) { zero = 0; break; }
        if (zero) l1 = NULL;
    }
    if (l1) {
        unsigned bitpos = 0;
        int hist[ORD + LB];
        bit_allocation(e, ba);
        for (i = 0; i < (int)ORD; i++) hist[i] = d->have_shape ? d->shape[i] : 0;
        for (n = 0; n < (int)LB; n++) {
            int code = 0, b, ex;
            double acc;
            for (b = 0; b < ba[n]; b++, bitpos++) code = (code << 1) | ((l1[bitpos >> 3] >> (7u - (bitpos & 7u))) & 1);
            ex = enhancement(d->alaw, code, e[n], sg[n], ba[n]);
            acc = (double)ex;
            for (i = 1; i <= (int)ORD; i++) acc -= (double)c[i - 1] / 4096.0 * (double)hist[(int)ORD + n - i];
            hist[ORD + (unsigned)n] = lround_i(acc);
            { int v = sig[n] + hist[ORD + (unsigned)n]; sig[n] = v < -32768 ? -32768 : v > 32767 ? 32767 : v; }
        }
        for (i = 0; i < (int)ORD; i++) d->shape[i] = hist[LB + (unsigned)i];
        d->have_shape = 1;
    }
    memcpy(out, sig, sizeof sig);
    /* bramka szumow: energia sygnalu rdzenia (bez warstwy 1) po filtrze 1 - 0,75 z^-1, dwie ramki, z podloga 300 w arytmetyce 32 bitowej jak w normie */
    {   i32 cur = 300, tot; int prev = (int)d->past[LB - 1u], g;
        for (n = 0; n < (int)LB; n++) {
            const int x = (int)d->past[LB + (unsigned)n], z = sat16((long long)x - mult_r(prev, 24576));
            cur = l_mac(cur, z, z); prev = x;
        }
        tot = l_add(d->e_prev, cur); d->e_prev = cur;
        g = sat16(l_shl(sqrt31(tot), 9) >> 16);
        *gt = (double)(g < 8192 ? 8192 : g);
    }
}

/* ---------------------------------------------------------------- gorne pasmo (warstwa 2) ---------------------------------------------------------------- */
extern const int16_t g7111_codebook0[32][6], g7111_codebook1[32][6];

static int mulaw_inverse(int idx) {                           /* wzmocnienie: 8 bitowe rozwijanie mu-law z wyjatkiem malych wartosci */
    int lb = idx & 0xff, e, m;
    if (lb < 4) return lb & 3;
    lb -= 3; e = (lb >> 5) & 7; m = lb & 31;
    return (((m << 2) + 130) << e) - 130;
}

/* 40 probek gornego pasma z 10 bajtow warstwy 2; Q: wzmocnienie lin / (4 sqrt 10) * wektor (srednia dwoch ksiazek) * odwrotna waga czestotliwosciowa; IMDCT z okien 80 probek */
static void hb_frame(g7111_t *d, const uint8_t *l2, int *out) {
    static double cosine[LB][80]; static int ready;
    const double pi = 3.14159265358979323846;
    double spec[LB], ola[80];
    unsigned bitpos = 0, v, j, k, n;
    int p0[6], p1[6], s0[6], s1[6], idx_g = 0;
    #define GETBITS(nb, dst) do { int _a = 0; unsigned _b; for (_b = 0; _b < (nb); _b++, bitpos++) _a = (_a << 1) | ((l2[bitpos >> 3] >> (7u - (bitpos & 7u))) & 1); (dst) = _a; } while (0)
    for (v = 0; v < 6; v++) { GETBITS(1, p0[v]); GETBITS(5, s0[v]); GETBITS(1, p1[v]); GETBITS(5, s1[v]); }
    GETBITS(8, idx_g);
    #undef GETBITS
    if (!ready) { for (k = 0; k < LB; k++) for (n = 0; n < 80; n++) cosine[k][n] = cos(pi / 40.0 * ((double)k + 0.5) * ((double)n + 20.5)); ready = 1; }
    {   /* dekwantyzacja w arytmetyce 16 bitowej jak w normie (wspolczynniki trafiaja do IMDCT jako liczby calkowite ze skala 2^-q) */
        const int g0 = mulaw_inverse(idx_g); int e = 0, gq, si[LB];
        static const int invw[6] = { 32768, 32768, 24576, 19661, 16384, 32768 }, sinw[6] = { 4277, 12540, 19948, 25997, 30274, 32488 };
        if (g0 != 0) while (((long long)g0 * (1LL << e)) < 16384) e++;
        gq = mult_r(sat16((long long)g0 * (1LL << e)), 20724);
        for (k = 0; k < LB; k++) si[k] = 0;
        for (v = 0; v < 6; v++) {
            const int16_t *c0 = g7111_codebook0[s0[v]], *c1 = g7111_codebook1[s1[v]];
            const int pol0 = (p0[v] ? -1 : 1) * 16384, pol1 = (p1[v] ? -1 : 1) * 16384;
            for (j = 0; j < 6; j++) {
                int val = (int)(l_mac(l_mult(pol0, c0[j]), pol1, c1[j]) >> 16);
                int r = (int)(((long long)gq * val) >> 15);
                if (j == 2 || j == 3) r = mult_r(invw[j], r); else if (j == 4) r >>= 1;
                k = 39u - v - 6u * j;
                if (j == 5) r = mult_r(r, sinw[k - 4u]);
                si[k] = r >> 1;
            }
        }
        for (k = 0; k < LB; k++) spec[k] = ldexp((double)si[k], -(e - 1));
    }
    for (n = 0; n < 80; n++) { double a = 0.0; for (k = 4; k < LB; k++) a += cosine[k][n] * spec[k]; ola[n] = 2.0 * a; }
    for (n = 0; n < LB; n++) {
        const double w0 = 2.0 * sin(pi * ((double)n + 0.5) / 80.0), w1 = 2.0 * sin(pi * (39.0 - (double)n + 0.5) / 80.0);
        const double val = HB_SCALE * (w0 * ola[n] + w1 * d->hb_prev[n]);
        out[n] = d->hb_muted ? 0 : sat16((long long)floor(val + 0.5));
    }
    for (n = 0; n < LB; n++) d->hb_prev[n] = ola[LB + n];
    d->hb_muted = 0;
}

/* synteza QMF 2 x 16 odczepow z sum i roznic obu pasm; wejscie: dolne pasmo lo[40], gorne hi[40]; wyjscie 80 probek 16 kHz */
static void qmf_tables(int *q0, int *q1) {
    static const double h0[16] = { -6.5064660e-4, -1.2601150e-3, 1.4272050e-3, -1.7219030e-4, -4.1094160e-3, 1.4468810e-2, -3.9244910e-2, 1.2846510e-1,
                                    4.6645830e-1, -9.9800110e-2, 5.2909300e-2, -3.1155320e-2, 1.7881950e-2, -9.3636330e-3, 4.1581240e-3, -1.3508480e-3 };
    static const double h1[16] = { -1.3508480e-3, 4.1581240e-3, -9.3636330e-3, 1.7881950e-2, -3.1155320e-2, 5.2909300e-2, -9.9800110e-2, 4.6645830e-1,
                                    1.2846510e-1, -3.9244910e-2, 1.4468810e-2, -4.1094160e-3, -1.7219030e-4, 1.4272050e-3, -1.2601150e-3, -6.5064660e-4 };
    int i;
    for (i = 0; i < 16; i++) { q0[i] = lround_i(h0[i] * 32768.0); q1[i] = lround_i(h1[i] * 32768.0); }
    q0[0] = -q0[0]; q1[15] = -q1[15];      /* pierwszy odczep rozni sie znakiem od tablicy 7-1 normy (blad druku tablicy; wzorzec ma +21): wynik potwierdzony na wzorcu */
}
static void qmf_synth(g7111_t *d, const int *lo, const int *hi, int *out) {
    static int q0[16], q1[16], ready;
    int buf_sum[LB + 15], buf_dif[LB + 15], i, j, ovf = 0, fmt, sshift;
    if (!ready) { qmf_tables(q0, q1); ready = 1; }
    for (i = 0; i < (int)LB; i++) if (abs(lo[i]) + abs(hi[i]) - 32767 >= 0) ovf = 1;
    for (i = 0; i < 15; i++) {                                        /* pamiec z poprzedniej ramki; przy zmianie skali (ryzyko przepelnienia) polowa */
        buf_sum[i] = d->qmf_sum[i]; buf_dif[i] = d->qmf_dif[i];
        if (!d->qmf_ov_pre && ovf) { buf_sum[i] >>= 1; buf_dif[i] >>= 1; }
    }
    fmt = (ovf || d->qmf_ov_pre) ? 1 : 0; sshift = 2 + fmt;
    for (i = 0; i < (int)LB; i++) {
        if (fmt) { const i32 l = l_mult(0x4000, lo[i]); buf_sum[15 + i] = sat16(((long long)l_mac(l, 0x4000, hi[i]) + 0x8000) >> 16); buf_dif[15 + i] = sat16(((long long)l_sub(l, l_mult(0x4000, hi[i])) + 0x8000) >> 16); }
        else { buf_sum[15 + i] = sat16((long long)lo[i] + hi[i]); buf_dif[15 + i] = sat16((long long)lo[i] - hi[i]); }
    }
    for (i = 0; i < (int)LB; i++) {
        long long a0 = 0, a1 = 0;
        for (j = 0; j < 16; j++) { a0 += (long long)q0[j] * buf_sum[i + j]; a1 += (long long)q1[j] * buf_dif[i + j]; }
        out[2 * i] = sat16((sat32(a1 * (1LL << sshift)) + 0x8000LL) >> 16);
        out[2 * i + 1] = sat16((sat32(a0 * (1LL << sshift)) + 0x8000LL) >> 16);
    }
    for (i = 0; i < 15; i++) {
        int s_ = buf_sum[LB + i], f_ = buf_dif[LB + i];
        if (!ovf && d->qmf_ov_pre) { s_ = sat16((long long)s_ * 2); f_ = sat16((long long)f_ * 2); }
        d->qmf_sum[i] = s_; d->qmf_dif[i] = f_;
    }
    d->qmf_ov_pre = ovf;
}

/* ---------------------------------------------------------------- ramka ---------------------------------------------------------------- */
static void dec_reset(g7111_t *d) {
    const int alaw = d->alaw, mode = d->mode;
    memset(d, 0, sizeof *d);
    d->alaw = alaw; d->mode = mode; d->ng_gain = 32767.0; d->lp.a[0] = 4096; d->hb_muted = 1;
}
/* probki wyjsciowe jednej ramki (40 przy 8 kHz, 80 przy 16 kHz); frame ma 40 / 50 / 60 bajtow wg trybu */
static void dec_frame(g7111_t *d, const uint8_t *frame, int *out) {
    int cur[LB], hb[LB], lbout[LB], n, g, nout, wb = (d->mode == 3 || d->mode == 4);
    double gt_d; int gt;
    lb_frame(d, frame, (d->mode == 2 || d->mode == 4) ? frame + LB : NULL, cur, &gt_d);
    gt = (int)gt_d;
    for (n = 0; n < (int)LB; n++) { lbout[n] = d->lb_delay[n]; d->lb_delay[n] = cur[n]; }
    if (wb) hb_frame(d, frame + (d->mode == 4 ? LB + 10 : LB), hb);
    if (wb) {
        long long pl = 0, ph = 0;
        qmf_synth(d, lbout, hb, out);
        for (n = 0; n < (int)LB; n++) { pl += abs(lbout[n]); ph += abs(hb[n]); }
        if (gt < 32767 && ph > 800 && (ph >> 4) > pl) gt = 32767;
        nout = 80;
    } else { memcpy(out, lbout, sizeof lbout); nout = (int)LB; }
    for (n = 0; n < nout; n++) {
        g = (int)d->ng_gain; g = (350 * gt + 32418 * g + 32768) >> 15; d->ng_gain = (double)g;
        if (g < 32767) out[n] = mult_r(g, out[n]);
    }
}

/* ---------------------------------------------------------------- strumien (surowe ramki, "hardbit") ---------------------------------------------------------------- */
typedef struct {
    const maktabasauti_host_t *host; maktabasauti_reader_t *rd;
    g7111_t dec;
    unsigned frame_bytes, out_rate, out_samples;      /* bajtow w ramce wejsciowej, czestotliwosc wyjscia, probek wyjscia na ramke */
    int g192;                                         /* wejscie w formacie ITU-T G.192 (slowa 16 bitowe: synchronizacja 0x6b21, liczba bitow, bity jako 0x007f / 0x0081) */
    int out[80]; unsigned out_at, out_n;
    uint64_t frames_total, pos;                       /* pos: probki wyjscia wydane */
    int total_known, seekable;
    maktabasauti_result_t pend;
} g7111s_t;

static unsigned mode_frame_bytes(int mode) { return mode == 1 ? 40u : mode == 4 ? 60u : 50u; }

static maktabasauti_result_t read_bytes(g7111s_t *p, uint8_t *dst, size_t need, int first) {
    size_t got = 0;
    while (got < need) {
        const int64_t m = p->host->read(p->host->user, p->rd, dst + got, need - got);
        if (m < 0) return (maktabasauti_result_t)m;
        if (m == 0) return (got || !first) ? MAKTABASAUTI_E_TRUNCATED : MAKTABASAUTI_EOF;
        got += (size_t)m;
    }
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t next_frame(g7111s_t *p) {
    uint8_t frame[60]; maktabasauti_result_t r;
    if (!p->g192) {
        r = read_bytes(p, frame, p->frame_bytes, 1);
        if (r != MAKTABASAUTI_OK) return r;
    } else {
        uint8_t head[4], words[960]; unsigned nbits, i;
        r = read_bytes(p, head, 4, 1); if (r != MAKTABASAUTI_OK) return r;
        nbits = (unsigned)head[2] | ((unsigned)head[3] << 8);
        if (head[1] != 0x6b || (head[0] != 0x21 && head[0] != 0x20) || nbits != p->frame_bytes * 8u) return MAKTABASAUTI_E_CORRUPT_DATA;
        r = read_bytes(p, words, 2u * nbits, 0); if (r != MAKTABASAUTI_OK) return r;
        memset(frame, 0, sizeof frame);
        for (i = 0; i < nbits; i++) { const unsigned w = (unsigned)words[2 * i] | ((unsigned)words[2 * i + 1] << 8); if (w > 0x80u && w < 0x8000u) frame[i >> 3] |= (uint8_t)(1u << (i & 7u)); }          /* bity strumienia G.192 trafiaja do bajtu od najmlodszego */
        if (head[0] == 0x20) { memset(p->out, 0, sizeof p->out); p->out_at = 0; p->out_n = p->out_samples; return MAKTABASAUTI_OK; }   /* ramka skasowana: bez ukrywania bledow cisza (FERC niezaimplementowane) */
    }
    dec_frame(&p->dec, frame, p->out);
    p->out_at = 0; p->out_n = p->out_samples;
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t MAKTABASAUTI_CALL g7111_read(void *state, void *dst, size_t n, size_t *frames) {
    g7111s_t *p = (g7111s_t *)state; float *o = (float *)dst; size_t done = 0; maktabasauti_result_t r = MAKTABASAUTI_OK;
    *frames = 0;
    if (p->pend != MAKTABASAUTI_OK) return p->pend;
    while (done < n) {
        if (p->out_at >= p->out_n) {
            r = next_frame(p);
            if (r != MAKTABASAUTI_OK) break;
        }
        o[done++] = (float)p->out[p->out_at++] / 32768.0f; p->pos++;
    }
    *frames = done;
    if (done) { if (r == MAKTABASAUTI_E_TRUNCATED) p->pend = r; return MAKTABASAUTI_OK; }
    if (r == MAKTABASAUTI_E_TRUNCATED) p->pend = r;
    return r;
}
static maktabasauti_result_t g7111_rewind(g7111s_t *p) {
    if (p->host->seek(p->host->user, p->rd, 0, 0) < 0) return MAKTABASAUTI_E_NOT_SEEKABLE;
    dec_reset(&p->dec); p->out_at = p->out_n = 0; p->pos = 0; p->pend = MAKTABASAUTI_OK;
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t MAKTABASAUTI_CALL g7111_seek(void *state, uint64_t frame, uint32_t flags, uint64_t *actual) {
    g7111s_t *p = (g7111s_t *)state; maktabasauti_result_t r; (void)flags;
    if (!p->seekable) return MAKTABASAUTI_E_NOT_SEEKABLE;
    if (p->total_known && frame > p->frames_total * p->out_samples) return MAKTABASAUTI_E_OUT_OF_RANGE;
    r = g7111_rewind(p);
    while (r == MAKTABASAUTI_OK && p->pos < frame) {
        if (p->out_at >= p->out_n) { r = next_frame(p); if (r != MAKTABASAUTI_OK) break; }
        { const uint64_t left = (uint64_t)(p->out_n - p->out_at), want = frame - p->pos, take = left < want ? left : want; p->out_at += (unsigned)take; p->pos += take; }
    }
    if (r == MAKTABASAUTI_EOF) r = MAKTABASAUTI_E_OUT_OF_RANGE;
    if (r == MAKTABASAUTI_OK) *actual = frame;
    return r;
}
static maktabasauti_result_t MAKTABASAUTI_CALL g7111_reset(void *state) { g7111s_t *p = (g7111s_t *)state; return p->seekable ? g7111_rewind(p) : MAKTABASAUTI_E_NOT_SEEKABLE; }
static maktabasauti_result_t MAKTABASAUTI_CALL g7111_flush(void *state) { ((g7111s_t *)state)->pend = MAKTABASAUTI_OK; return MAKTABASAUTI_OK; }
static const char *MAKTABASAUTI_CALL g7111_last_error(const void *state) { (void)state; return NULL; }
static void MAKTABASAUTI_CALL g7111_close(void *state) { g7111s_t *p = (g7111s_t *)state; if (p) p->host->free(p->host->user, p); }

static int ci_equal(const char *a, const char *b) {
    for (; *a && *b; a++, b++) { char x = *a, y = *b; if (x >= 'A' && x <= 'Z') x = (char)(x - 'A' + 'a'); if (y >= 'A' && y <= 'Z') y = (char)(y - 'A' + 'a'); if (x != y) return 0; }
    return *a == *b;
}
static int32_t MAKTABASAUTI_CALL g7111_raw_probe(const maktabasauti_codec_t *self, const uint8_t *p, size_t n, const char *hint) {
    const char *dot;
    (void)self; (void)p; (void)n;
    if (!hint) return 0;
    dot = strrchr(hint, '.'); dot = dot ? dot + 1 : hint;
    return (ci_equal(dot, "g7111") || ci_equal(dot, "g711wb")) ? 10 : 0;
}
static maktabasauti_result_t g7111_open_common(const maktabasauti_host_t *h, maktabasauti_reader_t *rd, maktabasauti_stream_info_t *info, void **state, int g192) {
    g7111s_t *p; int64_t size; int mode = 4, alaw = 1;
    const char *vm = h->option ? h->option(h->user, "g7111.mode") : NULL, *vl = h->option ? h->option(h->user, "g7111.law") : NULL;
    const uint32_t fl = h->input_flags ? h->input_flags(h->user, rd) : 0u;
    *state = NULL;
    if (g192) {                                                                          /* tryb z dlugosci pierwszej ramki: 320 = R1, 400 = R2b (R2a wymaga opcji), 480 = R3 */
        uint8_t head[4]; const int64_t pk = h->peek ? h->peek(h->user, rd, head, 4) : -1;
        unsigned nbits;
        if (pk < 4) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
        nbits = (unsigned)head[2] | ((unsigned)head[3] << 8);
        if (head[1] != 0x6b || (head[0] != 0x21 && head[0] != 0x20)) return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
        if (nbits == 320u) mode = 1; else if (nbits == 400u) mode = 3; else if (nbits == 480u) mode = 4; else return MAKTABASAUTI_E_UNSUPPORTED_FORMAT;
    }
    if (vm && vm[0]) {
        int want;
        if (ci_equal(vm, "1") || ci_equal(vm, "r1")) want = 1;
        else if (ci_equal(vm, "2") || ci_equal(vm, "r2a")) want = 2;
        else if (ci_equal(vm, "3") || ci_equal(vm, "r2b")) want = 3;
        else if (ci_equal(vm, "4") || ci_equal(vm, "r3")) want = 4;
        else return MAKTABASAUTI_E_INVALID_ARGUMENT;
        if (g192 && mode_frame_bytes(want) != mode_frame_bytes(mode)) return MAKTABASAUTI_E_INVALID_ARGUMENT;   /* tryb sprzeczny z dlugoscia ramki */
        mode = want;
    }
    if (vl && vl[0]) { if (ci_equal(vl, "a")) alaw = 1; else if (ci_equal(vl, "u") || ci_equal(vl, "mu")) alaw = 0; else return MAKTABASAUTI_E_INVALID_ARGUMENT; }
    p = (g7111s_t *)h->alloc(h->user, sizeof *p);
    if (!p) return MAKTABASAUTI_E_NO_MEMORY;
    memset(p, 0, sizeof *p);
    p->host = h; p->rd = rd; p->dec.alaw = alaw; p->dec.mode = mode; p->frame_bytes = mode_frame_bytes(mode); p->g192 = g192;
    p->out_rate = (mode == 3 || mode == 4) ? 16000u : 8000u; p->out_samples = (mode == 3 || mode == 4) ? 2u * LB : LB; p->seekable = (fl & MAKTABASAUTI_INPUT_SEEKABLE) != 0;
    dec_reset(&p->dec);
    size = h->size ? h->size(h->user, rd) : -1;
    if (size >= 0) { p->frames_total = (uint64_t)size / (g192 ? 4u + 16u * p->frame_bytes : p->frame_bytes); p->total_known = 1; }
    memset(info, 0, sizeof *info);
    info->struct_size = sizeof *info; info->sample_rate = p->out_rate; info->channels = 1u; info->channel_mask = MAKTABASAUTI_LAYOUT_MONO;
    info->native_format = MAKTABASAUTI_FORMAT_F32;
    info->total_frames = p->total_known ? p->frames_total * p->out_samples : MAKTABASAUTI_UNKNOWN_LENGTH;
    info->flags = p->seekable ? MAKTABASAUTI_STREAM_SEEKABLE : 0u;
    strncpy(info->codec_name, "g711.1", sizeof info->codec_name - 1u); strncpy(info->container_name, g192 ? "g192" : "raw", sizeof info->container_name - 1u);
    *state = p;
    return MAKTABASAUTI_OK;
}
static maktabasauti_result_t MAKTABASAUTI_CALL g7111_raw_open(const maktabasauti_codec_t *self, const maktabasauti_host_t *h, maktabasauti_reader_t *rd, maktabasauti_stream_info_t *info, void **state) {
    (void)self; return g7111_open_common(h, rd, info, state, 0);
}
static maktabasauti_result_t MAKTABASAUTI_CALL g7111_g192_open(const maktabasauti_codec_t *self, const maktabasauti_host_t *h, maktabasauti_reader_t *rd, maktabasauti_stream_info_t *info, void **state) {
    (void)self; return g7111_open_common(h, rd, info, state, 1);
}
static int32_t MAKTABASAUTI_CALL g7111_g192_probe(const maktabasauti_codec_t *self, const uint8_t *p, size_t n, const char *hint) {
    unsigned nbits;
    (void)self; (void)hint;
    if (!p || n < 4u || p[1] != 0x6b || (p[0] != 0x21 && p[0] != 0x20)) return 0;
    nbits = (unsigned)p[2] | ((unsigned)p[3] << 8);
    return (nbits == 320u || nbits == 400u || nbits == 480u) ? 55 : 0;
}
const maktabasauti_codec_t maktabasauti_g7111_raw_desc = {
    sizeof(maktabasauti_codec_t), MAKTABASAUTI_ABI_VERSION, "g7111-raw", "ITU-T G.711.1 frames without a header (options g7111.mode, g7111.law)", "maktabasauti_g7111",
    MAKTABASAUTI_CODEC_FLAG_STREAMING | MAKTABASAUTI_CODEC_FLAG_HAS_SEEK, 0u, NULL,
    g7111_raw_probe, g7111_raw_open, g7111_read, g7111_seek, g7111_reset, g7111_flush, NULL, g7111_last_error, NULL, g7111_close, { NULL }, { NULL }
};
const maktabasauti_codec_t maktabasauti_g7111_g192_desc = {
    sizeof(maktabasauti_codec_t), MAKTABASAUTI_ABI_VERSION, "g7111-g192", "ITU-T G.711.1 in the ITU-T G.192 bitstream format (option g7111.law, g7111.mode for R2a)", "maktabasauti_g7111",
    MAKTABASAUTI_CODEC_FLAG_STREAMING | MAKTABASAUTI_CODEC_FLAG_HAS_SEEK, 0u, NULL,
    g7111_g192_probe, g7111_g192_open, g7111_read, g7111_seek, g7111_reset, g7111_flush, NULL, g7111_last_error, NULL, g7111_close, { NULL }, { NULL }
};
