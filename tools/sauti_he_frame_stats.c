/* Narzedzie diagnostyczne (nie produkt): statystyka ramek SBR/PS w prawdziwym pliku ADTS HE-AAC mono-rdzeniowym.  Dla kazdej ramki probuje dekodowania standardowego, a przy odmowie wariantu "temporalnego"
 * (obwiednia SBR siegajaca poza biezaca ramke) i liczy: ramki przyjete standardowo, przyjete TYLKO temporalnie, odrzucone w obu, ramki z PS, klasy ramek SBR (FIXFIX/FIXVAR/VARFIX/VARVAR) i ramki z
 * ostatnia granica obwiedni > 16.  Uzycie: sauti_he_frame_stats <plik.aac> [--frames] (--frames: dodatkowo wiersz "F <indeks> class=.. env=.. noise=.. last_border=.." na ramke).  Wynik pokazuje, ile pracy zostalo do pelnego dekodera HE na tym pliku (docs/AAC_FAMILY_ASM.md, "HE-AAC na prawdziwych plikach"). */
#include "sauti_aac_he_adts_mono.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    FILE *f; long n; uint8_t *b; size_t off = 0;
    static SautiAacHeAdtsMonoState st;
    static float pcm[1024];
    unsigned frames = 0, ok_std = 0, ok_temp = 0, neither = 0, ps = 0, cls[4] = {0}, over = 0;
    const int per_frame = argc == 3 && !strcmp(argv[2], "--frames");
    if (argc != 2 && !per_frame) { fprintf(stderr, "uzycie: %s plik.aac [--frames]\n", argv[0]); return 2; }
    f = fopen(argv[1], "rb");
    if (!f || fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) <= 0 || n > 500000000L || fseek(f, 0, SEEK_SET) != 0) { perror(argv[1]); return 2; }
    b = (uint8_t *)malloc((size_t)n);
    if (!b || fread(b, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(b); return 2; }
    fclose(f);
    if (!sauti_aac_he_adts_mono_init(&st)) return 2;
    while (off + 7u < (size_t)n) {
        const unsigned len = ((unsigned)(b[off + 3] & 3u) << 11) | ((unsigned)b[off + 4] << 3) | ((unsigned)b[off + 5] >> 5);
        SautiAacSbrPayloadFrame p; SautiAacReason r = SAUTI_AAC_REASON_OK; SautiAacSbrPayloadStage stage = SAUTI_SBR_PAYLOAD_INPUT; unsigned seen = 0;
        SautiAacHeAdtsMonoState copy = st;
        if (len < 7u || off + len > (size_t)n) break;
        frames++;
        if (sauti_aac_he_adts_mono_decode_ex(b + off, len, &copy, pcm, &p, &r) == 1) {
            ok_std++; st = copy;
        } else {
            SautiAacHeAdtsMonoState t = st;
            if (sauti_aac_he_adts_mono_temporal_diagnose_ex(b + off, len, &t, pcm, &p, &r, &stage, &seen) == 1) { ok_temp++; st = t; }
            else { neither++; st = t; off += len; continue; }
        }
        if (per_frame) printf("F %u class=%u env=%u noise=%u last_border=%u invf0=%u invf_any=%u\n", frames - 1u, (unsigned)p.grid.frame_class, (unsigned)p.grid.env_count, (unsigned)p.grid.noise_count, (unsigned)p.grid.env_border[p.grid.env_count], (unsigned)p.control.invf_mode[0], ({ unsigned any = 0; for (unsigned q = 0; q < p.control.noise_band_count && q < SAUTI_AAC_SBR_MAX_NOISE_BANDS; q++) any |= p.control.invf_mode[q]; any; }));
        if (p.has_ps) ps++;
        cls[p.grid.frame_class & 3u]++;
        if (p.grid.env_border[p.grid.env_count] > 16u) over++;
        off += len;
    }
    printf("%s: frames=%u standard_ok=%u only_temporal_ok=%u neither=%u | with_ps=%u | classes FIXFIX/FIXVAR/VARFIX/VARVAR=%u/%u/%u/%u | last_border>16=%u\n",
           argv[1], frames, ok_std, ok_temp, neither, ps, cls[0], cls[1], cls[2], cls[3], over);
    free(b);
    return 0;
}
