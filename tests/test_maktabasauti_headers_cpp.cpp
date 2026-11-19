// Naglowki publiczne MaktabaSauti w C++17: musza sie kompilowac bez ostrzezen i dawac sie uzywac z C++ (extern "C", brak slow kluczowych C++ jako nazw).
#include "maktabasauti/maktabasauti.h"
#include "maktabasauti/maktabasauti_codec.h"
#include <cstdio>

static_assert(sizeof(maktabasauti_result_t) == 4, "result");
static_assert(MAKTABASAUTI_E_IO == -3, "io");

int main() {
    maktabasauti_open_params_t p = {};
    p.struct_size = sizeof p;
    p.abi_version = MAKTABASAUTI_ABI_VERSION;
    maktabasauti_stream_info_t info = {};
    info.struct_size = sizeof info;
    maktabasauti_codec_t codec = {};
    codec.struct_size = sizeof codec;
    std::puts("PASS MaktabaSauti public headers: C++17");
    return (p.struct_size && info.struct_size && codec.struct_size) ? 0 : 1;
}
