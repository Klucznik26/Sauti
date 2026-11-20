/* Cel 3: kontenery AAC wymuszone po nazwie (omija sondy: parsery ADTS, LOAS/LATM, ADIF, MP4 dostaja dowolne dane i dochodza gleboko).  Pierwszy bajt wybiera kodek i przewijalnosc. */
#include "fuzz_common.h"
int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
    static const char *const names[4] = { "aac-adts", "aac-loas", "aac-adif", "aac-mp4" };
    maktabasauti_io_t io; FzMem m; maktabasauti_allocator_t a; maktabasauti_open_params_t p; maktabasauti_decoder_t *d = NULL; maktabasauti_result_t r;
    uint8_t *copy;
    if (size < 2) return 0;
    memset(&m, 0, sizeof m); a = fz_allocator(&m);
    copy = (uint8_t *)malloc(size - 1u);
    if (!copy) return 0;
    memcpy(copy, data + 1, size - 1u);
    if (maktabasauti_io_from_memory(copy, size - 1u, &a, &io) != MAKTABASAUTI_OK) { free(copy); return 0; }
    if (data[0] & 4u) io.seek = NULL;                                     /* wejscie nieprzewijalne (rozmiar zostaje: wskazowka) */
    memset(&p, 0, sizeof p); p.struct_size = sizeof p; p.abi_version = MAKTABASAUTI_ABI_VERSION; p.io = io; p.allocator = &a; p.codec = names[data[0] & 3u];
    r = maktabasauti_decoder_open(&p, &d);
    if (r == MAKTABASAUTI_OK) { fz_exercise(d); maktabasauti_decoder_close(d); }
    else io.close(io.user);
    free(copy);
    fz_check_mem(&m);
    return 0;
}
