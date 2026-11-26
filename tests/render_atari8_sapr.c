/* Standalone diagnostic converter; output raw signed 16-bit LE PCM. */
#include "../src/decoders/retro/atari8/atari8_sapr.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s INPUT.sapr OUTPUT.pcm\n", argv[0]);
        return 1;
    }
    FILE *in = fopen(argv[1], "rb");
    if (!in) { perror(argv[1]); return 1; }
    if (fseek(in, 0, SEEK_END)) { fclose(in); return 1; }
    long n = ftell(in);
    if (n <= 0 || n > 64L * 1024 * 1024 || fseek(in, 0, SEEK_SET)) {
        fclose(in); return 1;
    }
    uint8_t *data = malloc((size_t)n);
    if (!data) { fclose(in); return 1; }
    bool ok = fread(data, 1, (size_t)n, in) == (size_t)n;
    if (fclose(in)) ok = false;
    Atari8Sapr s;
    if (!ok || atari8_sapr_open(&s, data, (size_t)n, 48000) != ATARI8_SAPR_OK) {
        fprintf(stderr, "Invalid or unsupported SAP-R\n");
        free(data); return 1;
    }
    FILE *out = fopen(argv[2], "wb");
    if (!out) { perror(argv[2]); free(data); return 1; }
    unsigned channels = atari8_core_channels(&s.core);
    fprintf(stderr, "PCM 48000 Hz, %u channel(s), signed 16-bit little-endian\n", channels);
    Atari8SaprResult r;
    int16_t frame[2];
    while ((r = atari8_sapr_next(&s, frame)) == ATARI8_SAPR_OK) {
        for (unsigned ch = 0; ch < channels; ++ch) {
            uint16_t v = (uint16_t)frame[ch];
            if (fputc(v & 255, out) == EOF || fputc(v >> 8, out) == EOF) {
                ok = false; break;
            }
        }
        if (!ok) break;
    }
    if (fclose(out)) ok = false;
    if (r != ATARI8_SAPR_EOF) {
        fprintf(stderr, "Rendering stopped at register frame %zu (status %d)\n",
                s.next_frame, (int)r);
        ok = false;
    }
    free(data);
    if (!ok) { remove(argv[2]); return 1; }
    return 0;
}
