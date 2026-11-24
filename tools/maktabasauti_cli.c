/* Test tool (not a product): decodes a file with the MaktabaSauti default registry to interleaved float32 LE and prints the stream description.
 * usage: maktabasauti_cli IN OUT.f32 [--nongapless] [--nonseekable] [--opt KEY=VALUE]...   ->  `STREAM <codec>/<profile> in <container> rate=.. channels=.. total=.. frames=..` */
#include "maktabasauti/maktabasauti.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    maktabasauti_open_params_t p; maktabasauti_decoder_t *d = NULL; maktabasauti_stream_info_t info; maktabasauti_result_t r; FILE *out; float *buf; size_t got, total = 0; int i;
    if (argc < 3) return 2;
    memset(&p, 0, sizeof p); p.struct_size = sizeof p; p.abi_version = MAKTABASAUTI_ABI_VERSION;
    for (i = 3; i < argc; i++) if (!strcmp(argv[i], "--nongapless")) p.flags |= MAKTABASAUTI_OPEN_NO_GAPLESS;
    {   /* --opt KEY=VALUE: codec options (e.g. aac.drc=on) */
        static maktabasauti_option_t opts[16]; static char kv[16][128]; size_t n = 0;
        for (i = 3; i + 1 < argc && n < 16; i++) if (!strcmp(argv[i], "--opt")) {
            char *eq; strncpy(kv[n], argv[++i], 127); kv[n][127] = 0; eq = strchr(kv[n], '=');
            if (eq) { *eq = 0; opts[n].key = kv[n]; opts[n].value = eq + 1; n++; }
        }
        p.options = n ? opts : NULL; p.option_count = n;
    }
    r = maktabasauti_io_from_file(argv[1], NULL, &p.io);
    if (r != MAKTABASAUTI_OK) { fprintf(stderr, "cannot open input: %d\n", (int)r); return 2; }
    r = maktabasauti_decoder_open(&p, &d);
    if (r != MAKTABASAUTI_OK) { fprintf(stderr, "open: %d %s\n", (int)r, maktabasauti_result_message(r)); return 1; }
    info.struct_size = sizeof info; (void)maktabasauti_decoder_get_info(d, &info);
    out = fopen(argv[2], "wb"); if (!out) return 2;
    buf = (float *)malloc(4096u * info.channels * sizeof(float));
    for (;;) {
        r = maktabasauti_decoder_read(d, buf, 4096, MAKTABASAUTI_FORMAT_F32, &got);
        if (got) { fwrite(buf, sizeof(float) * info.channels, got, out); total += got; }
        if (r == MAKTABASAUTI_EOF) break;
        if (r != MAKTABASAUTI_OK) { fprintf(stderr, "read: %d %s\n", (int)r, maktabasauti_result_message(r)); break; }
    }
    printf("STREAM %s/%s in %s rate=%u channels=%u total=%llu frames=%zu\n", info.codec_name, info.codec_profile, info.container_name, info.sample_rate, info.channels, (unsigned long long)info.total_frames, total);
    fclose(out); free(buf); maktabasauti_decoder_close(d);
    return r == MAKTABASAUTI_EOF ? 0 : 1;
}
