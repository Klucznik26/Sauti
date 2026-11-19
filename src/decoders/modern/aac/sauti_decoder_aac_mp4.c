/* Natywny AAC w MP4/M4A/M4B: demuxer ISO BMFF (sauti_aac_mp4) owija jednostki dostepu w ADTS i oddaje strumien pierwszemu natywnemu adapterowi ADTS, ktory go przyjmie
 * (LC stereo, LC mono, LC wielokanalowy; HE tylko po walidacji).  Przyciecie z listy edycji jak w FFmpeg.  Nic nie jest zastepowane: ALAC, fragmentowane MP4 i nieobslugiwane
 * konfiguracje odrzuca `open` (NULL), a rejestr przechodzi do kolejnego dekodera (np. FFmpeg). */
#include "sauti_decoder_aac_native.h"
#include "sauti_aac_mp4.h"
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint8_t *adts;
    SautiDecoder inner;
    uint64_t skip,total,position;
} Mp4Aac;

static int mp4_probe(const uint8_t *data,size_t size){return sauti_aac_mp4_probe(data,size);}
static void mp4_close(void *ctx){
    Mp4Aac *s=ctx;
    if(!s)return;
    sauti_legacy_decoder_close(&s->inner);
    free(s->adts);
    free(s);
}
static void *mp4_open(const uint8_t *data,size_t length,SautiPcmInfo *info){
    if(!info)return NULL;
    uint8_t *adts=NULL;size_t adts_len=0;SautiAacMp4Info mi;
    if(!sauti_aac_mp4_to_adts(data,length,&adts,&adts_len,&mi))return NULL;
    Mp4Aac *s=calloc(1,sizeof(*s));
    if(!s){free(adts);return NULL;}
    s->adts=adts;
    /* LC (zwalidowane wzgledem FFmpeg) oraz HE-AAC: SBR mono/stereo i HE-AAC v2 z PS (SBR-82..86, zgodnosc z FFmpeg do zaokraglen float).  Adaptery HE otwieraja
     * tylko strumien, ktorego pierwsza ramka dekoduje sie z SBR, wiec zwykle LC trafia do adapterow LC; reszta konfiguracji dalej oddaje plik rejestrowi (FFmpeg). */
    const SautiDecoderOps *const inner[]={&sauti_decoder_aac_native_ops,&sauti_decoder_aac_mono_native_ops,
                                          &sauti_decoder_aac_multi_native_ops,&sauti_decoder_aac_he_native_ops,
                                          &sauti_decoder_aac_he_cpe_native_ops};
    if(!sauti_legacy_decoder_open(&s->inner,inner,sizeof(inner)/sizeof(inner[0]),adts,adts_len)){mp4_close(s);return NULL;}
    /* skip/duration z elst sa w probkach rdzenia; HE podwaja czestotliwosc wyjscia */
    const uint64_t out_rate=s->inner.info.sample_rate;
    const uint64_t scale_num=out_rate,scale_den=mi.sample_rate;
    uint64_t skip=mi.skip_samples*scale_num/scale_den;
    uint64_t total=s->inner.info.total_frames;
    if(total==0u||skip>=total){mp4_close(s);return NULL;}
    uint64_t available=total-skip;
    if(mi.duration_samples){
        const uint64_t d=mi.duration_samples*scale_num/scale_den;
        if(d&&d<available)available=d;
    }
    /* no edit list: iTunSMPB. Its values are in samples of the CORE stream (a 24 kHz HE-AAC file stores 2048 / 768 / 288000 for 4096 / 1536 / 576000 output
     * samples, and FFmpeg doubles them the same way), so they scale like the edit list. */
    if(mi.gapless&&!mi.skip_samples&&!mi.duration_samples){
        skip=mi.gapless_priming*scale_num/scale_den;
        if(skip>=total){mp4_close(s);return NULL;}
        available=total-skip;
        const uint64_t d=mi.gapless_samples*scale_num/scale_den;
        if(d&&d<available)available=d;
    }
    if(skip&&!sauti_legacy_decoder_seek(&s->inner,skip)){mp4_close(s);return NULL;}
    s->skip=skip;s->total=available;s->position=0;
    *info=s->inner.info;
    info->total_frames=available;
    return s;
}
static size_t mp4_read(void *ctx,float *out,size_t frames){
    Mp4Aac *s=ctx;
    if(!s||!out||!frames||s->position>=s->total)return 0;
    if((uint64_t)frames>s->total-s->position)frames=(size_t)(s->total-s->position);
    const size_t got=sauti_legacy_decoder_read(&s->inner,out,frames);
    s->position+=got;
    return got;
}
static int mp4_seek(void *ctx,uint64_t position){
    Mp4Aac *s=ctx;
    if(!s||position>s->total)return 0;
    if(!sauti_legacy_decoder_seek(&s->inner,position+s->skip))return 0;
    s->position=position;
    return 1;
}
const SautiDecoderOps sauti_decoder_aac_mp4_native_ops={
    "aac-native-mp4",mp4_probe,mp4_open,mp4_read,mp4_seek,mp4_close
};
