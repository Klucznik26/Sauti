/* Naglowki publiczne MaktabaSauti: kompilacja jako C99 z -pedantic (bez rozszerzen), kontrola ukladu struktur (struct_size na poczatku, wyrownanie, stale rozmiary tablic)
 * i stalosci wartosci wyliczen (czesc ABI).  Nie linkuje sie z biblioteka. */
#include "maktabasauti/maktabasauti.h"
#include "maktabasauti/maktabasauti_codec.h"
#include <stddef.h>
#include <stdio.h>

#define FIRST(type) _Static_assert(offsetof(type, struct_size) == 0, #type ": struct_size musi byc pierwszym polem")
FIRST(maktabasauti_allocator_t);
FIRST(maktabasauti_io_t);
FIRST(maktabasauti_io_writer_t);
FIRST(maktabasauti_stream_info_t);
FIRST(maktabasauti_codec_info_t);
FIRST(maktabasauti_open_params_t);
FIRST(maktabasauti_encoder_params_t);
FIRST(maktabasauti_host_t);
FIRST(maktabasauti_codec_t);

/* struktury wyjsciowe o stalym ukladzie: rozmiar niezalezny od wskaznikow (tylko typy o stalej szerokosci i tablice znakow) */
_Static_assert(sizeof(maktabasauti_stream_info_t) == 4u*4u + 8u + 4u + 4u + 8u*3u + 4u + 4u + 32u*3u, "maktabasauti_stream_info_t: zmieniony uklad");
_Static_assert(sizeof(maktabasauti_codec_info_t) == 4u + 4u + 32u + 96u + 32u, "maktabasauti_codec_info_t: zmieniony uklad");
_Static_assert(offsetof(maktabasauti_stream_info_t, channel_mask) % 8u == 0u, "channel_mask musi byc wyrownane do 8");
_Static_assert(offsetof(maktabasauti_stream_info_t, total_frames) % 8u == 0u, "total_frames musi byc wyrownane do 8");
_Static_assert(sizeof(maktabasauti_result_t) == 4u && sizeof(maktabasauti_sample_format_t) == 4u, "typy wynikow i formatow maja 32 bity");

/* wartosci stale (ABI): zmiana ktorejkolwiek = niezgodna zmiana ABI */
_Static_assert(MAKTABASAUTI_OK == 0 && MAKTABASAUTI_EOF == 1, "OK/EOF");
_Static_assert(MAKTABASAUTI_E_INVALID_ARGUMENT == -1 && MAKTABASAUTI_E_NO_MEMORY == -2 && MAKTABASAUTI_E_IO == -3 &&
               MAKTABASAUTI_E_UNSUPPORTED_FORMAT == -4 && MAKTABASAUTI_E_UNSUPPORTED_FEATURE == -5 && MAKTABASAUTI_E_CORRUPT_DATA == -6 &&
               MAKTABASAUTI_E_TRUNCATED == -7 && MAKTABASAUTI_E_NOT_SEEKABLE == -8 && MAKTABASAUTI_E_OUT_OF_RANGE == -9 &&
               MAKTABASAUTI_E_BAD_STATE == -10 && MAKTABASAUTI_E_ABI_MISMATCH == -11 && MAKTABASAUTI_E_BUFFER_TOO_SMALL == -12 &&
               MAKTABASAUTI_E_WOULD_BLOCK == -13 && MAKTABASAUTI_E_CANCELLED == -14 && MAKTABASAUTI_E_NOT_IMPLEMENTED == -15 &&
               MAKTABASAUTI_E_INTERNAL == -16 && MAKTABASAUTI_E_FORMAT_CHANGED == -17, "kody bledow");
_Static_assert(MAKTABASAUTI_FORMAT_U8 == 1 && MAKTABASAUTI_FORMAT_S16 == 2 && MAKTABASAUTI_FORMAT_S24 == 3 && MAKTABASAUTI_FORMAT_S32 == 4 &&
               MAKTABASAUTI_FORMAT_F32 == 5 && MAKTABASAUTI_FORMAT_F64 == 6 && MAKTABASAUTI_FORMAT_PLANAR_FLAG == 0x100, "formaty probek");
_Static_assert(MAKTABASAUTI_CH_FL == 1u && MAKTABASAUTI_CH_FR == 2u && MAKTABASAUTI_CH_FC == 4u && MAKTABASAUTI_CH_LFE == 8u && MAKTABASAUTI_CH_BL == 0x10u &&
               MAKTABASAUTI_CH_BR == 0x20u && MAKTABASAUTI_CH_SL == 0x200u && MAKTABASAUTI_CH_SR == 0x400u, "maska kanalow jak WAVEFORMATEXTENSIBLE");
_Static_assert(MAKTABASAUTI_LAYOUT_5_1 == 0x3Fu && MAKTABASAUTI_LAYOUT_7_1 == 0x63Fu, "uklady");
_Static_assert(MAKTABASAUTI_ABI_VERSION == 0u, "ABI rozwojowe (0) do zamrozenia");

int main(void){
    puts("PASS MaktabaSauti public headers: C99 -pedantic, struct layout, ABI constants");
    return 0;
}
