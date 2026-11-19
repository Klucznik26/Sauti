/* Natywny AAC w LOAS/LATM: demuxer (sauti_aac_latm) przepisuje jednostki dostepu do ADTS i oddaje strumien pierwszemu natywnemu adapterowi ADTS, ktory go przyjmie (LC, HE-AAC,
 * HE-AAC v2).  Strumien nie ma informacji gapless, wiec nic nie jest przycinane.  Nieobslugiwane konfiguracje odrzuca `open` (NULL), a rejestr przechodzi do kolejnego dekodera. */
#include "sauti_decoder_aac_native.h"
#include "sauti_aac_latm.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t *adts;
    SautiDecoder inner;
} LatmAac;

static int latm_probe(const uint8_t *data,size_t size){return sauti_aac_latm_probe(data,size);}
static void latm_close(void *ctx){
    LatmAac *s=ctx;
    if(!s)return;
    sauti_legacy_decoder_close(&s->inner);
    free(s->adts);
    free(s);
}
static void *latm_open(const uint8_t *data,size_t length,SautiPcmInfo *info){
    if(!info)return NULL;
    uint8_t *adts=NULL;size_t adts_len=0;SautiAacLatmInfo li;
    if(!sauti_aac_latm_to_adts(data,length,&adts,&adts_len,&li))return NULL;
    LatmAac *s=calloc(1,sizeof(*s));
    if(!s){free(adts);return NULL;}
    s->adts=adts;
    const SautiDecoderOps *const inner[]={&sauti_decoder_aac_native_ops,&sauti_decoder_aac_mono_native_ops,
                                          &sauti_decoder_aac_multi_native_ops,&sauti_decoder_aac_he_native_ops,
                                          &sauti_decoder_aac_he_cpe_native_ops};
    if(!sauti_legacy_decoder_open(&s->inner,inner,sizeof(inner)/sizeof(inner[0]),adts,adts_len)){latm_close(s);return NULL;}
    *info=s->inner.info;
    return s;
}
static size_t latm_read(void *ctx,float *out,size_t frames){
    LatmAac *s=ctx;
    return s&&out&&frames?sauti_legacy_decoder_read(&s->inner,out,frames):0u;
}
static int latm_seek(void *ctx,uint64_t position){
    LatmAac *s=ctx;
    return s&&sauti_legacy_decoder_seek(&s->inner,position);
}
const SautiDecoderOps sauti_decoder_aac_latm_native_ops={
    "aac-native-latm",latm_probe,latm_open,latm_read,latm_seek,latm_close
};
