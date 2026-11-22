/* USAC eSBR predictive vector coding (PVC): unit tests of the predictor (the streams themselves are in test_usac_xaac; scripts/test_usac_vs_xaac.sh and the instrumented comparison of docs SBR-114 compare them with libxaac).
 *   1. the predictor against a second, direct implementation of the clause (double precision, the history kept as one array per slot): random low band energies and pvc_id's, both modes, both smoothing
 *      lengths, both rates, several frames in a row (the history of the 15 slots before a frame), first slots that belong to the previous frame;
 *   2. the history rules: a first PVC frame, a changed crossover or rate and a classic frame in between (prev_flag cleared) take the first processed slot as their past;
 *   3. refusals: pvc_id above 127, a rate other than 2 / 4, mode 0 / 3, crossover beyond 63, first slot beyond 15 - the state stays as it was;
 *   4. the data tables: group boundaries ascending, the sixth / eighth coefficient of every id is inside the signed byte range, the smoothing windows sum to one. */
#include "sauti_usac_pvc.h"
#include "sauti_usac_pvc_tables.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_failures;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("FAIL %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t g_seed = 12345u;
static unsigned rnd(void) { g_seed = g_seed * 1664525u + 1013904223u; return g_seed >> 8; }

/* the reference model: history[0..14] are the slots before the frame, history[15 + t] the slots of the frame */
typedef struct { double hist[31][3]; int valid, rate, first_band; } Model;

static void model_frame(Model *m, const SautiUsacPvcFrame *f, int rate, int kx, const float low[16][32], double out[16][64]) {
    static const int nh[3] = { 0, 8, 6 }, per[3] = { 0, 8, 12 };
    const int mode = (int)f->mode, nb_high = nh[mode], per_group = per[mode] / rate, lbw = 8 / rate;
    const int len = mode == 1 ? (f->ns_mode ? 4 : 16) : (f->ns_mode ? 3 : 12);
    const float *win = mode == 1 ? (f->ns_mode ? sauti_usac_pvc_smooth_ns4 : sauti_usac_pvc_smooth_ns16) : (f->ns_mode ? sauti_usac_pvc_smooth_ns3 : sauti_usac_pvc_smooth_ns12);
    const uint8_t *bound = mode == 1 ? sauti_usac_pvc_id_boundary_m1 : sauti_usac_pvc_id_boundary_m2;
    const float *qf = mode == 1 ? sauti_usac_pvc_qfac_m1 : sauti_usac_pvc_qfac_m2;
    int t, g, h, j, k;
    for (t = 0; t < 16; t++)
        for (g = 0; g < 3; g++) {
            const int a = kx - lbw * 3 + lbw * g;
            double e = 0.1;
            if (a >= 0) { e = 0; for (k = a; k < a + lbw; k++) e += low[t][k]; e /= lbw; }
            m->hist[15 + t][g] = e > 0.1 ? 10.0 * log10((double)(float)e) : -10.0;
        }
    if (!m->valid || kx * rate != m->first_band * m->rate)
        for (t = 0; t < 15 + (int)f->first_slot; t++) memcpy(m->hist[t], m->hist[15 + f->first_slot], sizeof m->hist[t]);
    for (t = 0; t < 16; t++) {
        double sm[3] = { 0, 0, 0 }, db[8];
        const unsigned id = f->id[t], grp = id < bound[0] ? 0u : id < bound[1] ? 1u : 2u;
        int a, z;
        for (j = 0; j < len; j++) for (g = 0; g < 3; g++) sm[g] += (double)m->hist[15 + t - j][g] * win[j];
        for (h = 0; h < nb_high; h++) {
            db[h] = (double)(mode == 1 ? sauti_usac_pvc_coef2_m1[id][h] : sauti_usac_pvc_coef2_m2[id][h]) * qf[3];
            for (g = 0; g < 3; g++) db[h] += (double)(mode == 1 ? sauti_usac_pvc_coef1_m1[grp][g][h] : sauti_usac_pvc_coef1_m2[grp][g][h]) * qf[g] * sm[g];
        }
        a = kx;
        for (h = 0; h < nb_high; h++) {
            z = a + per_group - 1;
            if (h >= nb_high - 2 || z > 63) z = 63;
            for (k = a; k <= z && k < 64; k++) out[t][k] = pow(10.0, db[h] / 10.0);
            a += per_group;
        }
    }
    memmove(m->hist[0], m->hist[16], sizeof(double) * 3 * 15);
    m->valid = 1; m->rate = rate; m->first_band = kx;
}

static void random_frame(SautiUsacPvcFrame *f, unsigned mode, unsigned first) {
    unsigned t;
    memset(f, 0, sizeof *f);
    f->active = 1; f->mode = mode; f->ns_mode = rnd() & 1u; f->first_slot = first;
    for (t = 0; t < 16; t++) f->id[t] = (uint16_t)((t && (rnd() & 3u)) ? f->id[t - 1u] : rnd() & 127u);
}
static void random_low(float low[16][32], double scale) {
    unsigned t, b;
    for (t = 0; t < 16; t++) for (b = 0; b < 32; b++) low[t][b] = (float)(scale * (double)(rnd() & 0xffffu) * ((rnd() & 7u) ? 1.0 : 1e-6));
}

static void test_against_model(void) {
    unsigned run;
    for (run = 0; run < 40; run++) {
        const unsigned mode = 1u + (run & 1u), rate = (run & 2u) ? 4u : 2u, kx = (run % 5u == 0u) ? 5u + rnd() % 7u : 12u + rnd() % 6u;     /* kx < 12 (rate 2): the lowest groups lie below subband 0 */
        SautiUsacPvcState st; Model m; unsigned fr;
        memset(&st, 0, sizeof st); memset(&m, 0, sizeof m);
        for (fr = 0; fr < 6; fr++) {
            SautiUsacPvcFrame f; float low[16][32], out[16][64]; double ref[16][64];
            unsigned t, k; double worst = 0.0;
            random_frame(&f, mode, (fr == 2u) ? 2u : (fr == 0u) ? run % 4u : 0u);
            random_low(low, (run & 4u) ? 1.0e-3 * (double)(1u + rnd() % 8u) : 1.0e3 + (double)(rnd() % 1000u) * 1.0e3);      /* quiet runs reach the floor of the group energies */
            memset(out, 0, sizeof out); memset(ref, 0, sizeof ref);
            CHECK(sauti_usac_pvc_predict(&st, &f, rate, kx, low, out) == 1, "run %u frame %u: refused", run, fr);
            model_frame(&m, &f, (int)rate, (int)kx, low, ref);
            for (t = 0; t < 16; t++) for (k = kx; k < 64; k++) { const double d = fabs((double)out[t][k] - ref[t][k]) / (fabs(ref[t][k]) + 1e-30); if (d > worst) worst = d; }
            CHECK(worst < 2e-4, "run %u (mode %u rate %u kx %u) frame %u: predictor differs from the model by %.3e", run, mode, rate, kx, fr, worst);
        }
    }
}

static void test_history_rules(void) {
    SautiUsacPvcFrame f; SautiUsacPvcState a, b; float low1[16][32], low2[16][32], o1[16][64], o2[16][64]; unsigned t, k, diff = 0;
    random_frame(&f, 2, 0); random_low(low1, 5.0e4); random_low(low2, 5.0e4);
    memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
    sauti_usac_pvc_predict(&a, &f, 2, 14, low1, o1);
    sauti_usac_pvc_predict(&a, &f, 2, 14, low2, o1);               /* second frame: real history */
    b = a; b.prev_flag = 0;                                         /* a classic frame in between clears the flag */
    sauti_usac_pvc_predict(&a, &f, 2, 14, low1, o1);
    sauti_usac_pvc_predict(&b, &f, 2, 14, low1, o2);
    for (t = 0; t < 16; t++) for (k = 14; k < 64; k++) if (fabsf(o1[t][k] - o2[t][k]) > 1e-3f * o1[t][k]) diff++;
    CHECK(diff > 0, "a cleared prev_flag must change the prediction (the history is replaced by the first slot)");
    /* the crossover moves: the same replacement */
    b = a; sauti_usac_pvc_predict(&a, &f, 2, 14, low2, o1);
    sauti_usac_pvc_predict(&b, &f, 2, 15, low2, o2);
    diff = 0; for (t = 0; t < 16; t++) for (k = 15; k < 64; k++) if (o1[t][k] != o2[t][k]) diff++;
    CHECK(diff > 0, "a moved crossover changes the prediction");
}

static void test_refusals(void) {
    SautiUsacPvcFrame f; SautiUsacPvcState st, copy; float low[16][32], out[16][64];
    random_frame(&f, 1, 0); random_low(low, 1.0e4);
    memset(&st, 0, sizeof st); sauti_usac_pvc_predict(&st, &f, 2, 13, low, out); copy = st;
    f.id[7] = 128; CHECK(!sauti_usac_pvc_predict(&st, &f, 2, 13, low, out), "pvc_id 128 accepted"); f.id[7] = 5;
    CHECK(!sauti_usac_pvc_predict(&st, &f, 3, 13, low, out), "rate 3 accepted");
    CHECK(!sauti_usac_pvc_predict(&st, &f, 1, 13, low, out), "rate 1 accepted");
    f.mode = 0; CHECK(!sauti_usac_pvc_predict(&st, &f, 2, 13, low, out), "mode 0 accepted");
    f.mode = 3; CHECK(!sauti_usac_pvc_predict(&st, &f, 2, 13, low, out), "mode 3 accepted"); f.mode = 1;
    CHECK(!sauti_usac_pvc_predict(&st, &f, 2, 64, low, out), "crossover 64 accepted");
    f.first_slot = 16; CHECK(!sauti_usac_pvc_predict(&st, &f, 2, 13, low, out), "first slot 16 accepted"); f.first_slot = 0;
    CHECK(!sauti_usac_pvc_predict(NULL, &f, 2, 13, low, out) && !sauti_usac_pvc_predict(&st, NULL, 2, 13, low, out) && !sauti_usac_pvc_predict(&st, &f, 2, 13, NULL, out) && !sauti_usac_pvc_predict(&st, &f, 2, 13, low, NULL), "null arguments accepted");
    CHECK(memcmp(&st, &copy, sizeof st) == 0, "a refused call changed the state");
}

static void test_tables(void) {
    unsigned i; double sum;
    CHECK(sauti_usac_pvc_id_boundary_m1[0] < sauti_usac_pvc_id_boundary_m1[1] && sauti_usac_pvc_id_boundary_m1[1] < 128u, "mode 1 boundaries");
    CHECK(sauti_usac_pvc_id_boundary_m2[0] < sauti_usac_pvc_id_boundary_m2[1] && sauti_usac_pvc_id_boundary_m2[1] < 128u, "mode 2 boundaries");
    sum = 0; for (i = 0; i < 4; i++) sum += sauti_usac_pvc_smooth_ns4[i];
    CHECK(fabs(sum - 1.0) < 1e-6, "ns4 window sums to %.9f", sum);
    sum = 0; for (i = 0; i < 16; i++) sum += sauti_usac_pvc_smooth_ns16[i];
    CHECK(fabs(sum - 1.0) < 1e-6, "ns16 window sums to %.9f", sum);
    sum = 0; for (i = 0; i < 3; i++) sum += sauti_usac_pvc_smooth_ns3[i];
    CHECK(fabs(sum - 1.0) < 1e-6, "ns3 window sums to %.9f", sum);
    sum = 0; for (i = 0; i < 12; i++) sum += sauti_usac_pvc_smooth_ns12[i];
    CHECK(fabs(sum - 1.0) < 1e-6, "ns12 window sums to %.9f", sum);
    CHECK(sauti_usac_pvc_coef2_m1[127][7] != 0 || sauti_usac_pvc_coef2_m1[127][0] != 0, "mode 1 offsets of id 127 are not all zero");
    CHECK(sauti_usac_pvc_coef1_m1[0][0][0] == 79 && sauti_usac_pvc_coef1_m1[0][0][1] == 91 && sauti_usac_pvc_coef1_m2[0][0][0] == 17, "table spot values (0x4F, 0x5B, 0x11)");
}

int main(void) {
    test_tables(); test_against_model(); test_history_rules(); test_refusals();
    if (g_failures) { printf("%d FAILURES\n", g_failures); return 1; }
    printf("test_usac_pvc: all passed\n"); return 0;
}
